<p align="center">
  <img src="assets/logo.svg" alt="eMRTD" width="420">
</p>

<p align="center">
  <em>The chip in a passport answers to two protocols. This one speaks both.</em>
</p>

<p align="center">
  <img src="../../actions/workflows/build.yml/badge.svg" alt="Build">
  <img src="../../actions/workflows/test.yml/badge.svg" alt="Tests">
  <img src="https://img.shields.io/badge/firmware-release%20%7C%20API%2087.1-orange" alt="Firmware release channel, API 87.1">
  <img src="https://img.shields.io/badge/license-MIT-green" alt="MIT">
</p>

---

eMRTD reads an electronic travel document - a passport or an identity card
built to **ICAO Doc 9303** - on a **Flipper Zero**, and writes every data
group it can reach to the SD card. Nothing else is needed: no computer, no
serial cable, no companion application.

## The difference

The readers that came before this one implement **BAC**, the access protocol
of 2006. BAC is being withdrawn. A document issued in the European Union
after 2017 may implement **PACE** only, and against such a chip a BAC reader
gets as far as the first command and stops.

eMRTD implements both, reads `EF.CardAccess` to find out what the chip wants,
and runs PACE first because that is what a modern document announces. That is
the whole reason this application exists.

## What it does

- **Its own ISO-DEP layer** on type A: RATS, block numbering, chaining both
  ways, waiting time extensions and retransmission. The firmware has all of
  that, and it gives a card 120 microseconds to answer whenever the ATS carries
  no TB1 - which no passport can meet, and which an application cannot change.
  See [docs/platform.md](docs/platform.md), items 10 to 14.
- **Access control**: PACE with the generic mapping over ECDH, and BAC over
  3DES. The driver is chosen from what the chip announces, with a fall back to
  the other if the first is refused.
- **Secure Messaging** for both cipher families, 3DES with a Retail MAC and
  AES with CMAC, checked byte for byte against the ICAO test vectors.
- **Reads** EF.COM, EF.SOD, DG1, DG2, DG11, DG12, DG14, DG15 and the rest of
  the non-EAC groups, and decodes the ones a reader can do something with.
- **Checks the data group hashes** against the list in EF.SOD, so that a group
  that does not match the security object is reported as such.
- **Exports** the raw files, the decoded MRZ, the facial image and a report to
  a directory per document.

DG2 is tens of kilobytes against a heap of about a hundred, so it is never
held in memory: it goes to the SD card as it arrives, hashed on the way past.

## Supported and not supported

| | |
| --- | --- |
| PACE-ECDH-GM, AES-128, AES-192, AES-256 | **yes** |
| PACE curves: brainpoolP192r1..P256r1, NIST P-192..P-256 (parameter ids 8-13) | **yes** |
| A frame waiting time the chip can actually meet | **yes** - the reader runs ISO-DEP itself on type A rather than using the firmware's poller |
| PACE curves above 256 bits (ids 14-18) | **no** - `MBEDTLS_ECP_MAX_BITS` is 256 in the firmware's mbed TLS, and the curve is refused by name |
| PACE over MODP/DH groups (ids 0-2) | **no** - `mbedtls_mpi_exp_mod` cannot be linked into an application, see [docs/platform.md](docs/platform.md) |
| PACE with the integrated or chip authentication mapping | **no** - detected and refused by name |
| BAC with 3DES | **yes** |
| Password from the MRZ, or from the CAN | **yes** |
| Data group hashes against EF.SOD | **yes** |
| EF.SOD signature, document signer and country signing certificates | **no** - this needs RSA and a certificate store; neither is on the device |
| Active Authentication, Chip Authentication, Terminal Authentication | **no** - DG14 and DG15 are read and reported, the protocols are not run |
| DG3 and DG4, the fingerprints and the iris | **no** - protected by EAC, which needs a state issued terminal certificate |
| Writing to a chip | **no**, and there is nothing in here that could |

The entries marked no are platform limits rather than missing work, and each
one is measured and written down in [docs/platform.md](docs/platform.md).

**What "verified" means here.** The data group hashes are checked against
EF.SOD, which proves the files belong together and have not been altered
since the chip was issued. The signature over EF.SOD is **not** checked, so
this reader cannot tell a genuine document from a well made copy of one. A
read that shows every hash matching says the chip is internally consistent,
nothing more.

## Install

### Which firmware

A package carries the API version it was built against, and the launcher
refuses one that does not match. That is what

```
App Too Old: APP:87 < FW:88
```

means: the package was built for the official firmware and put on Unleashed.
It is not a version of the application that is too old, only a package built
for the wrong Flipper.

The sources are the same for all of them - the application needs no change,
and the mbed TLS configuration these firmwares ship is byte for byte identical,
so PACE behaves the same on each. Only the SDK the package is built against
differs. Every release carries one file per firmware; take the one whose name
matches yours.

### With ufbt

```bash
pip install --upgrade ufbt
git clone https://github.com/filipsedivy/emrtd-flipperzero.git
cd emrtd-flipperzero
```

Then deploy the SDK for the firmware on your device:

```bash
# Official firmware
ufbt update --channel=release

# Unleashed
ufbt update --index-url https://up.unleashedflip.com/directory.json

# Momentum
ufbt update --index-url https://up.momentum-fw.dev/firmware/directory.json
```

and build:

```bash
ufbt launch                        # build, upload and start on a connected Flipper
```

`ufbt` alone builds `dist/emrtd.fap`, which can be copied to `/ext/apps/NFC/`
on the SD card instead.

`ufbt update` replaces whichever SDK was deployed before. To keep more than
one, point `UFBT_HOME` somewhere else for each:

```bash
UFBT_HOME=~/.ufbt-unleashed ufbt update --index-url https://up.unleashedflip.com/directory.json
UFBT_HOME=~/.ufbt-unleashed ufbt
```

### From the app catalogue

The catalogue entry - the description, the changelog and the screenshots a
submission needs - lives in [.catalog/](.catalog). Once the application is
accepted into the NFC category it installs from the Flipper mobile
application or from [lab.flipper.net](https://lab.flipper.net/apps), and the
catalogue builds it for every firmware channel itself.

## Reading a document

```
Start -> Document (number, date of birth, date of expiry, or a CAN)
      -> Read     select application -> EF.CardAccess -> PACE or BAC
                  -> Secure Messaging -> EF.COM -> EF.SOD -> the data groups
                  -> hashes against EF.SOD -> export
      -> Result   holder, document, security, files, photo
```

The document number and the two dates are printed inside the document, which
is why reading only works with it in hand: those three values **are** the key
to the chip. A card access number, when the document carries one, replaces
them for PACE. Dates are entered as day, month and year, and are stored the
way the MRZ carries them.

The antenna is in the **back** of the Flipper, so that is the side the
document goes against: open the passport at the data page, lay it flat, and
put the Flipper face up over the middle of the page. If nothing answers,
close the book and try the back cover instead - the chip is in one place or
the other, and a few centimetres decide it.

Every screen is described in [docs/usage.md](docs/usage.md).

## What lands on the SD card

```
/ext/apps_data/emrtd/L898902C_20260920_2114/
    report.txt      what was read, how it was opened, what verified
    trace.txt       the APDU log, when it was asked for
    EF_COM.bin      the raw files, exactly as the chip returned them
    EF_SOD.bin
    EF_DG1.bin
    ...
    mrz.txt         the decoded machine readable zone
    face.jpg        the facial image, lifted out of DG2
```

That directory is a complete identity, and the credentials that opened the
chip are stored next to it if you asked the application to remember them.
[docs/security.md](docs/security.md) says what that means and what to do
about it.

## Tests

The protocol and cryptographic layers have no Flipper dependencies, so they
compile and run natively - against the same mbed TLS release the firmware
ships, configured with the firmware's own header:

```bash
make -C tests/host test
make -C tests/host gcc-check   # GCC-only warnings, which clang does not raise
```

The first run clones mbed TLS 3.6.2 into `tests/host/.deps`. The suite builds
with the address and undefined behaviour sanitizers and pins the
implementation to the published vectors: ICAO Doc 9303 part 11 appendix D for
BAC and Secure Messaging over 3DES, appendix G for PACE over brainpoolP256r1,
RFC 4493 and NIST SP 800-38B for CMAC, and a simulated chip for a whole read
end to end. There is also `make -C tests/host config-drift`, which fails if
the vendored copy of the firmware's mbed TLS configuration has drifted from
the SDK that `ufbt` deployed.

## Documentation

| | |
| --- | --- |
| [usage.md](docs/usage.md) | Every screen, what to type, where the export lands |
| [architecture.md](docs/architecture.md) | The layers, and why the access drivers are a registry |
| [protocol.md](docs/protocol.md) | The APDU sequence, and the frame size that shapes it |
| [cryptography.md](docs/cryptography.md) | BAC, PACE, Secure Messaging, and the vector that pins each step |
| [platform.md](docs/platform.md) | What the firmware's mbed TLS can and cannot do, measured |
| [security.md](docs/security.md) | What is sensitive, and what the repository does about it |
| [troubleshooting.md](docs/troubleshooting.md) | What each error means and what to try |

## Art

The identity is one mark - the contact plate of the chip module, divided into
six pads - drawn wherever it is needed by
[assets/make_logo.py](assets/make_logo.py). It is designed at 10x10, because
that is where it has to survive: one bit per pixel, no antialiasing, in the
launcher. Nothing in it is thinner than two pixels, so it never degrades into
a dither pattern, and every larger form is the same grid scaled by a whole
number.

The banner sets the wordmark in type, so the frame around it is measured from
the font's own advance widths rather than guessed, and each line carries a
`textLength`: a reader whose machine has none of the fonts in the stack gets
the wordmark fitted to the frame instead of cropped at its edge. The ink
follows the reader's colour scheme, because GitHub renders an SVG in a README
as an image, and `currentColor` there is black.

```bash
uv run --with pillow python assets/make_logo.py
```

| File | Purpose |
| --- | --- |
| `images/emrtd_10px.png` | The `fap_icon`; 10x10 and one bit, as the manifest requires |
| `images/EmrtdChip_24x24.png` | The same mark at two pixels per cell, which the read view draws as `I_EmrtdChip_24x24` |
| `assets/logo.svg` | The mark as one even-odd path beside the wordmark, not a traced bitmap; 698x240, measured to fit |
| `assets/logo.png`, `assets/logo.txt` | The raster and text forms of the same mark |

`fbt` compiles every image in `images/` into `emrtd_icons.h` and does not
strip what goes unused, and a `.fap` is loaded into RAM, so an icon nothing
draws costs the read the memory it occupies.

## Requirements

A Flipper Zero on official firmware 1.4.3 (API 87.1, the release channel) and
a travel document of your own. Nothing else: the read, the cryptography and
the export all happen on the device.

## Legal note

Use this on your own document, or with the explicit and informed consent of
the person whose document it is. Access requires physical possession, because
the key is derived from what is printed on the data page - but consent is not
implied by possession, and a facial image is biometric data. Whatever you
export is subject to the rules that apply where you are.

## License

MIT - see [LICENSE](LICENSE).
