#ifndef URL_SHORTENER_SHORTENER_H
#define URL_SHORTENER_SHORTENER_H

#include "database.h"

#include <stddef.h>

#define SHORTENER_MAX_URL_LENGTH 2048U
#define SHORTENER_MAX_SHORT_CODE_LENGTH 11U
#define SHORTENER_SHORT_CODE_BUFFER_SIZE \
    (SHORTENER_MAX_SHORT_CODE_LENGTH + 1U)
#define SHORTENER_BASE_URL "http://localhost:8080"
#define SHORTENER_MAX_SHORT_URL_LENGTH \
    (sizeof(SHORTENER_BASE_URL) - 1U + 1U + SHORTENER_MAX_SHORT_CODE_LENGTH)
#define SHORTENER_SHORT_URL_BUFFER_SIZE (SHORTENER_MAX_SHORT_URL_LENGTH + 1U)

typedef struct {
    database_t *database;
} shortener_t;

typedef enum {
    SHORTENER_OK = 0,
    SHORTENER_INVALID,
    SHORTENER_NOT_FOUND,
    SHORTENER_BUFFER_TOO_SMALL,
    SHORTENER_INTERNAL_ERROR
} shortener_status_t;

shortener_status_t shortener_init(shortener_t *shortener,
                                  database_t *database);
shortener_status_t shortener_create(shortener_t *shortener,
                                    const char *original_url, char *short_code,
                                    size_t short_code_capacity,
                                    char *short_url,
                                    size_t short_url_capacity);
shortener_status_t shortener_resolve(shortener_t *shortener,
                                     const char *short_code,
                                     char *original_url,
                                     size_t original_url_capacity);

#endif
