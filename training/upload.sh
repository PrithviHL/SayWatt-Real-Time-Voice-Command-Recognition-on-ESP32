#!/bin/sh
# Uploads this dataset to your Edge Impulse project.
# Labels come from file names (e.g. "turn_on.tts12.wav" -> turn_on).
# Your API key is read from the keyboard and never stored or printed.
set -e
cd "$(dirname "$0")"
export PATH="$HOME/.local/node/bin:$PATH"
command -v npx >/dev/null || { echo "Node.js not found (expected in ~/.local/node)"; exit 1; }

printf "Paste your Edge Impulse API key (Dashboard > Keys), then press Enter: "
stty -echo; read -r EI_KEY; stty echo; echo
[ -n "$EI_KEY" ] || { echo "No key entered."; exit 1; }

echo "Uploading training data (8,960 clips)…"
npx --yes -p edge-impulse-cli edge-impulse-uploader --api-key "$EI_KEY" --category training training/*.wav
echo "Uploading test data (2,240 clips)…"
npx --yes -p edge-impulse-cli edge-impulse-uploader --api-key "$EI_KEY" --category testing testing/*.wav
echo "Done. Open your project's Data acquisition page to check the 28 labels."
