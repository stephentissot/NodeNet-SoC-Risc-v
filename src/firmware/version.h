/**
 * version.h — Firmware version string
 *
 * FIRMWARE_VERSION is normally injected at compile-time by the Makefile:
 *   -DFIRMWARE_VERSION="0.1.0-<git-hash>"
 *
 * The fallback below is only used when building outside the Makefile
 * (e.g. IDE IntelliSense, standalone compilation) and keeps editors happy.
 */

#pragma once

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "0.0.0-dev"
#endif
