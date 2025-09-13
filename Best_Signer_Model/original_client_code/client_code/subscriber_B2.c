#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h> 
#include "mosquitto.h"
#include <cjson/cJSON.h> 

#define FEEDBACK_TOPIC "internal/feedback"
#define SUBSCRIBE_TOPIC "+/+" // Wildcard to subscribe to all floor/room topics

void on_connect(struct mosquitto *mosq, void *obj, int rc) 
{
    if (rc == 0)
    {
        printf("Connected to Broker (Subscriber)\n");
        // Subscribe to the wildcard topic to receive messages from all floors and rooms.
        mosquitto_subscribe(mosq, NULL, SUBSCRIBE_TOPIC, 1);
        printf("Subscribed to topic: %s\n", SUBSCRIBE_TOPIC);
    } 
    else 
    {
        printf("Failed to connect, return code %d\n", rc);
    }
}

void on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg) 
{
    printf("\n----------------------------------------\n");
    printf("Message received on topic: %s\n", msg->topic);

    // --- 1. Parse the incoming JSON message ---
    cJSON *root = cJSON_Parse((const char *)msg->payload);
    if (!root) {
        printf("Error: Could not parse payload as JSON.\n");
        return;
    }

    // --- 2. Extract the origin_broker and the original message ---
    cJSON *origin_broker_item = cJSON_GetObjectItemCaseSensitive(root, "b");
    cJSON *original_msg_item = cJSON_GetObjectItemCaseSensitive(root, "msg");

    const char *origin_broker_id = "unknown";
    if (cJSON_IsString(origin_broker_item)) {
        origin_broker_id = origin_broker_item->valuestring;
    }
    
    printf("Data originated from: %s\n", origin_broker_id);
    if (cJSON_IsString(original_msg_item)) {
        printf("Original Message: \"%s\"\n", original_msg_item->valuestring);
    }
    
    // --- 3. Get feedback from the user ---
    printf("Provide feedback (p for positive, n for negative): ");
    int choice = getchar();
    // Consume the rest of the line, including the newline character
    while (getchar() != '\n' && getchar() != EOF);

    const char *feedback_type;
    if (choice == 'p' || choice == 'P') {
        feedback_type = "positive";
    } else if (choice == 'n' || choice == 'N') {
        feedback_type = "negative";
    } else {
        printf("Invalid input. Defaulting to positive feedback.\n");
        feedback_type = "positive";
    }

    // --- 4. Construct and publish feedback ---
    cJSON *feedback_json = cJSON_CreateObject();
    if (feedback_json) {
        cJSON_AddStringToObject(feedback_json, "origin_broker", origin_broker_id);
        cJSON_AddStringToObject(feedback_json, "feedback", feedback_type);
        
        char *feedback_payload = cJSON_PrintUnformatted(feedback_json);
        if (feedback_payload) {
            mosquitto_publish(mosq, NULL, FEEDBACK_TOPIC, strlen(feedback_payload), feedback_payload, 0, false);
            printf("Sent '%s' feedback for message from %s\n", feedback_type, origin_broker_id);
            free(feedback_payload);
        }
        cJSON_Delete(feedback_json);
    }
    
    printf("----------------------------------------\n");
    cJSON_Delete(root);
}

int main() {
    struct mosquitto *mosq;
    int rc;

    mosquitto_lib_init();
    
    mosq = mosquitto_new("interactive-subscriber", true, NULL);
    
    mosquitto_connect_callback_set(mosq, on_connect);
    mosquitto_message_callback_set(mosq, on_message);

    // Make sure this port corresponds to your subscriber's local broker
    rc = mosquitto_connect(mosq, "localhost", 1885, 60);
    if (rc != MOSQ_ERR_SUCCESS) 
    {
        fprintf(stderr, "Could not connect to broker. Error code: %d\n", rc);
        return 1;
    }

    printf("Subscriber is running. Waiting for messages...\n");
    mosquitto_loop_forever(mosq, -1, 1);

    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();

    return 0;
}
