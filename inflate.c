#include "zlib/include/zlib.h"
#include <stdint.h>
#include <stdio.h>

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define CHUNK 16384

// gracefully stolen!
int inflate_to_file(FILE *zip, FILE *out, uint32_t comp_size,
                    uint32_t expected_crc) {
    uint8_t in[CHUNK];
    uint8_t outbuf[CHUNK];
    z_stream stream = {0};
    uint32_t crc = crc32(0L, Z_NULL, 0);
    uint32_t remaining = comp_size;
    int ret = Z_OK;

    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        fprintf(stderr, "inflateInit2 failed\n");
        return -1;
    }

    while (remaining > 0 || stream.avail_in > 0) {
        if (stream.avail_in == 0 && remaining > 0) {
            size_t n = fread(in, 1, MIN(remaining, CHUNK), zip);
            if (n == 0) {
                fprintf(stderr, "unexpected EOF reading compressed data\n");
                inflateEnd(&stream);
                return -1;
            }
            remaining -= (uint32_t)n;
            stream.next_in = in;
            stream.avail_in = (uInt)n;
        }

        do {
            stream.next_out = outbuf;
            stream.avail_out = CHUNK;

            ret = inflate(&stream, Z_NO_FLUSH);

            switch (ret) {
            case Z_NEED_DICT:
            case Z_DATA_ERROR:
            case Z_MEM_ERROR:
                fprintf(stderr, "inflate error: %d (%s)\n", ret,
                        stream.msg ? stream.msg : "?");
                inflateEnd(&stream);
                return -1;
            }

            size_t have = CHUNK - stream.avail_out;
            if (have) {
                if (fwrite(outbuf, 1, have, out) != have) {
                    inflateEnd(&stream);
                    perror("fwirte");
                    return -1;
                }
                crc = crc32(crc, outbuf, (uInt)have);
            }
        } while (stream.avail_out == 0);

        if (ret == Z_STREAM_END)
            break;
    }

    inflateEnd(&stream);

    if (ret != Z_STREAM_END) {
        fprintf(stderr, "stream ended early\n");
        return -1;
    }
    if (crc != expected_crc) {
        fprintf(stderr, "CRC mismatch: got %08x, expected %08x\n", crc,
                expected_crc);
        return -1;
    }

    return 0;
}
