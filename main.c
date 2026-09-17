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

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint16_t version_made_by;
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
    uint16_t comment_len;
    uint16_t disk_num_start;
    uint16_t internal_attrs;
    uint32_t external_attrs;
    uint32_t local_header_offset;
} zip_central_dir_header_t;
#pragma pack(pop)

_Static_assert(sizeof(zip_central_dir_header_t) == 46, "unexpected padding");

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint16_t disk_num;
    uint16_t cd_start_disk;
    uint16_t cd_records_this_disk;
    uint16_t cd_records_total;
    uint32_t cd_size;
    uint32_t cd_offset;
    uint16_t comment_len;
} zip_eocd_t;
#pragma pack(pop)

_Static_assert(sizeof(zip_eocd_t) == 22, "unexpected padding");

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
    FILE *zip = fopen("zip.zip", "rb");

    if (zip == NULL) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    fseek(zip, -22, SEEK_END); // go to 22 bytes before the end (ignoring the
                               // fact that it can have comment)

    uint8_t *bytes = malloc(sizeof(uint8_t) * 22);
    fread(bytes, 1, 22, zip);
    zip_eocd_t eocd = {0};
    memcpy(&eocd, bytes, sizeof(zip_eocd_t));

    free(bytes);
    bytes = malloc(sizeof(uint8_t) * eocd.cd_size);
    fseek(zip, eocd.cd_offset, SEEK_SET); // seek file to central directory

    fread(bytes, 1, eocd.cd_size, zip);

    int offset = 0;
    for (int i = 0; i < eocd.cd_records_total; i++) {
        zip_central_dir_header_t* header = (zip_central_dir_header_t*) (bytes + offset);

        int16_t filename_len, extra_field_len, file_comment_len;

        char filename[header->filename_len + 1];
        memcpy(filename, bytes + 46 + offset, header->filename_len);
        filename[header->filename_len] = '\0';

        if (filename[header->filename_len - 1] == '/') {
            printf("Directory: %s\n", filename);
            if (mkdir(filename, 0700)) {
                perror("mkdir");
                exit(EXIT_FAILURE);
            }
        } else {
            printf("%s\n", filename);
            write_to_file(header->local_header_offset, zip, header->compressed_size, header->crc32);
        }

        offset += 46 + header->filename_len + header->extra_len + header->comment_len;
    }

    free(bytes);
    fclose(zip);
}
