# Before publishing

- [ ] Verify the upstream Realtek SDK commit/version and add its commit hash.
- [ ] Check `git diff` against the exact upstream snapshot and recover all
      build compatibility patches; if not included, keep the README marked
      as **reference examples, not a one-command turnkey build**.
- [ ] Confirm rights for example contributions and upstream-derived helpers;
      keep their original Apache-2.0 copyright notices.
- [ ] Ensure no SSIDs, Wi-Fi passphrases, personal names, tokens, public WAN
      IPs, private LAN IPs from real networks, or captured traffic are shared.
- [ ] Do not upload `*.bin`, `*.ntz`, toolchains, SDK binary blobs or keys.
- [ ] Test `npm install`, `npm test` and `npm start` in a clean folder.
- [ ] Optionally record a repeatable fresh-SDK build and end-to-end test.
- [ ] Add screenshots after inspecting them for personal information.
- [ ] Clearly state that the Node server must remain on a trusted LAN.
