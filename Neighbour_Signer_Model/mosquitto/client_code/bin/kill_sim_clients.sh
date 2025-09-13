#!/bin/bash

TOTAL_CLIENTS=16  # c0 to c15

echo "Killing all client processes..."

for i in $(seq 0 $((TOTAL_CLIENTS - 1))); do
  client_name="c$i"

  # Find and kill processes matching the client name
  pids=$(pgrep -f "./$client_name")
  if [ -n "$pids" ]; then
    echo "Killing $client_name (PID: $pids)"
    kill $pids
  else
    echo "$client_name is not running."
  fi
done

echo "All matching client processes have been processed."
