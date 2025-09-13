#!/usr/bin/env python3
import os
import sys
from datetime import datetime

# --- Configuration ---
# IMPORTANT: Adjust these paths to match your environment.
BASE_PATH = "/home/dhruv/winshare/Neighbour_Signer_Model/mosquitto/plugins/payload-modification"
NETWORK_MAP_FILE = os.path.join(BASE_PATH, "network_map.txt")
TRUST_STORE_DIR = os.path.join(BASE_PATH, "trust_history")

# List of all broker IDs in your network.
BROKER_IDS = [f"B{i}" for i in range(8)] # Generates B0, B1, ..., B7

# Trust calculation parameters (must match the C plugin)
BASE_RATE_DELTA = 0.5

def log(level, message):
    """Prints a formatted log message."""
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    print(f"[{timestamp}] [{level.upper()}] {message}")

def calculate_trust(r, s):
    """Calculates the trust score from r and s values."""
    r, s = int(r), int(s)
    if r < 0 or s < 0:
        return 0.0
    denominator = float(r + s + 2)
    alpha = float(r) / denominator
    gamma = 2.0 / denominator
    return alpha + (BASE_RATE_DELTA * gamma)

def main():
    """
    Reads all local trust stores, merges the knowledge, and writes a new
    authoritative network_map.txt file.
    """
    log("info", "Starting trust map aggregation process...")

    # Step 1: Read the base topology from the network map.
    # We only care about the links (source, target), not the old trust values.
    topology = {}
    try:
        log("debug", f"Reading base topology from: {NETWORK_MAP_FILE}")
        with open(NETWORK_MAP_FILE, 'r') as f:
            for line in f:
                if line.startswith('#') or not line.strip():
                    continue
                parts = line.strip().split(',')
                if len(parts) >= 2:
                    source, target = parts[0], parts[1]
                    if source not in topology:
                        topology[source] = {}
                    # Initialize with a baseline trust of 0.5
                    topology[source][target] = 0.5 
    except FileNotFoundError:
        log("error", f"Network map file not found at {NETWORK_MAP_FILE}. Cannot proceed.")
        sys.exit(1)
    
    log("info", f"Loaded base topology with {len(topology)} source brokers.")

    # Step 2: Read each broker's local trust store and update the topology.
    for broker_id in BROKER_IDS:
        trust_store_path = os.path.join(TRUST_STORE_DIR, f"trust_store_{broker_id}.txt")
        if not os.path.exists(trust_store_path):
            log("debug", f"Skipping. Trust store for {broker_id} not found.")
            continue

        log("info", f"Processing trust store for {broker_id}...")
        with open(trust_store_path, 'r') as f:
            for line in f:
                if line.startswith('#') or not line.strip():
                    continue
                
                # Format is: source_broker,r_value,s_value
                # This represents broker_id's opinion of source_broker.
                parts = line.strip().split(',')
                if len(parts) == 3:
                    source, r, s = parts
                    target = broker_id # The owner of the file is the target
                    
                    # Update the trust score in our master topology
                    if source in topology and target in topology[source]:
                        new_trust = calculate_trust(r, s)
                        topology[source][target] = new_trust
                        log("debug", f"  - Updated link {source}->{target} with trust {new_trust:.3f} (r={r}, s={s})")
                    else:
                        log("warn", f"  - Found link {source}->{target} in store, but not in base topology. Ignoring.")

    # Step 3: Write the new, authoritative network_map.txt file.
    try:
        log("info", f"Writing new authoritative network map to {NETWORK_MAP_FILE}...")
        with open(NETWORK_MAP_FILE, 'w') as f:
            timestamp = datetime.utcnow().strftime("%Y-%m-%d %H:%M:%S UTC")
            f.write(f"# Network Topology - Last aggregated at {timestamp}\n")
            
            # Sort for consistent output
            for source in sorted(topology.keys()):
                for target in sorted(topology[source].keys()):
                    score = topology[source][target]
                    f.write(f"{source},{target},{score:.3f}\n")
        
        log("success", "Aggregation complete. Network map has been updated.")

    except Exception as e:
        log("error", f"Failed to write network map file: {e}")

if __name__ == "__main__":
    main()
