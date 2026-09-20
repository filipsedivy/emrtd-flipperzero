# When it does not work

## The application will not start

```
App Too Old: APP:87 < FW:88
```

The launcher is comparing the API version the package was built against with
the one the firmware provides, and refusing the mismatch. Nothing is wrong with
the application: the package was simply built for a different Flipper firmware
than the one on the device. `APP:87` is the official firmware and `APP:88` is
Unleashed or Momentum.

Take the package whose name matches your firmware from the release, or build
one:

```bash
ufbt update --index-url https://up.unleashedflip.com/directory.json   # Unleashed
ufbt update --index-url https://up.momentum-fw.dev/firmware/directory.json
ufbt update --channel=release                                          # official
ufbt launch
```

The sources are identical for all of them, and so is the mbed TLS
configuration these firmwares ship, so the reader behaves the same on each.

The mirror image, `App Too New`, means the opposite: a package built against a
newer API than the firmware. Update the firmware, or rebuild against the SDK
that matches it.

## The errors, one by one

| What the screen says | What happened | What to try |
| --- | --- | --- |
| **No document found** | Nothing answered in the field. | Lay the Flipper flat on the open data page, over the middle. If nothing answers, try the closed book with the back cover against the device: the chip is in one place or the other. Take the document out of any case and move a phone or a second card away. |
| **The document moved away** | The chip stopped answering part way through. | Hold both still until the progress bar fills. DG2 takes several seconds on its own. |
| **Radio exchange failed** | A frame did not come back. | The same as above. If it happens at the same point every time, the trace shows where. |
| **The chip broke the protocol** | The answer did not fit ISO 14443-4. | Usually a marginal field rather than a faulty chip: move the document a little and read again. If it persists, attach a trace to a report. |
| **Not an electronic passport** | The chip answered but carries no eMRTD application. | Bank cards, transport cards and access badges all answer, and none of them carry the application identifier `A0 00 00 02 47 10 01`. |
| **The chip refused the command** | A status word other than 9000 came back. | The status word is shown with the error; the table below says what it means. |
| **That file is not on this chip** | `6A82`. | Normal. Only DG1, DG2 and EF.SOD are mandatory; the rest are up to the issuing state, and EF.COM lists what is there. |
| **The chip refused access** | `6982` or `6983`. | `6982` usually means the session was lost, so read again. `6983` means the chip has blocked itself after repeated wrong keys, and only the issuer can clear that. |
| **The key does not open the chip** | The document number or the dates are not the ones this chip was issued with. | See "the key looks right and it still fails" below. |
| **No way in to this chip** | Neither driver could establish a session. | The security screen names what the chip asked for. If PACE was announced with parameters this build cannot compute, one of the next three errors says which. |
| **PACE curve out of reach** | The chip wants a curve above 256 bits. | Nothing to do on the device: `MBEDTLS_ECP_MAX_BITS` is 256 in the firmware's mbed TLS. Pin the method to BAC and see whether the document also offers it. |
| **PACE mapping not implemented** | The chip wants the integrated or the chip authentication mapping. | Only the generic mapping is implemented, which covers nearly every document in issue. Try BAC. |
| **PACE needs MODP arithmetic** | The chip runs PACE over a Diffie-Hellman group. | Impossible in an application on this firmware - see [platform.md](platform.md). Try BAC. |
| **PACE authentication failed** | The chip's token did not match the one computed here. | Nearly always the password: a wrong CAN, or a wrong MRZ value. |
| **The secure channel broke** | A response failed its checksum, or the sequence counter slipped. | The session cannot be resynchronised; read again. If it fails on the same file every time, send a trace. |
| **The file is not what it claims** | A file is not the structure the standard describes. | The raw bytes are exported anyway. Attach the `.bin` and the report. |
| **Out of this reader's scope** | Understood, but not implemented. | DG3 and DG4 are the usual cause: Extended Access Control needs a state issued terminal certificate. |
| **The SD card refused the write** | The export could not be written. | Check the card is in, unlocked and has room. The read itself still works with **Export to SD** off. |
| **Value larger than expected** | A field is bigger than the space reserved for it. | The raw file is exported in full, so nothing is lost. Worth a report with that file. |
| **The credentials are incomplete** | The credentials are not well formed. | Up to twenty characters of A-Z and 0-9 for the number, and two dates that exist. |
| **Read cancelled** | You pressed back. | - |
| **Internal error** | A library call failed in a way that should not happen. | Restart the application, and report it with the trace. |

Each of these is an `EmrtdError`, and the error screen shows a hint under the
line as well; what follows here is the background the screen has no room for.

## Status words

| SW | Meaning |
| --- | --- |
| `6282` | End of file reached before the requested length - usually harmless |
| `6300` | Authentication failed; the key is wrong |
| `6982` | Security status not satisfied; the session is gone or was never there |
| `6983` | Authentication method blocked; the chip has locked itself |
| `6A82` | File not found |
| `6A86` | Incorrect parameters P1-P2 |
| `6C xx` | Wrong length; the chip will accept `xx` bytes |
| `6D00`, `6E00` | Instruction or class not supported |

## The chip does not answer at all

1. Open the passport at the data page, lay it flat, and put the Flipper on it
   face up, over the middle of the page.
2. If nothing answers, close the book and put the back cover against the
   device. The chip is in one place or the other, and a few centimetres
   decide it.
3. Take the document out of any wallet or cover: a metal clip, a blocking
   lining or a second contactless card will all stop it.
4. Keep both still. The field is weak and the read takes seconds.

## The key looks right and it still fails

- **Check the document number character by character.** `0` and `O`, `1` and
  `I`. The number is the one in the MRZ at the bottom of the data page, not a
  number printed elsewhere on it.
- **Check the two dates.** The expiry is the document's, not a visa's, and a
  date of birth in the 1900s and one in the 2000s are different keys.
- **Try the CAN**, if the document prints one. It is a shorter string with no
  check digit, so there is less to get wrong.
- **Pin the access method** in Options. If PACE fails and BAC works, or the
  other way round, that is worth reporting.
- A document that has been refused too many times can block itself
  permanently (`6983`). If a key is not working, stop and check it rather
  than trying variations.

## The read stops part way

The stage and the file on the read screen say where it stopped, and the
result screens still show everything that was read before. A failure on DG2
specifically is usually the document moving: it is the largest file and takes
the longest.

## Sending a report

Turn on **APDU trace** in Options, reproduce the problem, and take `trace.txt` and
`report.txt` from the export directory:

```
/ext/apps_data/emrtd/<document>_<date>/trace.txt
```

**Read the trace before you send it.** A trace of a failure before
authentication contains no document data; a trace of a read that got further
is a record of a session with your own document. There is a section about
exactly what is in one in [security.md](security.md).

Useful in a report, alongside the trace:

- the issuing country and the year the document was issued - not the number;
- what the security screen said about the access method and the curve;
- the application version, which the About screen shows, and the firmware
  version the Flipper is running.

Issues go to <https://github.com/filipsedivy/emrtd-flipperzero/issues>.
Security problems go through [SECURITY.md](../SECURITY.md) instead.
