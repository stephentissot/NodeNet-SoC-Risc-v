#pragma once

#include <cstddef>
#include <cstdint>

#include <esp_err.h>

namespace spi_link {

enum class FrameType : uint8_t {
    Request = 1,
    Response = 2,
    Event = 3,
    Error = 4,
};

struct FrameHeader {
    uint16_t magic;
    uint8_t version;
    uint8_t frame_type;
    uint16_t sequence;
    uint16_t payload_len;
    uint16_t flags;
} __attribute__((packed));

esp_err_t init();
esp_err_t poll();

} // namespace spi_link
