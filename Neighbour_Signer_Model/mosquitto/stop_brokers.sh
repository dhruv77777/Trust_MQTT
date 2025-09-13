#!/bin/bash

# Check if any mosquitto broker processes are still running
if pgrep -f "mosquitto.*broker" > /dev/null; then
  echo "Some Mosquitto broker processes are still running:"
  pgrep -af "mosquitto.*broker"
else
  echo "No Mosquitto brokers running currently."
fi

# Kill all brokers (broker0 to broker7)
for i in {0..7}; do
  pkill -f "mosquitto.*broker$i"
done
echo "All brokers killed."

# Delete all log files
LOG_DIR="/home/dhruv/winshare/Neighbour_Signer_Model/mosquitto/logs"
if [ -d "$LOG_DIR" ]; then
  rm -f "$LOG_DIR"/broker*.log
  echo "All broker logs deleted from $LOG_DIR."
else
  echo "Log directory $LOG_DIR not found."
fi

# Check again if any mosquitto broker processes are still running
if pgrep -f "mosquitto.*broker" > /dev/null; then
  echo "Some Mosquitto broker processes are still running:"
  pgrep -af "mosquitto.*broker"
else
  echo "All Mosquitto brokers successfully terminated."
fi
