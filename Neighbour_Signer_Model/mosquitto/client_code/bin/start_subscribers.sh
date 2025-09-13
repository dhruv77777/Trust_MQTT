#!/bin/bash

BIN_DIR="/home/dhruv/winshare/Neighbour_Signer_Model/mosquitto/client_code/bin"

echo "🔁 Launching all subscriber_B* binaries in new terminal tabs..."

for sub in "$BIN_DIR"/subscriber_B*
do
    if [[ -x "$sub" ]]; then
        gnome-terminal --tab --title="$(basename "$sub")" -- bash -c "$sub; exec bash"
        echo "✅ Launched: $(basename "$sub")"
    else
        echo "⚠️ Skipped (not executable): $sub"
    fi
done
