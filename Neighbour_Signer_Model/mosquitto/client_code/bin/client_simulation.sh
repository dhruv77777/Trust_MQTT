#!/bin/bash

TOTAL_CLIENTS=16      # c0 to c15
DELAY_BETWEEN_STARTS=5  # seconds
RUN_DURATION=3600     # 30 minutes in seconds

while true; do
  for i in $(seq 0 $((TOTAL_CLIENTS - 1))); do
    client_name="c$i"
    echo "Starting client $client_name ..."

    (
      "./$client_name" &
      pid=$!
      sleep $RUN_DURATION
      echo "Stopping client $client_name after $RUN_DURATION seconds."
      kill $pid
    ) &

    sleep $DELAY_BETWEEN_STARTS
  done
done
