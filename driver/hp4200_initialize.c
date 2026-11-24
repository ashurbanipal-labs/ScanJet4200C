#include "hp4200_initialize.h"
#include "hp4200_registers.h"
#include "hp4200_bridge.h"
#include "hp4200_home.h"
#include "hp4200_ram.h"

#include <string.h>

static int failure(const hp4200_io *io)
{
    hp4200_stop(io);
    return -1;
}

int hp4200_probe_core_registers(const hp4200_io *io)
{
    uint8_t value;
    if (!io || !io->write_register || !io->read_register) return -1;
    for (unsigned reg = 0x0b; reg <= 0x0d; ++reg) {
        if (io->cancelled && io->cancelled(io->context)) return failure(io);
        if (io->write_register(io->context, (uint8_t)reg,
                               hp4200_factory_registers[reg])) return failure(io);
    }
    for (unsigned reg = 0x0b; reg <= 0x0d; ++reg) {
        if (io->cancelled && io->cancelled(io->context)) return failure(io);
        if (io->read_register(io->context, (uint8_t)reg, &value) ||
            value != hp4200_factory_registers[reg]) return failure(io);
    }
    return 0;
}

static int initialize_register_image_mode(const hp4200_io *io,
                                          unsigned sensor_bits)
{
    uint8_t value;
    if (!io || !io->write_register || !io->read_register ||
        (sensor_bits != 8 && sensor_bits != 16)) return -1;
    if (io->write_register(io->context, 0x42,
                           hp4200_factory_registers[0x42] & 0xfe))
        return failure(io);
    for (unsigned reg = 8; reg < 0x60; ++reg) {
        if (io->cancelled && io->cancelled(io->context)) return failure(io);
        if (reg == 0x28) continue;
        if (io->write_register(io->context, (uint8_t)reg,
                               reg == 0x0c && sensor_bits == 8 ? 0x60 :
                               hp4200_factory_registers[reg])) return failure(io);
    }
    for (unsigned reg = 8; reg < 0x60; ++reg) {
        if (io->cancelled && io->cancelled(io->context)) return failure(io);
        if (reg == 0x28) continue;
        if (io->read_register(io->context, (uint8_t)reg, &value) ||
            value != (reg == 0x0c && sensor_bits == 8 ? 0x60 :
                      hp4200_factory_registers[reg])) return failure(io);
    }
    return 0;
}

int hp4200_initialize_register_image(const hp4200_io *io)
{
    return initialize_register_image_mode(io, 16);
}

int hp4200_initialize_core_mode_diagnose(
    const hp4200_io *io, unsigned sensor_bits,
    hp4200_ram_diagnostic *diagnostic)
{
    uint8_t asic_status;
    int ram_result;
    if (diagnostic) memset(diagnostic, 0, sizeof(*diagnostic));
    if (!io || (sensor_bits != 8 && sensor_bits != 16) ||
        !io->read_register || !io->write_register ||
        !io->write_bridge || !io->bulk_read_ram || !io->bulk_write_ram ||
        !io->sleep_ms || !io->monotonic_ms) return -1;
    if (hp4200_bridge_activate(io)) { failure(io); return -10; }
    if (initialize_register_image_mode(io, sensor_bits)) {
        failure(io); return -11;
    }
    if (io->read_register(io->context, 0x69, &asic_status)) {
        failure(io); return -12;
    }
    if (hp4200_home_start(io)) { failure(io); return -13; }
    if (hp4200_home_wait_clear(io)) { failure(io); return -14; }
    ram_result = hp4200_selftest_ram_diagnose(io, diagnostic);
    if (ram_result) { failure(io); return -1000 + ram_result; }
    return 0;
}

int hp4200_initialize_core_mode(const hp4200_io *io, unsigned sensor_bits)
{
    return hp4200_initialize_core_mode_diagnose(io, sensor_bits, NULL);
}

int hp4200_initialize_core_mode_with_ram_retry(
    const hp4200_io *io, unsigned sensor_bits,
    hp4200_ram_diagnostic *diagnostic, unsigned *retries)
{
    hp4200_ram_diagnostic local_diagnostic = {0};
    hp4200_ram_diagnostic *report = diagnostic ? diagnostic : &local_diagnostic;
    int result;
    if (retries) *retries = 0;
    result = hp4200_initialize_core_mode_diagnose(io, sensor_bits, report);
    if (!result || !report->has_mismatch || !io || !io->sleep_ms ||
        (io->cancelled && io->cancelled(io->context))) return result;
    io->sleep_ms(io->context, 500);
    if (io->cancelled && io->cancelled(io->context)) return result;
    if (retries) *retries = 1;
    return hp4200_initialize_core_mode_diagnose(io, sensor_bits, report);
}

int hp4200_initialize_core(const hp4200_io *io)
{
    return hp4200_initialize_core_mode(io, 16);
}
