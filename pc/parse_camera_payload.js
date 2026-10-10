'use strict';
/*
 * Ameba Local WebRTC Example compatibility repair.
 * Realtek v0.2 creates a malformed response only for SDP Answer:
 * {"type":"answer","payload":{"type":"answer","sdp":"{"type":...}"}}
 * The inner message is already a JSON-serialized SDP object. The camera
 * wrongly inserts it inside an additional JSON string. Recover ONLY this
 * exact legacy envelope, validating the embedded JSON before forwarding.
 * No impact on ICE candidates or valid messages.
 */
const legacyPrefix = '{"type":"answer","payload":{"type":"answer","sdp":"';
const legacySuffix = '"}}';

function parseCameraBody(raw, route) {
  try {
    return JSON.parse(raw || '{}');
  } catch (originalError) {
    if (route !== '/camera/send' ||
        !raw.startsWith(legacyPrefix) ||
        !raw.endsWith(legacySuffix)) throw originalError;

    const inner = raw.slice(legacyPrefix.length, -legacySuffix.length);
    let embedded;
    try { embedded = JSON.parse(inner); }
    catch { throw originalError; }

    let answer;
    if (embedded && typeof embedded === 'object' && !Array.isArray(embedded) &&
        embedded.type === 'answer' && typeof embedded.sdp === 'string') {
      answer = embedded;
    } else if (typeof embedded === 'string' && embedded.startsWith('v=0')) {
      answer = { type: 'answer', sdp: embedded };
    } else {
      throw originalError;
    }
    console.log(`[local-webrtc] recovered legacy SDP answer (sdp chars=${answer.sdp.length})`);
    return { type: 'answer', payload: answer };
  }
}

module.exports = { parseCameraBody };
