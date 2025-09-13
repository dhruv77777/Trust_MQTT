#!/bin/bash

# List of subscriber process names to kill
subscribers=(
    "subscriber_B0"
    "subscriber_B1"
    "subscriber_B2"
    "subscriber_B3"
    "subscriber_B4"
    "subscriber_B5"
    "subscriber_B6"
    "subscriber_B7"
)

for sub in "${subscribers[@]}"; do
    # Find and kill the process if it's running
    pids=$(pgrep -f -x "$sub")
    if [[ -n "$pids" ]]; then
        echo "Killing $sub (PID: $pids)"
        kill $pids
    else
        echo "$sub not running."
    fi
done


#!/bin/bash

BIN_DIR="/home/dhruv/winshare/Neighbour_Signer_Model/mosquitto/client_code/bin"

echo "🛑 Killing all subscriber_B* processes..."

for sub in "$BIN_DIR"/subscriber_B*
do
    name=$(basename "$sub")
    pids=$(pgrep -f "$name")
    if [[ -n "$pids" ]]; then
        echo "Killing $name (PID: $pids)"
        kill $pids
    else
        echo "$name not running."
    fi
done
