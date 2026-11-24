#include "hp4200_ram.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t color[3][1024];
    uint8_t shading[3][0x2aa8];
    uint8_t address, mode;
    unsigned writes, reads;
    int corrupt;
    int selective_corrupt;
    unsigned corrupt_channel, corrupt_shading;
    size_t corrupt_offset, corrupt_span;
    unsigned stale_reads;
    int shifted_shading;
    unsigned dropped_shading_writes;
    unsigned read_fail_at;
} fake;

static int write_reg(void *context, uint8_t reg, uint8_t value)
{
    fake *f = context;
    if (reg == 3) f->address = value;
    if (reg == 4) f->mode = value;
    return 0;
}

static int write_ram(void *context, uint8_t port,
                     const uint8_t *data, size_t length)
{
    fake *f = context;
    unsigned channel = f->address >> 1;
    assert(port == 6 && channel < 3 && f->mode == 0);
    assert(length == (f->address & 1 ? 0x2aa8U : 1024U));
    if ((f->address & 1) && channel == 2 && f->dropped_shading_writes)
        --f->dropped_shading_writes;
    else
        memcpy(f->address & 1 ? f->shading[channel] : f->color[channel],
               data, length);
    ++f->writes;
    return 0;
}

static int read_ram(void *context, uint8_t port,
                    uint8_t *data, size_t length)
{
    fake *f = context;
    unsigned channel = f->address >> 1;
    assert(port == 6 && channel < 3 && f->mode == 0x20);
    assert(length == (f->address & 1 ? 0x2aa8U : 1024U));
    if (++f->reads == f->read_fail_at) return -1;
    memcpy(data, f->address & 1 ? f->shading[channel] : f->color[channel],
           length);
    if (f->shifted_shading && channel == 2 && (f->address & 1)) {
        memmove(data, data + 1, length - 1);
        data[length - 1] = 0;
    }
    if (f->corrupt || f->stale_reads) data[0] ^= 1;
    if (f->selective_corrupt && channel == f->corrupt_channel &&
        (unsigned)(f->address & 1) == f->corrupt_shading)
        for (size_t i = 0; i < f->corrupt_span; ++i)
            data[f->corrupt_offset + i] ^= 1;
    if (f->stale_reads) --f->stale_reads;
    return 0;
}

int main(void)
{
    fake f = {0};
    unsigned final_rewrites = 0;
    hp4200_ram_diagnostic final_diagnostic = {0};
    hp4200_io io = {.context = &f, .write_register = write_reg,
                    .bulk_write_ram = write_ram, .bulk_read_ram = read_ram,
                    .verified_plane_rewrites = &final_rewrites,
                    .final_ram_diagnostic = &final_diagnostic};
    hp4200_ram_diagnostic diagnostic;
    assert(hp4200_selftest_ram(&io) == 0);
    assert(f.writes == 6 && f.reads == 6);
    assert(f.shading[0][0] == 0 && f.shading[0][3] == 1);
    f.stale_reads = 2;
    assert(hp4200_selftest_ram_diagnose(&io, &diagnostic) == 0);
    assert(!diagnostic.has_mismatch);
    assert(f.reads == 14); /* two full retries after stale first bytes */
    f.dropped_shading_writes = 1;
    memset(f.shading[2], 0, sizeof(f.shading[2]));
    unsigned prior_writes = f.writes;
    assert(hp4200_selftest_ram_diagnose(&io, &diagnostic) == 0);
    assert(!diagnostic.has_mismatch && diagnostic.write_retries == 1);
    assert(f.writes == prior_writes + 7);
    f.corrupt = 1;
    assert(hp4200_selftest_ram_diagnose(&io, &diagnostic) == -120);
    assert(diagnostic.has_mismatch && diagnostic.channel == 0 &&
           !diagnostic.shading && diagnostic.attempt == 3 &&
           diagnostic.offset == 0 && diagnostic.expected == 0 &&
           diagnostic.actual == 1 && diagnostic.last_offset == 0 &&
           diagnostic.mismatch_count == 1);
    f.corrupt = 0;
    f.selective_corrupt = 1;
    f.corrupt_channel = 2;
    f.corrupt_shading = 1;
    f.corrupt_offset = 317;
    f.corrupt_span = 3;
    assert(hp4200_selftest_ram_diagnose(&io, &diagnostic) == -152);
    assert(diagnostic.has_mismatch && diagnostic.channel == 2 &&
           diagnostic.shading && diagnostic.attempt == 3 &&
           diagnostic.offset == 317 &&
           diagnostic.last_offset == 319 && diagnostic.mismatch_count == 3 &&
           diagnostic.actual == (uint8_t)(diagnostic.expected ^ 1));
    f.selective_corrupt = 0;
    f.shifted_shading = 1;
    assert(hp4200_selftest_ram_diagnose(&io, &diagnostic) == -152);
    assert(diagnostic.channel == 2 && diagnostic.shading &&
           diagnostic.matches_next > diagnostic.mismatch_count / 2);
    f.shifted_shading = 0;
    f.dropped_shading_writes = 1;
    uint8_t final_plane[0x2aa8];
    memset(final_plane, 0x5a, sizeof(final_plane));
    unsigned before_writes = f.writes, before_reads = f.reads;
    assert(hp4200_write_verified_plane(&io, 2, 1, final_plane,
                                       sizeof(final_plane)) == 0);
    assert(f.writes == before_writes + 2 && f.reads == before_reads + 4);
    assert(final_rewrites == 1);
    assert(!final_diagnostic.has_mismatch &&
           final_diagnostic.write_retries == 1);
    assert(f.mode == 0 && !memcmp(f.shading[2], final_plane,
                                  sizeof(final_plane)));
    f.corrupt = 1;
    assert(hp4200_write_verified_plane(&io, 2, 1, final_plane,
                                       sizeof(final_plane)) == -2);
    assert(final_rewrites == 2);
    assert(final_diagnostic.has_mismatch &&
           final_diagnostic.channel == 2 && final_diagnostic.shading &&
           final_diagnostic.attempt == 3 &&
           final_diagnostic.offset == 0 &&
           final_diagnostic.mismatch_count == 1 &&
           final_diagnostic.write_retries == 1);
    assert(f.mode == 0);
    f.corrupt = 0;
    f.stale_reads = 1;
    f.read_fail_at = f.reads + 2;
    before_writes = f.writes;
    unsigned before_rewrites = final_rewrites;
    assert(hp4200_write_verified_plane(&io, 2, 1, final_plane,
                                       sizeof(final_plane)) == -1);
    assert(f.writes == before_writes + 1 &&
           final_rewrites == before_rewrites);
    assert(!final_diagnostic.has_mismatch);
    assert(f.mode == 0);
    puts("HP ASIC RAM self-test tests passed");
    return 0;
}
