#include "hp4200_io.h"
#include "hp4200_gain.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t available;
    unsigned polls;
    unsigned reads;
    size_t bytes;
    uint64_t now;
    int cancel;
    uint8_t last_reg;
    uint8_t last_value;
    uint8_t gains[3];
    int gain_sim;
    unsigned recover_after_polls;
    unsigned cancel_after_polls;
} fake;

static int read_reg(void *p, uint8_t reg, uint8_t *value)
{
    fake *f = p;
    assert(reg == 1 || reg == 0x45);
    ++f->polls;
    if (reg == 1 && f->recover_after_polls &&
        f->polls >= f->recover_after_polls) f->available = 80;
    *value = reg == 1 ? f->available : 0x13;
    return 0;
}

static int write_reg(void *p, uint8_t reg, uint8_t value)
{
    fake *f = p;
    f->last_reg = reg;
    f->last_value = value;
    if (reg >= 0x3b && reg <= 0x3d) f->gains[reg - 0x3b] = value;
    return 0;
}

static int bulk(void *p, uint8_t *data, size_t length)
{
    fake *f = p;
    assert(length <= 65535);
    memset(data, 0xa5, length);
    if (f->gain_sim && length >= 8) {
        const uint8_t target[3] = {0x12, 0x20, 0x05};
        for (unsigned channel = 0; channel < 3; ++channel) {
            unsigned sample = f->gains[channel] > target[channel] ? 0xf00 : 0x500;
            data[channel * 2] = (uint8_t)(sample >> 8);
            data[channel * 2 + 1] = (uint8_t)sample;
        }
    }
    ++f->reads;
    f->bytes += length;
    return 0;
}

static uint64_t tick(void *p)
{
    fake *f = p;
    f->now += 100;
    return f->now;
}

static void sleep_ms(void *p, unsigned ms) { ((fake *)p)->now += ms; }

static int cancelled(void *p) {
    fake *f = p;
    return f->cancel ||
        (f->cancel_after_polls && f->polls >= f->cancel_after_polls);
}

int main(void)
{
    fake f = {.available = 80};
    hp4200_read_wait_diagnostic wait = {0};
    hp4200_io io = {
        .context = &f, .read_register = read_reg, .write_register = write_reg,
        .bulk_read = bulk, .monotonic_ms = tick, .sleep_ms = sleep_ms,
        .cancelled = cancelled,
        .read_wait_diagnostic = &wait,
    };
    uint8_t data[70000];
    assert(hp4200_read_buffer(&io, data, sizeof(data)) == 0);
    assert(f.reads == 2 && f.bytes == sizeof(data));
    assert(data[0] == 0xa5 && data[69999] == 0xa5);
    assert(wait.last_available_kib == 80 && wait.low_buffer_polls == 0);
    f.available = 1; f.now = 0; f.polls = 0;
    assert(hp4200_read_buffer(&io, data, 10) == -4);
    assert(f.polls >= 150);
    assert(wait.last_available_kib == 1);
    assert(wait.low_buffer_polls >= 150);
    assert(wait.longest_low_buffer_ms >= 15000);
    assert(wait.total_low_buffer_ms == wait.longest_low_buffer_ms);
    memset(&wait, 0, sizeof(wait));
    f.available = 1; f.now = 0; f.polls = 0; f.recover_after_polls = 3;
    assert(hp4200_read_buffer(&io, data, 10) == 0);
    assert(wait.low_buffer_polls == 2 && wait.last_available_kib == 80);
    assert(wait.total_low_buffer_ms >= 100);
    assert(wait.longest_low_buffer_ms == wait.total_low_buffer_ms);
    f.recover_after_polls = 0;
    f.available = 1;
    memset(&wait, 0, sizeof(wait));
    f.now = 0; f.polls = 0; f.cancel_after_polls = 2;
    assert(hp4200_read_buffer(&io, data, 10) == -2);
    assert(wait.low_buffer_polls == 2 && wait.total_low_buffer_ms >= 100);
    f.cancel_after_polls = 0;
    f.cancel = 1; f.polls = 0;
    assert(hp4200_read_buffer(&io, data, 10) == -2);
    assert(f.polls == 0);
    assert(hp4200_stop(&io) == 0);
    assert(f.last_reg == 7 && f.last_value == 0);
    f.available = 80; f.cancel = 0; f.gain_sim = 1;
    uint8_t gains[3];
    assert(hp4200_calibrate_gain(&io, 6, 1, 0, gains) == 0);
    assert(gains[0] == 0x12 && gains[1] == 0x20 && gains[2] == 0x05);
    assert(f.last_reg == 0x45 && f.last_value == 0x13);
    puts("HP transport and gain tests passed");
    return 0;
}
