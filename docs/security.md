# Handling the data

This application reads real travel documents. What needs protecting is not a
token that can be rotated - it is an identity, and the key to it is printed on
a page that cannot be reissued cheaply. This page says what is sensitive, what
the repository does about it, and what is left to you.

## What counts as sensitive

| Material | Where it is | Why |
| --- | --- | --- |
| Document number, date of birth, date of expiry | typed on the device, kept in `emrtd.settings` if you ask | These three **are** the key. `Kseed = SHA-1(MRZ information)`, and PACE derives its password from the same string: anyone holding them can open that chip. |
| Card access number | the same | The PACE password in its own right. |
| `EF_DG*.bin`, `EF_SOD.bin` | the export directory | The exact bytes of the chip, the facial image among them. |
| `face.jpg` / `face.jp2` | the export directory | Biometric data in the ordinary legal sense of the term. |
| `mrz.txt`, `report.txt` | the export directory | The same identity in a form anything can read. |
| `trace.txt` | the export directory | The exchange itself. See below. |

## Where it lives on the device

Everything is under `/ext/apps_data/emrtd/` on the SD card - the exports in a
directory per read, and the credentials in `emrtd.settings` when **remember
credentials** is on.

That settings file is plain. It is not encrypted, and it cannot usefully be:
the device has no secure element, no user secret to derive a key from, and
anything the application could unlock unattended, a reader of the card can
unlock too. A Flipper is a small object that gets left on desks and lost in
bags, and the SD card can be taken out and read on any computer.

So the choice is deliberate and it is yours:

- leave **Remember on SD** off and type the values for each read, or
- turn it on for convenience, and use **Document -> Forget stored data** when
  you are finished. That deletes the settings file.

An export is deleted with the Flipper's own file manager, or from a computer
with the SD card in it; the reader browses its exports but does not erase
them. A read with **Export to SD** off never writes one in the first place.

## The trace

`trace.txt` is written only when you ask for it, and it is the right thing to
send with a bug report - but read it first. What is in it depends on how far
the read got:

- everything before a session exists is in the clear: the SELECT commands,
  `EF.CardAccess`, and the BAC or PACE exchange itself;
- that exchange does not contain the credentials, but it is derived from
  them, and the encrypted nonce and the authentication tokens are material an
  attacker with the document in hand could test a guess against;
- once Secure Messaging is running the payloads are ciphertext, and the data
  groups do not appear in the clear.

A trace of a failure before authentication is nearly always safe to share. A
trace of a successful read is a record of a session with your own document;
treat it as you would the export.

## What the repository does

`.gitignore` keeps the obvious things out of git: the build output, the
`ufbt` state, `/dumps/`, and `*.bin` anywhere in the tree - which covers every
raw file an export produces.

It does **not** cover the decoded output. `report.txt`, `mrz.txt`,
`trace.txt` and `face.jpg` are ordinary names that a commit would carry
without complaint. The repository cannot guess where you might copy an export
to, so the rule is simpler than a pattern: **do not put an export inside a
clone of this repository.** Keep it somewhere else, and if you must attach
something to an issue, attach the one file you mean to send.

The test material in this repository is the specimen identity ICAO publishes -
ANNA MARIA ERIKSSON of Utopia, document `L898902C` - which is exactly why it
can be committed.

## In memory

Session keys live in an `EmrtdSm` for the length of a session and are wiped by
`emrtd_sm_clear()` when it ends. The credentials are wiped the same way when a
read finishes, unless they are being remembered on purpose. Neither the keys
nor the password ever reach the export or the trace.

## What a read does and does not prove

The data group hashes are checked against EF.SOD, so a read can tell you that
the files on the chip belong together and have not been altered since it was
issued. The signature over EF.SOD is not verified here - that needs RSA and a
list of trusted country signing certificates, and neither is available on the
device. **A read cannot tell a genuine document from a competent copy of
one.** If that distinction matters for what you are doing, do the passive
authentication off the device, from the exported `EF_SOD.bin` and the raw
groups next to it.

## Before you point this at a document

Access requires physical possession, because the key is printed inside. That
is the whole protection model of BAC and PACE, and it is why this is not a
tool that can be used across a room. It also means possession is not consent:
use it on your own document, or with the explicit and informed agreement of
the person whose document it is. A facial image is biometric data, and the
rules where you are apply to whatever you export, however it was obtained.

## Reporting a problem

Security issues go through the process in [SECURITY.md](../SECURITY.md), not
through a public issue.
