#!/bin/bash

# Path to mosquitto binary (change if different)
MOSQUITTO_BIN="/home/dhruv/winshare/Best_Signer_Model/mosquitto/src/mosquitto"

# Config folder
CONFIG_DIR="/home/dhruv/winshare/Best_Signer_Model/mosquitto/brokers"

# Runtime output (log & pid) will be in this Linux-native path
LOG_DIR="/home/dhruv/winshare/Best_Signer_Model/mosquitto/logs"
mkdir -p "$LOG_DIR"

# Broker names
BROKERS=("broker0" "broker1" "broker2" "broker3" "broker4" "broker5" "broker6" "broker7")

echo "Starting all brokers..."

for BROKER in "${BROKERS[@]}"; do
    CONF="$CONFIG_DIR/$BROKER.conf"
    LOGFILE="$LOG_DIR/${BROKER}.log"

    echo "Starting $BROKER with config $CONF..."
    "$MOSQUITTO_BIN" -c "$CONF" > "$LOGFILE" 2>&1 &

    echo "$BROKER started with PID $!"
done

echo "All brokers started. Logs are in $LOG_DIR"
