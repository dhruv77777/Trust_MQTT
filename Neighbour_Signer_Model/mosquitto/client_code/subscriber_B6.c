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
        printf("Connected to Broker 6 (Subscriber)\n");
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

    // --- 2. Extract info for the feedback payload ---
    cJSON *signers_array = cJSON_GetObjectItemCaseSensitive(root, "S");
    const char *source_broker = "unknown";
    const char *target_broker = "B6"; // The broker this subscriber is connected to

    // We need at least two signers to identify the last link (source -> target)
    if (cJSON_IsArray(signers_array) && cJSON_GetArraySize(signers_array) >= 2) {
        // ---- THIS IS THE KEY CHANGE ----
        // Get the SECOND-TO-LAST signer in the array. This is the broker
        // that sent the message to our local broker.
        int second_to_last_index = cJSON_GetArraySize(signers_array) - 2;
        cJSON *source_signer_item = cJSON_GetArrayItem(signers_array, second_to_last_index);
        
        if (cJSON_IsString(source_signer_item)) {
            source_broker = source_signer_item->valuestring;
        }
    }
    
    // Feedback is still hardcoded to positive for now
    const char *feedback_type = "positive";

    // --- 3. Construct and publish feedback in the correct format ---
    cJSON *feedback_json = cJSON_CreateObject();
    if (feedback_json) {
        cJSON_AddStringToObject(feedback_json, "source", source_broker); 
        cJSON_AddStringToObject(feedback_json, "target", target_broker);
        cJSON_AddStringToObject(feedback_json, "feedback", feedback_type);
        
        char *feedback_payload = cJSON_PrintUnformatted(feedback_json);
        if (feedback_payload) {
            mosquitto_publish(mosq, NULL, FEEDBACK_TOPIC, strlen(feedback_payload), feedback_payload, 0, false);
            printf("Sent '%s' feedback for link %s -> %s\n", feedback_type, source_broker, target_broker);
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
    
    mosq = mosquitto_new("interactive-subscriberB6", true, NULL);
    
    mosquitto_connect_callback_set(mosq, on_connect);
    mosquitto_message_callback_set(mosq, on_message);

    // Make sure this port corresponds to your subscriber's local broker
    rc = mosquitto_connect(mosq, "localhost", 1889, 60);
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
