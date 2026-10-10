# Experimental local signaling protocol

The PC hosts one Node.js server; the camera uses plain HTTP and the viewer
uses WebSocket. The PC **does not handle the RTP/SRTP media path**.

| Direction | Endpoint / message | Purpose |
|---|---|---|
| Camera → PC | `POST /camera/hello` | Register/ping with `{}` |
| Camera → PC | `GET /camera/poll` | Wait for next queued offer/candidate/bye |
| Camera → PC | `POST /camera/send` | Deliver answer/candidate JSON |
| Browser → PC | `WS /signal`, `hello` viewer | Establish one viewer session |
| Browser ↔ PC | `offer`, `answer`, `candidate`, `bye` | Exchange SDP/ICE |

The embedded HTTP client assumes `Content-Length` responses and
`Connection: close`. Polling is approximately 200 ms. There is no session
nonce, authentication, or integrity protection at the application layer;
**trusted LAN testing only**.

The reference firmware has a known SDP serialization bug for `answer`:
it nests an already JSON-serialized SDP object inside an unescaped JSON
string. `pc/parse_camera_payload.js` temporarily repairs that exact format
and rejects unrelated malformed messages. This is not a robust or secure
public-facing parser. Fix the embedded JSON generation for any future
production system.

Only one viewer is allowed. On reload/disconnect an old peer session may
linger on the device; use a device reset during first-time tests if needed.

For internet access, `TCP/8080` forwards **signaling only**. ICE needs a
separate UDP route, usually STUN/TURN or another valid NAT traversal design.
Do **not** expose this sample server to the public internet.
