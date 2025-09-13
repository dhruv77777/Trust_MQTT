#!/bin/bash

# ==============================================================================
# Modified Build Script for MQTT Client Simulation
# ==============================================================================

# --- Configuration ---
CLIENT_CODE_DIR="/home/dhruv/winshare/Neighbour_Signer_Model/mosquitto/client_code"
COMMON_DIR="/home/dhruv/winshare/Neighbour_Signer_Model/mosquitto/common"
INCLUDE_DIR="/home/dhruv/winshare/Neighbour_Signer_Model/mosquitto/include"
LIB_DIR="/home/dhruv/winshare/Neighbour_Signer_Model/mosquitto/lib"
BIN_DIR="$CLIENT_CODE_DIR/bin"


echo "🔧 Compiling publisher clients (c0 to c15)..."
for i in {0..15}; do
    SRC="$CLIENT_CODE_DIR/c${i}.c"
    OUT="$BIN_DIR/c${i}"
    if [ -f "$SRC" ]; then
        echo "   -> Compiling c${i}.c"
        gcc "$SRC" "$COMMON_DIR/client_common.c" -I"$INCLUDE_DIR" -L"$LIB_DIR" \
            -lmosquitto -lpthread -lssl -lcrypto -lcjson -o "$OUT"
        if [ $? -ne 0 ]; then echo "❌ Failed to compile c${i}.c"; fi
    fi
done

echo "🔧 Compiling malicious publishers..."
for i in 0 2 4 6; do
    FILE="Cmalicious_B${i}"
    SRC="$CLIENT_CODE_DIR/${FILE}.c"
    OUT="$BIN_DIR/${FILE}"
    if [ -f "$SRC" ]; then
        echo "   -> Compiling ${FILE}.c"
        gcc "$SRC" "$COMMON_DIR/client_common.c" -I"$INCLUDE_DIR" -L"$LIB_DIR" \
            -lmosquitto -lpthread -lssl -lcrypto -lcjson -o "$OUT"
        if [ $? -ne 0 ]; then echo "❌ Failed to compile ${FILE}.c"; fi
    fi
done

echo "🔧 Compiling subscribers (subscriber_B0 to B7)..."
for i in {0..7}; do
    FILE="subscriber_B${i}"
    SRC="$CLIENT_CODE_DIR/${FILE}.c"
    OUT="$BIN_DIR/${FILE}"
    if [ -f "$SRC" ]; then
        echo "   -> Compiling ${FILE}.c"
        gcc "$SRC" -I"$INCLUDE_DIR" -L"$LIB_DIR" \
            -lmosquitto -lpthread -lssl -lcrypto -lcjson -o "$OUT"
        if [ $? -ne 0 ]; then echo "❌ Failed to compile ${FILE}.c"; fi
    fi
done

echo "🔧 Compiling subscriber_B4_neg..."
FILE="subscriber_B4_neg"
SRC="$CLIENT_CODE_DIR/${FILE}.c"
OUT="$BIN_DIR/${FILE}"
if [ -f "$SRC" ]; then
    echo "   -> Compiling ${FILE}.c"
    gcc "$SRC" -I"$INCLUDE_DIR" -L"$LIB_DIR" \
        -lmosquitto -lpthread -lssl -lcrypto -lcjson -o "$OUT"
    if [ $? -ne 0 ]; then echo "❌ Failed to compile ${FILE}.c"; fi
fi


echo "✅ All compilation complete. Executables are in $BIN_DIR"
