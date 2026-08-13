#ifndef FAL_H
#define FAL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FAL_PART_MAGIC_WORD 0x45503130u

struct fal_flash_ops {
    int (*init)(void);
    int (*read)(long offset, uint8_t *buf, size_t size);
    int (*write)(long offset, const uint8_t *buf, size_t size);
    int (*erase)(long offset, size_t size);
};

struct fal_flash_dev {
    const char *name;
    uint32_t addr;
    size_t len;
    size_t blk_size;
    struct fal_flash_ops ops;
    size_t write_gran;
};

struct fal_partition {
    uint32_t magic_word;
    const char *name;
    const char *flash_name;
    long offset;
    size_t len;
    uint32_t reserved;
};

int fal_init(void);
const struct fal_partition *fal_partition_find(const char *name);
const struct fal_flash_dev *fal_flash_device_find(const char *name);
int fal_partition_read(const struct fal_partition *part, uint32_t addr, uint8_t *buf, size_t size);
int fal_partition_write(const struct fal_partition *part, uint32_t addr, const uint8_t *buf, size_t size);
int fal_partition_erase(const struct fal_partition *part, uint32_t addr, size_t size);

#ifdef __cplusplus
}
#endif

#endif