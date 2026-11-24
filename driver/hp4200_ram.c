#include "hp4200_ram.h"
#include "hp4200_gamma.h"
#include <stdlib.h>
#include <string.h>

static int select_ram(const hp4200_io *io, unsigned channel,
                      int shading, int read_mode)
{
    uint8_t address = (uint8_t)((channel << 1) | (shading ? 1 : 0));
    return io->write_register(io->context, 0x03, address) ||
           io->write_register(io->context, 0x04, read_mode ? 0x20 : 0) ||
           io->write_register(io->context, 0x05, 0);
}

static int write_plane(const hp4200_io *io, unsigned channel,
                       int shading, const uint8_t *pattern, size_t length)
{
    return select_ram(io, channel, shading, 0) ||
           io->bulk_write_ram(io->context, 6, pattern, length) ? -1 : 0;
}

static int verify_plane(const hp4200_io *io, unsigned channel,
                        int shading, const uint8_t *pattern, size_t length,
                        hp4200_ram_diagnostic *diagnostic)
{
    uint8_t *readback = malloc(length);
    int status = -1;
    if (!readback) return -1;
    /* Live PV8630 reads can transiently retain a status bit in byte zero.
     * Re-selecting RAM and reading the entire plane distinguishes that from
     * persistent corruption. Never accept a partial or mismatched plane. */
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        if (select_ram(io, channel, shading, 1) ||
            io->bulk_read_ram(io->context, 6, readback, length)) {
            /* A failed USB read is not evidence that the stored plane is
             * wrong. In particular, do not rewrite after an earlier
             * mismatched read if the later verification transfer fails. */
            status = -1;
            if (diagnostic) diagnostic->has_mismatch = 0;
            break;
        }
        status = memcmp(pattern, readback, length) == 0 ? 0 : -2;
        if (status == -2 && diagnostic) {
            size_t offset = 0;
            while (offset < length && pattern[offset] == readback[offset])
                ++offset;
            size_t last = offset, count = 0;
            size_t zeros = 0, previous = 0, next = 0, swapped = 0;
            for (size_t byte = offset; byte < length; ++byte)
                if (pattern[byte] != readback[byte]) {
                    last = byte;
                    ++count;
                    if (readback[byte] == 0) ++zeros;
                    if (byte && readback[byte] == pattern[byte - 1])
                        ++previous;
                    if (byte + 1 < length &&
                        readback[byte] == pattern[byte + 1]) ++next;
                    if ((byte ^ 1) < length &&
                        readback[byte] == pattern[byte ^ 1]) ++swapped;
                }
            diagnostic->channel = channel;
            diagnostic->shading = shading;
            diagnostic->attempt = attempt + 1;
            diagnostic->offset = offset;
            diagnostic->last_offset = last;
            diagnostic->mismatch_count = count;
            diagnostic->zero_bytes = zeros;
            diagnostic->matches_previous = previous;
            diagnostic->matches_next = next;
            diagnostic->matches_swapped = swapped;
            diagnostic->expected = pattern[offset];
            diagnostic->actual = readback[offset];
            diagnostic->has_mismatch = 1;
        }
        if (status == 0) {
            if (diagnostic) diagnostic->has_mismatch = 0;
            break;
        }
    }
    free(readback);
    return status;
}

static int verify_or_rewrite_plane(const hp4200_io *io, unsigned channel,
                                   int shading, const uint8_t *pattern,
                                   size_t length,
                                   hp4200_ram_diagnostic *diagnostic)
{
    int check = verify_plane(io, channel, shading, pattern, length,
                             diagnostic);
    if (check != -2 || (io->cancelled && io->cancelled(io->context)))
        return check;
    /* A live PV8630 bulk write reported all bytes transferred while one
     * shading plane retained the previous scan's table. Re-select and
     * rewrite only that plane, then require a complete matching readback. */
    if (diagnostic) ++diagnostic->write_retries;
    if (write_plane(io, channel, shading, pattern, length)) return -1;
    if (io->sleep_ms) io->sleep_ms(io->context, 10);
    return verify_plane(io, channel, shading, pattern, length, diagnostic);
}

int hp4200_write_verified_plane(const hp4200_io *io, unsigned channel,
                                int shading, const uint8_t *data,
                                size_t length)
{
    int status;
    hp4200_ram_diagnostic local_diagnostic = {0};
    hp4200_ram_diagnostic *diagnostic =
        io && io->final_ram_diagnostic ? io->final_ram_diagnostic :
        &local_diagnostic;
    if (!io || !io->write_register || !io->bulk_write_ram ||
        !io->bulk_read_ram || !data || !length || length > 65535 ||
        channel > 2) return -1;
    memset(diagnostic, 0, sizeof(*diagnostic));
    if (io->cancelled && io->cancelled(io->context)) return -1;
    if (write_plane(io, channel, shading, data, length)) return -1;
    if (io->sleep_ms) io->sleep_ms(io->context, 5);
    status = verify_or_rewrite_plane(io, channel, shading, data, length,
                                     diagnostic);
    if (io->verified_plane_rewrites)
        *io->verified_plane_rewrites += diagnostic->write_retries;
    /* Readback selects register 04's RAM-read mode. The subsequent scan
     * setup expects the ordinary write mode, as after HP's upload. */
    if (select_ram(io, channel, shading, 0)) return -1;
    return status;
}

int hp4200_selftest_ram_diagnose(const hp4200_io *io,
                                 hp4200_ram_diagnostic *diagnostic)
{
    uint8_t identity[256], lookup[1024];
    uint8_t *shading;
    int status = -1;
    if (!io || !io->write_register || !io->bulk_read_ram ||
        !io->bulk_write_ram) return -1;
    if (diagnostic) memset(diagnostic, 0, sizeof(*diagnostic));
    for (unsigned i = 0; i < 256; ++i) identity[i] = (uint8_t)i;
    hp4200_expand_256_to_1024(identity, lookup);
    for (unsigned channel = 0; channel < 3; ++channel) {
        if (io->cancelled && io->cancelled(io->context)) goto done;
        if (write_plane(io, channel, 0, lookup, sizeof(lookup))) {
            status = -100 - (int)channel;
            goto done;
        }
    }
    if (io->sleep_ms) io->sleep_ms(io->context, 5);
    for (unsigned channel = 0; channel < 3; ++channel) {
        if (io->cancelled && io->cancelled(io->context)) goto done;
        int check = verify_or_rewrite_plane(io, channel, 0, lookup,
                                            sizeof(lookup), diagnostic);
        if (check) {
            status = (check == -2 ? -120 : -110) - (int)channel;
            goto done;
        }
    }
    shading = malloc(0x2aa8);
    if (!shading) goto done;
    for (unsigned word = 0; word < 0x1554; ++word) {
        unsigned value = word & 0x3ff;
        shading[word * 2] = (uint8_t)(value >> 8);
        shading[word * 2 + 1] = (uint8_t)value;
    }
    for (unsigned channel = 0; channel < 3; ++channel) {
        if (io->cancelled && io->cancelled(io->context)) {
            free(shading);
            goto done;
        }
        if (write_plane(io, channel, 1, shading, 0x2aa8)) {
            status = -130 - (int)channel;
            free(shading);
            goto done;
        }
    }
    if (io->sleep_ms) io->sleep_ms(io->context, 5);
    for (unsigned channel = 0; channel < 3; ++channel) {
        if (io->cancelled && io->cancelled(io->context)) {
            free(shading);
            goto done;
        }
        int check = verify_or_rewrite_plane(io, channel, 1, shading,
                                            0x2aa8, diagnostic);
        if (check) {
            status = (check == -2 ? -150 : -140) - (int)channel;
            free(shading);
            goto done;
        }
    }
    free(shading);
    status = 0;
done:
    if (status) hp4200_stop(io);
    return status;
}

int hp4200_selftest_ram(const hp4200_io *io)
{
    return hp4200_selftest_ram_diagnose(io, NULL);
}
