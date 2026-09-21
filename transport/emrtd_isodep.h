/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Filip Sedivy
 *
 * ISO-DEP, the block transmission protocol of ISO/IEC 14443-4, implemented
 * here rather than taken from the firmware.
 *
 * The Flipper has an ISO 14443-4A poller of its own and this reader used it
 * at first. It cannot be used for a travel document, for one reason that is
 * not a matter of taste:
 *
 *     uint32_t iso14443_4a_get_fwt_fc_max(const Iso14443_4aData* data) {
 *         uint32_t fwt_fc_max = ISO14443_4A_FDT_DEFAULT_FC;
 *         ...
 *         fwt_fc_max = 4096UL << fwi;
 *
 * with ISO14443_4A_FDT_DEFAULT_FC defined as ISO14443_3A_FDT_POLL_FC, which
 * is 1620 carrier cycles - 120 microseconds. That is the ISO 14443-3 poll
 * frame delay time, not a frame waiting time, and it is what every I-block
 * gets whenever the card's ATS carries no TB1. No smartcard answers anything
 * in 120 microseconds. Its RATS window is fixed at 40000 cycles as well,
 * which is below the 65536 that ISO/IEC 14443-4 allows a card for the answer
 * to RATS. Neither value is reachable from an application, and a chip that
 * loses either race is reported as a card that has been moved away.
 *
 * The Python reference this reader was ported from hit exactly this and
 * solved it the same way: it drives bare ISO 14443-3A frames and asks for a
 * frame waiting time of four million cycles, about 295 milliseconds, because
 * "a passport with FWI=9 needs roughly 150 ms to answer". This file is that
 * decision brought into the application.
 *
 * What it owns, therefore, is everything above the bare frame: RATS and the
 * ATS, the block number and when it toggles, chaining in both directions,
 * the waiting time extension a chip asks for while it does elliptic curve
 * arithmetic, retransmission of a block whose answer was lost, and the
 * deselect at the end. It is handed one function that puts a frame on the
 * wire and brings the answer back, which is what makes the whole layer
 * testable on a host against a simulated chip.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../emrtd_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Timings are in carrier cycles, the unit the radio takes. One cycle of the
 * 13.56 MHz carrier is about 73.7 nanoseconds.
 */

/**
 * How long the card has to answer RATS.
 *
 * ISO/IEC 14443-4 gives it 65536 cycles, 4.8 milliseconds. This is fifteen
 * times that, because the only cost of waiting longer is a slower failure on
 * a card that is not there, and the cost of waiting too little is a document
 * that cannot be read at all.
 */
#define EMRTD_ISODEP_RATS_FWT_FC 1000000u

/**
 * Floor under the frame waiting time, whatever the ATS asked for.
 *
 * FWI 9, which is what a passport typically announces, works out at 2097152
 * cycles - 155 milliseconds - and the reference implementation found that to
 * be marginal in practice on this hardware. This is the value it settled on.
 */
#define EMRTD_ISODEP_FWT_MIN_FC 4000000u

/** Ceiling, from FWI 14: the largest waiting time ISO/IEC 14443-4 defines. */
#define EMRTD_ISODEP_FWT_MAX_FC (4096u << 14)

/** FSDI 8, the frame size this reader announces in RATS. */
#define EMRTD_ISODEP_FSD 256

/** One frame, protocol control byte and information field, without the CRC. */
#define EMRTD_ISODEP_FRAME_MAX 256

/** An ATS is at most 20 bytes plus its length byte; this rounds it up. */
#define EMRTD_ISODEP_ATS_MAX 24

/**
 * How many times a chip may ask for more time before the reader gives up.
 *
 * Each round is worth up to the waiting time again, so this is a guard
 * against a chip that never finishes rather than a tight budget.
 */
#define EMRTD_ISODEP_WTX_ROUNDS_MAX 60

/**
 * Extra attempts at a block whose answer did not arrive.
 *
 * ISO/IEC 14443-4 section 7.5.6 puts recovery on the reader, and the rule
 * that makes it safe is in 7.5.4.2: a card that receives a block whose block
 * number is not its own re-transmits its last answer instead of executing
 * anything again. So resending an identical I-block asks for the lost answer
 * back; it does not run the command twice, which matters because under
 * Secure Messaging a second execution would advance the chip's send sequence
 * counter and end the session.
 */
#define EMRTD_ISODEP_RETRIES 2

/**
 * Put one bare frame on the wire and bring the answer back.
 *
 * The frame is the protocol control byte and what follows it. The cyclic
 * redundancy check belongs to the layer below and is neither written nor
 * expected here.
 *
 * @param[in]  context  implementation context
 * @param[in]  tx       frame to send
 * @param[in]  tx_len   its length
 * @param[out] rx       buffer for the answer
 * @param[in]  rx_cap   capacity of @p rx
 * @param[out] rx_len   bytes written
 * @param[in]  fwt_fc   how long to wait for the answer, in carrier cycles
 */
typedef EmrtdError (*EmrtdIsoDepFrameFn)(
    void* context,
    const uint8_t* tx,
    size_t tx_len,
    uint8_t* rx,
    size_t rx_cap,
    size_t* rx_len,
    uint32_t fwt_fc);

typedef struct {
    EmrtdIsoDepFrameFn send;
    void* context;

    bool activated;
    uint8_t block_number; /**< Toggles after every accepted exchange. */
    uint16_t fsc; /**< Card frame size, from the ATS. */
    uint8_t fwi; /**< Frame waiting time index, from TB1. */
    uint32_t fwt_fc; /**< What the reader actually waits, after the floor. */
    bool fwi_announced; /**< False when the ATS carried no TB1. */

    uint8_t ats[EMRTD_ISODEP_ATS_MAX];
    size_t ats_len;

    /* One frame each way. Held here so that no path needs the stack for it. */
    uint8_t tx_frame[EMRTD_ISODEP_FRAME_MAX];
    uint8_t rx_frame[EMRTD_ISODEP_FRAME_MAX];
} EmrtdIsoDep;

/** Bind the layer to a frame transport. Does not touch the card. */
void emrtd_isodep_init(EmrtdIsoDep* instance, EmrtdIsoDepFrameFn send, void* context);

/** Forget the session. The frame transport is left bound. */
void emrtd_isodep_reset(EmrtdIsoDep* instance);

/**
 * Send RATS and parse the ATS.
 *
 * On success the card is in the ISO 14443-4 protocol state and
 * emrtd_isodep_transceive() may be called.
 */
EmrtdError emrtd_isodep_activate(EmrtdIsoDep* instance);

/**
 * Exchange one APDU, chaining it in either direction as the frame size needs.
 *
 * @param[out] rx_len bytes of response, status word included
 */
EmrtdError emrtd_isodep_transceive(
    EmrtdIsoDep* instance,
    const uint8_t* tx,
    size_t tx_len,
    uint8_t* rx,
    size_t rx_cap,
    size_t* rx_len);

/**
 * Release the card with S(DESELECT).
 *
 * Failure is ignored: the field is about to go down, which releases it
 * anyway, and there is nothing useful to report to a caller that is already
 * finishing.
 */
void emrtd_isodep_deselect(EmrtdIsoDep* instance);

/** The card's frame size, capped at what this reader can send. */
uint16_t emrtd_isodep_fsc(const EmrtdIsoDep* instance);

/** The raw ATS, for the log and the trace. NULL until activation succeeds. */
const uint8_t* emrtd_isodep_ats(const EmrtdIsoDep* instance, size_t* len);

/** Parse an ATS into the fields this layer needs. Exposed for the tests. */
void emrtd_isodep_parse_ats(EmrtdIsoDep* instance, const uint8_t* ats, size_t len);

#ifdef __cplusplus
}
#endif
