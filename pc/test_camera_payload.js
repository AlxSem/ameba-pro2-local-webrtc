'use strict';
const assert = require('node:assert/strict');
const { parseCameraBody } = require('./parse_camera_payload');
const sdp = 'v=0\r\no=- 42 2 IN IP4 192.168.1.51\r\na=setup:active\r\n';
const inner = JSON.stringify({type:'answer',sdp});
const legacy = '{"type":"answer","payload":{"type":"answer","sdp":"' + inner + '"}}';
assert.throws(() => JSON.parse(legacy), SyntaxError);
assert.deepEqual(parseCameraBody(legacy, '/camera/send'), {
  type:'answer',payload:{type:'answer',sdp}
});
const valid = JSON.stringify({type:'candidate',payload:{candidate:'candidate:1 1 udp 1 192.168.1.51 5000 typ host'}});
assert.deepEqual(parseCameraBody(valid, '/camera/send'),JSON.parse(valid));
assert.throws(() => parseCameraBody(legacy, '/camera/hello'), SyntaxError);
assert.throws(() => parseCameraBody('{oops}', '/camera/send'), SyntaxError);
assert.throws(() => parseCameraBody('{"type":"answer","payload":{"type":"answer","sdp":"' +
  JSON.stringify({type:'offer',sdp}) + '"}}', '/camera/send'), SyntaxError);
console.log('local-webrtc camera payload compatibility tests: ok');
