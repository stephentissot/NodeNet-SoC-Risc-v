#pragma once

#include <esp_err.h>

#include <cstdint>

namespace spi_link {

struct BootProgress {
	uint8_t percent;
	uint16_t loaded_points;
	uint16_t total_points;
	bool link_ready;
	bool caps_received;
	bool snapshot_started;
	bool snapshot_complete;
};

esp_err_t init();
esp_err_t poll();
bool is_busy();
BootProgress get_boot_progress();

} // namespace spi_link
