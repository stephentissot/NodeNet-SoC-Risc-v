#pragma once

#include <cstdint>

#include <esp_err.h>

namespace boot_ui {

esp_err_t init();
void set_progress(uint8_t percent, const char* status_text);

} // namespace boot_ui