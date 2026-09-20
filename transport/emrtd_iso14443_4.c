/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Filip Sedivy
 */

#include "emrtd_iso14443_4.h"

#include <string.h>

#include <furi.h>
#include <nfc/protocols/iso14443_3b/iso14443_3b.h>
#include <toolbox/bit_buffer.h>

#define TAG "EmrtdIso14443_4"

/** Prologue (PCB) plus epilogue (CRC) of an ISO 14443-4 block. */
#define EMRTD_ISO14443_4_FRAME_OVERHEAD 3

/** ISO 14443-4 section 5.1: the frame size before the ATS has been read. */
#define EMRTD_ISO14443_4_FSC_DEFAULT 32

struct EmrtdIso14443_4 {
    EmrtdTransceiver transceiver;
    EmrtdIso14443_4Variant variant;
    Iso14443_4aPoller* poller_4a;
    Iso14443_4bPoller* poller_4b;
    BitBuffer* tx_buffer;
    BitBuffer* rx_buffer;
    EmrtdIso14443_4TraceCallback trace;
    void* trace_context;
};

static EmrtdError emrtd_iso14443_4_transceive(
    void* ctx,
    const uint8_t* tx,
    size_t tx_len,
    uint8_t* rx,
    size_t rx_cap,
    size_t* rx_len);

static const EmrtdTransceiverApi emrtd_iso14443_4a_api = {
    .name = "ISO 14443-4A",
    .transceive = emrtd_iso14443_4_transceive,
};

static const EmrtdTransceiverApi emrtd_iso14443_4b_api = {
    .name = "ISO 14443-4B",
    .transceive = emrtd_iso14443_4_transceive,
};

/** Nothing is bound yet; an exchange attempted now is a programming error. */
static const EmrtdTransceiverApi emrtd_iso14443_4_idle_api = {
    .name = "ISO 14443-4",
    .transceive = emrtd_iso14443_4_transceive,
};

EmrtdIso14443_4* emrtd_iso14443_4_alloc(void) {
    EmrtdIso14443_4* instance = malloc(sizeof(EmrtdIso14443_4));
    memset(instance, 0, sizeof(EmrtdIso14443_4));

    instance->variant = EmrtdIso14443_4VariantNone;
    instance->transceiver.api = &emrtd_iso14443_4_idle_api;
    instance->transceiver.ctx = instance;
    instance->transceiver.fsc = EMRTD_ISO14443_4_FSC_DEFAULT;
    instance->transceiver.fsd = EMRTD_ISO14443_4_FSD;

    instance->tx_buffer = bit_buffer_alloc(EMRTD_ISO14443_4_BUFFER_SIZE);
    instance->rx_buffer = bit_buffer_alloc(EMRTD_ISO14443_4_BUFFER_SIZE);

    return instance;
}

void emrtd_iso14443_4_free(EmrtdIso14443_4* instance) {
    furi_check(instance);

    bit_buffer_free(instance->tx_buffer);
    bit_buffer_free(instance->rx_buffer);
    free(instance);
}

/**
 * Decide the card's frame size.
 *
 * The ATS carries FSCI only when it is longer than its own length byte; a
 * one byte ATS means the card keeps the ISO 14443-4 default. A card that
 * announces more than the reader can hold is capped, because the limit that
 * binds is whichever of the two is smaller.
 */
static void emrtd_iso14443_4_set_fsc(EmrtdIso14443_4* instance, uint16_t fsc) {
    if(fsc < EMRTD_ISO14443_4_FSC_DEFAULT) {
        fsc = EMRTD_ISO14443_4_FSC_DEFAULT;
    }
    if(fsc > EMRTD_ISO14443_4_MAX_INF + EMRTD_ISO14443_4_FRAME_OVERHEAD) {
        fsc = EMRTD_ISO14443_4_MAX_INF + EMRTD_ISO14443_4_FRAME_OVERHEAD;
    }
    instance->transceiver.fsc = fsc;
}

void emrtd_iso14443_4_bind_4a(
    EmrtdIso14443_4* instance,
    Iso14443_4aPoller* poller,
    const Iso14443_4aData* data) {
    furi_check(instance);
    furi_check(poller);

    instance->variant = EmrtdIso14443_4VariantA;
    instance->poller_4a = poller;
    instance->poller_4b = NULL;
    instance->transceiver.api = &emrtd_iso14443_4a_api;

    uint16_t fsc = EMRTD_ISO14443_4_FSC_DEFAULT;
    if(data != NULL && data->ats_data.tl >= 2) {
        fsc = emrtd_transceiver_fsc_from_fsci(data->ats_data.t0 & 0x0F);
    }
    emrtd_iso14443_4_set_fsc(instance, fsc);

    FURI_LOG_I(TAG, "Type A card, FSC %u", instance->transceiver.fsc);
}

void emrtd_iso14443_4_bind_4b(
    EmrtdIso14443_4* instance,
    Iso14443_4bPoller* poller,
    const Iso14443_4bData* data) {
    furi_check(instance);
    furi_check(poller);

    instance->variant = EmrtdIso14443_4VariantB;
    instance->poller_4a = NULL;
    instance->poller_4b = poller;
    instance->transceiver.api = &emrtd_iso14443_4b_api;

    /*
     * Type B carries the frame size in the protocol info of the ATQB rather
     * than in an ATS, and the firmware exposes it only through the type 3B
     * accessor, which already applies the ISO 14443-3 table.
     */
    uint16_t fsc = EMRTD_ISO14443_4_FSC_DEFAULT;
    if(data != NULL) {
        const Iso14443_3bData* base = iso14443_4b_get_base_data(data);
        if(base != NULL) {
            const uint16_t announced = iso14443_3b_get_frame_size_max(base);
            if(announced > 0) {
                fsc = announced;
            }
        }
    }
    emrtd_iso14443_4_set_fsc(instance, fsc);

    FURI_LOG_I(TAG, "Type B card, FSC %u", instance->transceiver.fsc);
}

void emrtd_iso14443_4_unbind(EmrtdIso14443_4* instance) {
    furi_check(instance);

    instance->variant = EmrtdIso14443_4VariantNone;
    instance->poller_4a = NULL;
    instance->poller_4b = NULL;
    instance->transceiver.api = &emrtd_iso14443_4_idle_api;
    instance->transceiver.fsc = EMRTD_ISO14443_4_FSC_DEFAULT;
    bit_buffer_reset(instance->tx_buffer);
    bit_buffer_reset(instance->rx_buffer);
}

EmrtdTransceiver* emrtd_iso14443_4_transceiver(EmrtdIso14443_4* instance) {
    furi_check(instance);

    return &instance->transceiver;
}

EmrtdIso14443_4Variant emrtd_iso14443_4_variant(const EmrtdIso14443_4* instance) {
    furi_check(instance);

    return instance->variant;
}

void emrtd_iso14443_4_set_trace(
    EmrtdIso14443_4* instance,
    EmrtdIso14443_4TraceCallback callback,
    void* context) {
    furi_check(instance);

    instance->trace = callback;
    instance->trace_context = context;
}

static EmrtdError emrtd_iso14443_4_map_error_a(Iso14443_4aError error) {
    switch(error) {
    case Iso14443_4aErrorNone:
        return EmrtdErrorNone;
    case Iso14443_4aErrorNotPresent:
    case Iso14443_4aErrorTimeout:
        return EmrtdErrorCardLost;
    case Iso14443_4aErrorProtocol:
        return EmrtdErrorProtocol;
    default:
        return EmrtdErrorTransport;
    }
}

static EmrtdError emrtd_iso14443_4_map_error_b(Iso14443_4bError error) {
    switch(error) {
    case Iso14443_4bErrorNone:
        return EmrtdErrorNone;
    case Iso14443_4bErrorNotPresent:
    case Iso14443_4bErrorTimeout:
        return EmrtdErrorCardLost;
    case Iso14443_4bErrorProtocol:
        return EmrtdErrorProtocol;
    default:
        return EmrtdErrorTransport;
    }
}

/** Largest command data field that fits one frame towards the card. */
static size_t emrtd_iso14443_4_max_inf(const EmrtdIso14443_4* instance) {
    size_t fsc = instance->transceiver.fsc;
    if(fsc < EMRTD_ISO14443_4_FRAME_OVERHEAD + 1) {
        fsc = EMRTD_ISO14443_4_FSC_DEFAULT;
    }

    size_t inf = fsc - EMRTD_ISO14443_4_FRAME_OVERHEAD;
    if(inf > EMRTD_ISO14443_4_MAX_INF) {
        inf = EMRTD_ISO14443_4_MAX_INF;
    }
    return inf;
}

/**
 * Put one block on the wire.
 *
 * iso14443_4a_poller_send_block() answers S(WTX) on its own, which a chip
 * doing several seconds of elliptic curve arithmetic will ask for, so the
 * waiting time extension needs no handling here.
 */
static EmrtdError emrtd_iso14443_4_send_block(
    EmrtdIso14443_4* instance,
    const uint8_t* data,
    size_t len,
    bool chaining) {
    if(bit_buffer_get_capacity_bytes(instance->tx_buffer) < len) {
        return EmrtdErrorBufferTooSmall;
    }

    bit_buffer_reset(instance->tx_buffer);
    bit_buffer_append_bytes(instance->tx_buffer, data, len);
    bit_buffer_reset(instance->rx_buffer);

    if(instance->variant == EmrtdIso14443_4VariantA) {
        const Iso14443_4aError error =
            chaining ? iso14443_4a_poller_send_chain_block(
                           instance->poller_4a, instance->tx_buffer, instance->rx_buffer) :
                       iso14443_4a_poller_send_block(
                           instance->poller_4a, instance->tx_buffer, instance->rx_buffer);
        return emrtd_iso14443_4_map_error_a(error);
    }

    /*
     * The type B poller has no chaining helper, so a command that does not fit
     * one frame is refused above rather than silently truncated here.
     */
    const Iso14443_4bError error = iso14443_4b_poller_send_block(
        instance->poller_4b, instance->tx_buffer, instance->rx_buffer);
    return emrtd_iso14443_4_map_error_b(error);
}

static EmrtdError emrtd_iso14443_4_transceive(
    void* ctx,
    const uint8_t* tx,
    size_t tx_len,
    uint8_t* rx,
    size_t rx_cap,
    size_t* rx_len) {
    EmrtdIso14443_4* instance = ctx;
    if(instance == NULL || tx == NULL || rx == NULL || rx_len == NULL) {
        return EmrtdErrorInternal;
    }

    *rx_len = 0;

    if(instance->variant == EmrtdIso14443_4VariantNone) {
        return EmrtdErrorNoCard;
    }
    /* Four bytes is the shortest legal command APDU, CLA INS P1 P2. */
    if(tx_len < 4 || tx_len > EMRTD_ISO14443_4_BUFFER_SIZE) {
        return EmrtdErrorInvalidInput;
    }

    if(instance->trace != NULL) {
        instance->trace(instance->trace_context, true, tx, tx_len);
    }

    const size_t max_inf = emrtd_iso14443_4_max_inf(instance);
    if(tx_len > max_inf && instance->variant != EmrtdIso14443_4VariantA) {
        FURI_LOG_W(TAG, "Command of %zu bytes exceeds the type B frame size", tx_len);
        return EmrtdErrorProtocol;
    }

    /*
     * ISO 14443-4 section 7.5.2: a command longer than the card's frame size
     * goes out as a run of chained I-blocks, each acknowledged with an R(ACK),
     * and only the last one carries the chaining bit clear and brings back the
     * response.
     */
    size_t sent = 0;
    while(sent < tx_len) {
        const size_t remaining = tx_len - sent;
        const size_t block = remaining > max_inf ? max_inf : remaining;
        const bool last = (sent + block) == tx_len;

        const EmrtdError error = emrtd_iso14443_4_send_block(instance, tx + sent, block, !last);
        if(error != EmrtdErrorNone) {
            FURI_LOG_W(TAG, "Block at offset %zu failed: %d", sent, error);
            return error;
        }
        sent += block;
    }

    if(bit_buffer_has_partial_byte(instance->rx_buffer)) {
        return EmrtdErrorProtocol;
    }

    const size_t received = bit_buffer_get_size_bytes(instance->rx_buffer);
    /* Even a bare status word is two bytes; anything shorter is not an APDU. */
    if(received < 2) {
        return EmrtdErrorProtocol;
    }
    if(received > rx_cap) {
        return EmrtdErrorBufferTooSmall;
    }

    bit_buffer_write_bytes(instance->rx_buffer, rx, received);
    *rx_len = received;

    if(instance->trace != NULL) {
        instance->trace(instance->trace_context, false, rx, received);
    }

    return EmrtdErrorNone;
}
