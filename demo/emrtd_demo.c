/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Filip Sedivy
 */
#include "emrtd_demo.h"

#include <furi.h>

#include "../sim/emrtd_sim.h"

#define TAG "EmrtdDemo"

struct EmrtdDemo {
    EmrtdSim* chip;
    /** The port handed upwards: the chip's, with the pacing in front of it. */
    EmrtdTransceiver transceiver;
    EmrtdDemoTraceCallback trace;
    void* trace_context;
};

/**
 * Pass one APDU to the chip, slowly.
 *
 * The delay is here rather than inside the simulated chip because the chip is
 * also driven by the host test suite, which has no furi_delay_ms() and must
 * not sleep: a suite that waited four hundred milliseconds per exchange would
 * take minutes to say what it says now in a second.
 */
static EmrtdError emrtd_demo_transceive(
    void* ctx,
    const uint8_t* tx,
    size_t tx_len,
    uint8_t* rx,
    size_t rx_cap,
    size_t* rx_len) {
    EmrtdDemo* demo = ctx;

    if(demo->trace != NULL) {
        demo->trace(demo->trace_context, true, tx, tx_len);
    }

    furi_delay_ms(EMRTD_DEMO_APDU_DELAY_MS);

    const EmrtdError error = emrtd_transceiver_exchange(
        emrtd_sim_transceiver(demo->chip), tx, tx_len, rx, rx_cap, rx_len);

    if(error == EmrtdErrorNone && demo->trace != NULL) {
        demo->trace(demo->trace_context, false, rx, *rx_len);
    }

    return error;
}

static const EmrtdTransceiverApi emrtd_demo_api = {
    .name = "simulated chip (demo build)",
    .transceive = emrtd_demo_transceive,
};

EmrtdDemo* emrtd_demo_alloc(const EmrtdCredentials* credentials) {
    furi_check(credentials);

    EmrtdDemo* demo = malloc(sizeof(EmrtdDemo));
    memset(demo, 0, sizeof(EmrtdDemo));

    EmrtdSimConfig config;
    memset(&config, 0, sizeof(config));
    /*
     * A chip that answers both protocols, so that the Access method option -
     * Auto, PACE, BAC - can be photographed on any of its three settings.
     * Auto prefers PACE, which is what the application exists for.
     */
    config.access = EmrtdSimAccessBoth;
    config.credentials = *credentials;
    /* Zero means the defaults the header documents: AES-128 over
     * brainpoolP256r1, and the Flipper's own frame size at both ends. */

    demo->chip = emrtd_sim_alloc(&config);
    if(demo->chip == NULL) {
        FURI_LOG_E(TAG, "The simulated chip could not be built");
        free(demo);
        return NULL;
    }

    const EmrtdTransceiver* const inner = emrtd_sim_transceiver(demo->chip);
    demo->transceiver.api = &emrtd_demo_api;
    demo->transceiver.ctx = demo;
    /* The frame arithmetic above has to see the chip's numbers, not ours. */
    demo->transceiver.fsc = inner->fsc;
    demo->transceiver.fsd = inner->fsd;

    return demo;
}

void emrtd_demo_free(EmrtdDemo* demo) {
    if(demo == NULL) {
        return;
    }

    emrtd_sim_free(demo->chip);
    memset(demo, 0, sizeof(EmrtdDemo));
    free(demo);
}

EmrtdTransceiver* emrtd_demo_transceiver(EmrtdDemo* demo) {
    furi_check(demo);

    return &demo->transceiver;
}

void emrtd_demo_set_trace(EmrtdDemo* demo, EmrtdDemoTraceCallback callback, void* context) {
    furi_check(demo);

    demo->trace = callback;
    demo->trace_context = context;
}
