#ifndef HP4200_USB_H
#define HP4200_USB_H

#include "hp4200_io.h"
#include <libusb.h>
#include <stdatomic.h>

typedef struct {
    libusb_context *usb;
    libusb_device_handle *device;
    int claimed;
    atomic_int cancel;
    int diagnostic_scan;
    uint64_t diagnostic_next_ms, diagnostic_bulk_bytes;
    /* Most recent failed bulk operation; cleared by a complete transfer. */
    int bulk_result, bulk_transferred;
    size_t bulk_requested;
    uint8_t bulk_port;
    char bulk_direction;
} hp4200_usb;

int hp4200_usb_open(hp4200_usb *transport);
void hp4200_usb_close(hp4200_usb *transport);
void hp4200_usb_make_io(hp4200_usb *transport, hp4200_io *io);
void hp4200_usb_cancel(hp4200_usb *transport);

#endif
