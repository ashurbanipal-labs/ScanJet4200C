#include "hp4200_usb.h"
#include <stdio.h>

int main(void)
{
    hp4200_usb usb;
    hp4200_io io;
    if (hp4200_usb_open(&usb)) {
        fputs("Could not open ScanJet 4200C\n", stderr);
        return 1;
    }
    hp4200_usb_make_io(&usb, &io);
    int result = hp4200_stop(&io);
    hp4200_usb_close(&usb);
    if (result) fputs("Motor stop command failed\n", stderr);
    else puts("Motor stop command sent");
    return result ? 1 : 0;
}
