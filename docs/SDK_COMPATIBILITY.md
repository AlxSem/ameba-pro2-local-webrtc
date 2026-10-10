# SDK compatibility checklist — not a ready-to-apply patchset

The working lab firmware was based on the upstream Realtek
`kvs_webrtc_v2_mmf` example and a Windows GCC 10.4.1 environment. The original
SDK code, build tooling and firmware blobs are not included here.

**Important:** `scripts/apply_to_sdk.py` modifies only the signaling source
selection and two locations in `app_common.c`. It is intentionally not a
fully reproducible clean-SDK installer. A hardware-tested complete upstream
diff has **not** been exported into this archive.

Additional changes present in the tested development tree included:

1. `transport_dtls_mbedtls.c/.h`: API/symbol adjustments for mbedTLS 2.28.1
   DTLS-SRTP key-export support and correct callback use.
2. mbedTLS 2.28.1 `mbedtls_config.h`: enabling `MBEDTLS_SSL_DTLS_SRTP` with
   matching library configuration.
3. `ice_controller.c`: replacing a missing IPv6 string-length constant with
   a suitable buffer size.
4. `ice_controller_net.c` and `ice_controller_socket_listener.c`: guards
   around unavailable IPv6 paths for the IPv4-only lwIP configuration.
5. `transport_mbedtls.c`: guards for filesystem certificate loading where
   `MBEDTLS_FS_IO` is disabled on embedded builds; avoid silently disabling
   certificate/fingerprint verification.
6. TCP/UDP mbedTLS socket wrappers: removal of an unavailable
   `threading_alt.h` include in the tested version.
7. Build settings: `BUILD_USRSCTP_LIBRARY=OFF` for the minimal use case,
   SDK `toolchain.cmake`, long-path mitigation and response-file options.
8. The experimental local mode did not wait for external SNTP. This may
   affect security and proper time validation; it must be revisited.
9. The host-only ICE configuration cannot reach arbitrary peers across NAT.

This is a list of **observed integration changes**, not an assertion that all
upstream versions need these exact fixes. Inspect compiler errors against
the exact SDK checkout. Keep original third-party headers and license notices.

`firmware/local_http_signaling.c` demonstrates the runtime SignalingController
API surface used by the project. The wire protocol is documented in
`docs/SIGNALING.md`.

## Provenance and caution

- The example adapter includes Apache-2.0 helper code attributed to Amazon.
- No entire upstream Realtek sources or firmware blobs are bundled.
- Do not assume a successfully generated `flash_ntz.bin` guarantees the
  correct hardware sensor or a functioning camera.
- Keep a known-working firmware backup and verify UART boot recovery before
  changing the device flash.
