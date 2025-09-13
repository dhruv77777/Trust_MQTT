FROM eclipse-mosquitto:2

# Install build tools and dependencies
RUN apk add --no-cache build-base libc-dev openssl-dev cjson-dev

# Create plugin directory
RUN mkdir -p /mosquitto/plugins/payload-modification

# Copy your plugin source code into container
COPY ./mosquitto/plugins/payload-modification /plugin-src

WORKDIR /plugin-src

# Build the plugin using your local mosquitto source includes
# You will need to mount the mosquitto source directory as a volume during docker-compose build
RUN gcc -fPIC -shared mosquitto_payload_modification.c \
    -I/mosquitto-src/include -I/mosquitto-src/lib \
    -o /mosquitto/plugins/payload-modification/mosquitto_payload_modification.so \
    -lcjson -lssl -lcrypto
