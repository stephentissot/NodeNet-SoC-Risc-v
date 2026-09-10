#pragma once

#include <cstddef>
#include <cstdint>

#include <esp_err.h>

namespace display_st7789 {

esp_err_t init();
esp_err_t fill_screen(uint16_t color);
esp_err_t blit_rgb565(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const uint16_t* pixels, size_t pixel_count);

} // namespace display_st7789