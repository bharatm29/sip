#include "zlib/include/zconf.h"
#include "zlib/include/zlib.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

#pragma pack(push, 1) // disable compiler padding
typedef struct {
    uint32_t magic;
    uint16_t version_needed;
    uint16_t flags;
    uint16_t compression;
    uint16_t mod_time;
    uint16_t mod_date;
    uint32_t crc32;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint16_t filename_len;
    uint16_t extra_len;
} zip_local_file_header_t; // total 30 bytes
#pragma pack(pop)

_Static_assert(sizeof(zip_local_file_header_t) == 30, "unexpected padding");

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

void write_to_file(int32_t local_header_addr, FILE *zip,
                   uint32_t compressed_size, uint32_t crc) {
    uint8_t *data = malloc(sizeof(uint8_t) * 4096);
    fseek(zip, local_header_addr, SEEK_SET);
    fread(data, sizeof(uint8_t), 30, zip);

    zip_local_file_header_t *header = (zip_local_file_header_t *)data;

    char filename[header->filename_len + 1];
    fread(filename, sizeof(u_char), header->filename_len, zip);
    filename[header->filename_len] = '\0';

    fseek(zip, header->extra_len,
          SEEK_CUR); // putting at the start of file data
    FILE *out = fopen(filename, "wb+");
    if (out == NULL) {
        perror("fopen");
        return;
    }

    int ret = 0;
    if (header->compression == 0) {
        int32_t remaining = compressed_size;
        while (remaining > 0) {
            size_t n = fread(data, 1, MIN(4096, remaining), zip);
            if (n == 0) {
                break;
            }

            fwrite(data, 1, n, out);
            remaining -= n;
        }
    } else {
        ret = inflate_to_file(zip, out, compressed_size, crc);
    }

    if (ret != 0) {
        fprintf(stderr, "Error deflating or writing to file");
    }

    fclose(out);
    free(data);
}

int main() {
    FILE *zip = fopen("./zip.zip", "rb");

    if (zip == NULL) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    fseek(zip, -22, SEEK_END); // go to 22 bytes before the end

    uint8_t *bytes = malloc(sizeof(uint8_t) * 22);
    fread(bytes, 1, 22, zip);

    int cd_numbers = 0;
    memmove(&cd_numbers, bytes + 10, 2);

    int cd_addr_size = 0;
    memmove(&cd_addr_size, bytes + 12, 4);
    int cd_addr = 0;
    memmove(&cd_addr, bytes + 16, 4);

    free(bytes);
    bytes = malloc(sizeof(uint8_t) * cd_addr_size);
    fseek(zip, cd_addr, SEEK_SET);

    fread(bytes, 1, cd_addr_size, zip);

    int offset = 0;
    for (int i = 0; i < cd_numbers; i++) {
        int16_t filename_len, extra_field_len, file_comment_len;
        memmove(&filename_len, bytes + 28 + offset, 2);
        memmove(&extra_field_len, bytes + 30 + offset, 2);
        memmove(&file_comment_len, bytes + 32 + offset, 2);

        char filename[filename_len + 1];
        memmove(filename, bytes + 46 + offset, filename_len);
        filename[filename_len] = '\0';

        if (filename[filename_len - 1] == '/') {
            printf("Directory: %s\n", filename);
            if (mkdir(filename, 0700)) {
                perror("mkdir");
                exit(EXIT_FAILURE);
            }
        } else {
            printf("%s\n", filename);

            int32_t compr_size, crc;
            memmove(&compr_size, bytes + 20 + offset, 4);
            memmove(&crc, bytes + 16 + offset, 4);

            int32_t local_header_addr = 0;
            memmove(&local_header_addr, bytes + 42 + offset, 4);

            write_to_file(local_header_addr, zip, compr_size, crc);
        }

        offset += 46 + filename_len + extra_field_len + file_comment_len;
    }

    free(bytes);
}
