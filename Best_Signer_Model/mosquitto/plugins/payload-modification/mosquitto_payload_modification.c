/*
 * Mosquitto Trust-Based Plugin with Best Signer Model and Performance Metrics
 * For Mosquitto 2.x
 *
 * This plugin implements the trust-based data sharing approach by evaluating
 * the trust of the best direct neighbor who has signed the message. It also
 * measures and logs the processing time for each message.
 *
 * Compile with:
 * gcc -I<path_to_mosquitto_headers> -fPIC -shared broker_plugin.c -o broker_plugin.so -lcjson -lssl -lcrypto
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <stdarg.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include "mosquitto_broker.h"
#include "mosquitto_plugin.h"
#include "mosquitto.h"
#include "mqtt_protocol.h"
#include <cjson/cJSON.h>

#define UNUSED(A) (void)(A)

// ---- Static Configuration and State ----
static mosquitto_plugin_id_t *mosq_pid = NULL;
static char broker_id[32] = "DEFAULT_BROKER";
static char hmac_key[128] = "default_hmac_key";
static char trust_store_file[256] = "trust_store.txt";
static char log_file_path[256] = "plugin_log.txt";
static char acl_file_path[256] = "acl.txt";
static FILE *log_fp = NULL;

// ---- ACL Structures and State ----
#define MAX_ACL_RULES 256
typedef struct {
    char client_id[64];
    char access[8];
    char topic[256];
} acl_rule_t;
static acl_rule_t acl_rules[MAX_ACL_RULES];
static int acl_rule_count = 0;

// ---- Trust Model Structures and Parameters ----
#define MAX_NEIGHBORS 32
static const double LOCAL_THRESHOLD_THETA = 0.5;
static const double SIGNING_THRESHOLD_PI = 0.8;
static const double BASE_RATE_DELTA = 0.5;
static const int NEGATIVE_MULTIPLIER_MU = 5;

typedef struct {
    char broker_id[32];
    int r;
    int s;
} trust_entry_t;
static trust_entry_t trust_store[MAX_NEIGHBORS];
static int neighbor_count = 0;

// ---- Duplicate Message Cache ----
#define SEEN_CACHE_SIZE 256
static char seen_messages_cache[SEEN_CACHE_SIZE][128];
static int cache_index = 0;


// ---- Function Prototypes ----
void plugin_log(int level, const char *fmt, ...);
void load_acl_file(const char *filename);
void load_trust_store(const char *filename);
void save_trust_store(const char *filename);
// ... other prototypes ...

// ---- Enhanced Logging Function ----
void plugin_log(int level, const char *fmt, ...) {
    char buf[1024];
    va_list va;
    va_start(va, fmt);
    vsnprintf(buf, sizeof(buf), fmt, va);
    va_end(va);

    mosquitto_log_printf(level, "[PLUGIN][%s] %s", broker_id, buf);

    if (log_fp) {
        time_t now = time(NULL);
        char time_buf[20];
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
        const char *level_str = "INFO";
        if (level == MOSQ_LOG_WARNING) level_str = "WARN";
        else if (level == MOSQ_LOG_ERR) level_str = "ERROR";
        else if (level == MOSQ_LOG_DEBUG) level_str = "DEBUG";
        fprintf(log_fp, "[%s] [%s] %s\n", time_buf, level_str, buf);
        fflush(log_fp);
    }
}


// ---- File I/O for ACL and Trust Store ----
void load_acl_file(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) { plugin_log(MOSQ_LOG_ERR, "[ACL] Could not open ACL file: %s", filename); return; }
    char line[512];
    acl_rule_count = 0;
    while (fgets(line, sizeof(line), fp) && acl_rule_count < MAX_ACL_RULES) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '#' || strlen(line) < 5) continue;
        char *client = strtok(line, ",");
        char *access = strtok(NULL, ",");
        char *topic = strtok(NULL, ",");
        if (client && access && topic) {
            strncpy(acl_rules[acl_rule_count].client_id, client, sizeof(acl_rules[acl_rule_count].client_id)-1);
            strncpy(acl_rules[acl_rule_count].access, access, sizeof(acl_rules[acl_rule_count].access)-1);
            strncpy(acl_rules[acl_rule_count].topic, topic, sizeof(acl_rules[acl_rule_count].topic)-1);
            acl_rule_count++;
        }
    }
    fclose(fp);
    plugin_log(MOSQ_LOG_INFO, "[ACL] Loaded %d ACL rules from %s", acl_rule_count, filename);
}

void save_trust_store(const char *filename) {
    FILE *fp = fopen(filename, "w");
    if (!fp) { plugin_log(MOSQ_LOG_ERR, "[TRUST] Could not open trust store for writing: %s", filename); return; }
    for (int i = 0; i < neighbor_count; i++) {
        fprintf(fp, "%s,%d,%d\n", trust_store[i].broker_id, trust_store[i].r, trust_store[i].s);
    }
    fclose(fp);
    plugin_log(MOSQ_LOG_INFO, "[TRUST] Saved %d trust entries to %s", neighbor_count, filename);
}

void load_trust_store(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) { plugin_log(MOSQ_LOG_INFO, "[TRUST] No trust store found at %s. Starting fresh.", filename); return; }
    char line[512];
    neighbor_count = 0;
    while (fgets(line, sizeof(line), fp) && neighbor_count < MAX_NEIGHBORS) {
        line[strcspn(line, "\r\n")] = 0;
        char *broker = strtok(line, ",");
        char *r_str = strtok(NULL, ",");
        char *s_str = strtok(NULL, ",");
        if (broker && r_str && s_str) {
            strncpy(trust_store[neighbor_count].broker_id, broker, sizeof(trust_store[neighbor_count].broker_id)-1);
            trust_store[neighbor_count].r = atoi(r_str);
            trust_store[neighbor_count].s = atoi(s_str);
            neighbor_count++;
        }
    }
    fclose(fp);
    plugin_log(MOSQ_LOG_INFO, "[TRUST] Loaded %d trust entries from %s", neighbor_count, filename);
}

// ---- Helper functions (string_in_array, hex_encode, compute_hmac) ----
bool string_in_array(cJSON *array, const char *str) {
    if (!cJSON_IsArray(array)) return false;
    cJSON *element = NULL;
    cJSON_ArrayForEach(element, array) {
        if (cJSON_IsString(element) && strcmp(element->valuestring, str) == 0) return true;
    }
    return false;
}
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
    HMAC(EVP_sha256(), hmac_key, (int)strlen(hmac_key), (const unsigned char*)data, data_len, hmac, &hmac_len);
    hex_encode(hmac, hmac_len, hmac_hex_out);
}

// ---- Trust Calculation Function ----
double calculate_trust(int r, int s) {
    if (r < 0 || s < 0) return 0.0;
    double denominator = (double)(r + s + 2);
    double alpha = (double)r / denominator;
    double gamma = 2.0 / denominator;
    return alpha + BASE_RATE_DELTA * gamma;
}

// ---- Trust Feedback Handler ----
void handle_feedback(const char *payload) {
    if (!payload) return;
    cJSON *root = cJSON_Parse(payload);
    if (!root) { plugin_log(MOSQ_LOG_WARNING, "[TRUST] Could not parse feedback JSON."); return; }
    cJSON *origin_item = cJSON_GetObjectItemCaseSensitive(root, "origin_broker");
    cJSON *feedback_item = cJSON_GetObjectItemCaseSensitive(root, "feedback");

    if (!cJSON_IsString(origin_item) || !cJSON_IsString(feedback_item)) {
        plugin_log(MOSQ_LOG_WARNING, "[TRUST] Invalid feedback format.");
        cJSON_Delete(root);
        return;
    }
    const char *neighbor_id = origin_item->valuestring;
    const char *feedback = feedback_item->valuestring;

    int idx = -1;
    for (int i = 0; i < neighbor_count; i++) {
        if (strcmp(trust_store[i].broker_id, neighbor_id) == 0) { idx = i; break; }
    }
    if (idx == -1 && neighbor_count < MAX_NEIGHBORS) {
        idx = neighbor_count++;
        strncpy(trust_store[idx].broker_id, neighbor_id, sizeof(trust_store[idx].broker_id) - 1);
        trust_store[idx].r = 0;
        trust_store[idx].s = 0;
    }

    if (idx != -1) {
        if (strcmp(feedback, "positive") == 0) trust_store[idx].r++;
        else if (strcmp(feedback, "negative") == 0) trust_store[idx].s += NEGATIVE_MULTIPLIER_MU;
        plugin_log(MOSQ_LOG_INFO, "[TRUST] Feedback for %s. New counts: r=%d, s=%d", neighbor_id, trust_store[idx].r, trust_store[idx].s);
        save_trust_store(trust_store_file);
    }
    cJSON_Delete(root);
}

// ---- Trust Evaluation Function ----
double get_best_signer_trust_score(cJSON *signers_array) {
    if (!cJSON_IsArray(signers_array)) return 0.0;
    
    plugin_log(MOSQ_LOG_DEBUG, "[TRUST_EVAL] --- Begin Best Signer Trust Evaluation ---");
    
    double max_trust = 0.0;
    cJSON *signer_item = NULL;
    cJSON_ArrayForEach(signer_item, signers_array) {
        if (cJSON_IsString(signer_item)) {
            const char *signer_id = signer_item->valuestring;
            bool found = false;
            plugin_log(MOSQ_LOG_DEBUG, "[TRUST_EVAL] Evaluating signer: %s", signer_id);

            for (int i = 0; i < neighbor_count; i++) {
                if (strcmp(trust_store[i].broker_id, signer_id) == 0) {
                    plugin_log(MOSQ_LOG_DEBUG, "[TRUST_EVAL]  Found local record for %s with r=%d, s=%d", signer_id, trust_store[i].r, trust_store[i].s);
                    double current_trust = calculate_trust(trust_store[i].r, trust_store[i].s);
                    plugin_log(MOSQ_LOG_DEBUG, "[TRUST_EVAL]  Calculated trust for %s: %.3f", signer_id, current_trust);
                    if (current_trust > max_trust) {
                        max_trust = current_trust;
                    }
                    found = true;
                    break;
                }
            }
            if (!found) {
                double default_trust = calculate_trust(0, 0);
                plugin_log(MOSQ_LOG_DEBUG, "[TRUST_EVAL]  No local record for %s. Using default score: %.3f", signer_id, default_trust);
                if (default_trust > max_trust) {
                    max_trust = default_trust;
                }
            }
        }
    }
    plugin_log(MOSQ_LOG_INFO, "[TRUST_EVAL] --- Final Best Signer Trust: %.3f ---", max_trust);
    return max_trust;
}

// ---- ACL check ----
bool check_permission(const char *client_id, const char *topic, bool is_publish) {
    const char *access = is_publish ? "pub" : "sub";
    for (int i = 0; i < acl_rule_count; i++) {
        if (strcmp(acl_rules[i].client_id, client_id) == 0 && strcmp(acl_rules[i].access, access) == 0 && strcmp(acl_rules[i].topic, topic) == 0) {
            return true;
        }
    }
    plugin_log(MOSQ_LOG_WARNING, "[ACL] ❌ Denied %s: client='%s', topic='%s'", access, client_id, topic);
    return false;
}


// ---- Main Message Callback ----
static int callback_message(int event, void *event_data, void *userdata) {
    // [PERF] Start the timer for broker processing
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    struct mosquitto_evt_message *ed = event_data;
    UNUSED(event);
    UNUSED(userdata);

    if (strcmp(ed->topic, "internal/feedback") == 0) {
        const char *address = mosquitto_client_address(ed->client);
        if (address && (strcmp(address, "127.0.0.1") == 0 || strcmp(address, "localhost") == 0)) {
            char *payload_str = strndup((const char *)ed->payload, ed->payloadlen);
            if (payload_str) { handle_feedback(payload_str); free(payload_str); }
        }
        ed->topic = mosquitto_strdup("internal/feedback/processed");
        return MOSQ_ERR_SUCCESS;
    }

    if (!ed->payload || ed->payloadlen < 2 || ((char *)ed->payload)[0] != '{') {
        return MOSQ_ERR_SUCCESS;
    }
    
    plugin_log(MOSQ_LOG_DEBUG, "--- New Message Received on Topic: %s ---", ed->topic);

    char *payload_copy = strndup((const char *)ed->payload, ed->payloadlen);
    if (!payload_copy) return MOSQ_ERR_NOMEM;

    cJSON *root = cJSON_Parse(payload_copy);
    if (!root) { 
        free(payload_copy); 
        return MOSQ_ERR_SUCCESS; 
    }
    
    // --- 1. HMAC Verification ---
    cJSON *hmac_field = cJSON_GetObjectItemCaseSensitive(root, "hmac");
    if (!hmac_field || !cJSON_IsString(hmac_field)) {
        plugin_log(MOSQ_LOG_WARNING, "[AUTH] ❌ Dropping. Message has no HMAC field.");
        cJSON_Delete(root); free(payload_copy); 
        // [PERF] Log time before exiting
        clock_gettime(CLOCK_MONOTONIC, &end_time);
        double elapsed_ms = (end_time.tv_sec - start_time.tv_sec) * 1000.0 + (end_time.tv_nsec - start_time.tv_nsec) / 1000000.0;
        plugin_log(MOSQ_LOG_INFO, "[PERF] Total processing time for message on topic '%s': %.3f ms (Rejected: No HMAC)", ed->topic, elapsed_ms);
        return MOSQ_ERR_ACL_DENIED;
    }
    char *received_hmac = strdup(hmac_field->valuestring);
    cJSON_DeleteItemFromObject(root, "hmac");
    char *json_str_for_hmac = cJSON_PrintUnformatted(root);
    char computed_hmac[EVP_MAX_MD_SIZE * 2 + 1] = {0};
    compute_hmac(json_str_for_hmac, strlen(json_str_for_hmac), computed_hmac);

    if (strcmp(computed_hmac, received_hmac) != 0) {
        plugin_log(MOSQ_LOG_WARNING, "[AUTH] ❌ Dropping. HMAC verification failed.");
        free(received_hmac); free(json_str_for_hmac); cJSON_Delete(root); free(payload_copy);
        // [PERF] Log time before exiting
        clock_gettime(CLOCK_MONOTONIC, &end_time);
        double elapsed_ms = (end_time.tv_sec - start_time.tv_sec) * 1000.0 + (end_time.tv_nsec - start_time.tv_nsec) / 1000000.0;
        plugin_log(MOSQ_LOG_INFO, "[PERF] Total processing time for message on topic '%s': %.3f ms (Rejected: HMAC Fail)", ed->topic, elapsed_ms);
        return MOSQ_ERR_ACL_DENIED;
    }
    plugin_log(MOSQ_LOG_INFO, "[AUTH] ✅ HMAC verified successfully.");
    free(received_hmac); free(json_str_for_hmac);

    // --- 2. Permission Check ---
    cJSON *c_item = cJSON_GetObjectItemCaseSensitive(root, "c");
    cJSON *Fp_item = cJSON_GetObjectItemCaseSensitive(root, "Fp");
    if (!c_item || !cJSON_IsString(c_item) || !Fp_item || !cJSON_IsArray(Fp_item) || cJSON_GetArrayItem(Fp_item, 0) == NULL) {
        plugin_log(MOSQ_LOG_WARNING, "[AUTH] ❌ Dropping. Malformed AT.");
        cJSON_Delete(root); free(payload_copy); 
        clock_gettime(CLOCK_MONOTONIC, &end_time);
        double elapsed_ms = (end_time.tv_sec - start_time.tv_sec) * 1000.0 + (end_time.tv_nsec - start_time.tv_nsec) / 1000000.0;
        plugin_log(MOSQ_LOG_INFO, "[PERF] Total processing time for message on topic '%s': %.3f ms (Rejected: Malformed AT)", ed->topic, elapsed_ms);
        return MOSQ_ERR_ACL_DENIED;
    }
    if (!check_permission(c_item->valuestring, ed->topic, true)) {
        cJSON_Delete(root); free(payload_copy); 
        clock_gettime(CLOCK_MONOTONIC, &end_time);
        double elapsed_ms = (end_time.tv_sec - start_time.tv_sec) * 1000.0 + (end_time.tv_nsec - start_time.tv_nsec) / 1000000.0;
        plugin_log(MOSQ_LOG_INFO, "[PERF] Total processing time for message on topic '%s': %.3f ms (Rejected: ACL)", ed->topic, elapsed_ms);
        return MOSQ_ERR_ACL_DENIED;
    }
    plugin_log(MOSQ_LOG_INFO, "[AUTH] ✅ Client '%s' has permission for topic '%s'.", c_item->valuestring, ed->topic);

    // --- 3. Trust Evaluation ---
    cJSON *S_item = cJSON_GetObjectItemCaseSensitive(root, "S");
    cJSON *b_item = cJSON_GetObjectItemCaseSensitive(root, "b");
    bool is_local_origin = (b_item && cJSON_IsString(b_item) && strcmp(b_item->valuestring, broker_id) == 0);
    double best_signer_trust = 1.0;

    if (!is_local_origin) {
        best_signer_trust = get_best_signer_trust_score(S_item);
        if (best_signer_trust < LOCAL_THRESHOLD_THETA) {
            plugin_log(MOSQ_LOG_WARNING, "[TRUST] ❌ Dropping. Best signer trust %.3f is below threshold %.3f.", best_signer_trust, LOCAL_THRESHOLD_THETA);
            cJSON_Delete(root); free(payload_copy); 
            clock_gettime(CLOCK_MONOTONIC, &end_time);
            double elapsed_ms = (end_time.tv_sec - start_time.tv_sec) * 1000.0 + (end_time.tv_nsec - start_time.tv_nsec) / 1000000.0;
            plugin_log(MOSQ_LOG_INFO, "[PERF] Total processing time for message on topic '%s': %.3f ms (Rejected: Trust)", ed->topic, elapsed_ms);
            return MOSQ_ERR_ACL_DENIED;
        }
    }
    plugin_log(MOSQ_LOG_INFO, "[TRUST] ✅ Best signer trust score %.3f is acceptable.", best_signer_trust);

    // --- 4. Message Re-signing and Forwarding ---
    if (!string_in_array(S_item, broker_id)) {
        cJSON_AddItemToArray(S_item, cJSON_CreateString(broker_id));
        plugin_log(MOSQ_LOG_INFO, "[FORWARD] Appending own ID '%s' to signers list.", broker_id);
    }

    char *updated_payload_no_hmac = cJSON_PrintUnformatted(root);
    char new_hmac[EVP_MAX_MD_SIZE * 2 + 1] = {0};
    compute_hmac(updated_payload_no_hmac, strlen(updated_payload_no_hmac), new_hmac);
    cJSON_AddStringToObject(root, "hmac", new_hmac);

    char *final_payload = cJSON_PrintUnformatted(root);
    if (final_payload) {
        plugin_log(MOSQ_LOG_DEBUG, "[FORWARD] Replacing payload for forwarding.");
        ed->payload = mosquitto_strdup(final_payload);
        ed->payloadlen = (uint32_t)strlen(final_payload);
        free(final_payload);
    }

    plugin_log(MOSQ_LOG_INFO, "[DELIVERY] ✅ Message approved. Delivering to local clients and forwarding.");

    cJSON_Delete(root);
    free(payload_copy);
    free(updated_payload_no_hmac);

    // [PERF] Stop the timer and log the processing time for an approved message
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double elapsed_ms = (end_time.tv_sec - start_time.tv_sec) * 1000.0 + (end_time.tv_nsec - start_time.tv_nsec) / 1000000.0;
    plugin_log(MOSQ_LOG_INFO, "[PERF] Total processing time for message on topic '%s': %.3f ms (Approved)", ed->topic, elapsed_ms);

    return MOSQ_ERR_SUCCESS;
}


// ---- Plugin Lifecycle Functions ----
int mosquitto_plugin_version(int supported_version_count, const int *supported_versions) {
    for (int i = 0; i < supported_version_count; i++) {
        if (supported_versions[i] == 5) return 5;
    }
    return -1;
}

int mosquitto_plugin_init(mosquitto_plugin_id_t *identifier, void **user_data, struct mosquitto_opt *opts, int opt_count) {
    UNUSED(user_data);
    mosq_pid = identifier;

    for (int i = 0; i < SEEN_CACHE_SIZE; i++) {
        seen_messages_cache[i][0] = '\0';
    }

    for (int i = 0; i < opt_count; i++) {
        if (strcmp(opts[i].key, "broker_id") == 0) strncpy(broker_id, opts[i].value, sizeof(broker_id) - 1);
        if (strcmp(opts[i].key, "acl_file") == 0) strncpy(acl_file_path, opts[i].value, sizeof(acl_file_path) - 1);
        if (strcmp(opts[i].key, "hmac_key") == 0) strncpy(hmac_key, opts[i].value, sizeof(hmac_key) - 1);
        if (strcmp(opts[i].key, "trust_store_file") == 0) strncpy(trust_store_file, opts[i].value, sizeof(trust_store_file) - 1);
        if (strcmp(opts[i].key, "log_file") == 0) strncpy(log_file_path, opts[i].value, sizeof(log_file_path) - 1);
    }
    
    log_fp = fopen(log_file_path, "a");
    if (!log_fp) mosquitto_log_printf(MOSQ_LOG_ERR, "Failed to open custom log file: %s", log_file_path);

    plugin_log(MOSQ_LOG_INFO, "--- Trust-based plugin initializing ---");
    load_acl_file(acl_file_path);
    load_trust_store(trust_store_file);

    return mosquitto_callback_register(mosq_pid, MOSQ_EVT_MESSAGE, callback_message, NULL, NULL);
}

int mosquitto_plugin_cleanup(void *user_data, struct mosquitto_opt *opts, int opt_count) {
    UNUSED(user_data);
    UNUSED(opts);
    UNUSED(opt_count);
    
    plugin_log(MOSQ_LOG_INFO, "--- Trust-based plugin shutting down ---");
    save_trust_store(trust_store_file);
    
    if (log_fp) fclose(log_fp);
    
    return mosquitto_callback_unregister(mosq_pid, MOSQ_EVT_MESSAGE, callback_message, NULL);
}
