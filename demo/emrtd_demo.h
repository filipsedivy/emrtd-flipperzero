/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Filip Sedivy
 *
 * The demo build's chip.
 *
 * This file is compiled only when EMRTD_DEMO is defined, which happens for
 * one purpose: photographing the screens for the application catalogue
 * without holding a document up to the antenna. The released package contains
 * none of it - see application.fam - and there is no switch anywhere in the
 * user interface that reaches it. A simulation is a way of taking a picture
 * of the application, never a feature of it.
 *
 * What it is: sim/emrtd_sim.h behind the same EmrtdTransceiver port the radio
 * sits behind, with two things added that the simulated chip must not learn.
 * It is told which credentials to build its document from, and every exchange
 * is slowed to something a person can watch, because a chip that answers in
 * microseconds would take the whole read - and every screen of it - past
 * before the display had drawn once.
 *
 * Everything above the port is the real thing: PACE, Secure Messaging, the
 * parsers, the hash comparison against EF.SOD, the export and every scene. A
 * picture taken from this build is a true picture of what the application
 * does; only the radio is missing.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../access/emrtd_access.h"
#include "../transport/emrtd_transceiver.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * How long each exchange is held back, in milliseconds.
 *
 * A read is roughly twenty exchanges, so this sets the pace of the whole run;
 * EF.SOD alone takes several, which is what keeps "Reading" on the screen long
 * enough to be caught. Raise it if a stage still goes by too fast to
 * photograph - it is the only knob that needs tuning.
 */
#define EMRTD_DEMO_APDU_DELAY_MS 400u

/** How long "Waiting for document" is held before the read starts. */
#define EMRTD_DEMO_WAITING_MS 3000u

typedef struct EmrtdDemo EmrtdDemo;

/** Same shape as the transport's, so the worker's tracer can be handed over. */
typedef void (
    *EmrtdDemoTraceCallback)(void* context, bool outgoing, const uint8_t* data, size_t len);

/**
 * A chip holding the ICAO specimen document, opened by @p credentials.
 *
 * The machine readable zone is built from whatever is passed, so the document
 * number and the dates on the screen are the ones the demo build was told to
 * use; the name is always Anna Maria Eriksson of Utopia, who is not a person.
 *
 * @return NULL if the chip could not be built, which the caller must report
 *         as an ordinary read failure rather than ignore.
 */
EmrtdDemo* emrtd_demo_alloc(const EmrtdCredentials* credentials);

void emrtd_demo_free(EmrtdDemo* demo);

/** The port the reader talks through. Owned by @p demo. */
EmrtdTransceiver* emrtd_demo_transceiver(EmrtdDemo* demo);

/**
 * Report every APDU, so that the trace option writes a real log here too.
 *
 * On the radio path the tracer hooks into the ISO 14443-4 layer, which this
 * build never binds; without this the demo's trace.txt would carry its header
 * and not one exchange, and the option would be lying.
 */
void emrtd_demo_set_trace(EmrtdDemo* demo, EmrtdDemoTraceCallback callback, void* context);

#ifdef __cplusplus
}
#endif
