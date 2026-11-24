#include "hp4200_bridge.h"

int hp4200_bridge_activate(const hp4200_io *io)
{
    static const uint8_t daisy[4] = {0x99, 0x66, 0xcc, 0x33};
    int status = 0;
    if (!io || !io->write_bridge || !io->sleep_ms) return -1;
    if (io->write_bridge(io->context, 3, 2)) return -1;
    for (unsigned i = 0; i < 4; ++i) {
        if (io->cancelled && io->cancelled(io->context)) {
            status = -2;
            break;
        }
        if (io->write_bridge(io->context, 0, daisy[i])) {
            status = -1;
            break;
        }
        io->sleep_ms(io->context, 2);
    }
    if (io->write_bridge(io->context, 3, 0x16)) status = -1;
    return status;
}
