#include "hp4200_bridge.h"

#include <assert.h>
#include <stdio.h>

typedef struct {
    uint8_t index[6], value[6];
    unsigned writes, sleeps;
} fake;

static int write_bridge(void *context, uint8_t index, uint8_t value)
{
    fake *f = context;
    assert(f->writes < 6);
    f->index[f->writes] = index;
    f->value[f->writes++] = value;
    return 0;
}

static void sleep_ms(void *context, unsigned ms)
{
    fake *f = context;
    assert(ms == 2);
    ++f->sleeps;
}

int main(void)
{
    fake f = {0};
    hp4200_io io = {.context = &f, .write_bridge = write_bridge,
                    .sleep_ms = sleep_ms};
    const uint8_t expected[] = {2, 0x99, 0x66, 0xcc, 0x33, 0x16};
    assert(hp4200_bridge_activate(&io) == 0);
    assert(f.writes == 6 && f.sleeps == 4);
    for (unsigned i = 0; i < 6; ++i) {
        assert(f.index[i] == (i == 0 || i == 5 ? 3 : 0));
        assert(f.value[i] == expected[i]);
    }
    puts("HP PV8630 bridge activation tests passed");
    return 0;
}
