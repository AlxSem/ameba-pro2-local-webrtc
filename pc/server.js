'use strict';
/* Local WebRTC example signaling bridge v0.2. No auth: use on a trusted LAN only.
 * Viewer uses WebSocket; AMB82 uses small HTTP/1.1 requests (simpler on lwIP).
 * This server relays SDP and ICE only, never video or RTP.
 */
const http = require('http');
const fs = require('fs');
const path = require('path');
const { WebSocketServer, WebSocket } = require('ws');
const { parseCameraBody } = require('./parse_camera_payload');
const port = Number(process.env.PORT || 8080);
const host = process.env.HOST || '0.0.0.0';
let viewer = null;
let cameraLastSeen = 0;
let cameraOnlinePreviously = false;
let cameraQueue = [];
const queueLimit = 80;
const relayTypes = new Set(['offer','answer','candidate','bye']);
const cameraOnline = () => Date.now() - cameraLastSeen < 6000;
function json(ws, data) { if (ws && ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify(data)); }
function state() {
  const online = cameraOnline();
  json(viewer, {type:'status', camera:online, viewer:!!viewer});
  cameraOnlinePreviously = online;
}
function reply(res, status, value) {
  const data = value == null ? '' : JSON.stringify(value);
  res.writeHead(status, {'Content-Type':'application/json; charset=utf-8',
    'Content-Length':Buffer.byteLength(data), 'Connection':'close',
    'Cache-Control':'no-store', 'X-Content-Type-Options':'nosniff'});
  res.end(data);
}
async function readBody(req) {
  let data = '';
  for await (const c of req) {
    data += c.toString('utf8');
    if (Buffer.byteLength(data) > 12 * 1024) throw new Error('message too large');
  }
  return parseCameraBody(data, req.url);
}
const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, 'http://localhost');
  try {
    if (req.method === 'POST' && url.pathname === '/camera/hello') {
      await readBody(req);
      cameraLastSeen = Date.now();
      cameraQueue = []; // Prevent delivering stale SDP after reconnect.
      state();
      console.log(`[local-webrtc] camera online from ${req.socket.remoteAddress}`);
      return reply(res,200,{ok:true});
    }
    if (req.method === 'GET' && url.pathname === '/camera/poll') {
      cameraLastSeen = Date.now();
      if (!cameraOnlinePreviously) state();
      if (!cameraQueue.length) return reply(res,204,null);
      return reply(res,200,cameraQueue.shift());
    }
    if (req.method === 'POST' && url.pathname === '/camera/send') {
      const m = await readBody(req);
      cameraLastSeen = Date.now();
      if (!cameraOnlinePreviously) state();
      if (!m || !['answer','candidate','bye'].includes(m.type) || typeof m.payload !== 'object')
        return reply(res,400,{error:'invalid signaling message'});
      if (!viewer) return reply(res,409,{error:'viewer not connected'});
      json(viewer,{type:m.type,payload:m.payload});
      console.log(`[local-webrtc] camera -> viewer: ${m.type}`);
      return reply(res,200,{ok:true});
    }
    if(req.method === 'GET' && (url.pathname === '/' || url.pathname === '/index.html')) {
      const content = await fs.promises.readFile(path.join(__dirname,'index.html'));
      res.writeHead(200,{'Content-Type':'text/html; charset=utf-8',
        'Content-Length':content.length, 'Cache-Control':'no-store',
        'Content-Security-Policy':"default-src 'self'; connect-src 'self' ws:; style-src 'self' 'unsafe-inline'; script-src 'self' 'unsafe-inline'"});
      return res.end(content);
    }
    reply(res,404,{error:'not found'});
  } catch(e) { console.warn('[local-webrtc] bad HTTP request:',e.message); reply(res,400,{error:e.message}); }
});
const wss = new WebSocketServer({server,path:'/signal',maxPayload:12*1024});
wss.on('connection',(ws,req)=>{
  let authorized = false;
  ws.on('message',(raw,isBinary)=>{
    if(isBinary) return json(ws,{type:'error',message:'JSON only'});
    let m; try {m=JSON.parse(raw.toString('utf8'));} catch {return json(ws,{type:'error',message:'Invalid JSON'});}
    if(!authorized) {
      if(m.type!=='hello'||m.role!=='viewer') return json(ws,{type:'error',message:'Viewer hello required'});
      if(viewer) {json(ws,{type:'error',message:'Viewer already connected'});ws.close(1008,'occupied');return;}
      viewer=ws; authorized=true;
      console.log(`[local-webrtc] viewer connected from ${req.socket.remoteAddress}`);
      json(ws,{type:'hello',role:'viewer',ok:true});state();return;
    }
    if(!relayTypes.has(m.type) || !['offer','candidate','bye'].includes(m.type))
      return json(ws,{type:'error',message:'Unsupported message type'});
    if(!cameraOnline()) return json(ws,{type:'error',message:'Camera offline'});
    if(cameraQueue.length>=queueLimit) return json(ws,{type:'error',message:'Camera signaling queue full'});
    if(!m.payload || typeof m.payload!=='object') {
      if(m.type!=='bye')return json(ws,{type:'error',message:'Object payload required'});
    }
    // Flatten the payload so Realtek's existing JSON parser sees top-level `type`.
    let signal;
    if(m.type==='offer') {
      if(m.payload.type!=='offer'||typeof m.payload.sdp!=='string')return json(ws,{type:'error',message:'Invalid offer'});
      signal={type:'offer',sdp:m.payload.sdp};
    } else if(m.type==='candidate') {
      if(typeof m.payload.candidate!=='string')return json(ws,{type:'error',message:'Invalid candidate'});
      signal={type:'candidate',...m.payload};
    } else signal={type:'bye'};
    cameraQueue.push(signal);
    console.log(`[local-webrtc] viewer -> camera: ${m.type} (queued=${cameraQueue.length})`);
  });
  ws.on('close',()=>{
    if(viewer!==ws)return;
    viewer=null;
    cameraQueue=[{type:'bye'}];
    console.log('[local-webrtc] viewer disconnected');
  });
  ws.on('error',e=>console.warn('[local-webrtc] viewer websocket:',e.message));
});
setInterval(()=>{if(cameraOnlinePreviously!==cameraOnline())state();},1000).unref();
server.listen(port,host,()=>{
  console.log(`[local-webrtc] viewer: http://localhost:${port}/`);
  console.log(`[local-webrtc] camera HTTP: http://<pc-lan-ip>:${port}/camera/hello`);
  console.log('[local-webrtc] no authentication; trusted LAN only; video is peer-to-peer');
});
