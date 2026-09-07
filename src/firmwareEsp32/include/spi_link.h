#pragma once

#include <esp_err.h>

namespace spi_link {

esp_err_t init();
esp_err_t poll();
bool is_busy();

} // namespace spi_link
