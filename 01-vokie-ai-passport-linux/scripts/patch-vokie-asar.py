#!/usr/bin/env python3
"""
Create a patched copy of /opt/Vokie/resources/app.asar at ~/.config/vokie/app.asar
that fixes:
1. Main process PluginWebSocketServer race on Linux (isLinuxRemoteOnly() skipping server.start()).
2. Main process AiPassportDevicePresenter delegating to the installed AI Passport plugin on Linux.
3. Renderer AudioSettings hiding .device-lab-page when isLinuxTarget is true.
"""
import hashlib
import json
import os
import shutil
import struct

ORIG_ASAR = os.environ.get("VOKIE_ORIG_ASAR", "/opt/Vokie/resources/app.asar")
OUT_ASAR = os.environ.get(
    "VOKIE_PATCHED_ASAR",
    os.path.expanduser("~/.config/vokie/app.asar"),
)
BLOCK_SIZE = 4 * 1024 * 1024


def compute_integrity(data: bytes):
    full_hash = hashlib.sha256(data).hexdigest()
    blocks = []
    for i in range(0, len(data), BLOCK_SIZE):
        blocks.append(hashlib.sha256(data[i : i + BLOCK_SIZE]).hexdigest())
    return full_hash, blocks


def main():
    os.makedirs(os.path.dirname(OUT_ASAR), exist_ok=True)
    with open(ORIG_ASAR, "rb") as f:
        header_prefix = f.read(16)
        p0, p1, p2, json_len = struct.unpack("<IIII", header_prefix)
        padded_json_len = ((json_len + 3) // 4) * 4
        raw_header_bytes = f.read(padded_json_len)
        header_str = raw_header_bytes[:json_len].decode("utf-8")
        header = json.loads(header_str)
        base_offset = 16 + padded_json_len

        main_node = header["files"]["out"]["files"]["main"]["files"]["index.js"]
        f.seek(base_offset + int(main_node["offset"]))
        main_bytes = f.read(int(main_node["size"]))

        renderer_files = header["files"]["out"]["files"]["renderer"]["files"]["assets"]["files"]
        rend_name = [k for k in renderer_files if k.startswith("main-") and k.endswith(".js")][0]
        rend_node = renderer_files[rend_name]
        f.seek(base_offset + int(rend_node["offset"]))
        rend_bytes = f.read(int(rend_node["size"]))

    # 1. Patch out/main/index.js
    t1_old = b"const pluginServerReady = isLinuxRemoteOnly() ? Promise.resolve() : this.pluginWebSocketServer.start();"
    t1_new = b"const pluginServerReady = false               ? Promise.resolve() : this.pluginWebSocketServer.start();"
    assert len(t1_old) == len(t1_new)
    assert t1_old in main_bytes, "t1_old not found in out/main/index.js"
    main_bytes_patched = main_bytes.replace(t1_old, t1_new, 1)

    t2_old = (
        b"    this.aiPassportDevicePresenter = new AiPassportDevicePresenter({\n"
        b"      supported: aiPassportSupported,\n"
        b"      waitUntilReady: () => this.pluginConnectionsReady,\n"
        b"      getRuntimeState: () => this.getPluginRuntimeState(AI_PASSPORT_DEVICE_ID),\n"
        b"      getConfig: () => this.getAiPassportConfiguration(),\n"
        b"      configure: (config2) => this.configureAiPassportDevice(config2),\n"
        b"      getEnabled: () => this.configPresenter.get(AI_PASSPORT_ENABLED_CONFIG_KEY) === true,\n"
        b"      setEnabled: (enabled) => this.setAiPassportEnabled(enabled)\n"
        b"    });"
    )
    t2_body = (
        b"    const _p = AI_PASSPORT_EXTERNAL_PLUGIN_ID;\n"
        b"    this.aiPassportDevicePresenter = new AiPassportDevicePresenter({\n"
        b"      supported: true,\n"
        b"      waitUntilReady: () => this.pluginConnectionsReady,\n"
        b"      getRuntimeState: () => this.getPluginRuntimeState(_p),\n"
        b"      getConfig: () => this.pluginRegistry.getConfig(_p),\n"
        b"      configure: (c) => this.configureDevicePlugin(_p, c),\n"
        b"      getEnabled: () => this.isPluginEnabled(_p),\n"
        b"      setEnabled: (e) => this.setDevicePluginEnabled(_p, e)\n"
        b"    });"
    )
    assert len(t2_body) <= len(t2_old), f"{len(t2_body)} > {len(t2_old)}"
    t2_new = t2_body + b" " * (len(t2_old) - len(t2_body))
    assert len(t2_old) == len(t2_new)
    assert t2_old in main_bytes_patched, "t2_old not found in out/main/index.js"
    main_bytes_patched = main_bytes_patched.replace(t2_old, t2_new, 1)
    assert len(main_bytes_patched) == len(main_bytes)

    # 2. Patch out/renderer/assets/main-*.js
    r1_old = b'!unref(isLinuxTarget) ? (openBlock(), createElementBlock("div", {'
    r1_new = b'!false                ? (openBlock(), createElementBlock("div", {'
    assert len(r1_old) == len(r1_new)
    assert r1_old in rend_bytes, f"r1_old not found in {rend_name}"
    rend_bytes_patched = rend_bytes.replace(r1_old, r1_new, 1)
    assert len(rend_bytes_patched) == len(rend_bytes)

    # 3. Update integrity hashes in header_str (exact 64-char hex replacement preserves header length!)
    old_main_hash = main_node["integrity"]["hash"]
    new_main_hash, new_main_blocks = compute_integrity(main_bytes_patched)
    header_str = header_str.replace(old_main_hash, new_main_hash)
    for old_b, new_b in zip(main_node["integrity"]["blocks"], new_main_blocks):
        header_str = header_str.replace(old_b, new_b)

    old_rend_hash = rend_node["integrity"]["hash"]
    new_rend_hash, new_rend_blocks = compute_integrity(rend_bytes_patched)
    header_str = header_str.replace(old_rend_hash, new_rend_hash)
    for old_b, new_b in zip(rend_node["integrity"]["blocks"], new_rend_blocks):
        header_str = header_str.replace(old_b, new_b)

    new_header_bytes = header_str.encode("utf-8") + raw_header_bytes[json_len:]
    assert len(new_header_bytes) == len(raw_header_bytes)

    print("Copying base ASAR...")
    shutil.copyfile(ORIG_ASAR, OUT_ASAR)
    with open(OUT_ASAR, "r+b") as out:
        out.seek(16)
        out.write(new_header_bytes)
        out.seek(base_offset + int(main_node["offset"]))
        out.write(main_bytes_patched)
        out.seek(base_offset + int(rend_node["offset"]))
        out.write(rend_bytes_patched)

    print(f"Successfully created patched ASAR at {OUT_ASAR}")


if __name__ == "__main__":
    main()
