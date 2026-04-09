#include "network.h"

size_t writefunc(void *ptr, size_t size, size_t nmemb, struct Response *s) {
    size_t new_len = s->len + size * nmemb;
    s->ptr = realloc(s->ptr, new_len + 1);
    memcpy(s->ptr + s->len, ptr, size * nmemb);
    s->ptr[new_len] = '\0';
    s->len = new_len;
    return size * nmemb;
}

double call_fuzzy_controller(double error, double delta_error) {
    CURL *curl;
    CURLcode res;
    double control_signal = 0.0;
    char *url = getenv("CONTROLLER_URL");

    curl = curl_easy_init();
    if(curl) {
        struct Response s;
        s.len = 0;
        s.ptr = malloc(s.len + 1);
        s.ptr[0] = '\0';

        char json_data[100];
        sprintf(json_data, "{\"error\": %.4f, \"delta_error\": %.4f}", error, delta_error);

        // Service URL
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data);
        
        // JSON headers
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);

        res = curl_easy_perform(curl);

        if(res == CURLE_OK) {
            sscanf(s.ptr, "{\"u\": %lf}", &control_signal);
        } else {
            fprintf(stderr, "Network Error: %s\n", curl_easy_strerror(res));
        }

        free(s.ptr);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    return control_signal;
}