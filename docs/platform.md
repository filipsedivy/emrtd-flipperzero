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
