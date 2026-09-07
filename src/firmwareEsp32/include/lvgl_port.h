#pragma once

#include <esp_err.h>

namespace lvgl_port {

esp_err_t init();
void task_handler();

} // namespace lvgl_port
