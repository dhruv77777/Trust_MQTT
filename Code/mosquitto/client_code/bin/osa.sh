#!/bin/bash

# Track running PIDs
pids=()

terminate_process() {
    local pid=$1
    local name=$2

    if kill -0 "$pid" 2>/dev/null; then
        echo "Stopping $name (PID: $pid) with SIGTERM..."
        kill "$pid" 2>/dev/null
        for i in {1..3}; do
            if ! kill -0 "$pid" 2>/dev/null; then
                echo "$name stopped gracefully."
                return
            fi
            sleep 1
        done
        echo "$name did not stop gracefully, sending SIGKILL..."
        kill -9 "$pid" 2>/dev/null
    else
        echo "$name is not running."
    fi
}

cleanup() {
    echo -e "\nReceived interrupt. Cleaning up..."
    for pid_name in "${pids[@]}"; do
        IFS=: read -r pid name <<< "$pid_name"
        terminate_process "$pid" "$name"
    done
    echo "All processes stopped. Exiting."
    exit 0
}

trap cleanup SIGINT

iteration=1
network_map_path="/home/dhruv/winshare/Code/mosquitto/plugins/payload-modification/network_map.txt"
log_file="/home/dhruv/winshare/Code/mosquitto/plugins/payload-modification/osa_test.txt"

while true; do
    echo "========== Iteration $iteration =========="

    # Step 1
    echo "[Step 1] Starting subscriber_B6..."
    ./subscriber_B6 &
    pid_sub_b6=$!
    pids+=("$pid_sub_b6:subscriber_B6")

    sleep 5
    echo "[Step 1] Starting test.sh..."
    ./test.sh

    echo "[Step 1] Waiting 5 seconds before stopping subscriber_B6..."
    sleep 5
    terminate_process "$pid_sub_b6" "subscriber_B6"
    # Remove subscriber_B6 PID from array
    pids=("${pids[@]/$pid_sub_b6:subscriber_B6}")

    # Store first cat result
    before_map=$(cat "$network_map_path" 2>/dev/null || echo "File not found: $network_map_path")

    # Step 2
    echo "[Step 2] Waiting 10 seconds before starting subscriber_B6_neg and c8..."
    sleep 10
    ./subscriber_B6_neg &
    pid_sub_b6_neg=$!
    pids+=("$pid_sub_b6_neg:subscriber_B6_neg")

    echo "[Step 2] Starting c8..."
    ./c8

    echo "[Step 2] Waiting 5 seconds before stopping subscriber_B6_neg..."
    sleep 5
    terminate_process "$pid_sub_b6_neg" "subscriber_B6_neg"
    # Remove subscriber_B6_neg PID from array
    pids=("${pids[@]/$pid_sub_b6_neg:subscriber_B6_neg}")

    sleep 6
    # Store second cat result
    after_map=$(cat "$network_map_path" 2>/dev/null || echo "File not found: $network_map_path")

    # Append results to file
    {
    echo "===== Iteration $iteration - Network Map BEFORE Step 2 (B4,B6 lines only) ====="
    echo "$before_map" | grep '^B4,B6'
    echo
    echo "===== Iteration $iteration - Network Map AFTER Step 2 (B4,B6 lines only) ====="
    echo "$after_map" | grep '^B4,B6'
    echo "------------------------------------------------------------"
    } >> "$log_file"


    iteration=$((iteration + 1))
done
