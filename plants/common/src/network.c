#include "network.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Appends the response body to a fixed buffer; excess bytes are discarded
 * (the controller reply is a small JSON object). */
static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    controller_client_t *c = userdata;
    size_t n = size * nmemb;
    size_t room = RESPONSE_MAX - 1 - c->response_len;
    size_t copy = n < room ? n : room;

    memcpy(c->response + c->response_len, ptr, copy);
    c->response_len += copy;
    c->response[c->response_len] = '\0';
    return n;
}

/* Extracts the number following "key": in a flat JSON object. */
static int json_get_number(const char *body, const char *key, double *out) {
    const char *p = strstr(body, key);
    if (!p) return 0;
    p = strchr(p + strlen(key), ':');
    if (!p) return 0;

    char *end;
    double v = strtod(p + 1, &end);
    if (end == p + 1) return 0;
    *out = v;
    return 1;
}

/* Extracts the string following "key": in a flat JSON object. */
static void json_get_string(const char *body, const char *key, char *out, size_t out_len) {
    out[0] = '\0';
    const char *p = strstr(body, key);
    if (!p) return;
    p = strchr(p + strlen(key), ':');
    if (!p) return;
    p = strchr(p, '"');
    if (!p) return;
    p++;

    size_t i = 0;
    while (*p && *p != '"' && i + 1 < out_len) out[i++] = *p++;
    out[i] = '\0';
}

int network_global_init(void) {
    return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK ? 0 : -1;
}

void network_global_cleanup(void) {
    curl_global_cleanup();
}

int controller_client_init(controller_client_t *c, const char *url,
                           const char *client_id, int keepalive, long timeout_ms) {
    memset(c, 0, sizeof(*c));

    /* The id is embedded in a JSON string: keep it free of quotes/escapes. */
    snprintf(c->client_id, sizeof(c->client_id), "%s", client_id);
    for (char *p = c->client_id; *p; p++) {
        if (*p == '"' || *p == '\\' || (unsigned char)*p < 0x20) *p = '_';
    }

    c->curl = curl_easy_init();
    if (!c->curl) return -1;

    c->headers = curl_slist_append(NULL, "Content-Type: application/json");

    curl_easy_setopt(c->curl, CURLOPT_URL, url);
    curl_easy_setopt(c->curl, CURLOPT_HTTPHEADER, c->headers);
    curl_easy_setopt(c->curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c->curl, CURLOPT_WRITEDATA, c);
    curl_easy_setopt(c->curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c->curl, CURLOPT_FORBID_REUSE, keepalive ? 0L : 1L);
    if (timeout_ms > 0) {
        curl_easy_setopt(c->curl, CURLOPT_TIMEOUT_MS, timeout_ms);
    }
    return 0;
}

void controller_client_call(controller_client_t *c, double e, double de,
                            unsigned long seq, controller_reply_t *reply) {
    char body[256 + CLIENT_ID_MAX];
    snprintf(body, sizeof(body),
             "{\"error\": %.6f, \"delta_error\": %.6f, \"client_id\": \"%s\", \"seq\": %lu}",
             e, de, c->client_id, seq);

    memset(reply, 0, sizeof(*reply));
    c->response_len = 0;
    c->response[0] = '\0';

    curl_easy_setopt(c->curl, CURLOPT_POSTFIELDS, body);
    CURLcode res = curl_easy_perform(c->curl);
    reply->curl_code = (int)res;

    curl_off_t connect_us = 0, ttfb_us = 0;
    curl_easy_getinfo(c->curl, CURLINFO_RESPONSE_CODE, &reply->http_status);
    curl_easy_getinfo(c->curl, CURLINFO_CONNECT_TIME_T, &connect_us);
    curl_easy_getinfo(c->curl, CURLINFO_STARTTRANSFER_TIME_T, &ttfb_us);
    reply->connect_ms = connect_us / 1000.0;
    reply->first_byte_ms = ttfb_us / 1000.0;

    if (res != CURLE_OK) {
        fprintf(stderr, "Network Error: %s\n", curl_easy_strerror(res));
        return;
    }
    if (reply->http_status != 200) {
        fprintf(stderr, "Controller returned HTTP %ld\n", reply->http_status);
        return;
    }

    reply->ok = json_get_number(c->response, "\"u\"", &reply->u);
    if (!reply->ok) {
        reply->u = 0.0;
        fprintf(stderr, "Malformed controller reply: %s\n", c->response);
    }
    json_get_string(c->response, "\"pod\"", reply->pod, sizeof(reply->pod));
}

void controller_client_cleanup(controller_client_t *c) {
    if (c->curl) curl_easy_cleanup(c->curl);
    if (c->headers) curl_slist_free_all(c->headers);
    c->curl = NULL;
    c->headers = NULL;
}
