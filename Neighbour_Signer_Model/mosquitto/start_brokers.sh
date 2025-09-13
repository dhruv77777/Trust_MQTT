#!/bin/bash

# --- Configuration ---
MOSQUITTO_BIN="/home/dhruv/winshare/Neighbour_Signer_Model/mosquitto/src/mosquitto"
CONFIG_DIR="/home/dhruv/winshare/Neighbour_Signer_Model/mosquitto/brokers"
LOG_DIR="/home/dhruv/winshare/Neighbour_Signer_Model/mosquitto/logs"
AGGREGATOR_SCRIPT="/home/dhruv/winshare/Neighbour_Signer_Model/mosquitto/plugins/payload-modification/aggregator.py"
AGGREGATOR_LOG="/home/dhruv/winshare/Neighbour_Signer_Model/mosquitto/logs/aggregator.log"
AGGREGATOR_INTERVAL=15 # Run every 15 seconds

# --- Broker Setup ---
BROKERS=("broker0" "broker1" "broker2" "broker3" "broker4" "broker5" "broker6" "broker7")
mkdir -p "$LOG_DIR"

# --- Cleanup Function ---
# This function will be called when the script exits
cleanup() {
    echo " "
    echo "Caught exit signal. Shutting down aggregator loop..."
    # Find the PID of the background loop and kill it
    if [ ! -z "$aggregator_pid" ]; then
        kill "$aggregator_pid"
        echo "Aggregator loop (PID $aggregator_pid) stopped."
    fi
    echo "Exiting."
    exit 0
}

# Trap the EXIT signal to run the cleanup function
trap cleanup SIGINT SIGTERM EXIT

# --- Start Brokers ---
echo "Starting all brokers..."
for BROKER in "${BROKERS[@]}"; do
    CONF="$CONFIG_DIR/$BROKER.conf"
    LOGFILE="$LOG_DIR/${BROKER}.log"

    echo "Starting $BROKER with config $CONF..."
    # Start broker in the background
    "$MOSQUITTO_BIN" -c "$CONF" > "$LOGFILE" 2>&1 &
done
echo "All brokers started."

---

# --- Start Aggregator Loop in the Background ---
echo "Starting the aggregator script in a background loop (runs every ${AGGREGATOR_INTERVAL}s)..."
(
    # This runs in a subshell
    while true; do
        # Execute the python script, appending output to its log
        /usr/bin/python3 "$AGGREGATOR_SCRIPT" >> "$AGGREGATOR_LOG" 2>&1
        # Wait for the specified interval
        sleep "$AGGREGATOR_INTERVAL"
    done
) &

# Save the Process ID (PID) of the background loop
aggregator_pid=$!
echo "Aggregator script is running in the background with PID $aggregator_pid."
echo "Aggregator logs are in $AGGREGATOR_LOG"
echo "Startup complete. Press Ctrl+C to stop this script and the aggregator."

# Keep the main script alive to manage the background process
# This allows the 'trap' to work correctly.
wait $aggregator_pid
