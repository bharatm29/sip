#include "inflate.c"
#include "structs.c"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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
        zip_central_dir_header_t *header =
            (zip_central_dir_header_t *)(bytes + offset);

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
            write_to_file(header->local_header_offset, zip,
                          header->compressed_size, header->crc32);
        }

        offset +=
            46 + header->filename_len + header->extra_len + header->comment_len;
    }

    free(bytes);
    fclose(zip);
}
