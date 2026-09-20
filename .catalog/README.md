Reads an electronic passport or identity card - an eMRTD, built to ICAO Doc
9303 - over NFC, and writes what it finds to the SD card.

The chip in a travel document will not answer until the reader proves it is
holding the document. This application does that with **PACE** as well as with
**BAC**, which is the difference that matters: BAC is the protocol of 2006 and
is being withdrawn, and a document issued in the European Union after 2017 may
implement PACE only. Against such a chip a BAC-only reader gets as far as the
first command and stops.

## To use

You need the three values printed on the data page: the **document number**,
the **date of birth** and the **date of expiry**. Those three are the key to
the chip - a border reader takes them off the machine readable zone with a
camera, and here you type them in. An identity card that prints a six digit
**card access number** can be opened with that instead.

Enter them under Document, then choose Read. The antenna is in the back of the
Flipper: open the passport at the data page, lay it flat, and put the Flipper
face up over the middle of the page. If nothing answers, close the book and
try the back cover instead - the chip is in one place or the other. Take the
document out of any case with metal or another card in it, and keep both still
while the progress bar moves. A full read including the photograph takes
several seconds.

## What it reads

- The machine readable zone: name, nationality, document number, dates.
- The facial image, exported as a JPEG or JPEG 2000 file.
- The additional personal and document details, where the document carries
  them.
- The security object, and every data group's hash checked against it.
- Every other non-EAC data group, exported as raw bytes.

Each read becomes a directory under `/ext/apps_data/emrtd/` holding the raw
files, the decoded machine readable zone, the image and a report. An APDU
trace can be written next to them for diagnosis.

## What it cannot do

- **It cannot verify the signature on the security object.** The data group
  hashes are checked, which proves the files on the chip belong together; the
  signature needs RSA and a store of trusted country certificates, and neither
  fits on the device. A read cannot tell a genuine document from a copy.
- It cannot read the fingerprints or the iris (DG3 and DG4). Those are
  protected by Extended Access Control and need a state issued terminal
  certificate.
- PACE over MODP groups, PACE with the integrated mapping, and PACE curves
  above 256 bits are refused by name: the firmware's mbed TLS cannot compute
  them. Such a document may still open with BAC.
- It cannot write to a chip. There is no write command in it.

## About the data

The document number and the two dates are the key to that chip, and an export
directory is a complete identity, the photograph included. Both stay on the SD
card, in the clear, until you delete them - the application forgets the
credentials on request, and an export is removed with the Flipper's file
manager.

Use this on your own document, or with the explicit consent of the person
whose document it is. Local rules on biometric data apply to whatever you
export.

Source, documentation and issues:
<https://github.com/filipsedivy/emrtd-flipperzero>
