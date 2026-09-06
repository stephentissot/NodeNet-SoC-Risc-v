#include "spi_bus_shared.h"

#include <driver/spi_master.h>

#include "app_config.h"

namespace spi_bus_shared {
namespace {

bool g_bus_initialized = false;

} // namespace

esp_err_t ensure_bus()
{
    if (g_bus_initialized) {
        return ESP_OK;
    }

    spi_bus_config_t bus_config = {};
    bus_config.miso_io_num = app_config::kSpiMiso;
    bus_config.mosi_io_num = app_config::kSpiMosi;
    bus_config.sclk_io_num = app_config::kSpiSck;
    bus_config.quadwp_io_num = -1;
    bus_config.quadhd_io_num = -1;
    bus_config.max_transfer_sz = app_config::kSpiMaxTransferBytes;

    const esp_err_t result = spi_bus_initialize(app_config::kSpiHost, &bus_config, SPI_DMA_DISABLED);
    if ((result == ESP_OK) || (result == ESP_ERR_INVALID_STATE)) {
        g_bus_initialized = true;
        return ESP_OK;
    }

    return result;
}

} // namespace spi_bus_shared