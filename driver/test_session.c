#include "hp4200_session.h"
#include <assert.h>
#include <stdio.h>

typedef struct {
    unsigned stop_writes, return_writes, lamp_off_writes;
    uint8_t last_command;
} fake;

static int read_reg(void *context, uint8_t address, uint8_t *value)
{
    (void)context;
    assert(address == 0x58 || address == 0x69);
    *value = address == 0x58 ? 0 : 0xff;
    return 0;
}

static int write_reg(void *context, uint8_t address, uint8_t value)
{
    fake *f = context;
    if (address == 7) {
        f->last_command = value;
        if (value == 0) ++f->stop_writes;
        if (value == 2) ++f->return_writes;
    }
    if (address >= 0x2c && address <= 0x2f)
        ++f->lamp_off_writes;
    return 0;
}

static void sleep_ms(void *context, unsigned ms)
{
    (void)context;
    (void)ms;
}

static int bridge(void *context, uint8_t index, uint8_t value)
{
    (void)context; (void)index; (void)value;
    return 0;
}

static int read_bulk(void *context, uint8_t *data, size_t length)
{
    (void)context; (void)data; (void)length;
    return 0;
}

static int read_ram(void *context, uint8_t port, uint8_t *data, size_t length)
{
    (void)port;
    return read_bulk(context, data, length);
}

static int write_ram(void *context, uint8_t port,
                     const uint8_t *data, size_t length)
{
    (void)context; (void)port; (void)data; (void)length;
    return 0;
}

static uint64_t tick(void *context) { (void)context; return 0; }

int main(void)
{
    fake f = {0};
    hp4200_session session = {0};
    session.io.context = &f;
    session.io.read_register = read_reg;
    session.io.write_register = write_reg;
    session.io.sleep_ms = sleep_ms;
    session.engine.io = &session.io;
    session.engine.motor_running = 1;
    session.started = 1;
    session.lamp_on = 1;
    assert(hp4200_session_end(&session) == 0);
    assert(f.stop_writes == 1 && f.return_writes == 1);
    assert(f.last_command == 2 && f.lamp_off_writes == 4);
    assert(hp4200_session_end(&session) == 0);
    assert(f.return_writes == 1 && f.lamp_off_writes == 4);
    session.started = 1;
    session.stage = 19;
    assert(hp4200_session_end(&session) == 0);
    assert(f.stop_writes == 1 && f.last_command == 2);
    assert(hp4200_session_end(&session) == 0);
    assert(f.stop_writes == 1 && f.last_command == 2);
    /* A failed setup stops once; a second cleanup must not halt a later
     * return-to-home command. Preserve the failure stage for diagnostics. */
    session.ended = 0;
    session.started = 0;
    session.stage = 15;
    assert(hp4200_session_end(&session) == 0);
    assert(f.stop_writes == 2 && f.last_command == 0);
    assert(session.stage == 15);
    assert(hp4200_session_end(&session) == 0);
    assert(f.stop_writes == 2);
    hp4200_io io = {.context = &f, .read_register = read_reg,
                    .write_register = write_reg, .write_bridge = bridge,
                    .bulk_read = read_bulk, .bulk_read_ram = read_ram,
                    .bulk_write_ram = write_ram, .monotonic_ms = tick,
                    .sleep_ms = sleep_ms};
    hp4200_scan_request invalid = {.x_300 = HP4200_MAX_X_300,
                                   .width_300 = 2, .height_300 = 1,
                                   .x_dpi = 300, .y_dpi = 300};
    assert(hp4200_session_begin(&session, &io, &invalid) == -1);
    assert(session.stage == 1 && session.ended);
    assert(f.stop_writes == 2);
    assert(hp4200_session_end(&session) == 0 && f.stop_writes == 2);
    invalid.x_300 = 0;
    invalid.width_300 = 1;
    invalid.x_dpi = 75;
    assert(hp4200_session_begin(&session, &io, &invalid) == -1);
    assert(session.stage == 1 && f.stop_writes == 2);
    puts("HP session stop/return tests passed");
    return 0;
}
