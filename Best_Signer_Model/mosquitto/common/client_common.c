#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include "../include/mosquitto.h"
#include "client_common.h"
#include <cjson/cJSON.h>

#define MSG_ID_FILE "msg_id.txt"

// ---- HMAC Key ----
#define HMAC_KEY "4c1c4d7e2b9f7a0e8b6d3e5f1a2c7b4d"  // Must match broker/plugin

// ---- HMAC helpers ----
void hex_encode(const unsigned char *in, size_t len, char *out) {
    static const char hex_digits[] = "0123456789abcdef";
    for (size_t i = 0; i < len; ++i) {
        out[i*2] = hex_digits[(in[i] >> 4) & 0xF];
        out[i*2+1] = hex_digits[in[i] & 0xF];
    }
    out[len*2] = '\0';
}

void compute_hmac(const char *data, size_t data_len, char *hmac_hex_out) {
    unsigned char hmac[EVP_MAX_MD_SIZE];
    unsigned int hmac_len = 0;
    HMAC(EVP_sha256(), HMAC_KEY, strlen(HMAC_KEY), (const unsigned char*)data, data_len, hmac, &hmac_len);
    hex_encode(hmac, hmac_len, hmac_hex_out);
}

// ---- Mosquitto Callbacks ----
void on_connect(struct mosquitto *mosq, void *obj, int rc) 
{
    struct client *cli = (struct client *) obj;
    if (rc == 0) 
    {
        printf("Client %s connected to Broker %s\n", cli->client_id, cli->issuer_broker);

        // Subscribe to the configured topic
        int sub_rc = mosquitto_subscribe(mosq, NULL, cli->subscribe_topic, 0);
        if (sub_rc != MOSQ_ERR_SUCCESS) 
        {
            fprintf(stderr, "Client %s failed to subscribe to %s. Error: %d\n", cli->client_id, cli->subscribe_topic, sub_rc);
        } else 
        {
            printf("Client %s subscribed to %s\n", cli->client_id, cli->subscribe_topic);
        }
    }
    else {
        printf("Connection failed with code %d\n", rc);
    }
}

void on_publish(struct mosquitto *mosq, void *obj, int mid) 
{
    struct client *cli = (struct client *) obj;
    printf("Message published by client %s\n", cli->client_id);
}

void on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg) 
{
    struct client *cli = (struct client *) obj;
    printf("Client %s received message on topic %s: %s\n",
           cli->client_id, msg->topic, (char *)msg->payload);
}

// Load last used msg_id from file, default to 1
int load_last_msg_id() {
    FILE *fp = fopen(MSG_ID_FILE, "r");
    int msg_id = 1;

    if (fp) {
        if (fscanf(fp, "%d", &msg_id) != 1) {
            msg_id = 1;
        }
        fclose(fp);
    }

    return msg_id;
}

// Save new msg_id after publishing
void save_msg_id(int msg_id) {
    FILE *fp = fopen(MSG_ID_FILE, "w");
    if (fp) {
        fprintf(fp, "%d\n", msg_id);
        fclose(fp);
    }
}
// ---- AT Generation with HMAC ----
char* generate_at(struct client *cli)
{
    static char at_with_hmac[2200];
    char hmac_hex[EVP_MAX_MD_SIZE*2+1];
    int msg_id = load_last_msg_id();


    // 1. Build JSON object (no hmac yet)
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "b", cli->issuer_broker);
    cJSON_AddStringToObject(root, "c", cli->client_id);

    cJSON *S = cJSON_CreateArray();
    cJSON_AddItemToArray(S, cJSON_CreateString(cli->issuer_broker));
    cJSON_AddItemToObject(root, "S", S);

    cJSON *Fp = cJSON_CreateArray();
    cJSON_AddItemToArray(Fp, cJSON_CreateString(cli->publish_topic));
    cJSON_AddItemToObject(root, "Fp", Fp);

    cJSON *Fs = cJSON_CreateArray();
    cJSON_AddItemToArray(Fs, cJSON_CreateString(cli->subscribe_topic));
    cJSON_AddItemToObject(root, "Fs", Fs);

    cJSON_AddStringToObject(root, "msg", cli->message);
    cJSON_AddNumberToObject(root, "msg_id", msg_id);

    // 2. Serialize without hmac
    char *json_no_hmac = cJSON_PrintUnformatted(root);

    // 3. Compute HMAC
    compute_hmac(json_no_hmac, strlen(json_no_hmac), hmac_hex);

    // 4. Add hmac field
    cJSON_AddStringToObject(root, "hmac", hmac_hex);

    // 5. Serialize final JSON
    char *final_json = cJSON_PrintUnformatted(root);
    snprintf(at_with_hmac, sizeof(at_with_hmac), "%s", final_json);

    printf("AT Payload Sent: %s\n", at_with_hmac);
    printf("🆔 msg_id = %d\n", msg_id);

    save_msg_id(msg_id + 1);


    // Cleanup
    free(json_no_hmac);
    free(final_json);
    cJSON_Delete(root);

    return at_with_hmac;
}

// ---- Main Client Runner ----
void run_client(struct client *cli) {
    int rc;
    char *broker_address;
    int broker_port;

    // Set the broker address and port based on the client's broker ID
    if (strcmp(cli->issuer_broker, "B0") == 0) {
        broker_address = "localhost";
        broker_port = 1883;
    } else if (strcmp(cli->issuer_broker, "B1") == 0) {
        broker_address = "localhost";
        broker_port = 1884;
    } else if (strcmp(cli->issuer_broker, "B2") == 0) {
        broker_address = "localhost";
        broker_port = 1885;
    } else if (strcmp(cli->issuer_broker, "B3") == 0) {
        broker_address = "localhost";
        broker_port = 1886;
    } else if (strcmp(cli->issuer_broker, "B4") == 0) {
        broker_address = "localhost";
        broker_port = 1887; 
    }
    else if (strcmp(cli->issuer_broker, "B5") == 0) {
        broker_address = "localhost";
        broker_port = 1888;
    }
    else if (strcmp(cli->issuer_broker, "B6") == 0) {
        broker_address = "localhost";
        broker_port = 1889;
    }
    else if (strcmp(cli->issuer_broker, "B7") == 0) {
        broker_address = "localhost";
        broker_port = 1890;
    }
    else {
        fprintf(stderr, "Unknown broker ID: %s\n", cli->issuer_broker);
        return;
    }

    // Initialize the mosquitto library
    mosquitto_lib_init();

    // Create a new Mosquitto client
    cli->mosq = mosquitto_new(cli->client_id, true, cli);
    if (!cli->mosq) {
        fprintf(stderr, "Failed to create Mosquitto client for %s\n", cli->client_id);
        return;
    }

    // Set the callback functions
    mosquitto_connect_callback_set(cli->mosq, on_connect);
    mosquitto_publish_callback_set(cli->mosq, on_publish);
    mosquitto_message_callback_set(cli->mosq, on_message);

    // Connect to the broker with the appropriate address and port
    rc = mosquitto_connect(cli->mosq, broker_address, broker_port, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Unable to connect to broker. Error code: %d\n", rc);
        mosquitto_destroy(cli->mosq);
        mosquitto_lib_cleanup();
        return;
    }

    // Start the network loop
    mosquitto_loop_start(cli->mosq);

    // Wait for a short time to let the subscription happen
    sleep(1);

    // Generate the Authorization Token (AT) with HMAC
    char *at = generate_at(cli);

    // Publish a message with the AT
    rc = mosquitto_publish(cli->mosq, NULL, cli->publish_topic, strlen(at), at, 0, false);
        // [NEW] Print timestamp immediately after publishing
    // [MODIFIED] Use high-precision timer for more accurate logging
    if (rc == MOSQ_ERR_SUCCESS) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        struct tm *t = localtime(&ts.tv_sec);
        char time_buf[128];
        // Format the time down to milliseconds
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", t);
        printf(">>>> Message published at: %s.%03ld\n", time_buf, ts.tv_nsec / 1000000);
    } 
    else 
    {
        fprintf(stderr, "Error publishing message: %d\n", rc);
        at = NULL;
        mosquitto_destroy(cli->mosq);
        mosquitto_lib_cleanup();
        return;
    }
    // Allow enough time to receive messages
    sleep(4);

    // Clean up
    mosquitto_loop_stop(cli->mosq, true);
    mosquitto_destroy(cli->mosq);
    mosquitto_lib_cleanup();

    at = NULL;
}
