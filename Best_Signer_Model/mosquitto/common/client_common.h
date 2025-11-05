// client_common.h

#ifndef CLIENT_COMMON_H
#define CLIENT_COMMON_H

#include <mosquitto.h>

// Define the client structure to hold all client-specific information
struct client {
    char *client_id;          // Unique client ID
    char *issuer_broker;      // Broker ID (e.g., B0, B1, etc.)
    char *publish_topic;      // Topic to publish to
    char *subscribe_topic;    // Topic to subscribe to
    char *message;            // Message payload
    struct mosquitto *mosq;   // Mosquitto client instance
};

// Function declarations
void on_connect(struct mosquitto *mosq, void *obj, int rc);
void on_publish(struct mosquitto *mosq, void *obj, int mid);
void on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg);
char* generate_at(struct client *cli);
void run_client(struct client *cli);

#endif // CLIENT_COMMON_H
