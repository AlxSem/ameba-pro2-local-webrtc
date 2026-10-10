#!/usr/bin/env python3
"""Apply local WebRTC signaling example integration to a local Ameba Pro2 SDK tree.
Backs up touched files, refuses unrecognized upstream layout. No flasher invoked.
"""
from pathlib import Path
import argparse
import ipaddress
import shutil

parser=argparse.ArgumentParser(description='Install local WebRTC example signaling into Realtek SDK')
parser.add_argument('--sdk',required=True,help='Path to a clean or compatible Ameba Pro2 SDK checkout')
parser.add_argument('--ip',required=True,help='LAN IPv4 of the PC running Node.js')
args=parser.parse_args()
addr=ipaddress.IPv4Address(args.ip)
if not addr.is_private or addr.is_loopback or addr.is_multicast:
    raise SystemExit('Provide the PC private LAN IPv4 address, e.g. 192.168.1.50')
root=Path(args.sdk)
base=root/'component/example/kvs_webrtc_v2_mmf'
cmake=base/'kvs_webrtc_v2_mmf.cmake'
common=base/'webrtc_v2_app_src/app_common/app_common.c'
sig=base/'webrtc_v2_app_src/signaling_controller/local_http_signaling.c'
files={cmake:cmake.read_text(),common:common.read_text()}

c=files[cmake]
needle='${sdk_root}/component/example/kvs_webrtc_v2_mmf/webrtc_v2_app_src/signaling_controller/signaling_controller.c'
if c.count(needle)!=1:
    raise SystemExit(f'Cannot find unique CMake signaling source (found {c.count(needle)})')
insert='''\n# Local WebRTC signaling example, opt-in; original AWS transport remains default.\noption(LOCAL_WEBRTC_SIGNALING "Use local HTTP signaling bridge" OFF)\nif(LOCAL_WEBRTC_SIGNALING)\n    list(APPEND app_example_flags LOCAL_WEBRTC_SIGNALING=1)\n    set(LOCAL_SIGNALING_SOURCE "${sdk_root}/component/example/kvs_webrtc_v2_mmf/webrtc_v2_app_src/signaling_controller/local_http_signaling.c")\nelse()\n    set(LOCAL_SIGNALING_SOURCE "${sdk_root}/component/example/kvs_webrtc_v2_mmf/webrtc_v2_app_src/signaling_controller/signaling_controller.c")\nendif()\n'''
c=insert+c.replace(needle,'${LOCAL_SIGNALING_SOURCE}')

app=files[common]
start='    if( skipProcess == 0 )\n    {\n        /* Put the default STUN server into index 0. */'
end='    if( skipProcess == 0 )\n    {\n        /* Parse Ice server confgis into IceControllerIceServer_t structure. */'
if app.count(start)!=1 or app.count(end)!=1:
    raise SystemExit('GetIceServerList implementation differs; no files changed')
i=app.index(start); j=app.index(end,i+len(start))
app=app[:i]+'#if !defined(LOCAL_WEBRTC_SIGNALING)\n'+app[i:j]+'#endif /* !LOCAL_WEBRTC_SIGNALING: local LAN uses host ICE candidates */\n\n'+app[j:]
# Remove endless Internet time dependency in lab mode; leaves upstream unchanged.
time_start='    /* Block until get time via SNTP. */\n    sntp_init();'
time_end='    }\n}'
if app.count(time_start)!=1:
    raise SystemExit('SNTP initialization layout changed; no files changed')
i=app.index(time_start)
j=app.index(time_end,i)+len('    }\n')
app=app[:i]+'''#if defined(LOCAL_WEBRTC_SIGNALING)
    /* Lab-only: keep booting without external NTP. Validate clock and DTLS runtime.
     * This does NOT establish a trustworthy real-time clock for production. */
    LogWarn( ( "Local WebRTC lab: SNTP wait bypassed; check DTLS clock and entropy" ) );
#else
'''+app[i:j]+'''#endif /* LOCAL_WEBRTC_SIGNALING */
'''+app[j:]

code=(Path(__file__).resolve().parent.parent/'firmware/local_http_signaling.c').read_text().replace('"192.168.1.100"',f'"{addr}"')
if sig.exists():
    raise SystemExit(f'Refusing overwrite of {sig}; restore/inspect existing patch first')

# All validation above; now write, with preserved upstream copies.
for p in (cmake,common):
    bak=Path(str(p)+'.local_webrtc_prepatch.bak')
    if bak.exists():raise SystemExit(f'Backup already exists: {bak}; inspect before applying twice')
for p in (cmake,common):
    shutil.copy2(p,str(p)+'.local_webrtc_prepatch.bak')
cmake.write_text(c)
common.write_text(app)
sig.write_text(code)
print('Local WebRTC signaling installed. Original files backed up.')
print('Node PC IPv4:',addr)
print('CMake option: -DLOCAL_WEBRTC_SIGNALING:BOOL=ON')
print('No build or flashing was performed.')
