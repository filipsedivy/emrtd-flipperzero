/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Filip Sedivy
 *
 * ISO-DEP, the block transmission protocol of ISO/IEC 14443-4.
 *
 * This layer exists because the firmware's own ISO 14443-4A poller gives a
 * card 120 microseconds to answer whenever its ATS carries no TB1, which no
 * smartcard can meet; see transport/emrtd_isodep.h. The timings are therefore
 * not decoration and the first group of checks pins them.
 *
 * The rest drives the layer against a simulated chip that implements the card
 * half of the protocol: it reassembles a chained command, chains its own
 * answer, asks for a waiting time extension, and can swallow a frame so that
 * retransmission has something to recover from.
 */

#include "emrtd_test.h"

#include <stdlib.h>

#include "../../transport/emrtd_isodep.h"

/* --- A chip that speaks the card half ----------------------------------- */

#define CHIP_APDU_MAX 1024

typedef struct {
    const uint8_t* ats;
    size_t ats_len;

    bool activated;
    uint8_t block_number; /**< The last one the reader used. */

    uint8_t command[CHIP_APDU_MAX]; /**< The command, reassembled. */
    size_t command_len;

    const uint8_t* response;
    size_t response_len;
    size_t response_sent;
    size_t response_chunk; /**< 0 means the whole answer in one frame. */

    unsigned wtx_pending; /**< Extensions still to be asked for. */
    uint8_t wtxm;
    unsigned wtx_answered;

    unsigned drop_next; /**< Frames to swallow instead of answering. */

    uint32_t last_fwt;
    unsigned frames;
    unsigned commands; /**< Complete commands received, to catch a replay. */
} Chip;

/** Put one frame of the answer on the wire, chaining if the chip was told to. */
static EmrtdError chip_answer(Chip* chip, uint8_t* rx, size_t rx_cap, size_t* rx_len) {
    const size_t remaining = chip->response_len - chip->response_sent;
    size_t chunk = chip->response_chunk;
    if(chunk == 0 || chunk > remaining) {
        chunk = remaining;
    }
    const bool last = (chip->response_sent + chunk) == chip->response_len;

    if(1 + chunk > rx_cap) {
        return EmrtdErrorBufferTooSmall;
    }
    rx[0] = (uint8_t)(0x02 | (chip->block_number & 0x01) | (last ? 0x00 : 0x10));
    memcpy(rx + 1, chip->response + chip->response_sent, chunk);
    chip->response_sent += chunk;
    *rx_len = 1 + chunk;
    return EmrtdErrorNone;
}

static EmrtdError chip_frame(
    void* context,
    const uint8_t* tx,
    size_t tx_len,
    uint8_t* rx,
    size_t rx_cap,
    size_t* rx_len,
    uint32_t fwt_fc) {
    Chip* chip = context;

    chip->frames++;
    chip->last_fwt = fwt_fc;
    *rx_len = 0;

    if(chip->drop_next > 0) {
        chip->drop_next--;
        return EmrtdErrorCardLost;
    }
    if(tx_len == 0) {
        return EmrtdErrorInvalidInput;
    }

    const uint8_t pcb = tx[0];

    /* RATS, which is the only thing an unactivated card answers. */
    if(pcb == 0xE0) {
        if(chip->ats_len > rx_cap) {
            return EmrtdErrorBufferTooSmall;
        }
        memcpy(rx, chip->ats, chip->ats_len);
        *rx_len = chip->ats_len;
        chip->activated = true;
        return EmrtdErrorNone;
    }
    if(!chip->activated) {
        return EmrtdErrorCardLost;
    }

    /* S(DESELECT). */
    if((pcb & 0xF7) == 0xC2) {
        rx[0] = 0xC2;
        *rx_len = 1;
        chip->activated = false;
        return EmrtdErrorNone;
    }

    /* S(WTX): the reader granting the time that was asked for. */
    if((pcb & 0xF7) == 0xF2) {
        chip->wtx_answered++;
        if(tx_len < 2 || (tx[1] & 0x3F) != chip->wtxm) {
            return EmrtdErrorProtocol;
        }
        if(chip->wtx_pending > 0) {
            chip->wtx_pending--;
        }
        if(chip->wtx_pending > 0) {
            rx[0] = 0xF2;
            rx[1] = chip->wtxm;
            *rx_len = 2;
            return EmrtdErrorNone;
        }
        return chip_answer(chip, rx, rx_cap, rx_len);
    }

    /* R(ACK): the reader asking for the next block of a chained answer. */
    if((pcb & 0xE6) == 0xA2) {
        chip->block_number = pcb & 0x01;
        return chip_answer(chip, rx, rx_cap, rx_len);
    }

    /* I-block. */
    if((pcb & 0xE2) == 0x02) {
        const size_t inf = tx_len - 1;
        if(chip->command_len + inf > CHIP_APDU_MAX) {
            return EmrtdErrorBufferTooSmall;
        }
        memcpy(chip->command + chip->command_len, tx + 1, inf);
        chip->command_len += inf;
        chip->block_number = pcb & 0x01;

        if(pcb & 0x10) {
            /* Chained: acknowledge with the block number that was just used. */
            rx[0] = (uint8_t)(0xA2 | (pcb & 0x01));
            *rx_len = 1;
            return EmrtdErrorNone;
        }

        chip->commands++;
        chip->response_sent = 0;
        if(chip->wtx_pending > 0) {
            rx[0] = 0xF2;
            rx[1] = chip->wtxm;
            *rx_len = 2;
            return EmrtdErrorNone;
        }
        return chip_answer(chip, rx, rx_cap, rx_len);
    }

    return EmrtdErrorProtocol;
}

static void chip_init(Chip* chip, const uint8_t* ats, size_t ats_len) {
    memset(chip, 0, sizeof(*chip));
    chip->ats = ats;
    chip->ats_len = ats_len;
    chip->wtxm = 1;
}

/* --- The ATS, and the timings that come out of it ----------------------- */

/** Parse an ATS given as hex and return the layer's view of it. */
static void parse_ats(EmrtdIsoDep* isodep, const char* hex) {
    uint8_t ats[64];
    const size_t len = emrtd_test_hex(hex, ats, sizeof(ats));
    emrtd_isodep_init(isodep, NULL, NULL);
    emrtd_isodep_parse_ats(isodep, ats, len);
}

static void test_ats(void) {
    EmrtdIsoDep isodep;

    emrtd_test_begin("a passport ATS: frame size and waiting time index");
    /* TL T0 TA1 TB1 TC1: T0 = 78 is FSCI 8 with all three interface bytes. */
    parse_ats(&isodep, "0578807002");
    TEST_EQ_INT(emrtd_isodep_fsc(&isodep), 256);
    TEST_EQ_INT(isodep.fwi, 7);
    TEST_CHECK(isodep.fwi_announced);

    emrtd_test_begin("FWI 9 is below the floor, so the floor is what is waited");
    parse_ats(&isodep, "0578809002");
    TEST_EQ_INT(isodep.fwi, 9);
    /* 4096 << 9 is 2097152 cycles, about 155 ms, which the reference found marginal. */
    TEST_EQ_INT(isodep.fwt_fc, EMRTD_ISODEP_FWT_MIN_FC);

    emrtd_test_begin("a large FWI is honoured above the floor");
    parse_ats(&isodep, "057880D002");
    TEST_EQ_INT(isodep.fwi, 13);
    TEST_EQ_INT(isodep.fwt_fc, 4096u << 13);

    emrtd_test_begin("FWI 15 is reserved, and is read as the largest that is not");
    parse_ats(&isodep, "057880F002");
    TEST_EQ_INT(isodep.fwt_fc, EMRTD_ISODEP_FWT_MAX_FC);

    /*
     * This is the case the firmware's own poller gets wrong: with no TB1 it
     * falls back on ISO14443_3A_FDT_POLL_FC, 1620 cycles, and hands that to
     * every I-block. 1620 cycles is 120 microseconds.
     */
    emrtd_test_begin("an ATS without TB1 still gets a waiting time a chip can meet");
    parse_ats(&isodep, "031188");
    TEST_CHECK(!isodep.fwi_announced);
    TEST_EQ_INT(isodep.fwt_fc, EMRTD_ISODEP_FWT_MIN_FC);
    TEST_CHECK(isodep.fwt_fc > 1620u * 100u);

    emrtd_test_begin("TL alone means the ISO defaults");
    parse_ats(&isodep, "01");
    TEST_EQ_INT(emrtd_isodep_fsc(&isodep), 32);
    TEST_EQ_INT(isodep.fwt_fc, EMRTD_ISODEP_FWT_MIN_FC);

    emrtd_test_begin("no ATS at all is survivable");
    emrtd_isodep_init(&isodep, NULL, NULL);
    emrtd_isodep_parse_ats(&isodep, NULL, 0);
    TEST_EQ_INT(emrtd_isodep_fsc(&isodep), 32);
    TEST_EQ_INT(isodep.fwt_fc, EMRTD_ISODEP_FWT_MIN_FC);

    emrtd_test_begin("a frame size beyond what the reader announced is capped");
    /* FSCI 12 is 4096 bytes; the reader offered 256 in RATS. */
    parse_ats(&isodep, "057C809002");
    TEST_EQ_INT(emrtd_isodep_fsc(&isodep), 256);

    emrtd_test_begin("TB1 is found even when TA1 is absent");
    /* T0 = 68: TC1 and TB1 present, TA1 absent, FSCI 8. */
    parse_ats(&isodep, "0468A002");
    TEST_EQ_INT(isodep.fwi, 10);
    TEST_CHECK(isodep.fwi_announced);

    emrtd_test_begin("a TL longer than the bytes that arrived does not read past them");
    parse_ats(&isodep, "0A78");
    TEST_EQ_INT(emrtd_isodep_fsc(&isodep), 256);
    TEST_CHECK(!isodep.fwi_announced);
}

/* --- Activation --------------------------------------------------------- */

static void test_activation(void) {
    static const uint8_t ats[] = {0x05, 0x78, 0x80, 0x70, 0x02};
    Chip chip;
    EmrtdIsoDep isodep;

    emrtd_test_begin("RATS asks for 256 byte frames and no card identifier");
    chip_init(&chip, ats, sizeof(ats));
    emrtd_isodep_init(&isodep, chip_frame, &chip);
    TEST_EQ_INT(emrtd_isodep_activate(&isodep), EmrtdErrorNone);
    TEST_CHECK(chip.activated);
    TEST_EQ_INT(chip.frames, 1);

    emrtd_test_begin("the ATS is kept for the log, exactly as it arrived");
    size_t len = 0;
    const uint8_t* stored = emrtd_isodep_ats(&isodep, &len);
    TEST_CHECK(stored != NULL);
    TEST_EQ_HEX(stored, len, "0578807002");

    emrtd_test_begin("RATS is given a window far above what the standard requires");
    /* ISO/IEC 14443-4 allows 65536 cycles for the answer to RATS. */
    TEST_CHECK(chip.last_fwt >= 65536u);
    TEST_EQ_INT(chip.last_fwt, EMRTD_ISODEP_RATS_FWT_FC);

    emrtd_test_begin("a card that never answers RATS is not tried again from here");
    chip_init(&chip, ats, sizeof(ats));
    chip.drop_next = 1;
    emrtd_isodep_init(&isodep, chip_frame, &chip);
    TEST_EQ_INT(emrtd_isodep_activate(&isodep), EmrtdErrorCardLost);
    TEST_EQ_INT(chip.frames, 1);

    emrtd_test_begin("an exchange before activation is refused");
    uint8_t rx[64];
    size_t rx_len = 0;
    const uint8_t apdu[] = {0x00, 0xA4, 0x04, 0x0C};
    TEST_EQ_INT(
        emrtd_isodep_transceive(&isodep, apdu, sizeof(apdu), rx, sizeof(rx), &rx_len),
        EmrtdErrorNoCard);
}

/* --- Exchanging APDUs --------------------------------------------------- */

/** Activate against a chip and leave both ready for an exchange. */
static void open_session(Chip* chip, EmrtdIsoDep* isodep, const uint8_t* ats, size_t ats_len) {
    chip_init(chip, ats, ats_len);
    emrtd_isodep_init(isodep, chip_frame, chip);
    TEST_EQ_INT(emrtd_isodep_activate(isodep), EmrtdErrorNone);
    chip->command_len = 0;
}

static void test_exchange(void) {
    static const uint8_t ats[] = {0x05, 0x78, 0x80, 0x70, 0x02};
    static const uint8_t response[] = {0x6F, 0x02, 0x84, 0x00, 0x90, 0x00};
    static const uint8_t select[] = {
        0x00, 0xA4, 0x04, 0x0C, 0x07, 0xA0, 0x00, 0x00, 0x02, 0x47, 0x10, 0x01};

    Chip chip;
    EmrtdIsoDep isodep;
    uint8_t rx[512];
    size_t rx_len = 0;

    emrtd_test_begin("one APDU there and back");
    open_session(&chip, &isodep, ats, sizeof(ats));
    chip.response = response;
    chip.response_len = sizeof(response);
    TEST_EQ_INT(
        emrtd_isodep_transceive(&isodep, select, sizeof(select), rx, sizeof(rx), &rx_len),
        EmrtdErrorNone);
    TEST_EQ_HEX(chip.command, chip.command_len, "00A4040C07A0000002471001");
    TEST_EQ_HEX(rx, rx_len, "6F02840090 00");

    emrtd_test_begin("the block number toggles between commands");
    /* The reader started at 0, so the second command carries 1. */
    chip.command_len = 0;
    chip.response_sent = 0;
    TEST_EQ_INT(isodep.block_number, 1);
    TEST_EQ_INT(
        emrtd_isodep_transceive(&isodep, select, sizeof(select), rx, sizeof(rx), &rx_len),
        EmrtdErrorNone);
    TEST_EQ_INT(isodep.block_number, 0);
    TEST_EQ_INT(chip.commands, 2);

    emrtd_test_begin("a command longer than one frame is chained");
    /* FSCI 2 is 32 bytes, which leaves 28 for the information field. */
    static const uint8_t small_ats[] = {0x05, 0x72, 0x80, 0x70, 0x02};
    open_session(&chip, &isodep, small_ats, sizeof(small_ats));
    TEST_EQ_INT(emrtd_isodep_fsc(&isodep), 32);

    uint8_t long_command[70];
    for(size_t i = 0; i < sizeof(long_command); i++) {
        long_command[i] = (uint8_t)i;
    }
    chip.response = response;
    chip.response_len = sizeof(response);
    unsigned before = chip.frames;
    TEST_EQ_INT(
        emrtd_isodep_transceive(
            &isodep, long_command, sizeof(long_command), rx, sizeof(rx), &rx_len),
        EmrtdErrorNone);
    TEST_EQ_INT(chip.command_len, sizeof(long_command));
    TEST_CHECK(memcmp(chip.command, long_command, sizeof(long_command)) == 0);
    /* 70 bytes in 28 byte pieces is three blocks; the last one brings the answer. */
    TEST_EQ_INT(chip.frames - before, 3);
    TEST_EQ_HEX(rx, rx_len, "6F0284009000");

    emrtd_test_begin("a chained answer is reassembled");
    open_session(&chip, &isodep, ats, sizeof(ats));
    uint8_t long_response[200];
    for(size_t i = 0; i < sizeof(long_response); i++) {
        long_response[i] = (uint8_t)(0xFF - i);
    }
    chip.response = long_response;
    chip.response_len = sizeof(long_response);
    chip.response_chunk = 64;
    before = chip.frames;
    TEST_EQ_INT(
        emrtd_isodep_transceive(&isodep, select, sizeof(select), rx, sizeof(rx), &rx_len),
        EmrtdErrorNone);
    TEST_EQ_INT(rx_len, sizeof(long_response));
    TEST_CHECK(memcmp(rx, long_response, sizeof(long_response)) == 0);
    /* The command, then an acknowledgement for each of the three further blocks. */
    TEST_EQ_INT(chip.frames - before, 4);

    emrtd_test_begin("an answer larger than the caller's buffer is reported, not truncated");
    open_session(&chip, &isodep, ats, sizeof(ats));
    chip.response = long_response;
    chip.response_len = sizeof(long_response);
    chip.response_chunk = 64;
    uint8_t small_rx[100];
    TEST_EQ_INT(
        emrtd_isodep_transceive(
            &isodep, select, sizeof(select), small_rx, sizeof(small_rx), &rx_len),
        EmrtdErrorBufferTooSmall);
}

/* --- Waiting time extension --------------------------------------------- */

static void test_wtx(void) {
    static const uint8_t ats[] = {0x05, 0x78, 0x80, 0x70, 0x02};
    static const uint8_t response[] = {0x90, 0x00};
    static const uint8_t command[] = {0x00, 0x86, 0x00, 0x00};

    Chip chip;
    EmrtdIsoDep isodep;
    uint8_t rx[64];
    size_t rx_len = 0;

    emrtd_test_begin("a chip that asks for more time is granted it and then answers");
    open_session(&chip, &isodep, ats, sizeof(ats));
    chip.response = response;
    chip.response_len = sizeof(response);
    chip.wtx_pending = 3;
    chip.wtxm = 5;

    const uint32_t ordinary = isodep.fwt_fc;
    TEST_EQ_INT(
        emrtd_isodep_transceive(&isodep, command, sizeof(command), rx, sizeof(rx), &rx_len),
        EmrtdErrorNone);
    TEST_EQ_HEX(rx, rx_len, "9000");
    TEST_EQ_INT(chip.wtx_answered, 3);

    emrtd_test_begin("the granted window is the multiplier the chip asked for");
    uint64_t expected = (uint64_t)ordinary * 5u;
    if(expected > EMRTD_ISODEP_FWT_MAX_FC) {
        expected = EMRTD_ISODEP_FWT_MAX_FC;
    }
    TEST_EQ_INT(chip.last_fwt, (uint32_t)expected);

    emrtd_test_begin("the command is executed once, however long the chip takes");
    TEST_EQ_INT(chip.commands, 1);
}

/* --- Recovery ------------------------------------------------------------ */

static void test_retransmission(void) {
    static const uint8_t ats[] = {0x05, 0x78, 0x80, 0x70, 0x02};
    static const uint8_t response[] = {0x90, 0x00};
    static const uint8_t command[] = {0x00, 0xB0, 0x00, 0x00, 0x20};

    Chip chip;
    EmrtdIsoDep isodep;
    uint8_t rx[64];
    size_t rx_len = 0;

    emrtd_test_begin("a lost answer is asked for again rather than ending the read");
    open_session(&chip, &isodep, ats, sizeof(ats));
    chip.response = response;
    chip.response_len = sizeof(response);
    chip.drop_next = 1;
    TEST_EQ_INT(
        emrtd_isodep_transceive(&isodep, command, sizeof(command), rx, sizeof(rx), &rx_len),
        EmrtdErrorNone);
    TEST_EQ_HEX(rx, rx_len, "9000");

    emrtd_test_begin("a chip that has really gone is reported after the attempts run out");
    open_session(&chip, &isodep, ats, sizeof(ats));
    chip.response = response;
    chip.response_len = sizeof(response);
    chip.drop_next = 99;
    const unsigned before = chip.frames;
    TEST_EQ_INT(
        emrtd_isodep_transceive(&isodep, command, sizeof(command), rx, sizeof(rx), &rx_len),
        EmrtdErrorCardLost);
    /* The first attempt and then the retries, and not one frame more. */
    TEST_EQ_INT(chip.frames - before, EMRTD_ISODEP_RETRIES + 1);

    emrtd_test_begin("deselect releases the card and closes the session");
    open_session(&chip, &isodep, ats, sizeof(ats));
    emrtd_isodep_deselect(&isodep);
    TEST_CHECK(!chip.activated);
    TEST_EQ_INT(
        emrtd_isodep_transceive(&isodep, command, sizeof(command), rx, sizeof(rx), &rx_len),
        EmrtdErrorNoCard);
}

/* --- Refusing nonsense --------------------------------------------------- */

/** A chip that answers every block with something the reader must not accept. */
static EmrtdError rude_frame(
    void* context,
    const uint8_t* tx,
    size_t tx_len,
    uint8_t* rx,
    size_t rx_cap,
    size_t* rx_len,
    uint32_t fwt_fc) {
    (void)tx_len;
    (void)fwt_fc;
    unsigned* calls = context;
    (*calls)++;

    if(rx_cap < 2) {
        return EmrtdErrorBufferTooSmall;
    }
    if(tx[0] == 0xE0) {
        rx[0] = 0x05;
        rx[1] = 0x78;
        *rx_len = 2;
        return EmrtdErrorNone;
    }
    /* An I-block that never stops chaining. */
    rx[0] = 0x12;
    *rx_len = 1;
    return EmrtdErrorNone;
}

static void test_malformed(void) {
    unsigned calls = 0;
    EmrtdIsoDep isodep;
    uint8_t rx[64];
    size_t rx_len = 0;
    const uint8_t command[] = {0x00, 0xB0, 0x00, 0x00, 0x20};

    emrtd_test_begin("an endless chain is cut rather than followed for ever");
    emrtd_isodep_init(&isodep, rude_frame, &calls);
    TEST_EQ_INT(emrtd_isodep_activate(&isodep), EmrtdErrorNone);
    TEST_EQ_INT(
        emrtd_isodep_transceive(&isodep, command, sizeof(command), rx, sizeof(rx), &rx_len),
        EmrtdErrorProtocol);
    TEST_CHECK(calls < 200);

    emrtd_test_begin("a zero length command is refused before it reaches the radio");
    const unsigned before = calls;
    TEST_EQ_INT(
        emrtd_isodep_transceive(&isodep, command, 0, rx, sizeof(rx), &rx_len),
        EmrtdErrorInvalidInput);
    TEST_EQ_INT(calls, before);
}

void test_suite_isodep(void) {
    test_ats();
    test_activation();
    test_exchange();
    test_wtx();
    test_retransmission();
    test_malformed();
}
