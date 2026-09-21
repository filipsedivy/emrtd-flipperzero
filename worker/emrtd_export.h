/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Filip Sedivy
 *
 * Writing a read out to the SD card.
 *
 * DG2 alone can be forty kilobytes against a hundred kilobyte heap, so a file
 * is never assembled in memory. The export is opened before the read starts
 * and every chunk that comes off the chip goes straight to storage while a
 * running hash is updated for the passive authentication check.
 *
 * The result is a directory per document:
 *
 *     /ext/apps_data/emrtd/L898902C_20260920_2114/
 *         report.txt       what was read, how it was opened, what verified
 *         trace.txt        the APDU log, when it was asked for
 *         EF_COM.bin       the raw files, exactly as the chip returned them
 *         EF_SOD.bin
 *         EF_DG1.bin
 *         ...
 *         mrz.txt          the decoded machine readable zone
 *         face.jpg         the facial image, lifted out of DG2
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "emrtd_worker.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Where exports go. Shared by both builds, so the paths on screen match. */
#define EMRTD_EXPORT_DIR "/ext/apps_data/emrtd"

/*
 * Where the entered credentials are remembered between runs.
 *
 * The demo build keeps its own file, and the reason is not tidiness. Its
 * document is built from whatever credentials it is configured with, so if it
 * read the file the real build writes, the specimen's name would appear on
 * screen over a real document number and a real date of birth - and those
 * screens are about to be published. Writing is as bad in the other
 * direction: leaving the options screen saves, and the demo would overwrite
 * somebody's remembered credentials with the specimen.
 */
#ifdef EMRTD_DEMO
#define EMRTD_SETTINGS_PATH "/ext/apps_data/emrtd/emrtd_demo.settings"
#else
#define EMRTD_SETTINGS_PATH "/ext/apps_data/emrtd/emrtd.settings"
#endif

typedef struct EmrtdExport EmrtdExport;

/**
 * Open an export directory for this read.
 *
 * @param[in] document_number used in the directory name; may be empty
 */
EmrtdExport* emrtd_export_alloc(const char* document_number, bool write_trace);
void emrtd_export_free(EmrtdExport* export_ctx);

/** The directory that was created, for the success screen. */
const char* emrtd_export_path(const EmrtdExport* export_ctx);

/** True when the directory could not be created; every write then does nothing. */
bool emrtd_export_failed(const EmrtdExport* export_ctx);

/* --- Streaming one file ------------------------------------------------ */

/** Begin a raw file. Closes any file already open. */
EmrtdError emrtd_export_begin_file(EmrtdExport* export_ctx, const EmrtdFileInfo* info);

/** Append a chunk to the open file. */
EmrtdError emrtd_export_write(EmrtdExport* export_ctx, const uint8_t* data, size_t len);

/** Finish the open file. */
void emrtd_export_end_file(EmrtdExport* export_ctx);

/* --- The facial image -------------------------------------------------- */

/**
 * Start a second stream for the image carried inside DG2.
 *
 * Once armed, every emrtd_export_write() that lands at or beyond @p offset
 * also goes to the image file. This is what avoids holding DG2 twice.
 */
void emrtd_export_arm_image(EmrtdExport* export_ctx, size_t offset, const char* suffix);

/* --- The decoded output ------------------------------------------------ */

EmrtdError emrtd_export_write_mrz(EmrtdExport* export_ctx, const EmrtdMrz* mrz);

/** Write report.txt. Called once the read has finished. */
EmrtdError emrtd_export_write_report(
    EmrtdExport* export_ctx,
    const EmrtdReadResult* result,
    const EmrtdWorkerConfig* config);

/** Append one exchange to trace.txt, when tracing was asked for. */
void emrtd_export_trace(
    EmrtdExport* export_ctx,
    const char* label,
    const uint8_t* data,
    size_t len);

/** Append a line of free text to trace.txt. */
void emrtd_export_trace_note(EmrtdExport* export_ctx, const char* text);

#ifdef __cplusplus
}
#endif
