#pragma once

#include <cstdint>

namespace ui_text {

enum class BootStage : uint8_t {
    ScreenReady,
    WifiWeb,
    DisplayOnly,
    FpgaLink,
    InitSpi,
    ReadFpgaCaps,
    PrepareSnapshot,
    LoadingPoints,
    StartupOk,
    SpiError,
};

const char* normalize_language(const char* language);
const char* boot_stage_text(const char* language, BootStage stage);
const char* screen_title(const char* language);
const char* screen_subtitle(const char* language);
const char* slot_telemetry_pending(const char* language);
const char* footer_ip_label(const char* language);
const char* footer_ap_label(const char* language);

} // namespace ui_text