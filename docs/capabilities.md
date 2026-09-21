# What this reader can and cannot do

The reader does not distinguish between kinds of document. Anything whose
contactless chip carries the ICAO LDS1 application - a passport, a national
identity card, a residence permit - is read by the same code path, and the
only differences that reach the user are which password opens it and whether
the machine readable zone is TD1, TD2 or TD3.

Everything below is a property of this build on this firmware. The entries
marked **no** are platform limits rather than unfinished work, and each one is
measured and written down in [platform.md](platform.md).

## Access control

| | |
| --- | --- |
| PACE-ECDH-GM, AES-128, AES-192, AES-256 | **yes** |
| PACE curves: brainpoolP192r1..P256r1, NIST P-192..P-256 (parameter ids 8-13) | **yes** |
| BAC with 3DES | **yes** |
| Password from the MRZ, or from the CAN | **yes** - a passport prints only the first, an identity card usually both |
| PACE curves above 256 bits (ids 14-18) | **no** - `MBEDTLS_ECP_MAX_BITS` is 256 in the firmware's mbed TLS, and the curve is refused by name |
| PACE over MODP/DH groups (ids 0-2) | **no** - `mbedtls_mpi_exp_mod` cannot be linked into an application, see [platform.md](platform.md) |
| PACE with the integrated or chip authentication mapping | **no** - detected and refused by name |

The driver is chosen from what the chip announces in `EF.CardAccess`, with a
fall back to the other if the first is refused. A document that offers a PACE
variant this build cannot compute may still open with BAC, and the error names
which variant it was.

## Reading

| | |
| --- | --- |
| EF.COM, EF.SOD, DG1, DG2, DG11, DG12, DG14, DG15 and the other non-EAC groups | **yes** |
| The machine readable zone in all three layouts: TD1, TD2, TD3 | **yes** - three lines of 30, two of 36, two of 44 |
| Data group hashes against EF.SOD | **yes** |
| A frame waiting time the chip can actually meet | **yes** - the reader runs ISO-DEP itself on type A rather than using the firmware's poller |
| DG3 and DG4, the fingerprints and the iris | **no** - protected by Extended Access Control, which needs a state issued terminal certificate |
| EF.SOD signature, document signer and country signing certificates | **no** - this needs RSA and a certificate store; neither is on the device |
| Active Authentication, Chip Authentication, Terminal Authentication | **no** - DG14 and DG15 are read and reported, the protocols are not run |
| Writing to a chip | **no**, and there is nothing in here that could |

DG2 is tens of kilobytes against a heap of 186 KB that already holds this
application, so it is never held in memory: it goes to the SD card as it
arrives, hashed on the way past.

## What "verified" means here

The data group hashes are checked against EF.SOD, which proves the files belong
together and have not been altered since the chip was issued. The signature
over EF.SOD is **not** checked, so this reader cannot tell a genuine document
from a well made copy of one.

A read that shows every hash matching says the chip is internally consistent,
nothing more. [security.md](security.md) says the same thing at greater length,
alongside what a read leaves on the card.
