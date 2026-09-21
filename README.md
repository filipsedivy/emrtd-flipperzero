<p align="center">
  <img src="assets/logo.svg" alt="eMRTD" width="420">
</p>

<p align="center">
  <em>Passport, identity card, residence permit - one chip, two protocols.
  This one speaks both.</em>
</p>

<p align="center">
  <img src="../../actions/workflows/build.yml/badge.svg" alt="Build">
  <img src="../../actions/workflows/test.yml/badge.svg" alt="Tests">
  <img src="https://img.shields.io/badge/firmware-release%20%7C%20API%2087.1-orange" alt="Firmware release channel, API 87.1">
  <img src="https://img.shields.io/badge/license-MIT-green" alt="MIT">
</p>

---

eMRTD reads the contactless chip in an electronic identity document - a
passport, a national identity card or a residence permit, all built to **ICAO
Doc 9303** - on a **Flipper Zero**, and writes every data group it can reach to
the SD card. No computer, no serial cable, no companion application.

## Which documents

An eMRTD is not a kind of booklet, it is a data structure: any contactless chip
that carries the ICAO LDS1 application, `A0 00 00 02 47 10 01`, is one. That
covers

- **passports**, whose machine readable zone is TD3, two lines of 44;
- **national identity cards**, which in the European Union have carried the
  same biometric chip since 2021 because Regulation (EU) 2019/1157 requires it,
  with a TD1 zone of three lines of 30;
- **residence permits** and the other ID-1 and ID-2 documents built to the same
  part of the standard.

The reader is not told which one it is facing. It selects the application,
reads what `EF.CardAccess` announces and opens whatever answers, so the
difference between a passport and an identity card comes down to two things:
the key you type, and the layout of the zone that is decoded afterwards.

An identity card may carry a second, **contact** chip as well - in the Czech
card that is where the eIDAS certificates and the signature keys live. It
speaks over the gold pads, and a Flipper is 13.56 MHz only, so nothing here can
reach it.

## Why it exists

The readers that came before this one implement **BAC**, the access protocol of
2006. BAC is being withdrawn, and a document issued in the European Union after
2017 may implement **PACE** only - against such a chip a BAC reader gets as far
as the first command and stops.

eMRTD implements both, reads `EF.CardAccess` to find out what the chip wants,
and runs PACE first because that is what a modern document announces. The
identity card in the table below is that case in the flesh: its
`EF.CardAccess` announces one protocol, PACE over NIST P-256, and the European
specification behind the card does not provide for BAC at all.

## What it does

- **Access control**: PACE with the generic mapping over ECDH, and BAC over
  3DES, chosen from what the chip announces.
- **Secure Messaging** for both cipher families, checked byte for byte against
  the ICAO test vectors.
- **Its own ISO-DEP layer** on type A, because the firmware's poller gives a
  card a waiting time no eMRTD chip can meet.
- **Reads** EF.COM, EF.SOD and the non-EAC data groups, and checks every group
  against the hash EF.SOD lists for it.
- **Exports** the raw files, the decoded MRZ, the facial image and a report to
  a directory per document.

The signature on EF.SOD, the EAC-protected groups DG3 and DG4, and writing to
a chip are all out of reach, for reasons that are measured rather than guessed.
[docs/capabilities.md](docs/capabilities.md) has the full matrix.

**A read that shows every hash matching says the chip is internally consistent,
nothing more.** The signature over EF.SOD is not checked, so this reader cannot
tell a genuine document from a well made copy of one.

## Tested documents

| Document | Key you type | Access | Data groups | Hashes |
| --- | --- | --- | --- | --- |
| Czech passport | the three MRZ values | BAC, 3DES - `EF.CardAccess` could not be read | DG1, DG2, DG14, DG15; DG3 announced and skipped | all four match |
| Czech identity card, 2021 series | **the CAN** | PACE-ECDH-GM, AES-128, NIST P-256 - `EF.CardAccess` read | DG1 as TD1, DG2, DG14, DG15; DG3 announced and skipped | all four match |

**The identity card is opened with its card access number.** The CAN is the six
digit figure printed on the card; it goes in under Document -> CAN, and with it
stored the reader uses it in place of the machine readable zone. A passport
prints no CAN, so there the three MRZ values - document number, date of birth,
date of expiry - are the only way in. The card's own TD1 zone was not tried as a
key, so nothing here says whether that would work too.

The passport row is a BAC read. That document's DG14 declares PACE-ECDH-GM with
AES-128 over NIST P-256, but DG14 is read only once a session is open, so the
declaration arrives long after the moment it would have been useful. **PACE has
now run against a real chip** - the identity card, generic mapping over NIST
P-256 with AES-128 - rather than only against the ICAO test vectors and the host
simulator.

Both rows were read on the device. For the identity card the detail was
cross-checked against a host reference implementation driven over the same
Flipper as its radio: the protocols the chip announces, the four hashes, the
`6982` that DG3 answers a reader without a terminal certificate, and the EF.SOD
signature that this reader cannot check and that one could.

Rows come from pull requests - [CONTRIBUTING.md](CONTRIBUTING.md) says what may
be written down about a document, and the pull request template already asks
for it.

## Install

```bash
pip install --upgrade ufbt
ufbt update --channel=release      # official firmware; see docs/install.md for others
ufbt launch                        # build, upload and start
```

Unleashed and Momentum need their own SDK, and a package built for one firmware
will not start on another. [docs/install.md](docs/install.md) has the channels,
the prebuilt packages and the app catalogue route.

## Reading a document

```
Start -> Document (number, date of birth, date of expiry, or a CAN)
      -> Read     select application -> EF.CardAccess -> PACE or BAC
                  -> Secure Messaging -> EF.COM -> EF.SOD -> the data groups
                  -> hashes against EF.SOD -> export
      -> Result   holder, document, security, files, photo
```

The key is whatever the document prints on itself, which is why reading only
works with it in hand: either the three values of the machine readable zone, or
the six digit card access number where the document carries one. A passport has
only the first; an identity card usually prints both, and the CAN is the
shorter thing to type. Each read lands in its own directory under
`/ext/apps_data/emrtd/`, and that directory is a complete identity -
[docs/security.md](docs/security.md) says what it holds and what to do about
it.

[docs/usage.md](docs/usage.md) describes every screen, where to hold the
document and what the export contains.

## Documentation

| | |
| --- | --- |
| [install.md](docs/install.md) | Firmware channels, ufbt, the app catalogue |
| [usage.md](docs/usage.md) | Every screen, what to type, where the export lands |
| [capabilities.md](docs/capabilities.md) | What it reads, what it refuses, and why |
| [troubleshooting.md](docs/troubleshooting.md) | What each error means and what to try |
| [security.md](docs/security.md) | What is sensitive, and what the repository does about it |
| [architecture.md](docs/architecture.md) | The layers, and why the access drivers are a registry |
| [protocol.md](docs/protocol.md) | The APDU sequence, and the frame size that shapes it |
| [cryptography.md](docs/cryptography.md) | BAC, PACE, Secure Messaging, and the vector that pins each step |
| [platform.md](docs/platform.md) | What the firmware's mbed TLS can and cannot do, measured |
| [branding.md](docs/branding.md) | Why the mark is six pads on a ten by ten grid |

Building, testing and the house rules: [CONTRIBUTING.md](CONTRIBUTING.md).

## Legal note

Use this on your own document, or with the explicit and informed consent of the
person whose document it is. Access requires physical possession, because the
key is derived from what is printed on the document itself - but consent is not
implied by possession, and a facial image is biometric data. Whatever you
export is subject to the rules that apply where you are.

## Support

The work behind this is a chip, a specification and a lot of measuring. If it
saved you some of that, you can buy me a coffee:
[buymeacoffee.com/filipsedivy](https://buymeacoffee.com/filipsedivy).

## License

MIT - see [LICENSE](LICENSE).
