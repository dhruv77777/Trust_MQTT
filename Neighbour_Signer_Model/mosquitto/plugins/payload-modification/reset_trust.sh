#!/bin/bash

# This script stops all running Mosquitto brokers and then deletes all
# trust store history files, allowing for a clean start to a new experiment.

# Define the path to the directory containing your trust files.
# IMPORTANT: Make sure this path is correct for your setup.
TRUST_HISTORY_DIR="/home/dhruv/winshare/Neighbour_Signer_Model/mosquitto/plugins/payload-modification/trust_history"

echo "--- Trust Reset Script ---"
echo "WARNING: This will stop all running 'mosquitto' broker processes."
echo ""

# Stop any running mosquitto brokers
# The 'pkill -f' command finds processes by their full command line name.
# The '[m]osquitto' is a trick to prevent pkill from finding its own process.
echo "Stopping all Mosquitto broker instances..."
pkill -f '[m]osquitto'
sleep 2 # Give the processes a moment to shut down gracefully

echo "Searching for trust store files in: $TRUST_HISTORY_DIR"

# Find all files matching the pattern trust_store_b*.txt and delete them.
find "$TRUST_HISTORY_DIR" -name "trust_store_b*.txt" -print -delete

echo ""
echo "Trust history has been successfully reset."
echo "You can now restart your brokers."
