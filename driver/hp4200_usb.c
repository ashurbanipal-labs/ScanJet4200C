#define _POSIX_C_SOURCE 200809L
#include "hp4200_usb.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#define HP_VENDOR 0x03f0
#define HP_PRODUCT 0x0105
#define PV_EPP_ADDRESS 0x01
#define PV_DATA 0x00
#define PV_READ_BYTE 0x00
#define PV_WRITE_BYTE 0x01
#define PV_PREP_READ 0x02
#define PV_PREP_WRITE 0x03
#define HP_BULK_IN 0x82
#define HP_BULK_OUT 0x01
#define HP_BULK_TIMEOUT_MS 5000

static uint64_t monotonic_ms(void *context);

static void record_bulk_result(hp4200_usb *t, char direction,
                               uint8_t port, size_t requested,
                               int result, int transferred)
{
    if (result == 0 && transferred == (int)requested) {
        t->bulk_requested = 0;
        return;
    }
    t->bulk_direction = direction;
    t->bulk_port = port;
    t->bulk_requested = requested;
    t->bulk_result = result;
    t->bulk_transferred = transferred;
}

static int control_write(hp4200_usb *t, uint8_t index, uint8_t value)
{
    int rc = libusb_control_transfer(t->device, 0x40, PV_WRITE_BYTE,
                                     value, index, NULL, 0, 3000);
    return rc == 0 ? 0 : -1;
}

static int write_bridge(void *context, uint8_t index, uint8_t value)
{
    return control_write(context, index, value);
}

static int read_register(void *context, uint8_t reg, uint8_t *value)
{
    hp4200_usb *t = context;
    int rc;
    if (!value || control_write(t, PV_EPP_ADDRESS, reg)) return -1;
    rc = libusb_control_transfer(t->device, 0xc0, PV_READ_BYTE,
                                 0, PV_DATA, value, 1, 3000);
    if (rc == 1 && reg == 1 && t->diagnostic_scan) {
        uint64_t now = monotonic_ms(context);
        if (now >= t->diagnostic_next_ms) {
            fprintf(stderr, "native USB: scanner buffer %u KiB, received %llu bytes\n",
                    *value, (unsigned long long)t->diagnostic_bulk_bytes);
            t->diagnostic_next_ms = now + 5000;
        }
    }
    return rc == 1 ? 0 : -1;
}

static int write_register(void *context, uint8_t reg, uint8_t value)
{
    hp4200_usb *t = context;
    if (control_write(t, PV_EPP_ADDRESS, reg)) return -1;
    return control_write(t, PV_DATA, value);
}

static int bulk_read_ram(void *context, uint8_t port,
                          uint8_t *data, size_t length)
{
    hp4200_usb *t = context;
    int transferred = 0;
    int rc;
    if (!data || !length || length > 65535) return -1;
    if (control_write(t, PV_EPP_ADDRESS, port)) return -1;
    rc = libusb_control_transfer(t->device, 0x40, PV_PREP_READ,
                                 (uint16_t)length, (uint16_t)(length >> 16),
                                 NULL, 0, 3000);
    if (rc != 0) return -1;
    /* HP's 15-second limit applies to the whole read's lack of progress.
     * Bound one USB transaction more tightly so SIGTERM can reach the
     * motor-return path when a host transfer itself stalls. */
    rc = libusb_bulk_transfer(t->device, HP_BULK_IN, data, (int)length,
                              &transferred, HP_BULK_TIMEOUT_MS);
    record_bulk_result(t, 'R', port, length, rc, transferred);
    if (t->diagnostic_scan && port == 0 && rc == 0)
        t->diagnostic_bulk_bytes += (uint64_t)transferred;
    return rc == 0 && transferred == (int)length ? 0 : -1;
}

static int bulk_read(void *context, uint8_t *data, size_t length)
{
    return bulk_read_ram(context, 0, data, length);
}

static int bulk_write_ram(void *context, uint8_t port,
                          const uint8_t *data, size_t length)
{
    hp4200_usb *t = context;
    int transferred = 0;
    int rc;
    if (!data || !length || length > 65535) return -1;
    if (control_write(t, PV_EPP_ADDRESS, port)) return -1;
    rc = libusb_control_transfer(t->device, 0x40, PV_PREP_WRITE,
                                 (uint16_t)length, (uint16_t)(length >> 16),
                                 NULL, 0, 3000);
    if (rc != 0) return -1;
    rc = libusb_bulk_transfer(t->device, HP_BULK_OUT, (unsigned char *)data,
                              (int)length, &transferred, HP_BULK_TIMEOUT_MS);
    record_bulk_result(t, 'W', port, length, rc, transferred);
    return rc == 0 && transferred == (int)length ? 0 : -1;
}

static uint64_t monotonic_ms(void *context)
{
    struct timespec ts;
    (void)context;
    if (clock_gettime(CLOCK_MONOTONIC, &ts)) return 0;
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static void sleep_ms(void *context, unsigned milliseconds)
{
    struct timespec remaining = {(time_t)(milliseconds / 1000),
                                 (long)(milliseconds % 1000) * 1000000L};
    (void)context;
    while (nanosleep(&remaining, &remaining) != 0 && errno == EINTR) { }
}

static int cancelled(void *context)
{
    return atomic_load(&((hp4200_usb *)context)->cancel);
}

int hp4200_usb_open(hp4200_usb *t)
{
    libusb_device **devices = NULL;
    ssize_t count;
    int rc;
    if (!t) return -1;
    memset(t, 0, sizeof(*t));
    atomic_init(&t->cancel, 0);
    rc = libusb_init(&t->usb);
    if (rc) return rc;
    count = libusb_get_device_list(t->usb, &devices);
    if (count < 0) {
        hp4200_usb_close(t);
        return (int)count;
    }
    rc = LIBUSB_ERROR_NO_DEVICE;
    for (ssize_t i = 0; i < count; ++i) {
        struct libusb_device_descriptor descriptor;
        if (libusb_get_device_descriptor(devices[i], &descriptor)) continue;
        if (descriptor.idVendor != HP_VENDOR ||
            descriptor.idProduct != HP_PRODUCT) continue;
        int open_result = libusb_open(devices[i], &t->device);
        if (open_result == 0) break;
        rc = open_result;
    }
    libusb_free_device_list(devices, 1);
    if (!t->device) { hp4200_usb_close(t); return rc; }
    rc = libusb_claim_interface(t->device, 0);
    if (rc) { hp4200_usb_close(t); return rc; }
    t->claimed = 1;
    return 0;
}

void hp4200_usb_close(hp4200_usb *t)
{
    if (!t) return;
    if (t->claimed) libusb_release_interface(t->device, 0);
    if (t->device) libusb_close(t->device);
    if (t->usb) libusb_exit(t->usb);
    memset(t, 0, sizeof(*t));
}

void hp4200_usb_cancel(hp4200_usb *t)
{
    if (t) atomic_store(&t->cancel, 1);
}

void hp4200_usb_make_io(hp4200_usb *t, hp4200_io *io)
{
    memset(io, 0, sizeof(*io));
    io->context = t;
    io->read_register = read_register;
    io->write_register = write_register;
    io->write_bridge = write_bridge;
    io->bulk_read = bulk_read;
    io->bulk_read_ram = bulk_read_ram;
    io->bulk_write_ram = bulk_write_ram;
    io->monotonic_ms = monotonic_ms;
    io->sleep_ms = sleep_ms;
    io->cancelled = cancelled;
}
