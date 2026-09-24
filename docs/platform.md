# Platform findings (verified empirically, 2026-09-20)

1. FAP + `fap_libs=["mbedtls"]` links `mbedtls_ecp_mul`, `mbedtls_ecp_muladd`,
   `mbedtls_ecp_point_read/write_binary`, `mbedtls_ecp_check_pubkey`, the whole
   `mbedtls_mpi_*` public API, AES, DES/3DES, SHA-1, SHA-256. APPCHK passes.
2. `mbedtls_mpi_exp_mod` / `mbedtls_mpi_core_exp_mod` CANNOT be used: they pull
   `mbedtls_mpi_core_montmul` -> `mbedtls_ct_memcpy_if`, which is undefined in
   libmbedtls.a and absent from the firmware API table. APPCHK fails the build.
   => PACE with DH over MODP groups (param ids 0..2) is impossible. ECDH only.
   => RSA is impossible anyway (MBEDTLS_RSA_C off).
3. Brainpool curves are NOT compiled into the shipped mbedtls, but a hand
   populated `mbedtls_ecp_group` works. Verified against ICAO 9303-11 appendix G
   (generic mapping + key agreement on brainpoolP256r1).
   - `grp->id = MBEDTLS_ECP_DP_NONE`, `grp->modp = NULL` (generic reduction).
   - cofactor `h` left at 0 so `mbedtls_ecp_group_free()` releases the MPIs;
     `h` takes no part in the arithmetic.
   - the generator must be seeded with `mbedtls_ecp_set_zero()` first, because
     `mbedtls_ecp_point_read_binary()` asks the group for its curve type and
     that answer is read off the generator.
4. `MBEDTLS_ECP_MAX_BITS` is 256 with the firmware config (only secp256r1
   enabled). `COMB_MAX_D` sizes a stack array in `ecp_mul_comb()`, so curves
   above 256 bits must be refused. Covers ids 8..13; ids 14..18 are listed but
   rejected with a named error.
5. AES-CMAC is absent from the firmware mbedtls (no `MBEDTLS_CMAC_C`).
   Implemented on `mbedtls_aes_crypt_ecb`, verified against RFC 4493 and
   NIST SP 800-38B D.2/D.3.
6. APPCHK does validate FAP imports and fails the build on an unresolvable one.
7. SDK: ufbt 0.2.6, firmware 1.4.3, API 87.1, mbedtls 3.6.2.
8. `mbedtls_ecp_gen_privkey` links cleanly in a FAP (verified with APPCHK): it does not
   reach `exp_mod`. It is therefore the right way to draw a PACE ephemeral scalar, because
   it produces a value in the correct range for the group without a reject loop.
9. FAP size for a minimal app pulling in ECP + bignum + AES + DES: about 21 KB. A FAP is
   loaded into RAM, so this counts against the heap the read itself needs.

## Radio timing (verified empirically, 2026-09-21)

10. The firmware's ISO 14443-4A poller gives a card a frame waiting time of
    **1620 carrier cycles - 120 microseconds** - for every I-block whenever the
    card's ATS carries no TB1. `iso14443_4a_get_fwt_fc_max()` starts from
    `ISO14443_4A_FDT_DEFAULT_FC`, which is `#define`d to
    `ISO14443_3A_FDT_POLL_FC` (1620), and replaces it only when `tl > 1`,
    `t0 & TB1` and `fwi != 0x0F` all hold. 1620 cycles is the ISO 14443-3 poll
    frame delay time, not a frame waiting time; it is forty times below the
    ISO 14443-4 default and no smart card can answer anything inside it.
    Source: `lib/nfc/protocols/iso14443_4a/iso14443_4a.c:14,240-256` and
    `lib/nfc/protocols/iso14443_3a/iso14443_3a.h:16`, identical in the official
    `release` tree and in Unleashed.
11. The same poller allows **40000 cycles (2.95 ms)** for the answer to RATS,
    fixed, at `iso14443_4a_poller_i.c:41` with the constant at
    `iso14443_4a_poller_i.h:13`. ISO/IEC 14443-4 allows a card 65536 cycles
    before FWI is known, so a fully conformant chip can lose this race.
12. Neither value is reachable from an application: `iso14443_4a_poller_send_block()`
    takes no timeout argument. `iso14443_3a_poller_send_standard_frame()` does,
    and it is exported and enabled in both API tables (87.1 and 88.9). Running
    the block transmission protocol on top of it - RATS, block numbering,
    chaining both ways, S(WTX), retransmission - is therefore the only way to
    choose the waiting time, and it is what `transport/emrtd_isodep.c` does.
    The Python reference implementation reached the same conclusion
    independently and asks the Flipper's `raw` command for 4000000 cycles
    (295 ms) on every frame.
13. Owning that layer also removes a difference between the two firmwares.
    Unleashed rewrote `iso14443_4a_poller_send_block()` to use
    `iso14443_4_layer_encode_command()` / `decode_response()` and added
    `iso14443_4a_poller_send_block_pwt_ext()`; the official tree still uses
    `encode_block()` / `decode_block()`. Nothing above the transport now
    depends on which of the two is installed.
14. The type B poller is not affected: `iso14443_4b_poller_send_block()` gets
    its waiting time from `iso14443_3b_get_fwt_fc_max()`, which reads the ATQB
    protocol info. That path still uses the firmware's own implementation.

## Memory (verified empirically, 2026-09-21)

15. **The heap is 190,144 bytes**, not the hundred kilobytes this project used
    to say. `__heap_start__` is `0x20001540` and `__heap_end__` is
    `0x2002fc00`; the pair is in the literal pool of
    `__furi_crash_implementation` at `0x08012980`, which prints the difference
    as the total, and a crash dump from a real device confirms it
    (`heap total: 190144`). The 1,024 bytes up to `_stack_end` at `0x20030000`
    are the main stack, not heap.
16. **The application's own image is half of it.** A FAP is loaded into RAM
    section by section, each as one contiguous block: `.text` 67,888,
    `.rodata` 23,712, `.bss` 3,200, and the rest 48, for 94,848 bytes. The
    `.text` block alone is a 35 per cent contiguous demand on the whole heap.
    Rank any size reduction by what it takes off `.text` first.
17. **`pvPortMalloc` never returns NULL.** Both of its failure exits - not
    enough free in total at `0x801486c`, and no single block large enough at
    `0x801488a` - reach `0x801481a`, which loads `"out of memory"` and falls
    into `__furi_crash_implementation`. Every `if(p != NULL)` after an
    allocation is therefore unreachable on this firmware. A shortage can only
    be handled by asking `memmgr_heap_get_max_free_block()` **before**
    allocating, which is what the read scene and the two large mid-read
    buffers now do.
18. **There are two different "out of memory" screens**, and which one appears
    says where the failure was. `"out of memory"` in lower case is the crash
    above: it prints the crashing thread's name, r0 to r11, LR, the stack
    watermark and the heap figures **to the log UART on pins 13 and 14**, then
    stores the message pointer in an RTC backup register and reboots.
    `"Error: Out of Memory - Not enough RAM to run the app"` with a `Reboot`
    button is the loader dialog: `elf_load_section_data` checks
    `memmgr_heap_get_max_free_block() >= sh_size + 1024` before each section
    and gives up cleanly. The first means the application was running; the
    second means it never started.
19. **`furi_hal_usb_is_locked()` means "an RPC session is open"**, precisely.
    Scanning the whole firmware for callers of `furi_hal_usb_lock`
    (`0x08010490`) finds exactly one: `rpc_cli_command_start_session` at
    `0x0808692a`. So it distinguishes lab.flipper.net, qFlipper or the mobile
    app from a cable that is only charging - which
    `furi_hal_power_is_charging()` cannot. It allocates an event flag and
    blocks on the USB thread, so it belongs on a failure path, not a hot one.
20. **A connected computer costs about 20.2 kB of heap**, in three layers that
    arrive separately: opening the serial port at all starts a CLI shell with
    a 4 kB thread stack (about 5.6 kB); `start_rpc_session` adds a second 4 kB
    command thread, a 3 kB session worker and 51 handler records (about
    11.8 kB); and the screen mirror adds a framebuffer, a protobuf message and
    a 1 kB thread (about 2.9 kB). lab.flipper.net starts the screen mirror by
    itself - its landing page is the Device page, whose `onMounted` calls
    `startScreenStream()` as soon as RPC is up.

## Radio errors (read from the source, 2026-09-24)

21. **A damaged answer is reported as a card that is not there.**
    `iso14443_3a_poller_process_error()` in
    `lib/nfc/protocols/iso14443_3a/iso14443_3a_poller_i.c` of the 1.4.3 tree
    maps `NfcErrorTimeout` to `Iso14443_3aErrorTimeout` and every other
    `NfcError` - an incomplete frame, a data format error, a FIFO overflow - to
    `Iso14443_3aErrorNotPresent`. Only a bad CRC on a frame that arrived whole
    comes back as `Iso14443_3aErrorWrongCrc`. So a chip that answered at the
    edge of the field is named as one that is not present, and this reader,
    which maps a timeout and "not present" alike to "The document moved
    away", can only tell the two apart by the trace's radio codes: `7` is a
    timeout, `1` not present, `6` a bad CRC. Both are recovered from with
    R(NAK) rather than by sending the command again - see
    `EMRTD_ISODEP_RETRIES` in `transport/emrtd_isodep.h` for why.
