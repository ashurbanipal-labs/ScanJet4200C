#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <cairo-pdf.h>
#include <signal.h>
#include <math.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "driver/image_dpi.h"
#include "driver/hp4200_registers.h"

#define BED_W (HP4200_MAX_X_300 * 25.4 / 300.0)
#define BED_H (HP4200_MAX_Y_300 * 25.4 / 300.0)
#define HANDLE_SIZE 11.0
#define HANDLE_HIT 12.0

enum {
    DRAG_NEW = 0,
    DRAG_LEFT = 1,
    DRAG_RIGHT = 2,
    DRAG_TOP = 4,
    DRAG_BOTTOM = 8,
    DRAG_MOVE = 16
};

typedef struct {
    GtkWidget *window, *canvas, *status, *preview_button, *auto_button;
    GtkWidget *full_button, *draw_button, *scan_button, *cancel_button;
    GtkWidget *dpi, *format, *buffer_mode, *folder, *filename, *spin[4];
    GdkPixbuf *image;
    char *preview_path, *active_path;
    double area[4], start[4], origin_x, origin_y;
    GMutex process_lock;
    GSubprocess *scan_process;
    guint progress_timer;
    gint64 scan_started, last_growth, last_size;
    gint64 last_transport_activity;
    guint64 transport_bytes, transport_base;
    gboolean syncing, busy, draw_new, cancel_requested;
    int drag_mode;
} App;

typedef struct {
    App *app;
    gboolean preview;
    char *path;
    char *scan_path;
    char *warning;
    double left, top, width, height;
    int x_dpi, y_dpi, format, host_buffer_mib;
} ScanJob;

static char *native_scan_path;

static const char *formats[] = {"png", "tiff", "jpeg", "pdf"};
static const struct { int x, y; } resolutions[] = {
    {75, 75}, {150, 150}, {300, 300}, {600, 600}, {600, 1200}
};

static double clamp(double n, double low, double high) {
    return fmin(high, fmax(low, n));
}

static void message(App *app, const char *text) {
    gtk_label_set_text(GTK_LABEL(app->status), text);
}

static void selection(App *app, double left, double top, double right, double bottom) {
    app->area[0] = clamp(fmin(left, right), 0, 1);
    app->area[1] = clamp(fmin(top, bottom), 0, 1);
    app->area[2] = clamp(fmax(left, right), 0, 1);
    app->area[3] = clamp(fmax(top, bottom), 0, 1);
    app->syncing = TRUE;
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->spin[0]), app->area[0] * BED_W);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->spin[1]), app->area[1] * BED_H);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->spin[2]), (app->area[2] - app->area[0]) * BED_W);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->spin[3]), (app->area[3] - app->area[1]) * BED_H);
    app->syncing = FALSE;
    gtk_widget_queue_draw(app->canvas);
}

static void spin_changed(GtkSpinButton *spin, gpointer user_data) {
    (void)spin;
    App *app = user_data;
    if (app->syncing) return;
    double left = gtk_spin_button_get_value(GTK_SPIN_BUTTON(app->spin[0])) / BED_W;
    double top = gtk_spin_button_get_value(GTK_SPIN_BUTTON(app->spin[1])) / BED_H;
    double width = gtk_spin_button_get_value(GTK_SPIN_BUTTON(app->spin[2])) / BED_W;
    double height = gtk_spin_button_get_value(GTK_SPIN_BUTTON(app->spin[3])) / BED_H;
    /* Keep the numeric fields equal to the rectangle that can actually be
     * scanned when a side reaches the edge of the glass. */
    selection(app, left, top, fmin(1, left + width),
              fmin(1, top + height));
}

static void image_rect(int width, int height, double *x, double *y, double *w, double *h) {
    double available_w = fmax(1, width - 24), available_h = fmax(1, height - 24);
    if (available_w / available_h > BED_W / BED_H) {
        *h = available_h; *w = available_h * BED_W / BED_H;
    } else {
        *w = available_w; *h = available_w * BED_H / BED_W;
    }
    *x = (width - *w) / 2; *y = (height - *h) / 2;
}

static void draw(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
    (void)area;
    App *app = user_data;
    double x, y, w, h;
    image_rect(width, height, &x, &y, &w, &h);
    cairo_set_source_rgb(cr, 0.83, 0.83, 0.83);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);
    if (app->image) {
        cairo_save(cr);
        cairo_translate(cr, x, y);
        cairo_scale(cr, w / gdk_pixbuf_get_width(app->image), h / gdk_pixbuf_get_height(app->image));
        gdk_cairo_set_source_pixbuf(cr, app->image, 0, 0);
        cairo_paint(cr);
        cairo_restore(cr);
    }
    cairo_rectangle(cr, x + app->area[0] * w, y + app->area[1] * h,
                    (app->area[2] - app->area[0]) * w, (app->area[3] - app->area[1]) * h);
    cairo_set_source_rgba(cr, 0.04, 0.35, 0.88, 0.22);
    cairo_fill_preserve(cr);
    cairo_set_source_rgb(cr, 0.02, 0.28, 0.75);
    cairo_set_line_width(cr, 2);
    cairo_stroke(cr);
    double left = x + app->area[0] * w, right = x + app->area[2] * w;
    double top = y + app->area[1] * h, bottom = y + app->area[3] * h;
    double mid_x = (left + right) / 2, mid_y = (top + bottom) / 2;
    double handles[8][2] = {
        {left, top}, {mid_x, top}, {right, top}, {right, mid_y},
        {right, bottom}, {mid_x, bottom}, {left, bottom}, {left, mid_y}
    };
    for (int i = 0; i < 8; ++i) {
        cairo_rectangle(cr, handles[i][0] - HANDLE_SIZE / 2,
                        handles[i][1] - HANDLE_SIZE / 2, HANDLE_SIZE, HANDLE_SIZE);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_fill_preserve(cr);
        cairo_set_source_rgb(cr, 0.02, 0.28, 0.75);
        cairo_set_line_width(cr, 1.5);
        cairo_stroke(cr);
    }
}

static int hit_handle(App *app, double px, double py) {
    double x, y, w, h;
    image_rect(gtk_widget_get_width(app->canvas), gtk_widget_get_height(app->canvas), &x, &y, &w, &h);
    double left = x + app->area[0] * w, right = x + app->area[2] * w;
    double top = y + app->area[1] * h, bottom = y + app->area[3] * h;
    double mid_x = (left + right) / 2, mid_y = (top + bottom) / 2;
    const struct { double x, y; int mode; } handles[] = {
        {left, top, DRAG_LEFT | DRAG_TOP},
        {right, top, DRAG_RIGHT | DRAG_TOP},
        {right, bottom, DRAG_RIGHT | DRAG_BOTTOM},
        {left, bottom, DRAG_LEFT | DRAG_BOTTOM},
        {mid_x, top, DRAG_TOP},
        {right, mid_y, DRAG_RIGHT},
        {mid_x, bottom, DRAG_BOTTOM},
        {left, mid_y, DRAG_LEFT}
    };
    for (guint i = 0; i < G_N_ELEMENTS(handles); ++i) {
        if (fabs(px - handles[i].x) <= HANDLE_HIT &&
            fabs(py - handles[i].y) <= HANDLE_HIT) return handles[i].mode;
    }
    return DRAG_NEW;
}

static void normalized(App *app, double px, double py, double *nx, double *ny) {
    double x, y, w, h;
    image_rect(gtk_widget_get_width(app->canvas), gtk_widget_get_height(app->canvas), &x, &y, &w, &h);
    *nx = clamp((px - x) / w, 0, 1);
    *ny = clamp((py - y) / h, 0, 1);
}

static void drag_begin(GtkGestureDrag *gesture, double x, double y, gpointer user_data) {
    (void)gesture;
    App *app = user_data;
    if (app->busy) return;
    normalized(app, x, y, &app->origin_x, &app->origin_y);
    memcpy(app->start, app->area, sizeof(app->start));
    app->drag_mode = app->draw_new ? DRAG_NEW : hit_handle(app, x, y);
    if (!app->draw_new && app->drag_mode == DRAG_NEW &&
        app->origin_x > app->area[0] && app->origin_x < app->area[2] &&
        app->origin_y > app->area[1] && app->origin_y < app->area[3]) {
        app->drag_mode = DRAG_MOVE;
    }
}

static void resized_bounds(const double start[4], int mode, double dx, double dy,
                           double min_w, double min_h, double out[4]) {
    memcpy(out, start, 4 * sizeof(double));
    if (mode & DRAG_LEFT) out[0] = clamp(start[0] + dx, 0, start[2] - min_w);
    if (mode & DRAG_RIGHT) out[2] = clamp(start[2] + dx, start[0] + min_w, 1);
    if (mode & DRAG_TOP) out[1] = clamp(start[1] + dy, 0, start[3] - min_h);
    if (mode & DRAG_BOTTOM) out[3] = clamp(start[3] + dy, start[1] + min_h, 1);
}

static void drag_update(GtkGestureDrag *gesture, double dx, double dy, gpointer user_data) {
    (void)gesture;
    App *app = user_data;
    if (app->busy) return;
    double x, y, w, h;
    image_rect(gtk_widget_get_width(app->canvas), gtk_widget_get_height(app->canvas), &x, &y, &w, &h);
    (void)x; (void)y;
    double nx = clamp(app->origin_x + dx / w, 0, 1);
    double ny = clamp(app->origin_y + dy / h, 0, 1);
    if (app->drag_mode == DRAG_MOVE) {
        double mx = clamp(nx - app->origin_x, -app->start[0], 1 - app->start[2]);
        double my = clamp(ny - app->origin_y, -app->start[1], 1 - app->start[3]);
        selection(app, app->start[0] + mx, app->start[1] + my,
                  app->start[2] + mx, app->start[3] + my);
    } else if (app->drag_mode != DRAG_NEW) {
        double bounds[4];
        resized_bounds(app->start, app->drag_mode, dx / w, dy / h,
                       fmax(1.0 / BED_W, 4.0 / w), fmax(1.0 / BED_H, 4.0 / h), bounds);
        selection(app, bounds[0], bounds[1], bounds[2], bounds[3]);
    } else {
        selection(app, app->origin_x, app->origin_y, nx, ny);
    }
}

static void pointer_motion(GtkEventControllerMotion *controller, double x, double y, gpointer user_data) {
    (void)controller;
    App *app = user_data;
    const char *cursor = "default";
    if (app->draw_new) cursor = "crosshair";
    else {
        int mode = hit_handle(app, x, y);
        if (mode == (DRAG_LEFT | DRAG_TOP) || mode == (DRAG_RIGHT | DRAG_BOTTOM)) cursor = "nwse-resize";
        else if (mode == (DRAG_RIGHT | DRAG_TOP) || mode == (DRAG_LEFT | DRAG_BOTTOM)) cursor = "nesw-resize";
        else if (mode == DRAG_LEFT || mode == DRAG_RIGHT) cursor = "ew-resize";
        else if (mode == DRAG_TOP || mode == DRAG_BOTTOM) cursor = "ns-resize";
        else {
            double nx, ny;
            normalized(app, x, y, &nx, &ny);
            if (nx > app->area[0] && nx < app->area[2] &&
                ny > app->area[1] && ny < app->area[3]) cursor = "move";
            else cursor = "crosshair";
        }
    }
    gtk_widget_set_cursor_from_name(app->canvas, cursor);
}

static void drag_end(GtkGestureDrag *gesture, double dx, double dy, gpointer user_data) {
    App *app = user_data;
    if (app->busy) return;
    if (app->drag_mode == DRAG_NEW && fabs(dx) < 3 && fabs(dy) < 3)
        selection(app, app->start[0], app->start[1],
                  app->start[2], app->start[3]);
    else
        drag_update(gesture, dx, dy, user_data);
    app->draw_new = FALSE;
    gtk_button_set_label(GTK_BUTTON(app->draw_button), "Draw area");
}

static gboolean find_content(GdkPixbuf *image, double bounds[4]) {
    int w = gdk_pixbuf_get_width(image), h = gdk_pixbuf_get_height(image);
    int channels = gdk_pixbuf_get_n_channels(image), stride = gdk_pixbuf_get_rowstride(image);
    guchar *pixels = gdk_pixbuf_get_pixels(image);
    if (w < 20 || h < 20 || channels < 3) return FALSE;
    int edge = MAX(3, MIN(w, h) / 40), step = MAX(1, MIN(w, h) / 150);
    guint64 sum[3] = {0, 0, 0}, white_sum[3] = {0, 0, 0};
    int count = 0, white_count = 0;
    for (int x = edge; x < w - edge; x += step) {
        int ys[] = {edge, h - edge - 1};
        for (int i = 0; i < 2; ++i) {
            guchar *p = pixels + ys[i] * stride + x * channels;
            for (int c = 0; c < 3; ++c) sum[c] += p[c];
            int low = MIN(p[0], MIN(p[1], p[2]));
            int high = MAX(p[0], MAX(p[1], p[2]));
            if (low > 175 && high - low < 55) {
                for (int c = 0; c < 3; ++c) white_sum[c] += p[c];
                ++white_count;
            }
            ++count;
        }
    }
    for (int y = edge; y < h - edge; y += step) {
        int xs[] = {edge, w - edge - 1};
        for (int i = 0; i < 2; ++i) {
            guchar *p = pixels + y * stride + xs[i] * channels;
            for (int c = 0; c < 3; ++c) sum[c] += p[c];
            int low = MIN(p[0], MIN(p[1], p[2]));
            int high = MAX(p[0], MAX(p[1], p[2]));
            if (low > 175 && high - low < 55) {
                for (int c = 0; c < 3; ++c) white_sum[c] += p[c];
                ++white_count;
            }
            ++count;
        }
    }
    /* A large dark object can cover three edges, leaving white lid visible
     * on only one. Use that lid segment as the background when present. */
    gboolean white_backing = white_count >= count / 20;
    int background[3];
    for (int c = 0; c < 3; ++c)
        background[c] = white_backing ? (int)(white_sum[c] / white_count) :
                                        (int)(sum[c] / count);
    int *row_hits = g_new0(int, h), *column_hits = g_new0(int, w);
    int left = w, top = h, right = 0, bottom = 0, hits = 0;
    for (int y = edge; y < h - edge; ++y) {
        for (int x = edge; x < w - edge; ++x) {
            int strong = 0;
            for (int yy = y - 1; yy <= y + 1; ++yy) {
                for (int xx = x - 1; xx <= x + 1; ++xx) {
                    guchar *p = pixels + yy * stride + xx * channels;
                    int difference = MAX(abs((int)p[0] - background[0]),
                                         MAX(abs((int)p[1] - background[1]), abs((int)p[2] - background[2])));
                    strong += difference > 35;
                }
            }
            if (strong < 5) continue;
            ++row_hits[y];
            ++column_hits[x];
            ++hits;
        }
    }
    /* An isolated scratch or narrow shading streak must not expand the box
     * to the glass edge. Require a small run of foreground pixels per row
     * and column before accepting that row or column as object extent. */
    int row_min = MAX(3, w / 30), column_min = MAX(3, h / 30);
    for (int y = edge; y < h - edge; ++y)
        if (row_hits[y] >= row_min) {
            top = MIN(top, y); bottom = MAX(bottom, y);
        }
    for (int x = edge; x < w - edge; ++x)
        if (column_hits[x] >= column_min) {
            left = MIN(left, x); right = MAX(right, x);
        }
    g_free(row_hits);
    g_free(column_hits);
    if (hits < w * h * 0.003 || left >= right || top >= bottom ||
        (right - left) * (bottom - top) > w * h * 0.94) return FALSE;
    int pad = MAX(5, MIN(w, h) * 0.015);
    bounds[0] = (double)MAX(0, left - pad) / w;
    bounds[1] = (double)MAX(0, top - pad) / h;
    bounds[2] = (double)MIN(w, right + pad + 1) / w;
    bounds[3] = (double)MIN(h, bottom + pad + 1) / h;
    return TRUE;
}

static void auto_select(GtkButton *button, gpointer user_data) {
    (void)button;
    App *app = user_data;
    if (!app->image) { message(app, "Make a preview first."); return; }
    double bounds[4];
    if (find_content(app->image, bounds)) {
        selection(app, bounds[0], bounds[1], bounds[2], bounds[3]);
        message(app, "Object detected. Adjust the blue box before scanning.");
    } else {
        message(app, "No distinct object found. Drag outside the box to draw an area manually.");
    }
}

static void set_busy(App *app, gboolean busy) {
    app->busy = busy;
    GtkWidget *buttons[] = {app->preview_button, app->auto_button, app->full_button,
                            app->draw_button, app->scan_button};
    for (guint i = 0; i < G_N_ELEMENTS(buttons); ++i) gtk_widget_set_sensitive(buttons[i], !busy);
    gtk_widget_set_sensitive(app->cancel_button, busy);
    gtk_widget_set_sensitive(app->buffer_mode, !busy);
}

static gboolean progress_tick(gpointer user_data) {
    App *app = user_data;
    if (!app->busy) return G_SOURCE_REMOVE;
    gint64 now = g_get_monotonic_time();
    GStatBuf info;
    if (app->active_path && g_stat(app->active_path, &info) == 0 && info.st_size > app->last_size) {
        app->last_size = info.st_size;
        app->last_growth = now;
    }
    g_mutex_lock(&app->process_lock);
    gint64 transport_activity = app->last_transport_activity;
    guint64 transport_bytes = app->transport_bytes;
    g_mutex_unlock(&app->process_lock);
    if (transport_activity > app->last_growth)
        app->last_growth = transport_activity;
    gint64 elapsed = (now - app->scan_started) / G_USEC_PER_SEC;
    gint64 idle = (now - app->last_growth) / G_USEC_PER_SEC;
    char *text;
    if (idle >= 60) {
        text = g_strdup_printf("No scanner data for %ld seconds (elapsed %ld seconds). Use Cancel scan if the head has stopped.",
                               (long)idle, (long)elapsed);
    } else {
        text = g_strdup_printf("Scanning… %ld seconds elapsed, %" G_GUINT64_FORMAT
                               " KiB received, %ld KiB written. Keep an eye on the scanner.",
                               (long)elapsed, transport_bytes / 1024,
                               (long)(app->last_size / 1024));
    }
    message(app, text);
    g_free(text);
    return G_SOURCE_CONTINUE;
}

static void cancel_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    App *app = user_data;
    g_mutex_lock(&app->process_lock);
    app->cancel_requested = TRUE;
    if (app->scan_process) g_subprocess_send_signal(app->scan_process, SIGTERM);
    g_mutex_unlock(&app->process_lock);
    message(app, "Cancelling scan…");
}

static void job_free(ScanJob *job) {
    g_free(job->path);
    g_free(job->scan_path);
    g_free(job->warning);
    g_free(job);
}

static gboolean job_cancelled(App *app) {
    gboolean cancelled;
    g_mutex_lock(&app->process_lock);
    cancelled = app->cancel_requested;
    g_mutex_unlock(&app->process_lock);
    return cancelled;
}

static char *scan_failure_summary(const char *stderr_text) {
    if (!stderr_text || !*stderr_text) return g_strdup("Scan failed.");
    char **lines = g_strsplit(stderr_text, "\n", -1);
    const char *chosen = NULL, *last = NULL, *ram_mismatch = NULL;
    const char *usb_bulk = NULL, *buffer_status = NULL;
    for (unsigned i = 0; lines[i]; ++i) {
        if (!*lines[i]) continue;
        last = lines[i];
        if (g_str_has_prefix(lines[i], "Startup ASIC ") ||
            g_str_has_prefix(lines[i], "Final upload ASIC "))
            ram_mismatch = lines[i];
        if (g_str_has_prefix(lines[i], "USB bulk "))
            usb_bulk = lines[i];
        if (g_str_has_prefix(lines[i], "Last scanner buffer status: "))
            buffer_status = lines[i];
        if (g_str_has_prefix(lines[i], "Native scan read failed") ||
            g_str_has_prefix(lines[i], "Native scan setup failed") ||
            g_str_has_prefix(lines[i], "Native scan output failed") ||
            g_str_has_prefix(lines[i], "ScanJet 4200C USB open failed"))
            chosen = lines[i];
    }
    GString *summary = g_string_new(chosen ? chosen : last ? last : "Scan failed.");
    if (ram_mismatch && chosen) g_string_append_printf(summary, "\n%s", ram_mismatch);
    if (usb_bulk && chosen) g_string_append_printf(summary, "\n%s", usb_bulk);
    if (buffer_status && chosen)
        g_string_append_printf(summary, "\n%s", buffer_status);
    g_strfreev(lines);
    return g_string_free(summary, FALSE);
}

static gboolean convert_native_image(ScanJob *job, const char *png_path,
                                     GError **error) {
    unsigned output_x_dpi = job->y_dpi == job->x_dpi * 2 ?
                            (unsigned)job->y_dpi : (unsigned)job->x_dpi;
    GdkPixbuf *image = gdk_pixbuf_new_from_file(png_path, error);
    if (!image) return FALSE;
    char *temp = g_strdup_printf("%s.tmp-XXXXXX", job->path);
    int fd = g_mkstemp(temp);
    if (fd < 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                    "Cannot create output file: %s", g_strerror(errno));
        g_object_unref(image); g_free(temp);
        return FALSE;
    }
    close(fd);
    gboolean saved = FALSE;
    if (job->format == 1 || job->format == 2) {
        if (job->format == 1) {
            char x_dpi_text[16], y_dpi_text[16];
            g_snprintf(x_dpi_text, sizeof(x_dpi_text), "%u", output_x_dpi);
            g_snprintf(y_dpi_text, sizeof(y_dpi_text), "%u",
                       (unsigned)job->y_dpi);
            /* Some TIFF encoders omit resolution tags unless asked for them.
             * The metadata writer below can then verify and set exact DPI. */
            saved = gdk_pixbuf_save(image, temp, "tiff", error,
                                    "x-dpi", x_dpi_text,
                                    "y-dpi", y_dpi_text, NULL);
        } else {
            saved = gdk_pixbuf_save(image, temp, "jpeg", error, NULL);
        }
        if (saved && (job->format == 1 ?
            hp4200_set_tiff_xy_dpi(temp, output_x_dpi,
                                   (unsigned)job->y_dpi) :
            hp4200_set_jpeg_xy_dpi(temp, output_x_dpi,
                                   (unsigned)job->y_dpi))) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                                "Could not record scan DPI in the image.");
            saved = FALSE;
        }
    } else if (job->format == 3) {
        double x_scale = 72.0 / output_x_dpi;
        double y_scale = 72.0 / job->y_dpi;
        cairo_surface_t *surface = cairo_pdf_surface_create(temp,
            gdk_pixbuf_get_width(image) * x_scale,
            gdk_pixbuf_get_height(image) * y_scale);
        cairo_t *cr = cairo_create(surface);
        cairo_scale(cr, x_scale, y_scale);
        gdk_cairo_set_source_pixbuf(cr, image, 0, 0);
        cairo_paint(cr);
        cairo_show_page(cr);
        cairo_status_t result = cairo_status(cr);
        cairo_destroy(cr);
        cairo_surface_finish(surface);
        if (result == CAIRO_STATUS_SUCCESS)
            result = cairo_surface_status(surface);
        cairo_surface_destroy(surface);
        saved = result == CAIRO_STATUS_SUCCESS;
        if (!saved)
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "PDF export failed: %s", cairo_status_to_string(result));
    }
    g_object_unref(image);
    if (saved && job_cancelled(job->app)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_CANCELLED,
                            "Scan cancelled.");
        saved = FALSE;
    }
    if (saved) {
        GFile *source = g_file_new_for_path(temp);
        GFile *destination = g_file_new_for_path(job->path);
        saved = g_file_move(source, destination, G_FILE_COPY_NONE,
                            NULL, NULL, NULL, error);
        g_object_unref(source);
        g_object_unref(destination);
    }
    g_unlink(temp);
    g_free(temp);
    return saved;
}

/* The native CLI reports USB reads before its first PNG row is available.
 * A large host buffer can otherwise look stalled in the file-growth UI. */
static void record_transport_line(App *app, const char *line) {
    if (g_str_has_prefix(line, "Native scan: 8-bit sensor") ||
        g_str_has_prefix(line, "Native scan: 16-bit sensor")) {
        g_mutex_lock(&app->process_lock);
        /* The CLI resets its diagnostic byte counter after calibration.
         * Keep the app's displayed total monotonic across that reset. */
        app->transport_base = app->transport_bytes;
        app->last_transport_activity = g_get_monotonic_time();
        g_mutex_unlock(&app->process_lock);
        return;
    }
    if (!g_str_has_prefix(line, "native USB: scanner buffer ")) return;
    const char *received = strstr(line, ", received ");
    if (!received) return;
    received += strlen(", received ");
    char *end = NULL;
    guint64 bytes = g_ascii_strtoull(received, &end, 10);
    if (end == received || !g_str_has_prefix(end, " bytes")) return;
    g_mutex_lock(&app->process_lock);
    app->transport_bytes = app->transport_base + bytes;
    app->last_transport_activity = g_get_monotonic_time();
    g_mutex_unlock(&app->process_lock);
}

static void run_job(GTask *task, gpointer source, gpointer task_data, GCancellable *cancellable) {
    (void)source; (void)cancellable;
    ScanJob *job = task_data;
    App *app = job->app;
    GError *error = NULL;
    char *stderr_text = NULL;
    if (!native_scan_path || !g_file_test(native_scan_path, G_FILE_TEST_IS_EXECUTABLE)) {
        g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                            "Native scanner program is missing. Run make first.");
        goto failed;
    }
    unsigned x = (unsigned)lround(job->left * 300 / 25.4);
    unsigned y = (unsigned)lround(job->top * 300 / 25.4);
    unsigned right = (unsigned)lround((job->left + job->width) * 300 / 25.4);
    unsigned bottom = (unsigned)lround((job->top + job->height) * 300 / 25.4);
    x = MIN(x, HP4200_MAX_X_300); y = MIN(y, HP4200_MAX_Y_300);
    right = MIN(right, HP4200_MAX_X_300);
    bottom = MIN(bottom, HP4200_MAX_Y_300);
    if (right <= x || bottom <= y) {
        g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "The selected scan area is empty.");
        goto failed;
    }
    char *x_text = g_strdup_printf("%u", x);
    char *y_text = g_strdup_printf("%u", y);
    char *width_text = g_strdup_printf("%u", right - x);
    char *height_text = g_strdup_printf("%u", bottom - y);
    char *x_dpi_text = g_strdup_printf("%d", job->x_dpi);
    char *y_dpi_text = g_strdup_printf("%d", job->y_dpi);
    const char *scan_args[] = {native_scan_path, x_text, y_text,
        width_text, height_text, x_dpi_text, y_dpi_text,
        job->scan_path, NULL};
    GSubprocessLauncher *launcher = g_subprocess_launcher_new(
        G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_PIPE);
    g_subprocess_launcher_setenv(launcher, "HP4200_DIAGNOSTIC", "1", TRUE);
    char *buffer_text = g_strdup_printf("%d", job->host_buffer_mib);
    g_subprocess_launcher_setenv(launcher, "HP4200_HOST_BUFFER_MIB",
                                   buffer_text, TRUE);
    g_subprocess_launcher_setenv(launcher, "HP4200_SQUARE_PIXELS",
        job->y_dpi == job->x_dpi * 2 ? "1" : "0", TRUE);
    g_free(buffer_text);
    GSubprocess *process = g_subprocess_launcher_spawnv(launcher, scan_args, &error);
    g_object_unref(launcher);
    g_free(x_text); g_free(y_text); g_free(width_text);
    g_free(height_text); g_free(x_dpi_text); g_free(y_dpi_text);
    if (!process) goto failed;
    g_mutex_lock(&app->process_lock);
    app->scan_process = g_object_ref(process);
    if (app->cancel_requested) g_subprocess_send_signal(process, SIGTERM);
    g_mutex_unlock(&app->process_lock);
    GDataInputStream *stream = g_data_input_stream_new(
        g_subprocess_get_stderr_pipe(process));
    GString *log = g_string_new(NULL);
    char *line;
    while ((line = g_data_input_stream_read_line(stream, NULL, NULL, &error))) {
        g_string_append_printf(log, "%s\n", line);
        record_transport_line(app, line);
        g_free(line);
    }
    g_object_unref(stream);
    if (error) g_subprocess_force_exit(process);
    gboolean started = error == NULL;
    if (!g_subprocess_wait(process, NULL, started ? &error : NULL))
        started = FALSE;
    stderr_text = g_string_free(log, FALSE);
    g_mutex_lock(&app->process_lock);
    gboolean cancelled = app->cancel_requested;
    g_clear_object(&app->scan_process);
    g_mutex_unlock(&app->process_lock);
    gboolean succeeded = started && g_subprocess_get_successful(process);
    g_object_unref(process);
    if (!succeeded) {
        if (!error) {
            char *summary = scan_failure_summary(stderr_text);
            g_set_error_literal(&error, G_IO_ERROR,
                cancelled ? G_IO_ERROR_CANCELLED : G_IO_ERROR_FAILED,
                cancelled ? "Scan cancelled." : summary);
            g_free(summary);
        }
        goto failed;
    }
    if (stderr_text) {
        char **lines = g_strsplit(stderr_text, "\n", -1);
        for (unsigned i = 0; lines[i]; ++i)
            if (g_str_has_prefix(lines[i], "HP scanner power-drop status ")) {
                job->warning = g_strdup(lines[i]);
                break;
            }
        g_strfreev(lines);
    }
    if (job->format != 0 && !convert_native_image(job, job->scan_path, &error))
        goto failed;
    GStatBuf info;
    if (g_stat(job->path, &info) != 0 || info.st_size <= 0) {
        g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_FAILED, "Scanner returned an empty file.");
        goto failed;
    }
    if (job->format != 0) g_unlink(job->scan_path);
    g_free(stderr_text);
    g_task_return_boolean(task, TRUE);
    return;
failed:
    if (job->scan_path && job->format != 0) g_unlink(job->scan_path);
    if (!error) {
        char *summary = scan_failure_summary(stderr_text);
        g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_FAILED, summary);
        g_free(summary);
    }
    g_free(stderr_text);
    g_task_return_error(task, error);
}

static void job_done(GObject *source, GAsyncResult *result, gpointer user_data) {
    (void)source;
    App *app = user_data;
    ScanJob *job = g_task_get_task_data(G_TASK(result));
    if (app->progress_timer) {
        g_source_remove(app->progress_timer);
        app->progress_timer = 0;
    }
    g_clear_pointer(&app->active_path, g_free);
    GError *error = NULL;
    if (!g_task_propagate_boolean(G_TASK(result), &error)) {
        message(app, error->message);
        g_clear_error(&error);
    } else if (job->preview) {
        GdkPixbuf *image = gdk_pixbuf_new_from_file(job->path, &error);
        if (image) {
            g_clear_object(&app->image);
            app->image = image;
            if (app->preview_path) g_unlink(app->preview_path);
            g_free(app->preview_path);
            app->preview_path = g_strdup(job->path);
            selection(app, 0, 0, 1, 1);
            auto_select(NULL, app);
            gtk_widget_queue_draw(app->canvas);
        } else {
            message(app, error->message);
            g_clear_error(&error);
        }
    } else {
        char *text = g_strdup_printf("Saved %s", job->path);
        message(app, text);
        g_free(text);
        GDateTime *now = g_date_time_new_now_local();
        char *name = g_date_time_format(now, "scan-%Y%m%d-%H%M%S");
        gtk_editable_set_text(GTK_EDITABLE(app->filename), name);
        g_free(name); g_date_time_unref(now);
    }
    if (job->warning) {
        char *text = g_strdup_printf("%s\n%s", gtk_label_get_text(GTK_LABEL(app->status)),
                                     job->warning);
        message(app, text);
        g_free(text);
    }
    set_busy(app, FALSE);
    app->cancel_requested = FALSE;
}

static void start_scan(App *app, gboolean preview) {
    if (app->busy) return;
    ScanJob *job = g_new0(ScanJob, 1);
    job->app = app;
    job->preview = preview;
    if (preview) {
        int fd = g_file_open_tmp("scanjet-preview-XXXXXX.png", &job->path, NULL);
        if (fd < 0) { message(app, "Could not create preview file."); job_free(job); return; }
        close(fd);
        g_unlink(job->path);
        job->scan_path = g_strdup(job->path);
        job->left = job->top = 0;
        job->width = BED_W; job->height = BED_H;
        job->x_dpi = job->y_dpi = 75; job->format = 0;
        job->host_buffer_mib = 8;
    } else {
        job->left = gtk_spin_button_get_value(GTK_SPIN_BUTTON(app->spin[0]));
        job->top = gtk_spin_button_get_value(GTK_SPIN_BUTTON(app->spin[1]));
        job->width = fmin(gtk_spin_button_get_value(GTK_SPIN_BUTTON(app->spin[2])), BED_W - job->left);
        job->height = fmin(gtk_spin_button_get_value(GTK_SPIN_BUTTON(app->spin[3])), BED_H - job->top);
        if (job->width < 1 || job->height < 1) {
            message(app, "Select an area at least 1 mm wide and high."); job_free(job); return;
        }
        const char *name = gtk_editable_get_text(GTK_EDITABLE(app->filename));
        const char *folder = gtk_editable_get_text(GTK_EDITABLE(app->folder));
        if (!*name || !strcmp(name, ".") || !strcmp(name, "..") || strchr(name, '/') || strchr(name, '\\')) {
            message(app, "Enter a file name without path separators."); job_free(job); return;
        }
        if (!g_file_test(folder, G_FILE_TEST_IS_DIR)) {
            message(app, "Choose an existing save folder."); job_free(job); return;
        }
        unsigned resolution = gtk_drop_down_get_selected(GTK_DROP_DOWN(app->dpi));
        if (resolution >= G_N_ELEMENTS(resolutions)) {
            message(app, "Choose a scan resolution."); job_free(job); return;
        }
        job->x_dpi = resolutions[resolution].x;
        job->y_dpi = resolutions[resolution].y;
        unsigned format = gtk_drop_down_get_selected(GTK_DROP_DOWN(app->format));
        if (format >= G_N_ELEMENTS(formats)) {
            message(app, "Choose an output format."); job_free(job); return;
        }
        job->format = (int)format;
        unsigned buffer_mode = gtk_drop_down_get_selected(
            GTK_DROP_DOWN(app->buffer_mode));
        const int buffer_sizes[] = {8, 16, 128};
        if (buffer_mode >= G_N_ELEMENTS(buffer_sizes)) {
            message(app, "Choose a scan speed."); job_free(job); return;
        }
        job->host_buffer_mib = buffer_sizes[buffer_mode];
        char *base = g_strdup_printf("%s.%s", name, formats[job->format]);
        job->path = g_build_filename(folder, base, NULL);
        g_free(base);
        if (g_file_test(job->path, G_FILE_TEST_EXISTS)) {
            message(app, "That file already exists. Choose a different name."); job_free(job); return;
        }
        if (job->format == 0) {
            job->scan_path = g_strdup(job->path);
        } else {
            int fd = g_file_open_tmp("scanjet-native-XXXXXX.png",
                                     &job->scan_path, NULL);
            if (fd < 0) {
                message(app, "Could not create scan temporary file.");
                job_free(job); return;
            }
            close(fd);
            g_unlink(job->scan_path);
        }
    }
    set_busy(app, TRUE);
    message(app, "Scanning… Keep an eye on the scanner while its head moves.");
    app->active_path = g_strdup(job->scan_path);
    app->scan_started = app->last_growth = g_get_monotonic_time();
    app->last_size = 0;
    g_mutex_lock(&app->process_lock);
    app->last_transport_activity = 0;
    app->transport_bytes = 0;
    app->transport_base = 0;
    g_mutex_unlock(&app->process_lock);
    app->progress_timer = g_timeout_add_seconds(5, progress_tick, app);
    GTask *task = g_task_new(NULL, NULL, job_done, app);
    g_task_set_task_data(task, job, (GDestroyNotify)job_free);
    g_task_run_in_thread(task, run_job);
    g_object_unref(task);
}

static void preview_clicked(GtkButton *button, gpointer data) { (void)button; start_scan(data, TRUE); }
static void scan_clicked(GtkButton *button, gpointer data) { (void)button; start_scan(data, FALSE); }
static void full_clicked(GtkButton *button, gpointer data) { (void)button; selection(data, 0, 0, 1, 1); }

static void draw_clicked(GtkButton *button, gpointer data) {
    App *app = data;
    app->draw_new = !app->draw_new;
    gtk_button_set_label(button, app->draw_new ? "Drawing next area…" : "Draw area");
    message(app, app->draw_new ? "Drag on the preview to draw a new area." : "Ready.");
}

static void folder_response(GObject *dialog, GAsyncResult *result, gpointer user_data) {
    App *app = user_data;
    GError *error = NULL;
    GFile *file = gtk_file_dialog_select_folder_finish(GTK_FILE_DIALOG(dialog), result, &error);
    if (file) {
        char *path = g_file_get_path(file);
        if (path) gtk_editable_set_text(GTK_EDITABLE(app->folder), path);
        g_free(path); g_object_unref(file);
    }
    g_clear_error(&error);
}

static void browse_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    App *app = user_data;
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Choose scan folder");
    GFile *folder = g_file_new_for_path(gtk_editable_get_text(GTK_EDITABLE(app->folder)));
    gtk_file_dialog_set_initial_folder(dialog, folder);
    g_object_unref(folder);
    gtk_file_dialog_select_folder(dialog, GTK_WINDOW(app->window), NULL, folder_response, app);
    g_object_unref(dialog);
}

static GtkWidget *label(const char *text) {
    GtkWidget *widget = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(widget), 0);
    return widget;
}

static gboolean close_request(GtkWindow *window, gpointer user_data) {
    (void)window;
    App *app = user_data;
    if (!app->busy) return FALSE;
    message(app, "Wait for the scan to finish before closing the window.");
    return TRUE;
}

static void window_destroy(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    App *app = user_data;
    if (app->preview_path) g_unlink(app->preview_path);
    g_free(app->preview_path);
    g_free(app->active_path);
    g_clear_object(&app->image);
    g_mutex_clear(&app->process_lock);
    g_free(app);
}

static void activate(GtkApplication *application, gpointer user_data) {
    (void)user_data;
    App *app = g_new0(App, 1);
    g_mutex_init(&app->process_lock);
    app->window = gtk_application_window_new(application);
    gtk_window_set_title(GTK_WINDOW(app->window), "ScanJet 4200C");
    gtk_window_set_default_size(GTK_WINDOW(app->window), 1050, 800);
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(root, 12); gtk_widget_set_margin_end(root, 12);
    gtk_widget_set_margin_top(root, 12); gtk_widget_set_margin_bottom(root, 12);
    gtk_window_set_child(GTK_WINDOW(app->window), root);
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(root), bar);
    app->preview_button = gtk_button_new_with_label("Preview glass");
    app->auto_button = gtk_button_new_with_label("Auto select");
    app->full_button = gtk_button_new_with_label("Full glass");
    app->draw_button = gtk_button_new_with_label("Draw area");
    app->scan_button = gtk_button_new_with_label("Scan selected area");
    app->cancel_button = gtk_button_new_with_label("Cancel scan");
    gtk_widget_set_sensitive(app->cancel_button, FALSE);
    gtk_widget_add_css_class(app->scan_button, "suggested-action");
    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(bar), app->preview_button);
    gtk_box_append(GTK_BOX(bar), app->auto_button);
    gtk_box_append(GTK_BOX(bar), app->full_button);
    gtk_box_append(GTK_BOX(bar), app->draw_button);
    gtk_box_append(GTK_BOX(bar), spacer);
    gtk_box_append(GTK_BOX(bar), app->cancel_button);
    gtk_box_append(GTK_BOX(bar), app->scan_button);
    g_signal_connect(app->preview_button, "clicked", G_CALLBACK(preview_clicked), app);
    g_signal_connect(app->auto_button, "clicked", G_CALLBACK(auto_select), app);
    g_signal_connect(app->full_button, "clicked", G_CALLBACK(full_clicked), app);
    g_signal_connect(app->draw_button, "clicked", G_CALLBACK(draw_clicked), app);
    g_signal_connect(app->scan_button, "clicked", G_CALLBACK(scan_clicked), app);
    g_signal_connect(app->cancel_button, "clicked", G_CALLBACK(cancel_clicked), app);
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_vexpand(paned, TRUE);
    gtk_box_append(GTK_BOX(root), paned);
    GtkWidget *frame = gtk_frame_new("Preview · drag handles to resize or inside to move");
    gtk_paned_set_start_child(GTK_PANED(paned), frame);
    gtk_paned_set_position(GTK_PANED(paned), 740);
    app->canvas = gtk_drawing_area_new();
    gtk_widget_set_size_request(app->canvas, 380, 500);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(app->canvas), draw, app, NULL);
    gtk_frame_set_child(GTK_FRAME(frame), app->canvas);
    GtkGesture *drag = gtk_gesture_drag_new();
    gtk_widget_add_controller(app->canvas, GTK_EVENT_CONTROLLER(drag));
    g_signal_connect(drag, "drag-begin", G_CALLBACK(drag_begin), app);
    g_signal_connect(drag, "drag-update", G_CALLBACK(drag_update), app);
    g_signal_connect(drag, "drag-end", G_CALLBACK(drag_end), app);
    GtkEventController *motion = gtk_event_controller_motion_new();
    gtk_widget_add_controller(app->canvas, motion);
    g_signal_connect(motion, "motion", G_CALLBACK(pointer_motion), app);
    GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(panel, 12); gtk_widget_set_margin_top(panel, 12);
    gtk_widget_set_size_request(panel, 265, -1);
    gtk_paned_set_end_child(GTK_PANED(paned), panel);
    gtk_box_append(GTK_BOX(panel), label("Scan area (mm)"));
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_box_append(GTK_BOX(panel), grid);
    const char *names[] = {"Left", "Top", "Width", "Height"};
    double maximums[] = {BED_W, BED_H, BED_W, BED_H};
    for (int i = 0; i < 4; ++i) {
        gtk_grid_attach(GTK_GRID(grid), label(names[i]), 0, i, 1, 1);
        app->spin[i] = gtk_spin_button_new_with_range(0, maximums[i], 0.1);
        gtk_spin_button_set_digits(GTK_SPIN_BUTTON(app->spin[i]), 1);
        gtk_grid_attach(GTK_GRID(grid), app->spin[i], 1, i, 1, 1);
        g_signal_connect(app->spin[i], "value-changed", G_CALLBACK(spin_changed), app);
    }
    gtk_box_append(GTK_BOX(panel), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(panel), label("Final scan"));
    gtk_box_append(GTK_BOX(panel), label("Resolution"));
    const char *dpi_names[] = {"75 dpi", "150 dpi", "300 dpi", "600 dpi",
                               "600 × 1200 dpi (correct aspect)", NULL};
    app->dpi = gtk_drop_down_new_from_strings(dpi_names);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->dpi), 2);
    gtk_box_append(GTK_BOX(panel), app->dpi);
    gtk_box_append(GTK_BOX(panel), label("Format"));
    const char *format_names[] = {"PNG", "TIFF", "JPEG", "PDF", NULL};
    app->format = gtk_drop_down_new_from_strings(format_names);
    gtk_box_append(GTK_BOX(panel), app->format);
    gtk_box_append(GTK_BOX(panel), label("Scan speed"));
    const char *buffer_names[] = {"Standard", "Faster (experimental)",
                                  "Fastest (experimental)", NULL};
    app->buffer_mode = gtk_drop_down_new_from_strings(buffer_names);
    gtk_widget_set_tooltip_text(app->buffer_mode,
        "Faster modes use more host memory and fewer scanner refill pauses. "
        "Standard follows HP's 8 MiB buffer size.");
    gtk_box_append(GTK_BOX(panel), app->buffer_mode);
    gtk_box_append(GTK_BOX(panel), label("Save in"));
    GtkWidget *folder_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_append(GTK_BOX(panel), folder_row);
    app->folder = gtk_entry_new();
    gtk_widget_set_hexpand(app->folder, TRUE);
    const char *pictures = g_get_user_special_dir(G_USER_DIRECTORY_PICTURES);
    const char *initial_folder = pictures && g_file_test(pictures, G_FILE_TEST_IS_DIR) ?
                                 pictures : g_get_home_dir();
    gtk_editable_set_text(GTK_EDITABLE(app->folder), initial_folder);
    gtk_box_append(GTK_BOX(folder_row), app->folder);
    GtkWidget *browse = gtk_button_new_with_label("…");
    gtk_box_append(GTK_BOX(folder_row), browse);
    g_signal_connect(browse, "clicked", G_CALLBACK(browse_clicked), app);
    gtk_box_append(GTK_BOX(panel), label("File name (without extension)"));
    app->filename = gtk_entry_new();
    GDateTime *now = g_date_time_new_now_local();
    char *name = g_date_time_format(now, "scan-%Y%m%d-%H%M%S");
    gtk_editable_set_text(GTK_EDITABLE(app->filename), name);
    g_free(name); g_date_time_unref(now);
    gtk_box_append(GTK_BOX(panel), app->filename);
    GtkWidget *hint = label("Preview uses the native HP driver at 75 dpi. Scans may take several minutes while hardware validation continues. Cancel scan asks the driver to return the head home.");
    gtk_label_set_wrap(GTK_LABEL(hint), TRUE);
    gtk_box_append(GTK_BOX(panel), hint);
    app->status = label("Ready. Place an item on the glass, then choose Preview glass.");
    gtk_label_set_wrap(GTK_LABEL(app->status), TRUE);
    gtk_box_append(GTK_BOX(root), app->status);
    selection(app, 0, 0, 1, 1);
    g_signal_connect(app->window, "close-request", G_CALLBACK(close_request), app);
    g_signal_connect(app->window, "destroy", G_CALLBACK(window_destroy), app);
    gtk_window_present(GTK_WINDOW(app->window));
}

int main(int argc, char **argv) {
    char *located = g_find_program_in_path(argv[0]);
    char *absolute = g_canonicalize_filename(located ? located : argv[0], NULL);
    char *directory = g_path_get_dirname(absolute);
    const char *configured_scan = g_getenv("HP4200_NATIVE_SCAN");
    if (configured_scan && *configured_scan) {
        native_scan_path = g_canonicalize_filename(configured_scan, NULL);
    } else {
        native_scan_path = g_build_filename(directory, "driver", "native_scan", NULL);
        if (!g_file_test(native_scan_path, G_FILE_TEST_IS_EXECUTABLE)) {
            g_free(native_scan_path);
            native_scan_path = g_build_filename(directory, "native_scan", NULL);
        }
    }
    g_free(directory);
    g_free(absolute);
    g_free(located);
    GtkApplication *application = gtk_application_new("org.scanjet4200c.Scanner", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(application, "activate", G_CALLBACK(activate), NULL);
    int result = g_application_run(G_APPLICATION(application), argc, argv);
    g_object_unref(application);
    g_free(native_scan_path);
    return result;
}
