/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Filip Sedivy
 *
 * The text behind every error code.
 *
 * The person holding the passport sees one line and one paragraph, and they
 * are the only explanation they get: there is no log to consult and no second
 * screen. So the hint always names the next thing to try, and where the
 * failure is a property of this hardware rather than of the document it says
 * so, because otherwise the user keeps retrying something that cannot work.
 */

#include "emrtd_error.h"

typedef struct {
    const char* text;
    const char* hint;
} EmrtdErrorStrings;

/*
 * Deliberately left unsized: the static assertion below then fails if an
 * enumerator is added without a line of text, instead of silently yielding a
 * NULL entry. The designated indices keep the table in step with the enum
 * even when the order changes.
 */
static const EmrtdErrorStrings emrtd_error_strings[] = {
    [EmrtdErrorNone] = {"No error", "The read finished without a problem."},

    [EmrtdErrorNoCard] =
        {"No document found",
         "Lay the Flipper flat on the data page of the passport, over the middle of the "
         "page, and hold it still. The chip sits in the cover or in the data page itself, "
         "so a few centimetres either way can be the difference."},

    [EmrtdErrorCardLost] =
        {"The document moved away",
         "The chip lost the field before the read finished. Nothing was damaged: put the "
         "document back and start again, keeping both still until the progress bar fills."},

    [EmrtdErrorActivation] =
        {"The chip will not open a session",
         "The document answered when the reader looked for it, and then would not start "
         "an ISO 14443-4 session. Lift the Flipper away, lay it back on the data page and "
         "read again. If it fails every time, send a trace: the card's own timing "
         "parameters head the file and they say what the chip asked for."},

    [EmrtdErrorTransport] =
        {"Radio exchange failed",
         "A frame did not come back. Metal in a wallet, a phone underneath, or a second "
         "card in the field will all do this. Remove everything else and try again."},

    [EmrtdErrorProtocol] =
        {"The chip broke the protocol",
         "The answer did not fit ISO 14443-4. This is usually a marginal field rather than "
         "a faulty chip, so move the document slightly and read again."},

    [EmrtdErrorNotEmrtd] =
        {"Not an electronic passport",
         "The chip answered but carries no eMRTD application. Bank cards, transit cards "
         "and most identity cards without the passport symbol are not readable here."},

    [EmrtdErrorApdu] =
        {"The chip refused the command",
         "The document answered with an error status instead of data. The status word is "
         "shown above; it is the chip's own wording for what it disliked."},

    [EmrtdErrorFileNotFound] =
        {"That file is not on this chip",
         "The document does not carry this data group. That is normal - only DG1, DG2 and "
         "EF.SOD are mandatory, and the rest are up to the issuing state."},

    [EmrtdErrorAccessDenied] =
        {"The chip refused access",
         "The document will not serve this file without more rights than a reader can "
         "have. Fingerprints and iris images (DG3, DG4) need a state issued certificate "
         "and are out of reach for everyone else."},

    [EmrtdErrorWrongKey] =
        {"The key does not open the chip",
         "Check the document number, the date of birth and the date of expiry against the "
         "data page. The check digit is computed for you, so type the number exactly as "
         "printed, letters included, and use the dates from the machine readable zone at "
         "the bottom of the page rather than the printed ones."},

    [EmrtdErrorNoAccessMethod] =
        {"No way in to this chip",
         "Neither PACE nor BAC could be established. If the chip announced PACE with "
         "parameters this build cannot compute, the detail above names them; otherwise the "
         "credentials are the first thing to check."},

    [EmrtdErrorPaceUnsupportedCurve] =
        {"PACE curve out of reach",
         "The chip asks for an elliptic curve wider than 256 bits. The mbed TLS that ships "
         "with the Flipper firmware is built with a 256 bit limit, so this curve cannot be "
         "computed on the device. If the document also offers BAC, choose it in the menu."},

    [EmrtdErrorPaceUnsupportedMapping] =
        {"PACE mapping not implemented",
         "This chip wants the integrated or the chip authentication mapping. Only the "
         "generic mapping is implemented, which covers nearly every document in issue. If "
         "the document also offers BAC, choose it in the menu."},

    [EmrtdErrorPaceUnsupportedDh] =
        {"PACE needs MODP arithmetic",
         "This chip runs PACE over MODP (Diffie-Hellman) groups. The firmware's mbed TLS "
         "cannot perform modular exponentiation - see docs/platform.md - so only the "
         "elliptic curve variants work here. If the document also offers BAC, choose it."},

    [EmrtdErrorPaceFailed] =
        {"PACE authentication failed",
         "The chip's token did not match the one computed here, which nearly always means "
         "the MRZ input or the CAN is wrong. The CAN is the six digit number printed on "
         "the data page, separate from the document number."},

    [EmrtdErrorSecureMessaging] =
        {"The secure channel broke",
         "A response failed its checksum, so it was discarded rather than trusted. An "
         "unstable field corrupts the encrypted stream: move the document a little and "
         "read again."},

    [EmrtdErrorParse] =
        {"The file is not what it claims",
         "The chip returned something that does not match the structure ICAO Doc 9303 "
         "describes. The raw bytes are still exported, so the file can be examined on a "
         "computer."},

    [EmrtdErrorUnsupported] =
        {"Out of this reader's scope",
         "The document uses a feature this reader understands but does not implement. The "
         "raw file is exported unchanged."},

    [EmrtdErrorOutOfMemory] =
        {"Not enough memory",
         "A read needs about 28 kB free, and one unbroken piece of 8 kB for the radio "
         "thread. The Flipper has neither right now.\n\nA computer talking to the device "
         "costs about 20 kB of that: close lab.flipper.net or qFlipper, unplug the cable, "
         "and restart the Flipper. Restarting is what gives the memory back, because this "
         "application is loaded into it."},

    [EmrtdErrorStorage] =
        {"The SD card refused the write",
         "Check that a card is inserted, is not write protected and has room left. The "
         "read itself succeeded; only the export failed."},

    [EmrtdErrorBufferTooSmall] =
        {"Value larger than expected",
         "A field on the chip is bigger than the space this reader reserves for it. The "
         "raw file is exported in full, so nothing is lost."},

    [EmrtdErrorInvalidInput] =
        {"The credentials are incomplete",
         "The document number may be up to twenty characters of A-Z and 0-9. Both dates "
         "are six digits in YYMMDD order, exactly as they appear in the machine readable "
         "zone."},

    [EmrtdErrorCancelled] =
        {"Read cancelled",
         "You left the read screen before it finished. Nothing was written to the card and "
         "nothing was written to the SD card."},

    [EmrtdErrorInternal] =
        {"Internal error",
         "A library call failed in a way that should not happen. Restarting the "
         "application clears any state that may have caused it."},
};

_Static_assert(
    sizeof(emrtd_error_strings) / sizeof(emrtd_error_strings[0]) == EmrtdErrorCount,
    "every EmrtdError needs a line of text and a hint");

static const EmrtdErrorStrings* emrtd_error_lookup(EmrtdError error) {
    if((size_t)error >= (size_t)EmrtdErrorCount) {
        return NULL;
    }
    const EmrtdErrorStrings* entry = &emrtd_error_strings[error];
    /* A hole left by a future enumerator reads as NULL rather than crashing. */
    return entry->text != NULL ? entry : NULL;
}

const char* emrtd_error_text(EmrtdError error) {
    const EmrtdErrorStrings* entry = emrtd_error_lookup(error);
    return entry != NULL ? entry->text : "Unknown error";
}

const char* emrtd_error_hint(EmrtdError error) {
    const EmrtdErrorStrings* entry = emrtd_error_lookup(error);
    return entry != NULL ? entry->hint :
                           "This error has no description, which is itself a bug. Please "
                           "report what you were reading when it appeared.";
}

EmrtdError emrtd_error_from_sw(uint16_t sw) {
    switch(sw) {
    case 0x9000:
        return EmrtdErrorNone;

    /* ISO 7816-4 table 6: the file or the application is simply not there. */
    case 0x6A82:
    case 0x6A83:
        return EmrtdErrorFileNotFound;

    /*
     * 6982 is the ordinary "you have not authenticated" and 6983 is a chip
     * that has locked itself after repeated failures; both are refusals of
     * the request rather than of the key.
     */
    case 0x6982:
    case 0x6983:
        return EmrtdErrorAccessDenied;

    /*
     * 6300 is what a chip returns from EXTERNAL AUTHENTICATE when the BAC
     * response did not verify, which means the MRZ input was wrong.
     */
    case 0x6300:
        return EmrtdErrorWrongKey;

    /* 6987 and 6988 are about the Secure Messaging data objects themselves. */
    case 0x6987:
    case 0x6988:
        return EmrtdErrorSecureMessaging;

    default:
        break;
    }

    /* 63Cx counts down the remaining attempts; the key was still wrong. */
    if((sw & 0xFFF0) == 0x63C0) {
        return EmrtdErrorWrongKey;
    }

    return EmrtdErrorApdu;
}

const char* emrtd_sw_text(uint16_t sw) {
    switch(sw) {
    case 0x9000:
        return "OK";
    case 0x6200:
        return "warning, no information";
    case 0x6281:
        return "part of the data may be corrupted";
    case 0x6282:
        return "end of file reached before Le bytes";
    case 0x6300:
        return "verification failed";
    case 0x6400:
        return "execution error, state unchanged";
    case 0x6700:
        return "wrong length";
    case 0x6800:
        return "class byte not supported";
    case 0x6882:
        return "secure messaging not supported";
    case 0x6883:
        return "the last command of the chain is missing";
    case 0x6900:
        return "command not allowed";
    case 0x6982:
        return "security status not satisfied";
    case 0x6983:
        return "authentication method blocked";
    case 0x6984:
        return "reference data unusable";
    case 0x6985:
        return "conditions of use not satisfied";
    case 0x6986:
        return "no current elementary file";
    case 0x6987:
        return "expected secure messaging objects missing";
    case 0x6988:
        return "secure messaging objects incorrect";
    case 0x6A80:
        return "incorrect parameters in the data field";
    case 0x6A81:
        return "function not supported";
    case 0x6A82:
        return "file or application not found";
    case 0x6A83:
        return "record not found";
    case 0x6A86:
        return "incorrect parameters P1-P2";
    case 0x6A88:
        return "referenced data not found";
    case 0x6B00:
        return "wrong parameters, offset outside the file";
    case 0x6D00:
        return "instruction not supported";
    case 0x6E00:
        return "class not supported";
    case 0x6F00:
        return "no precise diagnosis";
    default:
        break;
    }

    /*
     * The three status word families. They carry a count in the low byte that
     * the caller already has, so the text stays a constant and this function
     * keeps no state of its own - it is called from the NFC thread.
     */
    if((sw & 0xFF00) == 0x6100) {
        return "more data available, issue GET RESPONSE";
    }
    if((sw & 0xFF00) == 0x6C00) {
        return "wrong Le, the low byte is the correct length";
    }
    if((sw & 0xFFF0) == 0x63C0) {
        return "verification failed, the low nibble counts the attempts left";
    }

    return "unknown status word";
}
