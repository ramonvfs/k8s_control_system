#ifndef NETWORK_H
#define NETWORK_H

/*
 * HTTP client used by the plants to query the remote fuzzy controller.
 *
 * Each plant owns one controller_client_t (one libcurl easy handle). The
 * handle is reused across requests to avoid per-request allocation, but by
 * default a NEW TCP connection is opened for every request
 * (CURLOPT_FORBID_REUSE). This mirrors the original implementation and keeps
 * Kubernetes Service load balancing effective: kube-proxy balances
 * connections, not HTTP requests, so a persistent connection would pin a
 * plant to a single controller replica. Set keepalive = 1 to reuse the
 * connection instead.
 */

#include <curl/curl.h>

#define CLIENT_ID_MAX 128
#define POD_NAME_MAX  128
#define RESPONSE_MAX  512

typedef struct {
    CURL *curl;
    struct curl_slist *headers;
    char client_id[CLIENT_ID_MAX];
    char response[RESPONSE_MAX];
    size_t response_len;
} controller_client_t;

/* Result of one control request. */
typedef struct {
    int ok;                  /* 1 if transport succeeded, HTTP 200 and "u" parsed */
    double u;                /* controller output (increment); 0.0 when !ok */
    char pod[POD_NAME_MAX];  /* replica that served the request ("" if unknown) */
    long http_status;        /* HTTP status code, 0 if no response */
    int curl_code;           /* CURLcode of the transfer (0 = CURLE_OK) */
    double connect_ms;       /* time until TCP connect completed (0 if reused) */
    double first_byte_ms;    /* time until first response byte (TTFB) */
} controller_reply_t;

/* Must be called once, from main(), before any thread is created. */
int network_global_init(void);
void network_global_cleanup(void);

/*
 * url        : full controller endpoint, e.g. http://host:30005/control
 * client_id  : identifier sent with every request (logged by the controller)
 * keepalive  : 0 = new TCP connection per request, 1 = reuse connection
 * timeout_ms : total request timeout, 0 = no timeout (libcurl default)
 */
int controller_client_init(controller_client_t *c, const char *url,
                           const char *client_id, int keepalive, long timeout_ms);

/* Sends {"error": e, "delta_error": de, "client_id": ..., "seq": seq}. */
void controller_client_call(controller_client_t *c, double e, double de,
                            unsigned long seq, controller_reply_t *reply);

void controller_client_cleanup(controller_client_t *c);

#endif
