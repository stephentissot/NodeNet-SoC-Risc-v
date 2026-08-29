#include "flashdb_port.h"

#include <cstring>

extern "C" {
#include "flashdb.h"
#include "fal.h"
}

namespace {

static Flash* g_flash = nullptr;
static fdb_kvdb g_kvdb;
static bool g_kvdb_ready = false;

// Keep FlashDB away from the raw point-catalog and PLC package slots.
// One sector at the end remains reserved for low-level flash self-test scratch.
static constexpr uint32_t kFlashDbOffset = Flash::kFlashDbBase;
static constexpr uint32_t kFlashDbSize = Flash::kFlashDbSize;
static constexpr const char* kFlashDbPartitionName = "nodenet_kv";

static bool read_span_raw(uint32_t flash_offset, uint8_t* out, size_t len) {
  if (g_flash == nullptr || out == nullptr) {
    return false;
  }
  if ((flash_offset + len) > Flash::kFlashSize) {
    return false;
  }

  uint8_t page[Flash::kPageSize];
  size_t copied = 0;
  while (copied < len) {
    const uint32_t pos = flash_offset + static_cast<uint32_t>(copied);
    const uint32_t page_base = pos & ~(Flash::kPageSize - 1u);
    const uint32_t page_off = pos - page_base;
    if (!g_flash->readPage(page_base, page)) {
      return false;
    }

    size_t chunk = Flash::kPageSize - page_off;
    if (chunk > (len - copied)) {
      chunk = len - copied;
    }
    std::memcpy(out + copied, page + page_off, chunk);
    copied += chunk;
  }

  return true;
}

static bool write_span_raw(uint32_t flash_offset, const uint8_t* in, size_t len) {
  if (g_flash == nullptr || in == nullptr) {
    return false;
  }
  if ((flash_offset + len) > Flash::kFlashSize) {
    return false;
  }

  uint8_t page[Flash::kPageSize];
  size_t consumed = 0;
  while (consumed < len) {
    const uint32_t pos = flash_offset + static_cast<uint32_t>(consumed);
    const uint32_t page_base = pos & ~(Flash::kPageSize - 1u);
    const uint32_t page_off = pos - page_base;

    if (!g_flash->readPage(page_base, page)) {
      return false;
    }

    size_t chunk = Flash::kPageSize - page_off;
    if (chunk > (len - consumed)) {
      chunk = len - consumed;
    }

    bool page_changed = false;
    for (size_t i = 0; i < chunk; ++i) {
      const uint8_t old_v = page[page_off + i];
      const uint8_t new_v = in[consumed + i];
      if ((old_v & new_v) != new_v) {
        return false;
      }
      if (old_v != new_v) {
        page[page_off + i] = new_v;
        page_changed = true;
      }
    }

    if (page_changed && !g_flash->writePage(page_base, page)) {
      return false;
    }

    consumed += chunk;
  }

  return true;
}

static bool erase_span_raw(uint32_t flash_offset, size_t len) {
  if (g_flash == nullptr) {
    return false;
  }
  if ((flash_offset + len) > Flash::kFlashSize) {
    return false;
  }
  if ((flash_offset % Flash::kSectorSize) != 0u || (len % Flash::kSectorSize) != 0u) {
    return false;
  }

  for (size_t off = 0; off < len; off += Flash::kSectorSize) {
    if (!g_flash->eraseSector(flash_offset + static_cast<uint32_t>(off))) {
      return false;
    }
  }
  return true;
}

static const struct fal_flash_dev kFlashDev = {
    "w25q64",
    0,
    Flash::kFlashSize,
    Flash::kSectorSize,
    {nullptr, nullptr, nullptr, nullptr},
    1,
};

static const struct fal_partition kFlashPartitions[] = {
    {FAL_PART_MAGIC_WORD, kFlashDbPartitionName, "w25q64", static_cast<long>(kFlashDbOffset), kFlashDbSize, 0},
};

}  // namespace

extern "C" int fal_init(void) {
  return (g_flash != nullptr) ? 0 : -1;
}

extern "C" const struct fal_partition* fal_partition_find(const char* name) {
  if (name == nullptr) {
    return nullptr;
  }
  for (size_t i = 0; i < (sizeof(kFlashPartitions) / sizeof(kFlashPartitions[0])); ++i) {
    if (std::strcmp(kFlashPartitions[i].name, name) == 0) {
      return &kFlashPartitions[i];
    }
  }
  return nullptr;
}

extern "C" const struct fal_flash_dev* fal_flash_device_find(const char* name) {
  if (name == nullptr) {
    return nullptr;
  }
  if (std::strcmp(kFlashDev.name, name) == 0) {
    return &kFlashDev;
  }
  return nullptr;
}

extern "C" int fal_partition_read(const struct fal_partition* part, uint32_t addr, uint8_t* buf, size_t size) {
  if (part == nullptr || buf == nullptr) {
    return -1;
  }
  if ((addr + size) > part->len) {
    return -1;
  }

  const uint32_t abs = static_cast<uint32_t>(part->offset) + addr;
  return read_span_raw(abs, buf, size) ? static_cast<int>(size) : -1;
}

extern "C" int fal_partition_write(const struct fal_partition* part, uint32_t addr, const uint8_t* buf, size_t size) {
  if (part == nullptr || buf == nullptr) {
    return -1;
  }
  if ((addr + size) > part->len) {
    return -1;
  }

  const uint32_t abs = static_cast<uint32_t>(part->offset) + addr;
  return write_span_raw(abs, buf, size) ? static_cast<int>(size) : -1;
}

extern "C" int fal_partition_erase(const struct fal_partition* part, uint32_t addr, size_t size) {
  if (part == nullptr) {
    return -1;
  }
  if ((addr + size) > part->len) {
    return -1;
  }

  const uint32_t abs = static_cast<uint32_t>(part->offset) + addr;
  return erase_span_raw(abs, size) ? static_cast<int>(size) : -1;
}

bool flashdb_init(Flash* flash, Flash::StatusCallback callback) {
  if (flash == nullptr) {
    if (callback) {
      callback(20, "[FDB] init flash ptr null");
    }
    return false;
  }

  g_flash = flash;
  g_kvdb_ready = false;
  std::memset(&g_kvdb, 0, sizeof(g_kvdb));
  if (callback) {
    callback(20, "[FDB] init start");
  }

  uint32_t sec_size = Flash::kSectorSize;
  fdb_kvdb_control(&g_kvdb, FDB_KVDB_CTRL_SET_SEC_SIZE, &sec_size);
  if (callback) {
    callback(20, "[FDB] sec cfg ok");
  }

  const fdb_err_t err = fdb_kvdb_init(&g_kvdb, "nodenet", kFlashDbPartitionName, nullptr, nullptr);
  if (err != FDB_NO_ERR) {
    if (callback) {
      callback(21, "[FDB] init fail");
    }
    return false;
  }

  g_kvdb_ready = true;
  if (callback) {
    callback(21, "[FDB] init ok");
  }
  return true;
}

bool flashdb_is_ready() {
  return g_kvdb_ready;
}

bool flashdb_boot_counter_test(Flash::StatusCallback callback) {
  if (!g_kvdb_ready) {
    if (callback) {
      callback(22, "[FDB] boot cnt skipped");
    }
    return false;
  }

  struct fdb_blob blob;
  int32_t boot_count = 0;
  (void)fdb_kv_get_blob(&g_kvdb, "boot_count", fdb_blob_make(&blob, &boot_count, sizeof(boot_count)));

  boot_count += 1;
  if (fdb_kv_set_blob(&g_kvdb, "boot_count", fdb_blob_make(&blob, &boot_count, sizeof(boot_count))) != FDB_NO_ERR) {
    if (callback) {
      callback(23, "[FDB] set boot cnt fail");
    }
    return false;
  }

  if (callback) {
    callback(23, "[FDB] boot cnt updated");
  }
  return true;
}

bool flashdb_set_i32(const char* key, int32_t value) {
  if (!g_kvdb_ready || key == nullptr) {
    return false;
  }

  struct fdb_blob blob;
  return fdb_kv_set_blob(&g_kvdb, key, fdb_blob_make(&blob, &value, sizeof(value))) == FDB_NO_ERR;
}

bool flashdb_get_i32(const char* key, int32_t* value_out) {
  if (!g_kvdb_ready || key == nullptr || value_out == nullptr) {
    return false;
  }

  struct fdb_blob blob;
  *value_out = 0;
  return fdb_kv_get_blob(&g_kvdb, key, fdb_blob_make(&blob, value_out, sizeof(*value_out))) == sizeof(*value_out);
}

bool flashdb_set_str(const char* key, const char* value) {
  if (!g_kvdb_ready || key == nullptr || value == nullptr) {
    return false;
  }
  return fdb_kv_set(&g_kvdb, key, value) == FDB_NO_ERR;
}

bool flashdb_get_str(const char* key, char* out, size_t out_size) {
  if (!g_kvdb_ready || key == nullptr || out == nullptr || out_size == 0u) {
    return false;
  }

  const char* value = fdb_kv_get(&g_kvdb, key);
  if (value == nullptr) {
    out[0] = '\0';
    return false;
  }

  size_t n = std::strlen(value);
  if (n >= out_size) {
    n = out_size - 1u;
  }
  std::memcpy(out, value, n);
  out[n] = '\0';
  return true;
}

bool flashdb_delete_key(const char* key) {
  if (!g_kvdb_ready || key == nullptr) {
    return false;
  }

  return fdb_kv_del(&g_kvdb, key) == FDB_NO_ERR;
}
