#ifndef FIRMWARE_IMAGE_H
#define FIRMWARE_IMAGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Stage0 firmware image header stored in SPI flash before payload.
// All fields are little-endian.
#define FW_IMAGE_MAGIC        0x46574E4Eu  // "NNWF"
#define FW_IMAGE_VERSION      0x0001u
#define FW_IMAGE_HEADER_SIZE  64u
#define FW_IMAGE_OFFSET       64u

typedef struct __attribute__((packed)) fw_image_header {
    uint32_t magic;
    uint16_t header_version;
    uint16_t header_size;
    uint32_t flags;
    uint32_t load_addr;
    uint32_t entry_addr;
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t header_crc32;
    uint32_t image_offset;
    uint8_t reserved[24];
} fw_image_header_t;

#ifdef __cplusplus
}
#endif

#endif
