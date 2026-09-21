/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Filip Sedivy
 */

#include "emrtd_isodep.h"

#include <string.h>

#include "emrtd_transceiver.h"

/*
 * Protocol control bytes, ISO/IEC 14443-4 section 7.1.
 *
 * An I-block is 000 c i n 1 b: c chaining, i CID follows, n NAD follows, b the
 * block number. An R-block is 10 a 0 i 1 b, with a set for a negative
 * acknowledgement. An S-block is 11 w w i 1 0, with ww = 11 for a waiting time
 * extension and 00 for deselect.
 */
#define EMRTD_PCB_I_BASE     0x02
#define EMRTD_PCB_I_CHAINING 0x10
#define EMRTD_PCB_I_MASK     0xE2
#define EMRTD_PCB_CID        0x08
#define EMRTD_PCB_NAD        0x04

#define EMRTD_PCB_R_ACK      0xA2
#define EMRTD_PCB_R_NAK_FLAG 0x10
#define EMRTD_PCB_R_MASK     0xE6

#define EMRTD_PCB_S_WTX      0xF2
#define EMRTD_PCB_S_DESELECT 0xC2
#define EMRTD_PCB_S_MASK     0xC7

#define EMRTD_PCB_BLOCK_NUMBER 0x01

/** RATS, ISO/IEC 14443-4 section 5.6.1.2. */
#define EMRTD_ISODEP_CMD_RATS 0xE0

/** FSDI 8, which is 256 bytes - what EMRTD_ISODEP_FSD announces. */
#define EMRTD_ISODEP_FSDI 0x08

/** ISO/IEC 14443-4 section 5.2.5: the frame size assumed before the ATS. */
#define EMRTD_ISODEP_FSC_DEFAULT 32

/** Frame waiting time index a card is assumed to want when it names none. */
#define EMRTD_ISODEP_FWI_DEFAULT 4

/** FWI 15 is reserved; 14 is the largest the standard defines. */
#define EMRTD_ISODEP_FWI_MAX 14

/**
 * Blocks one chained response may run to.
 *
 * The buffer bound already stops a chain that carries data, but a chip that
 * chains empty blocks would otherwise spin the reader for ever.
 */
#define EMRTD_ISODEP_CHAIN_BLOCKS_MAX 64

/** Room kept in a frame for the PCB, the CRC and a CID byte the card may add. */
#define EMRTD_ISODEP_FRAME_OVERHEAD 4

static bool emrtd_isodep_is_i_block(uint8_t pcb) {
    return (pcb & EMRTD_PCB_I_MASK) == EMRTD_PCB_I_BASE;
}

static bool emrtd_isodep_is_r_block(uint8_t pcb) {
    return (pcb & EMRTD_PCB_R_MASK) == EMRTD_PCB_R_ACK;
}

static bool emrtd_isodep_is_r_nak(uint8_t pcb) {
    return emrtd_isodep_is_r_block(pcb) && (pcb & EMRTD_PCB_R_NAK_FLAG) != 0;
}

static bool emrtd_isodep_is_s_wtx(uint8_t pcb) {
    return (pcb & EMRTD_PCB_S_MASK) == (EMRTD_PCB_S_WTX & EMRTD_PCB_S_MASK) &&
           (pcb & 0x30) == 0x30;
}

/** Bytes before the information field: the PCB, and CID or NAD when present. */
static size_t emrtd_isodep_header_len(uint8_t pcb) {
    size_t len = 1;
    if(pcb & EMRTD_PCB_CID) {
        len++;
    }
    if(pcb & EMRTD_PCB_NAD) {
        len++;
    }
    return len;
}

/** The waiting time the reader arms, from what the card announced. */
static uint32_t emrtd_isodep_fwt_from_fwi(uint8_t fwi) {
    if(fwi > EMRTD_ISODEP_FWI_MAX) {
        fwi = EMRTD_ISODEP_FWI_MAX;
    }
    uint64_t fwt = 4096ull << fwi;
    if(fwt < EMRTD_ISODEP_FWT_MIN_FC) {
        fwt = EMRTD_ISODEP_FWT_MIN_FC;
    }
    if(fwt > EMRTD_ISODEP_FWT_MAX_FC) {
        fwt = EMRTD_ISODEP_FWT_MAX_FC;
    }
    return (uint32_t)fwt;
}

/** Largest information field that fits one frame towards the card. */
static size_t emrtd_isodep_inf_capacity(const EmrtdIsoDep* instance) {
    size_t fsc = instance->fsc;
    if(fsc <= EMRTD_ISODEP_FRAME_OVERHEAD) {
        fsc = EMRTD_ISODEP_FSC_DEFAULT;
    }
    size_t inf = fsc - EMRTD_ISODEP_FRAME_OVERHEAD;
    if(inf > EMRTD_ISODEP_FRAME_MAX - 1) {
        inf = EMRTD_ISODEP_FRAME_MAX - 1;
    }
    return inf;
}

void emrtd_isodep_init(EmrtdIsoDep* instance, EmrtdIsoDepFrameFn send, void* context) {
    if(instance == NULL) {
        return;
    }
    memset(instance, 0, sizeof(*instance));
    instance->send = send;
    instance->context = context;
    instance->fsc = EMRTD_ISODEP_FSC_DEFAULT;
    instance->fwi = EMRTD_ISODEP_FWI_DEFAULT;
    instance->fwt_fc = emrtd_isodep_fwt_from_fwi(EMRTD_ISODEP_FWI_DEFAULT);
}

void emrtd_isodep_reset(EmrtdIsoDep* instance) {
    if(instance == NULL) {
        return;
    }
    EmrtdIsoDepFrameFn send = instance->send;
    void* context = instance->context;
    emrtd_isodep_init(instance, send, context);
}

void emrtd_isodep_parse_ats(EmrtdIsoDep* instance, const uint8_t* ats, size_t len) {
    if(instance == NULL) {
        return;
    }

    instance->fsc = EMRTD_ISODEP_FSC_DEFAULT;
    instance->fwi = EMRTD_ISODEP_FWI_DEFAULT;
    instance->fwi_announced = false;
    instance->ats_len = 0;

    if(ats != NULL && len > 0) {
        const size_t copied = len < EMRTD_ISODEP_ATS_MAX ? len : EMRTD_ISODEP_ATS_MAX;
        memcpy(instance->ats, ats, copied);
        instance->ats_len = copied;

        /*
         * TL counts itself. A card that answers with more bytes than TL
         * promises is not unheard of, so the smaller of the two bounds the
         * interface bytes; the historical bytes beyond them are not read here.
         */
        size_t body = ats[0];
        if(body > len) {
            body = len;
        }

        if(body >= 2) {
            const uint8_t t0 = ats[1];
            instance->fsc = emrtd_transceiver_fsc_from_fsci(t0 & 0x0F);

            size_t index = 2;
            if((t0 & 0x10) && index < body) {
                index++; /* TA(1) carries the bit rates, which are not changed. */
            }
            if((t0 & 0x20) && index < body) {
                instance->fwi = (uint8_t)(ats[index] >> 4);
                instance->fwi_announced = true;
                index++;
            }
            /*
             * TC(1) announces whether the card accepts a CID and a NAD. This
             * reader offers neither, so there is nothing to negotiate, but a
             * card is still free to put a CID in its own blocks and
             * emrtd_isodep_header_len() allows for that.
             */
        }
    }

    if(instance->fsc > EMRTD_ISODEP_FSD) {
        instance->fsc = EMRTD_ISODEP_FSD;
    }
    if(instance->fsc < 16) {
        instance->fsc = 16;
    }
    instance->fwt_fc = emrtd_isodep_fwt_from_fwi(instance->fwi);
}

/**
 * One frame out and one back, retried while the answer does not arrive.
 *
 * @param[in] attempts how many times the frame may be put on the wire in all
 */
static EmrtdError emrtd_isodep_send_raw(
    EmrtdIsoDep* instance,
    const uint8_t* frame,
    size_t len,
    uint32_t fwt_fc,
    unsigned attempts,
    size_t* out_len) {
    EmrtdError error = EmrtdErrorInternal;

    for(unsigned attempt = 0; attempt < attempts; attempt++) {
        size_t received = 0;
        error = instance->send(
            instance->context,
            frame,
            len,
            instance->rx_frame,
            sizeof(instance->rx_frame),
            &received,
            fwt_fc);

        if(error == EmrtdErrorNone) {
            *out_len = received;
            return EmrtdErrorNone;
        }
        /*
         * Only a lost answer is worth asking for again. A refused frame, a
         * buffer that was too small or an argument this layer got wrong will
         * fail exactly the same way every time.
         */
        if(error != EmrtdErrorCardLost && error != EmrtdErrorTransport) {
            break;
        }
    }

    *out_len = 0;
    return error;
}

/** Send a block, service every waiting time extension, return the real answer. */
static EmrtdError emrtd_isodep_exchange(
    EmrtdIsoDep* instance,
    const uint8_t* frame,
    size_t len,
    size_t* out_len) {
    EmrtdError error = emrtd_isodep_send_raw(
        instance, frame, len, instance->fwt_fc, EMRTD_ISODEP_RETRIES + 1, out_len);
    if(error != EmrtdErrorNone) {
        return error;
    }

    unsigned rounds = 0;
    while(*out_len >= 1 && emrtd_isodep_is_s_wtx(instance->rx_frame[0])) {
        if(++rounds > EMRTD_ISODEP_WTX_ROUNDS_MAX) {
            return EmrtdErrorProtocol;
        }

        /*
         * ICAO 9303-11 has the chip do elliptic curve arithmetic inside one
         * command, and this is how it asks for the time: S(WTX) names a
         * multiplier, and the answer repeats it to grant it. The reply is
         * built here rather than in the caller's buffer because that buffer
         * still holds the block being sent.
         */
        uint8_t wtxm = (*out_len >= 2 ? instance->rx_frame[1] : 1u) & 0x3F;
        if(wtxm == 0) {
            wtxm = 1;
        }
        const uint8_t reply[2] = {EMRTD_PCB_S_WTX, wtxm};

        uint64_t granted = (uint64_t)instance->fwt_fc * wtxm;
        if(granted > EMRTD_ISODEP_FWT_MAX_FC) {
            granted = EMRTD_ISODEP_FWT_MAX_FC;
        }

        error = emrtd_isodep_send_raw(
            instance, reply, sizeof(reply), (uint32_t)granted, EMRTD_ISODEP_RETRIES + 1, out_len);
        if(error != EmrtdErrorNone) {
            return error;
        }
    }

    return EmrtdErrorNone;
}

EmrtdError emrtd_isodep_activate(EmrtdIsoDep* instance) {
    if(instance == NULL || instance->send == NULL) {
        return EmrtdErrorInternal;
    }

    instance->activated = false;
    instance->block_number = 0;

    const uint8_t rats[2] = {
        EMRTD_ISODEP_CMD_RATS,
        (uint8_t)(EMRTD_ISODEP_FSDI << 4), /* FSDI in the high nibble, CID 0 in the low. */
    };

    /*
     * One attempt only. A card that answered RATS has already left the state
     * in which RATS means anything, so a second one would be ignored even
     * though the first succeeded; recovering from that needs the field
     * dropped, which is the caller's business, not this layer's.
     */
    size_t received = 0;
    const EmrtdError error = emrtd_isodep_send_raw(
        instance, rats, sizeof(rats), EMRTD_ISODEP_RATS_FWT_FC, 1, &received);
    if(error != EmrtdErrorNone) {
        return error;
    }
    if(received == 0) {
        return EmrtdErrorProtocol;
    }

    emrtd_isodep_parse_ats(instance, instance->rx_frame, received);
    instance->activated = true;
    return EmrtdErrorNone;
}

/** Collect one response, following the chain the card may have split it into. */
static EmrtdError emrtd_isodep_receive(
    EmrtdIsoDep* instance,
    uint8_t* rx,
    size_t rx_cap,
    size_t* rx_len,
    size_t frame_len) {
    size_t total = 0;

    for(unsigned block = 0; block < EMRTD_ISODEP_CHAIN_BLOCKS_MAX; block++) {
        if(frame_len == 0) {
            return EmrtdErrorProtocol;
        }

        const uint8_t pcb = instance->rx_frame[0];
        if(!emrtd_isodep_is_i_block(pcb)) {
            return EmrtdErrorProtocol;
        }

        const size_t header = emrtd_isodep_header_len(pcb);
        if(frame_len < header) {
            return EmrtdErrorProtocol;
        }
        const size_t inf = frame_len - header;
        if(inf > rx_cap - total) {
            return EmrtdErrorBufferTooSmall;
        }
        memcpy(rx + total, instance->rx_frame + header, inf);
        total += inf;

        /* ISO/IEC 14443-4 rule C: the block was accepted, so the number moves. */
        instance->block_number ^= 1;

        if(!(pcb & EMRTD_PCB_I_CHAINING)) {
            *rx_len = total;
            return EmrtdErrorNone;
        }

        const uint8_t ack[1] = {
            (uint8_t)(EMRTD_PCB_R_ACK | (instance->block_number & EMRTD_PCB_BLOCK_NUMBER)),
        };
        const EmrtdError error = emrtd_isodep_exchange(instance, ack, sizeof(ack), &frame_len);
        if(error != EmrtdErrorNone) {
            return error;
        }
    }

    /* The chain ran past what any travel document file could need. */
    return EmrtdErrorProtocol;
}

EmrtdError emrtd_isodep_transceive(
    EmrtdIsoDep* instance,
    const uint8_t* tx,
    size_t tx_len,
    uint8_t* rx,
    size_t rx_cap,
    size_t* rx_len) {
    if(instance == NULL || instance->send == NULL || tx == NULL || rx == NULL || rx_len == NULL) {
        return EmrtdErrorInternal;
    }
    *rx_len = 0;
    if(!instance->activated) {
        return EmrtdErrorNoCard;
    }
    if(tx_len == 0 || rx_cap == 0) {
        return EmrtdErrorInvalidInput;
    }

    const size_t capacity = emrtd_isodep_inf_capacity(instance);
    size_t sent = 0;
    size_t frame_len = 0;

    while(sent < tx_len) {
        const size_t remaining = tx_len - sent;
        const size_t chunk = remaining > capacity ? capacity : remaining;
        const bool last = (sent + chunk) == tx_len;

        uint8_t pcb =
            (uint8_t)(EMRTD_PCB_I_BASE | (instance->block_number & EMRTD_PCB_BLOCK_NUMBER));
        if(!last) {
            pcb |= EMRTD_PCB_I_CHAINING;
        }
        instance->tx_frame[0] = pcb;
        memcpy(instance->tx_frame + 1, tx + sent, chunk);

        const EmrtdError error =
            emrtd_isodep_exchange(instance, instance->tx_frame, chunk + 1, &frame_len);
        if(error != EmrtdErrorNone) {
            return error;
        }
        sent += chunk;

        if(last) {
            break;
        }

        /*
         * Every block but the last is answered with an acknowledgement
         * carrying the block number that was just used; only then does the
         * number move on.
         */
        if(frame_len < 1) {
            return EmrtdErrorProtocol;
        }
        const uint8_t answer = instance->rx_frame[0];
        if(!emrtd_isodep_is_r_block(answer) || emrtd_isodep_is_r_nak(answer) ||
           (answer & EMRTD_PCB_BLOCK_NUMBER) !=
               (instance->block_number & EMRTD_PCB_BLOCK_NUMBER)) {
            return EmrtdErrorProtocol;
        }
        instance->block_number ^= 1;
    }

    return emrtd_isodep_receive(instance, rx, rx_cap, rx_len, frame_len);
}

void emrtd_isodep_deselect(EmrtdIsoDep* instance) {
    if(instance == NULL || instance->send == NULL || !instance->activated) {
        return;
    }

    const uint8_t deselect[1] = {EMRTD_PCB_S_DESELECT};
    size_t received = 0;
    (void)emrtd_isodep_send_raw(
        instance, deselect, sizeof(deselect), instance->fwt_fc, 1, &received);
    instance->activated = false;
}

uint16_t emrtd_isodep_fsc(const EmrtdIsoDep* instance) {
    return instance != NULL ? instance->fsc : EMRTD_ISODEP_FSC_DEFAULT;
}

const uint8_t* emrtd_isodep_ats(const EmrtdIsoDep* instance, size_t* len) {
    if(instance == NULL || instance->ats_len == 0) {
        if(len != NULL) {
            *len = 0;
        }
        return NULL;
    }
    if(len != NULL) {
        *len = instance->ats_len;
    }
    return instance->ats;
}
