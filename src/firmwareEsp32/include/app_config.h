#pragma once

#include <driver/gpio.h>
#include <driver/spi_master.h>

namespace app_config {

static constexpr spi_host_device_t kSpiHost = SPI3_HOST;
static constexpr gpio_num_t kSpiSck = GPIO_NUM_18;
static constexpr gpio_num_t kSpiMosi = GPIO_NUM_23;
static constexpr gpio_num_t kSpiMiso = GPIO_NUM_19;
static constexpr gpio_num_t kFpgaCs = GPIO_NUM_27;
static constexpr gpio_num_t kFpgaIrq = GPIO_NUM_32;
static constexpr gpio_num_t kOptionalSideband = GPIO_NUM_33;

static constexpr gpio_num_t kDisplayCs = GPIO_NUM_5;
static constexpr gpio_num_t kDisplayDc = GPIO_NUM_16;
static constexpr gpio_num_t kDisplayReset = GPIO_NUM_17;
static constexpr bool kDisplayBringupOwnsSpiBus = false;

static constexpr int kSpiClockHz = 1000000;
static constexpr int kSpiQueueSize = 2;
static constexpr int kSpiMaxTransferBytes = 4096;
static constexpr int kDisplayClockHz = 10000000;
static constexpr int kDisplayWidth = 76;
static constexpr int kDisplayHeight = 284;
static constexpr int kDisplayTestStripeRows = 16;
static constexpr int kDisplayXGap = 82;
static constexpr int kDisplayYGap = 18;
static constexpr bool kDisplaySwapXY = false;
static constexpr bool kDisplayMirrorX = false;
static constexpr bool kDisplayMirrorY = false;
static constexpr uint16_t kTransportMagic = 0x4E53u;
static constexpr uint8_t kTransportVersion = 1u;

} // namespace app_config
