#!/bin/bash

# --- Configuration ---
# Make sure these paths match your environment
BASE_PATH="/home/dhruv/winshare/Code/mosquitto/plugins/payload-modification"
TRUST_STORE_DIR="$BASE_PATH/trust_store" # Corrected path
AGGREGATOR_SCRIPT="$BASE_PATH/aggregator.py"

echo "--- Trust Reset Script ---"

# --- Step 1: Delete all local trust stores ---
if [ -d "$TRUST_STORE_DIR" ]; then
    echo "Deleting all local trust stores in: $TRUST_STORE_DIR"
    # Use 'find' to safely delete the files
    find "$TRUST_STORE_DIR" -name "trust_store_*.txt" -print -delete
    echo "All local trust stores have been deleted."
else
    echo "Warning: Trust store directory not found. Nothing to delete."
fi

# --- Step 2: Run the aggregator once ---
# This will read the base topology and write a fresh network_map.txt
# with all default values (0.500) since no local stores exist.
echo ""
echo "Running the aggregator to regenerate the network map with baseline trust..."
/usr/bin/python3 "$AGGREGATOR_SCRIPT"

echo ""
echo "✅ Trust reset complete."
echo "The network_map.txt has been reset to all 0.5 values."
echo "You can now safely restart your broker system."