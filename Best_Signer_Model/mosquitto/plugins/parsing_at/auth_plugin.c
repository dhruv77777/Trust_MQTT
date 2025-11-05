#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <mosquitto_broker.h>
#include <mosquitto_plugin.h>
#include <mosquitto.h>
#include <cjson/cJSON.h>

static char broker_id[32] = "Unknown";

// Helper: Check if string in cJSON array
bool string_in_array(cJSON *array, const char *str) {
    if (!cJSON_IsArray(array)) return false;
    cJSON *element = NULL;
    cJSON_ArrayForEach(element, array) {
        if (cJSON_IsString(element) && strcmp(element->valuestring, str) == 0) {
            return true;
        }
    }
    return false;
}

// ACL check helper
bool check_permission(const char *client_id, const char *topic, bool is_publish) {
   /* if (!client_id || !topic) return false;

    if (strcmp(client_id, "C1") == 0) {
        return (is_publish && strcmp(topic, "home/firstfloor/kitchen") == 0) ||
               (!is_publish && strcmp(topic, "home/firstfloor/bedroom") == 0);
    }

    if (strcmp(client_id, "C2") == 0) {
        return (is_publish && strcmp(topic, "home/firstfloor/bathroom") == 0) ||
               (!is_publish && strcmp(topic, "home/firstfloor/tv") == 0);
    }

    return false;*/
    return true;
}

// Append broker id to S array if missing
void append_broker_to_S(cJSON *S_array) {
    if (!S_array || !cJSON_IsArray(S_array)) return;

    if (!string_in_array(S_array, broker_id)) {
        printf("[PLUGIN] ➕ Appending broker ID '%s' to S\n", broker_id);
        cJSON_AddItemToArray(S_array, cJSON_CreateString(broker_id));
    }
}

// Event callback for incoming messages
int on_message(int event, void *event_data, void *userdata) {
    struct mosquitto_evt_message *msg = (struct mosquitto_evt_message *)event_data;

    printf("\n[PLUGIN] 📩 Message received on topic: %s\n", msg->topic);
    printf("[PLUGIN] 📦 Payload (%d bytes): %.*s\n", (int)msg->payloadlen, (int)msg->payloadlen, (char *)msg->payload);

    if (!msg->payload || msg->payloadlen < 2 || ((char *)msg->payload)[0] != '{') {
        return MOSQ_ERR_SUCCESS;  // Not JSON payload
    }

    char *payload_copy = strndup((const char *)msg->payload, msg->payloadlen);
    if (!payload_copy) return MOSQ_ERR_SUCCESS;

    cJSON *root = cJSON_Parse(payload_copy);
    if (!root) {
        printf("[PLUGIN] ❌ Invalid JSON payload!\n");
        free(payload_copy);
        return MOSQ_ERR_SUCCESS;
    }

    cJSON *issuer = cJSON_GetObjectItemCaseSensitive(root, "b");
    cJSON *client = cJSON_GetObjectItemCaseSensitive(root, "c");
    cJSON *msg_field = cJSON_GetObjectItemCaseSensitive(root, "msg");
    cJSON *Fp = cJSON_GetObjectItemCaseSensitive(root, "Fp");
    cJSON *Fs = cJSON_GetObjectItemCaseSensitive(root, "Fs");
    cJSON *S = cJSON_GetObjectItemCaseSensitive(root, "S");

    printf("\n[PLUGIN] 🧾 Parsed AT Token:\n");
    printf("  🔹 Issuer: %s\n", issuer ? issuer->valuestring : "null");
    printf("  🔹 Client: %s\n", client ? client->valuestring : "null");
    printf("  🔹 Message: %s\n", msg_field ? msg_field->valuestring : "null");

    if (!client || !client->valuestring) {
        printf("[PLUGIN] ❌ No client ID found in payload.\n");
        goto cleanup;
    }

    if (Fp && cJSON_IsArray(Fp)) {
        printf("  🔸 Publish Topics:\n");
        cJSON *elem = NULL;
        cJSON_ArrayForEach(elem, Fp) {
            if (cJSON_IsString(elem)) {
                printf("    - %s\n", elem->valuestring);
                if (!check_permission(client->valuestring, elem->valuestring, true)) {
                    printf("[PLUGIN] ❌ Unauthorized publish: %s\n", elem->valuestring);
                    goto cleanup;
                }
            }
        }
    }

    if (Fs && cJSON_IsArray(Fs)) {
        printf("  🔸 Subscribe Topics:\n");
        cJSON *elem = NULL;
        cJSON_ArrayForEach(elem, Fs) {
            if (cJSON_IsString(elem)) {
                printf("    - %s\n", elem->valuestring);
                if (!check_permission(client->valuestring, elem->valuestring, false)) {
                    printf("[PLUGIN] ❌ Unauthorized subscribe: %s\n", elem->valuestring);
                    goto cleanup;
                }
            }
        }
    }

    // If S missing, create array
    if (!S || !cJSON_IsArray(S)) {
        S = cJSON_CreateArray();
        cJSON_AddItemToObject(root, "S", S);
    }

    append_broker_to_S(S);

    char *updated_payload = cJSON_PrintUnformatted(root);
    if (updated_payload) {
        printf("[PLUGIN] 🆕 Updated Payload: %s\n", updated_payload);
        int rc = mosquitto_broker_publish_copy(
            NULL,           // broker client
            msg->topic,
            strlen(updated_payload),
            updated_payload,
            msg->qos,
            msg->retain,
            NULL
        );
        if (rc != MOSQ_ERR_SUCCESS) {
            printf("[PLUGIN] ⚠️ Publish failed (rc=%d)\n", rc);
        }
        free(updated_payload);
    }

cleanup:
    cJSON_Delete(root);
    free(payload_copy);
    return MOSQ_ERR_SUCCESS;
}

// Plugin initialization
int mosquitto_plugin_init(mosquitto_plugin_id_t *identifier, void **userdata,
                          struct mosquitto_opt *options, int option_count)
{
    printf("[PLUGIN] ✅ Plugin initialized\n");

    for (int i = 0; i < option_count; i++) {
        if (strcmp(options[i].key, "broker_id") == 0) {
            strncpy(broker_id, options[i].value, sizeof(broker_id) - 1);
            broker_id[sizeof(broker_id) - 1] = '\0';
            printf("[PLUGIN] 🔧 Broker ID set to: %s\n", broker_id);
        }
    }

    return mosquitto_callback_register(identifier, MOSQ_EVT_MESSAGE, on_message, NULL, NULL);
}

// Plugin cleanup
int mosquitto_plugin_cleanup(void *userdata, struct mosquitto_opt *options, int option_count)
{
    printf("[PLUGIN] 🧼 Plugin cleanup complete\n");
    return MOSQ_ERR_SUCCESS;
}

// Plugin API version
int mosquitto_plugin_version(int supported_version_count, const int *supported_versions)
{
    return 5;
}
