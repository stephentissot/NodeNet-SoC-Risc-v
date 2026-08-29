#ifndef FLASHDB_PORT_H
#define FLASHDB_PORT_H

#include <cstddef>
#include <cstdint>

#include "flash.h"

bool flashdb_init(Flash* flash, Flash::StatusCallback callback = nullptr);
bool flashdb_is_ready();
bool flashdb_boot_counter_test(Flash::StatusCallback callback = nullptr);

bool flashdb_set_i32(const char* key, int32_t value);
bool flashdb_get_i32(const char* key, int32_t* value_out);
bool flashdb_set_str(const char* key, const char* value);
bool flashdb_get_str(const char* key, char* out, size_t out_size);
bool flashdb_delete_key(const char* key);

#endif