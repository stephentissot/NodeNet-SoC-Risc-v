#include "ui_text.h"

#include <cstring>

namespace ui_text {
namespace {

struct Strings {
    const char* language;
    const char* title;
    const char* subtitle;
    const char* telemetry_pending;
    const char* footer_ip;
    const char* footer_ap;
    const char* boot_texts[10];
};

constexpr Strings kStrings[] = {
    {
        "uk",
        "BIGSISTER",
        "PLC slot status",
        "Slot telemetry pending defs/string support",
        "IP",
        "AP",
        {
            "Screen ready",
            "Wi-Fi / Web",
            "Display-only mode",
            "FPGA link",
            "Init SPI",
            "Reading FPGA capabilities",
            "Preparing snapshot",
            "Loading points",
            "Startup complete",
            "SPI error",
        },
    },
    {
        "fr",
        "BIGSISTER",
        "Etat des slots PLC",
        "Telemetrie slots en attente du support defs/string",
        "IP",
        "PA",
        {
            "Ecran pret",
            "Wi-Fi / Web",
            "Mode ecran seul",
            "Lien FPGA",
            "Init SPI",
            "Lecture capacites FPGA",
            "Preparation snapshot",
            "Chargement points",
            "Demarrage termine",
            "Erreur SPI",
        },
    },
    {
        "de",
        "BIGSISTER",
        "PLC-Slotstatus",
        "Slot-Telemetrie wartet auf Defs-/String-Unterstutzung",
        "IP",
        "AP",
        {
            "Bildschirm bereit",
            "Wi-Fi / Web",
            "Nur-Display-Modus",
            "FPGA-Verbindung",
            "Init SPI",
            "FPGA-Fahigkeiten werden gelesen",
            "Snapshot wird vorbereitet",
            "Punkte werden geladen",
            "Start abgeschlossen",
            "SPI-Fehler",
        },
    },
    {
        "es",
        "BIGSISTER",
        "Estado de slots PLC",
        "Telemetria de slots pendiente del soporte defs/string",
        "IP",
        "AP",
        {
            "Pantalla lista",
            "Wi-Fi / Web",
            "Modo solo pantalla",
            "Enlace FPGA",
            "Init SPI",
            "Leyendo capacidades FPGA",
            "Preparando snapshot",
            "Cargando puntos",
            "Inicio completo",
            "Error SPI",
        },
    },
    {
        "zh-Hans",
        "BIGSISTER",
        "PLC 槽位状态",
        "槽位遥测仍等待 defs/string 支持",
        "IP",
        "AP",
        {
            "屏幕就绪",
            "Wi-Fi / Web",
            "仅显示模式",
            "FPGA 链路",
            "初始化 SPI",
            "读取 FPGA 能力",
            "准备快照",
            "加载点位",
            "启动完成",
            "SPI 错误",
        },
    },
};

const Strings& find_strings(const char* language)
{
    if (language != nullptr) {
        for (const auto& entry : kStrings) {
            if (std::strcmp(entry.language, language) == 0) {
                return entry;
            }
        }
    }

    return kStrings[0];
}

} // namespace

const char* normalize_language(const char* language)
{
    return find_strings(language).language;
}

const char* boot_stage_text(const char* language, BootStage stage)
{
    return find_strings(language).boot_texts[static_cast<unsigned>(stage)];
}

const char* screen_title(const char* language)
{
    return find_strings(language).title;
}

const char* screen_subtitle(const char* language)
{
    return find_strings(language).subtitle;
}

const char* slot_telemetry_pending(const char* language)
{
    return find_strings(language).telemetry_pending;
}

const char* footer_ip_label(const char* language)
{
    return find_strings(language).footer_ip;
}

const char* footer_ap_label(const char* language)
{
    return find_strings(language).footer_ap;
}

} // namespace ui_text