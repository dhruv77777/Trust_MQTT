/*
 * Mosquitto Trust-Based Plugin with Least-Trustworthy Path Model
 * Version 4.1: Simplified for Independent Aggregator (Full Code with Logs)
 *
 * This version removes all broadcasting and shared map update logic.
 * Its only responsibilities are to:
 * 1. Evaluate messages based on the network map.
 * 2. Update its own local trust store upon receiving feedback.
 * 3. Provide detailed logs for all major operations.
 *
 * It is designed to be used with the external, independent aggregator script.
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
#include <cjson/cJSON.h>
#include <math.h>

#include <unistd.h>
#include <sys/file.h> 

#include "mosquitto_broker.h"
#include "mosquitto_plugin.h"
#include "mosquitto.h"
#include "mqtt_protocol.h"

#define UNUSED(A) (void)(A)

// ---- Static Configuration and State ----
static mosquitto_plugin_id_t *mosq_pid = NULL;
static char broker_id[32] = "DEFAULT_BROKER";
static char hmac_key[128] = "default_hmac_key";
static char acl_file_path[256] = "acl.txt";
static char log_file_path[256] = "plugin_log.txt";
static char network_map_file[256] = "/home/dhruv/winshare/Neighbour_Signer_Model/mosquitto/plugins/payload-modification/network_map.txt";
static char trust_store_template[256] = "/home/dhruv/winshare/Neighbour_Signer_Model/mosquitto/plugins/payload-modification/trust_history/trust_store_%s.txt";

static FILE *log_fp = NULL;
static time_t last_map_refresh_time = 0;
static const int MAP_REFRESH_INTERVAL_SECONDS = 10; // Set to 10 seconds

// ---- Graph Representation & Trust Model ----
#define MAX_NODES_IN_GRAPH 32
#define MAX_LINKS_PER_NODE 8
typedef struct { char target_broker_id[32]; int r; int s; } trust_link_t;
typedef struct { char broker_id[32]; trust_link_t links[MAX_LINKS_PER_NODE]; int link_count; } network_node_t;
static network_node_t network_graph[MAX_NODES_IN_GRAPH];
static int node_count = 0;
static const double LOCAL_THRESHOLD_THETA = 0.5;
static const double BASE_RATE_DELTA = 0.5;
static const int NEGATIVE_MULTIPLIER_MU = 5;

// ---- ACL Structures ----
#define MAX_ACL_RULES 256
typedef struct { char client_id[64]; char access[8]; char topic[256]; } acl_rule_t;
static acl_rule_t acl_rules[MAX_ACL_RULES];
static int acl_rule_count = 0;

// ---- Function Prototypes ----
void plugin_log(int level, const char *fmt, ...);
void load_acl_file(const char *filename);
void load_network_graph(const char *filename);
double calculate_trust(int r, int s);
void save_local_trust_store(void);
bool load_local_trust_store(void);
static int find_node_index(const char* broker_id);
void compute_hmac(const char *data, size_t data_len, char *hmac_hex_out);
bool check_permission(const char *client_id, const char *topic, bool is_publish);
bool string_in_array(cJSON *array, const char *str);
double get_least_trustworthy_path_score(const char *start_id, const char *end_id);
void handle_feedback(const char *payload);
static int callback_message(int event, void *event_data, void *userdata);

// =================================================================================
// CORE HELPER AND LOGIC FUNCTIONS
// =================================================================================

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

static int find_node_index(const char* broker_id) {
    for (int i = 0; i < node_count; i++) {
        if (strcmp(network_graph[i].broker_id, broker_id) == 0) return i;
    }
    return -1;
}

double calculate_trust(int r, int s) {
    if (r < 0 || s < 0) return 0.0;
    double denominator = (double)(r + s + 2);
    double alpha = (double)r / denominator;
    double gamma = 2.0 / denominator;
    return alpha + BASE_RATE_DELTA * gamma;
}

void save_local_trust_store(void) {
    char local_store_path[512];
    snprintf(local_store_path, sizeof(local_store_path), trust_store_template, broker_id);
    FILE *fp = fopen(local_store_path, "w");
    if (!fp) {
        plugin_log(MOSQ_LOG_ERR, "[PERSIST] Could not open local trust store for writing: %s", local_store_path);
        return;
    }
    fprintf(fp, "# Local trust data held by %s\n# Format: source_broker,r_value,s_value\n", broker_id);
    for (int i = 0; i < node_count; i++) {
        for (int j = 0; j < network_graph[i].link_count; j++) {
            if (strcmp(network_graph[i].links[j].target_broker_id, broker_id) == 0) {
                fprintf(fp, "%s,%d,%d\n", network_graph[i].broker_id, network_graph[i].links[j].r, network_graph[i].links[j].s);
            }
        }
    }
    fclose(fp);
}

bool load_local_trust_store(void) {
    char local_store_path[512];
    snprintf(local_store_path, sizeof(local_store_path), trust_store_template, broker_id);
    FILE *fp = fopen(local_store_path, "r");
    if (!fp) { return false; }
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#') continue;
        char *source_id = strtok(line, ",");
        char *r_str = strtok(NULL, ",");
        char *s_str = strtok(NULL, ",");
        if (source_id && r_str && s_str) {
            int source_idx = find_node_index(source_id);
            if (source_idx == -1) continue;
            for (int i = 0; i < network_graph[source_idx].link_count; i++) {
                if (strcmp(network_graph[source_idx].links[i].target_broker_id, broker_id) == 0) {
                    network_graph[source_idx].links[i].r = atoi(r_str);
                    network_graph[source_idx].links[i].s = atoi(s_str);
                    break;
                }
            }
        }
    }
    fclose(fp);
    return true;
}

static void find_paths_recursive(int current_idx, int end_idx,
                                 double current_path_sum, int path_len,
                                 bool visited[], double* min_avg_trust, int path_indices[]) {
    path_indices[path_len] = current_idx;
    visited[current_idx] = true;

    if (current_idx == end_idx) {
        double current_path_avg = (path_len > 0) ? current_path_sum / path_len : 1.0;
        
        char path_str[512] = {0}, temp_buf[64];
        for (int i = 0; i < path_len; i++) {
            sprintf(temp_buf, "%s -> ", network_graph[path_indices[i]].broker_id);
            strcat(path_str, temp_buf);
        }
        sprintf(temp_buf, "%s", network_graph[end_idx].broker_id);
        strcat(path_str, temp_buf);

        plugin_log(MOSQ_LOG_DEBUG, "[PATH_FIND] \tFound Path: %s", path_str);
        plugin_log(MOSQ_LOG_DEBUG, "[PATH_FIND] \tCalculation: (Sum: %.3f / Links: %d) = Avg: %.3f", current_path_sum, path_len, current_path_avg);

        if (current_path_avg < *min_avg_trust) {
            plugin_log(MOSQ_LOG_DEBUG, "[PATH_FIND] \tNew minimum found! Updating from %.3f to %.3f", *min_avg_trust, current_path_avg);
            *min_avg_trust = current_path_avg;
        }
        
        visited[current_idx] = false;
        return;
    }

    for (int i = 0; i < network_graph[current_idx].link_count; i++) {
        int neighbor_idx = find_node_index(network_graph[current_idx].links[i].target_broker_id);
        if (neighbor_idx != -1 && !visited[neighbor_idx]) {
            double link_trust = calculate_trust(network_graph[current_idx].links[i].r, network_graph[current_idx].links[i].s);
            find_paths_recursive(neighbor_idx, end_idx, current_path_sum + link_trust, path_len + 1, visited, min_avg_trust, path_indices);
        }
    }
    visited[current_idx] = false;
}

/**
 * Finds the direct trust score from a specific source broker to the target (current) broker.
 */
double get_direct_trust_score(const char *source_id, const char *target_id) {
    plugin_log(MOSQ_LOG_DEBUG, "[TRUST] Looking up direct trust from '%s' to '%s'", source_id, target_id);
    
    int source_idx = find_node_index(source_id);
    if (source_idx == -1) {
        plugin_log(MOSQ_LOG_INFO, "[TRUST] No node entry found for source '%s'. Returning default trust.", source_id);
        return calculate_trust(0, 0); // Return default trust for an unknown broker
    }

    // Find the specific link from the source to the target
    for (int i = 0; i < network_graph[source_idx].link_count; i++) {
        if (strcmp(network_graph[source_idx].links[i].target_broker_id, target_id) == 0) {
            trust_link_t *link = &network_graph[source_idx].links[i];
            double direct_trust = calculate_trust(link->r, link->s);
            plugin_log(MOSQ_LOG_DEBUG, "[TRUST] Found link from '%s'. r=%d, s=%d. Direct trust is %.3f", 
                       source_id, link->r, link->s, direct_trust);
            return direct_trust;
        }
    }

    plugin_log(MOSQ_LOG_INFO, "[TRUST] Node for '%s' exists, but no direct link to '%s' found. Returning default trust.", source_id, target_id);
    return calculate_trust(0, 0); // Return default if no direct link exists
}

double get_least_trustworthy_path_score(const char *start_id, const char *end_id) {
    int start_idx = find_node_index(start_id);
    int end_idx = find_node_index(end_id);
    if (start_idx == -1 || end_idx == -1) return 0.0;
    if (start_idx == end_idx) return 1.0;

    bool visited[MAX_NODES_IN_GRAPH] = {false};
    int path_indices[MAX_NODES_IN_GRAPH];
    double min_avg_trust = 1.1;
    
    plugin_log(MOSQ_LOG_DEBUG, "[PATH_FIND] Searching for paths from %s to %s...", start_id, end_id);
    find_paths_recursive(start_idx, end_idx, 0.0, 0, visited, &min_avg_trust, path_indices);

    if (min_avg_trust > 1.0) {
        plugin_log(MOSQ_LOG_INFO, "[PATH_FIND] No path found from '%s' to '%s'.", start_id, end_id);
        return 0.0;
    }
    
    plugin_log(MOSQ_LOG_DEBUG, "[PATH_FIND] Final least-trustworthy path score for %s -> %s is %.3f", start_id, end_id, min_avg_trust);
    return min_avg_trust;
}

void load_network_graph(const char *map_filename) {
    FILE *map_fp = fopen(map_filename, "r");
    if (!map_fp) { return; }
    memset(network_graph, 0, sizeof(network_graph));
    node_count = 0;
    char line[512];
    while (fgets(line, sizeof(line), map_fp)) {
        if (line[0] == '#') continue;
        char temp_line[512]; strcpy(temp_line, line);
        char *source_id = strtok(temp_line, ",");
        char *dest_id = strtok(NULL, ",");
        if (source_id && find_node_index(source_id) == -1) { strncpy(network_graph[node_count++].broker_id, source_id, 31); }
        if (dest_id && find_node_index(dest_id) == -1) { strncpy(network_graph[node_count++].broker_id, dest_id, 31); }
    }
    fseek(map_fp, 0, SEEK_SET);
    while (fgets(line, sizeof(line), map_fp)) {
        if (line[0] == '#') continue;
        char *source_id = strtok(line, ",");
        char *dest_id = strtok(NULL, ",");
        char *trust_str = strtok(NULL, ",");
        if (source_id && dest_id && trust_str) {
            int source_idx = find_node_index(source_id);
            if (source_idx != -1 && network_graph[source_idx].link_count < MAX_LINKS_PER_NODE) {
                trust_link_t *link = &network_graph[source_idx].links[network_graph[source_idx].link_count++];
                strncpy(link->target_broker_id, dest_id, 31);
                double static_trust = atof(trust_str);
                if (static_trust > 0.5) {
                    link->r = (int)round((2 * static_trust - 1) / (1 - static_trust));
                    link->s = 0;
                } else {
                    link->r = 0;
                    link->s = (static_trust > 0) ? (int)round((1.0 / static_trust) - 2.0) : 99;
                }
            }
        }
    }
    fclose(map_fp);
}

void load_acl_file(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) { return; }
    char line[512];
    acl_rule_count = 0;
    while (fgets(line, sizeof(line), fp) && acl_rule_count < MAX_ACL_RULES) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '#' || strlen(line) < 5) continue;
        char *client = strtok(line, ",");
        char *access = strtok(NULL, ",");
        char *topic = strtok(NULL, ",");
        if (client && access && topic) {
            strncpy(acl_rules[acl_rule_count].client_id, client, sizeof(acl_rules[0].client_id)-1);
            strncpy(acl_rules[acl_rule_count].access, access, sizeof(acl_rules[0].access)-1);
            strncpy(acl_rules[acl_rule_count].topic, topic, sizeof(acl_rules[0].topic)-1);
            acl_rule_count++;
        }
    }
    fclose(fp);
}

void compute_hmac(const char *data, size_t data_len, char *hmac_hex_out) {
    unsigned char hmac[EVP_MAX_MD_SIZE];
    unsigned int hmac_len = 0;
    HMAC(EVP_sha256(), hmac_key, (int)strlen(hmac_key), (const unsigned char*)data, data_len, hmac, &hmac_len);
    static const char hex_digits[] = "0123456789abcdef";
    for (size_t i = 0; i < hmac_len; ++i) {
        hmac_hex_out[i*2] = hex_digits[(hmac[i] >> 4) & 0xF];
        hmac_hex_out[i*2+1] = hex_digits[hmac[i] & 0xF];
    }
    hmac_hex_out[hmac_len*2] = '\0';
}

bool check_permission(const char *client_id, const char *topic, bool is_publish) {
    const char *access = is_publish ? "pub" : "sub";
    for (int i = 0; i < acl_rule_count; i++) {
        if (strcmp(acl_rules[i].client_id, client_id) == 0 && strcmp(acl_rules[i].access, access) == 0 && strcmp(acl_rules[i].topic, topic) == 0) {
            return true;
        }
    }
    return false;
}

bool string_in_array(cJSON *array, const char *str) {
    if (!cJSON_IsArray(array)) return false;
    cJSON *element;
    cJSON_ArrayForEach(element, array) {
        if (cJSON_IsString(element) && strcmp(element->valuestring, str) == 0) return true;
    }
    return false;
}

void handle_feedback(const char *payload) {
    if (!payload) return;
    cJSON *root = cJSON_Parse(payload);
    if (!root) { return; }
    cJSON *source_item = cJSON_GetObjectItemCaseSensitive(root, "source");
    cJSON *target_item = cJSON_GetObjectItemCaseSensitive(root, "target");
    cJSON *feedback_item = cJSON_GetObjectItemCaseSensitive(root, "feedback");
    if (!cJSON_IsString(source_item) || !cJSON_IsString(target_item) || !cJSON_IsString(feedback_item)) {
        cJSON_Delete(root); return;
    }
    const char *source_id = source_item->valuestring;
    const char *target_id = target_item->valuestring;
    const char *feedback = feedback_item->valuestring;
    if (strcmp(target_id, broker_id) != 0) { cJSON_Delete(root); return; }
    int source_idx = find_node_index(source_id);
    if (source_idx == -1) { cJSON_Delete(root); return; }
    trust_link_t* target_link = NULL;
    for (int i = 0; i < network_graph[source_idx].link_count; i++) {
        if (strcmp(network_graph[source_idx].links[i].target_broker_id, target_id) == 0) {
            target_link = &network_graph[source_idx].links[i];
            break;
        }
    }
    if (!target_link) { cJSON_Delete(root); return; }
    if (strcmp(feedback, "positive") == 0) { target_link->r++; } 
    else if (strcmp(feedback, "negative") == 0) { target_link->s += NEGATIVE_MULTIPLIER_MU; }
    
    plugin_log(MOSQ_LOG_INFO, "[TRUST] Feedback for link %s->%s. New counts: r=%d, s=%d. Link trust now %.3f",
              source_id, target_id, target_link->r, target_link->s, calculate_trust(target_link->r, target_link->s));

    cJSON_Delete(root);
    save_local_trust_store();
}

/**
 * This callback runs periodically (every second) and triggers a refresh
 * of the network map from disk if the interval has passed.
 */
static int callback_tick(int event, void *event_data, void *userdata) {
    UNUSED(event);
    UNUSED(event_data);
    UNUSED(userdata);

    time_t current_time = time(NULL);
    if (current_time - last_map_refresh_time >= MAP_REFRESH_INTERVAL_SECONDS) {
        plugin_log(MOSQ_LOG_DEBUG, "[REFRESH] Periodic map refresh triggered.");
        
        // The order is important: load the global map first, then
        // overwrite it with our specific, authoritative local knowledge.
        load_network_graph(network_map_file);
        load_local_trust_store();
        
        last_map_refresh_time = current_time;
    }
    return MOSQ_ERR_SUCCESS;
}

static int callback_message(int event, void *event_data, void *userdata) 
{
    struct mosquitto_evt_message *ed = event_data;
    UNUSED(event); UNUSED(userdata);

    if (strcmp(ed->topic, "internal/feedback") == 0) {
        char *payload_str = strndup((const char *)ed->payload, ed->payloadlen);
        if (payload_str) { handle_feedback(payload_str); free(payload_str); }
        ed->topic = mosquitto_strdup("internal/feedback/processed");
        return MOSQ_ERR_SUCCESS;
    }

    if (!ed->payload || ed->payloadlen < 2 || ((char *)ed->payload)[0] != '{') {
        return MOSQ_ERR_SUCCESS;
    }
    
    char *payload_copy = strndup((const char *)ed->payload, ed->payloadlen);
    if (!payload_copy) return MOSQ_ERR_NOMEM;

    cJSON *root = cJSON_Parse(payload_copy);
    if (!root) { free(payload_copy); return MOSQ_ERR_SUCCESS; }
    
    cJSON *hmac_field = cJSON_GetObjectItemCaseSensitive(root, "hmac");
    if (!hmac_field || !cJSON_IsString(hmac_field)) {
        cJSON_Delete(root); free(payload_copy); return MOSQ_ERR_ACL_DENIED;
    }
    char *received_hmac = strdup(hmac_field->valuestring);
    cJSON_DeleteItemFromObject(root, "hmac");
    char *json_str_for_hmac = cJSON_PrintUnformatted(root);
    char computed_hmac[EVP_MAX_MD_SIZE * 2 + 1] = {0};
    compute_hmac(json_str_for_hmac, strlen(json_str_for_hmac), computed_hmac);

    if (strcmp(computed_hmac, received_hmac) != 0) {
        free(received_hmac); free(json_str_for_hmac); cJSON_Delete(root); free(payload_copy);
        return MOSQ_ERR_ACL_DENIED;
    }
    free(received_hmac); free(json_str_for_hmac);

    cJSON *c_item = cJSON_GetObjectItemCaseSensitive(root, "c");
    if (!c_item || !cJSON_IsString(c_item) || !check_permission(c_item->valuestring, ed->topic, true)) {
        cJSON_Delete(root); free(payload_copy); 
        return MOSQ_ERR_ACL_DENIED;
    }

    cJSON *S_item = cJSON_GetObjectItemCaseSensitive(root, "S");
    cJSON *b_item = cJSON_GetObjectItemCaseSensitive(root, "b");
    bool is_local_origin = (b_item && cJSON_IsString(b_item) && strcmp(b_item->valuestring, broker_id) == 0);

    if (!is_local_origin) {
        char signers_str[512] = {0};
        cJSON *signer_log_item;
        cJSON_ArrayForEach(signer_log_item, S_item) {
            if (cJSON_IsString(signer_log_item)) {
                strcat(signers_str, signer_log_item->valuestring);
                strcat(signers_str, " ");
            }
        }
        plugin_log(MOSQ_LOG_INFO, "[TRUST] Evaluating message with signers: [ %s]", signers_str);

        plugin_log(MOSQ_LOG_INFO, "[TRUST] Evaluating message based on last signer only.");

bool is_accepted = false;
const char* last_signer_id = NULL;

// Get the last signer from the S array
int signer_count = cJSON_GetArraySize(S_item);
if (signer_count > 0) {
    cJSON *last_signer_item = cJSON_GetArrayItem(S_item, signer_count - 1);
    if (cJSON_IsString(last_signer_item)) {
        last_signer_id = last_signer_item->valuestring;
    }
}

if (last_signer_id) {
    // Get the direct trust score of the last signer
    double direct_trust = get_direct_trust_score(last_signer_id, broker_id);
    
    // Compare the direct trust against the threshold
    if (direct_trust >= LOCAL_THRESHOLD_THETA) {
        plugin_log(MOSQ_LOG_INFO, "[TRUST] ✅ Last signer '%s' is trusted (%.3f >= %.3f). Accepting message.", 
                   last_signer_id, direct_trust, LOCAL_THRESHOLD_THETA);
        is_accepted = true;
    } else {
        plugin_log(MOSQ_LOG_INFO, "[TRUST] ❌ Last signer '%s' is not trusted (%.3f < %.3f).", 
                   last_signer_id, direct_trust, LOCAL_THRESHOLD_THETA);
    }
} else {
    plugin_log(MOSQ_LOG_WARNING, "[TRUST] ❌ Message has no signers to evaluate.");
}

if (!is_accepted) {
    plugin_log(MOSQ_LOG_WARNING, "[TRUST] ❌ Dropping message. Last signer check failed.");
    cJSON_Delete(root); 
    free(payload_copy); 
    return MOSQ_ERR_ACL_DENIED;
}
    }

    if (!string_in_array(S_item, broker_id)) {
        cJSON_AddItemToArray(S_item, cJSON_CreateString(broker_id));
    }

    char *updated_payload_no_hmac = cJSON_PrintUnformatted(root);
    char new_hmac[EVP_MAX_MD_SIZE * 2 + 1] = {0};
    compute_hmac(updated_payload_no_hmac, strlen(updated_payload_no_hmac), new_hmac);
    cJSON_AddStringToObject(root, "hmac", new_hmac);

    char *final_payload = cJSON_PrintUnformatted(root);
    if (final_payload) {
        ed->payload = mosquitto_strdup(final_payload);
        ed->payloadlen = (uint32_t)strlen(final_payload);
        free(final_payload);
    }

    cJSON_Delete(root);
    free(payload_copy);
    free(updated_payload_no_hmac);

    return MOSQ_ERR_SUCCESS;
}

// =================================================================================
// PLUGIN LIFECYCLE FUNCTIONS (SIMPLIFIED)
// =================================================================================

int mosquitto_plugin_version(int supported_version_count, const int *supported_versions) {
    for (int i = 0; i < supported_version_count; i++) {
        if (supported_versions[i] == 5) return 5;
    }
    return -1;
}

int mosquitto_plugin_init(mosquitto_plugin_id_t *identifier, void **user_data, struct mosquitto_opt *opts, int opt_count) {
    UNUSED(user_data);
    mosq_pid = identifier;

    for (int i = 0; i < opt_count; i++) {
        if (strcmp(opts[i].key, "broker_id") == 0) strncpy(broker_id, opts[i].value, sizeof(broker_id) - 1);
        if (strcmp(opts[i].key, "acl_file") == 0) strncpy(acl_file_path, opts[i].value, sizeof(acl_file_path) - 1);
        if (strcmp(opts[i].key, "hmac_key") == 0) strncpy(hmac_key, opts[i].value, sizeof(hmac_key) - 1);
        if (strcmp(opts[i].key, "log_file") == 0) strncpy(log_file_path, opts[i].value, sizeof(log_file_path) - 1);
    }
    
    log_fp = fopen(log_file_path, "a");
    
    plugin_log(MOSQ_LOG_INFO, "--- Trust-based plugin initializing (V4.1 - Standalone Mode w/Logs) ---");
    load_acl_file(acl_file_path);
    load_network_graph(network_map_file);
    load_local_trust_store();
    
    mosquitto_callback_register(mosq_pid, MOSQ_EVT_MESSAGE, callback_message, NULL, NULL);
    mosquitto_callback_register(mosq_pid, MOSQ_EVT_TICK, callback_tick, NULL, NULL);

    
    plugin_log(MOSQ_LOG_INFO, "[INIT] ✅ Plugin initialization complete.");
    return MOSQ_ERR_SUCCESS;
}

int mosquitto_plugin_cleanup(void *user_data, struct mosquitto_opt *opts, int opt_count) {
    UNUSED(user_data); UNUSED(opts); UNUSED(opt_count);
    
    plugin_log(MOSQ_LOG_INFO, "--- Trust-based plugin shutting down ---");
    save_local_trust_store();
    
    if (log_fp) { fclose(log_fp); }
    
    mosquitto_callback_unregister(mosq_pid, MOSQ_EVT_MESSAGE, callback_message, NULL);
    mosquitto_callback_unregister(mosq_pid, MOSQ_EVT_TICK, callback_tick, NULL);

    
    return MOSQ_ERR_SUCCESS;
}
