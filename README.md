# Local WebRTC H.264 streaming on AMB82 Mini (RTL8735B)

**Experimental examples; not a drop-in firmware release.**

This collection documents a hardware-tested proof of concept: the Realtek
AMB82 Mini streams **1920x1080 H.264** to **Firefox** using a direct WebRTC
connection on a trusted local Wi-Fi network, **without AWS signaling**.

The browser and the AMB82 exchange SDP and ICE candidates through a tiny
Node.js bridge. **The server does not relay RTP/video.** A host-only ICE
configuration is used, so remote internet viewing behind NAT is **not
supported** by this example.

## Measured proof of concept

- Real hardware: AMB82 Mini (RTL8735B) with stock JXF37/JXF37P camera.
- Local Firefox WebRTC: SDP answer applied, ICE/PeerConnection connected,
  H.264 video track unmuted and decoded.
- One diagnostic snapshot: 4,589 RTP packets, 2,239 frames received,
  2,238 frames decoded. These are cumulative counters, **not FPS** or a
  packet-loss measurement.
- Approx. 110-120 ms end-to-end latency in initial visual stopwatch tests.
  This is **not** a controlled latency benchmark.

## Architecture

```text
               signaling only (HTTP)        signaling only (WS)
AMB82 Mini  <--------------------------> Node.js <--------------> Firefox
    |                                                               ^
    +------------------ H.264 / SRTP / UDP -------------------------+
                     direct peer-to-peer LAN media
```

## What's in this archive?

- `pc/server.js` — Node.js single-viewer signaling bridge.
- `pc/index.html` — browser viewer with RTP / decoded-frame diagnostics.
- `pc/parse_camera_payload.js` — legacy SDP JSON workaround.
- `pc/test_camera_payload.js` — tests for the workaround.
- `firmware/local_http_signaling.c` — **reference** replacement transport for
  the official WebRTC signaling controller API.
- `scripts/apply_to_sdk.py` — source integration helper for a **compatible SDK
  layout only**. It does not solve all SDK build incompatibilities.
- `docs/` — camera selection, build notes, source changes and limitations.

This archive intentionally excludes upstream SDK sources, Realtek binary
camera/ISP data, toolchains, compiled firmware, personal network captures and
private configuration.

## 1. Obtain the SDK

Use the upstream Ameba Pro2 FreeRTOS SDK:
https://github.com/Ameba-AIoT/ameba-rtos-pro2

The sample target is `kvs_webrtc_v2_mmf`, with a local signaling transport
instead of the example's AWS signaling controller. SDK releases differ:
**lock down your own checkout/revision**, and expect API compatibility patches
as explained in `docs/SDK_COMPATIBILITY.md`.

## 2. Run the local PC signaling server

Requires Node.js with npm.

```bash
cd pc
npm install
npm test
npm start
```

Open `http://localhost:8080/` in Firefox on the same PC. If port 8080 is
occupied, set `PORT` on the server (the embedded `LOCAL_SIGNALING_PORT` must
then match). The server binds `0.0.0.0` by default so the camera can reach it;
**restrict it to a trusted local network and do not expose it to WAN**.

A browser-only smoke test displays a waiting state until the firmware connects.

## 3. Configure the physical camera

**Important:** using the wrong sensor can yield `VOE command 0x206 fail` and
**zero inbound video RTP packets even when ICE connects**. The default sensor
config in the SDK used for this experiment selected GC2053, but this board's
stock module was JXF37/JXF37P. See `docs/CAMERA_F37.md` before building.

## 4. Integrate the experimental firmware adapter

Back up a clean upstream SDK checkout first. For a matching `kvs_webrtc_v2_mmf`
source layout:

```bash
python scripts/apply_to_sdk.py --sdk /path/to/ameba-rtos-pro2 --ip 192.168.1.50
```

Replace `192.168.1.50` with the **PC LAN IPv4** that runs Node.js.
The script backs up two upstream files before patching them, writes the
reference local controller to the SDK source tree and adds the CMake option
`LOCAL_WEBRTC_SIGNALING=ON`. It does **not** flash the board.

**This source integration is only part of the modifications used in the tested
prototype.** The tested tree also needed mbedTLS 2.28.1/DTLS-SRTP compatibility,
IPv4-only ICE build fixes and TLS wrapper fixes. Those source patches are **not
included** in this example bundle: see `docs/SDK_COMPATIBILITY.md`.
Consequently, **compilation from a pristine SDK is not verified** using just
this archive. This package is meant for reference and further development.

## 5. Configure and build (after SDK-specific compatibility work)

On the tested Windows setup a short SDK path and the SDK GCC toolchain file
were necessary. One illustrative CMake configuration is:

```bash
cmake -S /path/to/sdk/project/realtek_amebapro2_v0_example/gcc-release \
  -B build_local -G 'Unix Makefiles' \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/sdk/project/realtek_amebapro2_v0_example/gcc-release/toolchain.cmake \
  -DEXAMPLE:STRING=kvs_webrtc_v2_mmf \
  -DLOCAL_WEBRTC_SIGNALING:BOOL=ON \
  -DBUILD_USRSCTP_LIBRARY:BOOL=OFF \
  -DCMAKE_C_USE_RESPONSE_FILE_FOR_INCLUDES=ON \
  -DCMAKE_CXX_USE_RESPONSE_FILE_FOR_INCLUDES=ON
cmake --build build_local --parallel 1
cmake --build build_local --target flash --parallel 1
```

The exact CMake toolchain path and generator depend on the environment.
The flash build target **creates** `flash_ntz.bin`; it does **not** write to
hardware. Verify generated `amebapro2_sensor_set.json` selects F37 first,
and follow official Realtek programming instructions, keeping a recovery
path for the board.

## 6. First local test

1. Connect AMB82 and the PC to the same trusted Wi-Fi network.
2. Boot the board and connect Wi-Fi using its serial AT console when present.
3. Confirm `camera online` in Node.js logs.
4. In Firefox open `http://localhost:8080/`, click **Connect video**.
5. Check `SDP answer applied`, `ICE: connected` and increasing
   `packets` / `framesDecoded`.

Some Firefox versions obfuscate local ICE addresses with `.local` mDNS names.
The embedded ICE implementation in this example does not resolve them. For
**diagnosis only**, Firefox `about:config` setting
`media.peerconnection.ice.obfuscate_host_addresses=false` may help on a
trusted LAN. This exposes local IP addresses to WebRTC sites; **restore the
privacy setting after testing**. See `docs/TROUBLESHOOTING.md`.

## Limitations and safety

- **One viewer, trusted LAN, no authentication, no TLS.** No Internet exposure.
- No STUN/TURN and no production NAT traversal.
- SDK integration/build is **not yet fully repeatable from clean upstream**.
- A limited legacy SDP workaround is in the Node bridge. Fix at the embedded
  source before considering production usage.
- The example bypasses waiting for SNTP and requires production-grade DTLS
  time, entropy and identity verification before real deployment.
- Reset the device between rejoin tests if old peer state survives.
- Not a product, not safety-critical software, no stability guarantees.

See `docs/SDK_COMPATIBILITY.md` and `docs/TROUBLESHOOTING.md` for the exact
known compatibility hurdles.

## Licenses

See `LICENSE` (Apache-2.0) and `docs/THIRD_PARTY_NOTICES.md`. The archive
contains adapted AWS helper implementations with original attribution.
Upstream Realtek and other dependency licenses remain their own.
