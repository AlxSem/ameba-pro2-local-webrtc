# Troubleshooting

| Symptom | Likely area / what to inspect |
|---|---|
| `WIFI initialized` then `wait for wifi connection` | UART AT console and station SSID/password configuration; never post passwords in logs |
| Camera does not appear in Node.js logs | PC LAN IPv4 in adapter, firewall, port, Wi-Fi isolation, HTTP connectivity |
| `bad HTTP request` while sending SDP answer | Legacy malformed SDP JSON; enable bundled v0.3 compatibility parser; use `npm test` |
| `invalid IP ... .local` | Firefox mDNS ICE host-address privacy; see README diagnostic setting |
| SDP answer applied but ICE checking/failed | Candidate parsing, UDP host reachability, NAT, TCP-only candidates |
| ICE connected, video RTP absent | Incorrect sensor/ISP config, VOE initialization, encoder start, RTP sender |
| `VOE command 0x206 fail` | Validate physical camera, F37 `sensor.h` and generated `amebapro2_sensor_set.json` |
| RTP grows, decoded frames remain zero | H.264 SDP profile, codec matching, SPS/PPS/IDR, RTP packetization |
| Decoded frames grow but no image | Browser stream attachment, video playback and HTML rendering |
| `viewer already connected` | This example allows only one browser WebSocket viewer |
| External signaling works, ICE failed | Expected: no STUN/TURN and no public UDP route provided |

During local testing, isolate failures by checking each stage separately:
firmware boot → Wi-Fi → signaling → SDP → ICE → SRTP → RTP → H.264 decode.

Use the viewer's `packets`, `bytes`, `framesReceived` and `framesDecoded`
indicators, and UART logs. A single screenshot does not prove long-run
stability, frame rate or absence of packet loss.
