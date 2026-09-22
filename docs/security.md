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

So the choice is deliberate and it is yours. **Remember on SD** is off by
default, so unless you turn it on nothing of the key is written to the card:

- leave it off and type the values for each read, or
- turn it on for convenience, and use **Document -> Forget stored data** when
  you are finished. That deletes the settings file.

A settings file written by a build with a different file format is discarded
*and removed* rather than left in place, because it may hold credentials under
keys this build no longer reads - and Forget would not know to clear them.

An export is deleted with the Flipper's own file manager, or from a computer
with the SD card in it; the reader browses its exports but does not erase
them. A read with **Export to SD** off never writes one in the first place -
that switch is the master one, and the APDU trace cannot write past it.

## The trace

`trace.txt` is written only when you ask for it, and it is the right thing to
send with a bug report - but read it first. What is in it depends on how far
the read got:

- everything before a session exists is in the clear: the SELECT commands,
  `EF.CardAccess`, and the BAC or PACE exchange itself;
- that exchange does not contain the credentials, but it is derived from
  them, and the encrypted nonce and the authentication tokens are material an
  attacker with the document in hand could test a guess against;
- **once Secure Messaging is running, the trace carries both forms**: the
  ciphertext as it crossed the radio, on the `>` and `<` lines, and the same
  exchange with the envelope taken off it, on the `>>` and `<<` lines. The
  second is the readable one, and it is the whole reason the file is worth
  sending: a trace that is ciphertext from the session onwards says nothing
  about the read that went wrong. It also means the data groups are in there in
  the clear - the machine readable zone, the additional details, and every
  byte of the facial image as it streamed past.

No key, no nonce and none of the three credentials is ever written to a trace;
what is written is what the chip sent. That makes a trace no more sensitive
than the `EF_DG*.bin` files beside it, and no less: the two come out of the
same read and **Export to SD** is the one switch over both.

A trace of a failure before authentication is nearly always safe to share. A
trace of a successful read is a record of a session with your own document;
treat it as you would the export. [trace.md](trace.md) describes what each line
of one means.

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
read succeeds, unless they are being remembered on purpose or **Wipe after
read** has been turned off; a read that failed keeps them, so that the error
screen can show what was used and Retry can use it again.

Wiping is `emrtd_secure_wipe()`, which is `mbedtls_platform_zeroize()` and not
`memset()`. The difference is not pedantry: a `memset()` over a buffer that is
never read again is a dead store, and at the `-Os` every FAP is built with, the
compiler deletes it. On this device a thread stack is a heap block, and the
allocator does not zero what it hands out, so a wipe that the optimiser removed
would leave the session keys in the next application's memory.

The keys never reach the export or the trace. The **password** is a different
matter, and the sentence that used to stand here was wrong about it: the MRZ
password *is* the document number and the two dates, and all three are written
into `mrz.txt` and `report.txt` in the clear, because they are part of the
document's own data. The export directory is named after the document number
as well. Treat an export as the identity it contains.

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
