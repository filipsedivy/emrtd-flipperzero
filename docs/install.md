# Installing

## What you need

A Flipper Zero, and a travel document of your own. Nothing else: the read, the
cryptography and the export all happen on the device.

The application runs on the official firmware, on Unleashed and on Momentum.
The sources are the same for all three - the application needs no change, and
the mbed TLS configuration these firmwares ship is byte for byte identical, so
PACE behaves the same on each. Development and the release channel build track
official firmware 1.4.3, API 87.1.

## Which package

Only the SDK a package was built against differs between the firmwares, and a
package carries the API version with it. The launcher refuses a mismatch, which
is what `App Too Old` on the Flipper's screen means - not a stale application,
just a package built for a different firmware.
[troubleshooting.md](troubleshooting.md) has that message and its mirror image
in full.

Every release carries one file per firmware. Take the one whose name matches
yours.

## From source, with ufbt

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

### The demo build

```bash
EMRTD_DEMO=1 ufbt launch
```

builds `dist/emrtd_demo.fap`, a separate package whose reads run against a
simulated chip instead of the radio. It exists for one job: photographing the
screens for the catalogue entry without holding a document up to the antenna,
and without a real document number ending up in a public listing. The document
it reads is the ICAO specimen - Anna Maria Eriksson of Utopia, who is not a
person - and the About screen says `DEMO BUILD`, which is the only screen that
differs from the released package.

Everything else is the real application: PACE, Secure Messaging, the parsers,
the hash check against EF.SOD, the export. It keeps its own settings file and
its own application identifier, so it cannot overwrite the released package or
read the credentials that one remembers. `ufbt` without the variable never
builds any of it - the simulator and the demo code are excluded from the
released package's sources - which is why the two can live in one tree.

The pace of the read is set by `EMRTD_DEMO_APDU_DELAY_MS` in
[demo/emrtd_demo.h](../demo/emrtd_demo.h); raise it if a stage goes by too
fast to catch. Screenshots for the catalogue have to come from qFlipper's own
screenshot feature, at its resolution and in its format.

### Keeping more than one SDK

`ufbt update` replaces whichever SDK was deployed before. To keep several, point
`UFBT_HOME` somewhere else for each:

```bash
UFBT_HOME=~/.ufbt-unleashed ufbt update --index-url https://up.unleashedflip.com/directory.json
UFBT_HOME=~/.ufbt-unleashed ufbt
```

## From the app catalogue

The catalogue entry - the description and the changelog a submission needs -
lives in [.catalog/](../.catalog). Once the application is accepted into the NFC
category it installs from the Flipper mobile application or from
[lab.flipper.net](https://lab.flipper.net/apps), and the catalogue builds it for
every firmware channel itself.

## Next

[usage.md](usage.md) walks through every screen. If the application will not
start or the chip will not answer, [troubleshooting.md](troubleshooting.md) is
organised by what the screen says.
