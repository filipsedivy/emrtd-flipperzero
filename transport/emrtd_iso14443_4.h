/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Filip Sedivy
 *
 * An EmrtdTransceiver backed by the Flipper's ISO 14443-4 pollers.
 *
 * A travel document chip answers on either of the two ISO 14443 flavours and
 * the holder has no way of knowing which; European identity cards in
 * particular are frequently type B while passports are usually type A. The
 * two firmware pollers expose almost the same operation - hand over an I-block
 * and get one back - so the difference is confined to this file and everything
 * above it sees one port.
 *
 * Frame sizes matter more here than they would on a general purpose reader.
 * The Flipper's ISO 14443-4 layer does not reassemble a chained response: if
 * the card answers with the chaining bit set, iso14443_4_layer_decode_block()
 * sees a PCB it did not expect and reports a protocol error. The only defence
 * is never to ask a question whose answer does not fit in one frame, which is
 * what emrtd_transceiver_max_le() is for. In the other direction the layer
 * does chain, one block at a time, and this file drives that loop.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <nfc/protocols/iso14443_4a/iso14443_4a_poller.h>
#include <nfc/protocols/iso14443_4b/iso14443_4b_poller.h>

#include "emrtd_transceiver.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Largest INF field this reader will put on, or expect off, the wire.
 *
 * Both pollers assemble PCB || INF into a 256 byte BitBuffer, and the type B
 * path then appends the two CRC bytes inside another 256 byte buffer, so 253
 * is the largest INF that cannot overflow either. BitBuffer overflow is a
 * furi_check(), which aborts the application rather than returning an error,
 * so this bound is enforced before every append instead of being assumed.
 */
#define EMRTD_ISO14443_4_MAX_INF 253

/** Frame size the Flipper announces in RATS and in ATTRIB. */
#define EMRTD_ISO14443_4_FSD 256

/** Capacity of the working BitBuffers; comfortably above one frame. */
#define EMRTD_ISO14443_4_BUFFER_SIZE 512

/** Which poller is behind the port. */
typedef enum {
    EmrtdIso14443_4VariantNone,
    EmrtdIso14443_4VariantA,
    EmrtdIso14443_4VariantB,
} EmrtdIso14443_4Variant;

/**
 * Called with every block of bytes that crosses the interface.
 *
 * Runs on the NFC thread, inside the poller callback, so an implementation
 * has to be quick and must not touch the user interface.
 *
 * @param[in] outgoing true for a command, false for the response to it
 */
typedef void (
    *EmrtdIso14443_4TraceCallback)(void* context, bool outgoing, const uint8_t* data, size_t len);

typedef struct EmrtdIso14443_4 EmrtdIso14443_4;

EmrtdIso14443_4* emrtd_iso14443_4_alloc(void);
void emrtd_iso14443_4_free(EmrtdIso14443_4* instance);

/**
 * Point the port at an activated type A poller.
 *
 * @param[in] data the poller's card data, whose ATS carries the card's frame
 *                 size; may be NULL, in which case the ISO 14443-4 default of
 *                 32 bytes is assumed
 */
void emrtd_iso14443_4_bind_4a(
    EmrtdIso14443_4* instance,
    Iso14443_4aPoller* poller,
    const Iso14443_4aData* data);

/** Point the port at an activated type B poller. */
void emrtd_iso14443_4_bind_4b(
    EmrtdIso14443_4* instance,
    Iso14443_4bPoller* poller,
    const Iso14443_4bData* data);

/**
 * Forget the poller.
 *
 * The pollers belong to the NFC stack and are destroyed when the session ends,
 * so the port has to be told to stop holding on to them.
 */
void emrtd_iso14443_4_unbind(EmrtdIso14443_4* instance);

/** The port itself, ready to be handed to the access drivers. */
EmrtdTransceiver* emrtd_iso14443_4_transceiver(EmrtdIso14443_4* instance);

/** Which poller is bound, for the report and for the error messages. */
EmrtdIso14443_4Variant emrtd_iso14443_4_variant(const EmrtdIso14443_4* instance);

/** Install the diagnostic hook. Pass NULL to remove it. */
void emrtd_iso14443_4_set_trace(
    EmrtdIso14443_4* instance,
    EmrtdIso14443_4TraceCallback callback,
    void* context);

#ifdef __cplusplus
}
#endif
