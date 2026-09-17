#include <stdint.h>

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
