#!/bin/bash

echo "▶️ Starting Docker containers..."
docker compose up -d

echo "🔧 Fixing trust store permissions inside containers..."

for broker in broker{0..7}; do
  echo "➡️  Processing $broker ..."
  docker exec "$broker" chown -R 1883:1883 /mosquitto/plugins/payload-modification/trust_history 2>/dev/null
  if [ $? -eq 0 ]; then
    echo "✅ Permissions fixed in $broker"
  else
    echo "❌ Failed to fix permissions in $broker (may not be running yet)"
  fi
done

echo "🔁 Restarting all broker containers..."
for broker in broker{0..7}; do
  docker restart "$broker" >/dev/null 2>&1 && echo "🔄 Restarted $broker" || echo "❌ Failed to restart $broker"
done

echo "✅ All done."
