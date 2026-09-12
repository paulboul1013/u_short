#ifndef URL_SHORTENER_ROUTER_H
#define URL_SHORTENER_ROUTER_H

#include "http.h"
#include "shortener.h"

void router_handle(const http_request_t *request, shortener_t *shortener,
                   http_response_t *response);

#endif
