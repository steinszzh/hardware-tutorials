#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VOKIE_CFG="${XDG_CONFIG_HOME:-$HOME/.config}/vokie"
SO_PATH="$VOKIE_CFG/libvokie_asar_override.so"
PATCHED_ASAR="$VOKIE_CFG/app.asar"
ORIG_ASAR="${VOKIE_ORIG_ASAR:-/opt/Vokie/resources/app.asar}"

mkdir -p "$VOKIE_CFG"

# Prevent Vokie's built-in autostart entry from launching unpatched /opt/Vokie/vokie first
rm -f "${XDG_CONFIG_HOME:-$HOME/.config}/autostart/com.vokie.desktop"

# Ensure Node.js is in PATH when launched from GNOME desktop/autostart (non-interactive shell skips ~/.bashrc/nvm)
if ! command -v node >/dev/null 2>&1; then
    NODE_BIN_DIR=""
    for candidate in \
        "$HOME/.local/bin" \
        "$HOME/.volta/bin" \
        "$HOME/.asdf/shims" \
        "$HOME/.fnm/node-versions"/*/installation/bin \
        "$HOME/.nvm/versions/node"/*/bin
    do
        if [[ -x "$candidate/node" ]]; then
            NODE_BIN_DIR="$candidate"
        fi
    done
    if [[ -n "$NODE_BIN_DIR" ]]; then
        export PATH="$NODE_BIN_DIR:$PATH"
    fi
fi

if [[ ! -f "$SO_PATH" || "$SCRIPT_DIR/asar_override.c" -nt "$SO_PATH" ]]; then
    gcc -shared -fPIC -O2 "$SCRIPT_DIR/asar_override.c" -o "$SO_PATH" -ldl
fi

if [[ ! -f "$PATCHED_ASAR" || "$ORIG_ASAR" -nt "$PATCHED_ASAR" || "$SCRIPT_DIR/patch-vokie-asar.py" -nt "$PATCHED_ASAR" ]]; then
    VOKIE_ORIG_ASAR="$ORIG_ASAR" VOKIE_PATCHED_ASAR="$PATCHED_ASAR" \
        python3 "$SCRIPT_DIR/patch-vokie-asar.py"
fi

export VOKIE_PATCHED_ASAR="$PATCHED_ASAR"
export LD_PRELOAD="$SO_PATH${LD_PRELOAD:+:$LD_PRELOAD}"
exec /opt/Vokie/vokie "$@"
