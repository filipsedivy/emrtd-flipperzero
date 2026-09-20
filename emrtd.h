/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Filip Sedivy
 *
 * eMRTD - an electronic passport reader for the Flipper Zero.
 *
 * Reads an ICAO Doc 9303 travel document over NFC, opening it with PACE or
 * BAC, and exports every data group it can reach to the SD card.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define EMRTD_VERSION "1.0.0"

typedef struct Emrtd Emrtd;

#ifdef __cplusplus
}
#endif
