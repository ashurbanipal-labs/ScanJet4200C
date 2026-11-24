#include "image_dpi.h"

#include <stdint.h>
#include <stdio.h>

static unsigned read16(const uint8_t *p, int little)
{
    return little ? (unsigned)p[0] | (unsigned)p[1] << 8 :
                    (unsigned)p[0] << 8 | p[1];
}

static uint32_t read32(const uint8_t *p, int little)
{
    return little ? (uint32_t)p[0] | (uint32_t)p[1] << 8 |
                    (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24 :
                    (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
                    (uint32_t)p[2] << 8 | p[3];
}

static void write16(uint8_t *p, unsigned value, int little)
{
    p[little ? 0 : 1] = (uint8_t)value;
    p[little ? 1 : 0] = (uint8_t)(value >> 8);
}

static void write32(uint8_t *p, uint32_t value, int little)
{
    if (little) {
        for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (i * 8));
    } else {
        for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> ((3 - i) * 8));
    }
}

int hp4200_set_jpeg_xy_dpi(const char *path, unsigned x_dpi,
                           unsigned y_dpi)
{
    uint8_t soi[2], segment_length[2], jfif[14];
    int status = -1;
    if (!path || !x_dpi || !y_dpi || x_dpi > 65535 || y_dpi > 65535)
        return -1;
    FILE *file = fopen(path, "r+b");
    if (!file) return -1;
    if (fread(soi, 1, 2, file) != 2 || soi[0] != 0xff || soi[1] != 0xd8)
        goto done;
    /* JFIF need not be the first JPEG segment; an APP1 metadata segment
     * may precede it. Stop at the image payload and never scan compressed
     * bytes for an accidental JFIF signature. */
    for (unsigned segment = 0; segment < 256; ++segment) {
        int prefix = fgetc(file), marker;
        if (prefix != 0xff) break;
        do { marker = fgetc(file); } while (marker == 0xff);
        if (marker == EOF || marker == 0xda || marker == 0xd9) break;
        if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7)) continue;
        if (fread(segment_length, 1, 2, file) != 2) break;
        unsigned length = read16(segment_length, 0);
        if (length < 2) break;
        long payload_at = ftell(file);
        if (payload_at < 0) break;
        if (marker == 0xe0 && length >= 16 &&
            fread(jfif, 1, sizeof(jfif), file) == sizeof(jfif) &&
            jfif[0] == 'J' && jfif[1] == 'F' && jfif[2] == 'I' &&
            jfif[3] == 'F' && jfif[4] == 0) {
            jfif[7] = 1; /* dots per inch */
            write16(jfif + 8, x_dpi, 0);
            write16(jfif + 10, y_dpi, 0);
            if (fseek(file, payload_at + 7, SEEK_SET) ||
                fwrite(jfif + 7, 1, 5, file) != 5 || fflush(file)) break;
            status = 0;
            break;
        }
        if (fseek(file, payload_at + length - 2, SEEK_SET)) break;
    }
done:
    if (fclose(file)) status = -1;
    return status;
}

int hp4200_set_tiff_xy_dpi(const char *path, unsigned x_dpi,
                           unsigned y_dpi)
{
    uint8_t header[8], count_bytes[2], entry[12], rational[8];
    unsigned found = 0, count;
    int little, status = -1;
    uint32_t ifd;
    if (!path || !x_dpi || !y_dpi) return -1;
    FILE *file = fopen(path, "r+b");
    if (!file) return -1;
    if (fread(header, 1, 8, file) != 8) goto done;
    little = header[0] == 'I' && header[1] == 'I';
    if (!little && !(header[0] == 'M' && header[1] == 'M')) goto done;
    if (read16(header + 2, little) != 42) goto done;
    ifd = read32(header + 4, little);
    if (ifd < 8 || fseek(file, (long)ifd, SEEK_SET) ||
        fread(count_bytes, 1, 2, file) != 2) goto done;
    count = read16(count_bytes, little);
    if (count > 256) goto done;
    for (unsigned i = 0; i < count; ++i) {
        long position = (long)ifd + 2 + (long)i * 12;
        if (fseek(file, position, SEEK_SET) ||
            fread(entry, 1, sizeof(entry), file) != sizeof(entry)) goto done;
        unsigned tag = read16(entry, little);
        if (tag == 282 || tag == 283) {
            if (read16(entry + 2, little) != 5 ||
                read32(entry + 4, little) != 1) goto done;
            uint32_t offset = read32(entry + 8, little);
            write32(rational, tag == 282 ? x_dpi : y_dpi, little);
            write32(rational + 4, 1, little);
            if (offset < 8 || fseek(file, (long)offset, SEEK_SET) ||
                fwrite(rational, 1, 8, file) != 8) goto done;
            found |= tag == 282 ? 1U : 2U;
        } else if (tag == 296) {
            if (read16(entry + 2, little) != 3 ||
                read32(entry + 4, little) != 1) goto done;
            write16(entry + 8, 2, little); /* inch */
            if (fseek(file, position + 8, SEEK_SET) ||
                fwrite(entry + 8, 1, 2, file) != 2) goto done;
            found |= 4U;
        }
    }
    if (found == 7 && fflush(file) == 0) status = 0;
done:
    if (fclose(file)) status = -1;
    return status;
}

int hp4200_set_jpeg_dpi(const char *path, unsigned dpi)
{
    return hp4200_set_jpeg_xy_dpi(path, dpi, dpi);
}

int hp4200_set_tiff_dpi(const char *path, unsigned dpi)
{
    return hp4200_set_tiff_xy_dpi(path, dpi, dpi);
}
