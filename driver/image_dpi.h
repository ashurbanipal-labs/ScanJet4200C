#ifndef HP4200_IMAGE_DPI_H
#define HP4200_IMAGE_DPI_H

/* Add physical resolution to the JPEG JFIF header or existing TIFF IFD.
 * The GdkPixbuf savers used by the GTK app emit these structures but ignore
 * x-dpi/y-dpi options. Returns zero only if both axes were updated. */
int hp4200_set_jpeg_xy_dpi(const char *path, unsigned x_dpi,
                           unsigned y_dpi);
int hp4200_set_tiff_xy_dpi(const char *path, unsigned x_dpi,
                           unsigned y_dpi);
int hp4200_set_jpeg_dpi(const char *path, unsigned dpi);
int hp4200_set_tiff_dpi(const char *path, unsigned dpi);

#endif
