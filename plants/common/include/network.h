#ifndef NETWORK_H
#define NETWORK_H

#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

struct Response {
    char *ptr;
    size_t len;
};

size_t writefunc(void *ptr, size_t size, size_t nmemb, struct Response *s);
double call_fuzzy_controller(double error, double delta_error);

#endif