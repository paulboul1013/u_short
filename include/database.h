#ifndef URL_SHORTENER_DATABASE_H
#define URL_SHORTENER_DATABASE_H

#include <stddef.h>
#include <stdint.h>

typedef struct database database_t;

typedef enum {
    DATABASE_OK = 0,
    DATABASE_NOT_FOUND,
    DATABASE_INVALID_ARGUMENT,
    DATABASE_BUFFER_TOO_SMALL,
    DATABASE_INTERNAL_ERROR
} database_status_t;

database_status_t database_open(const char *path, database_t **database);
void database_close(database_t *database);
database_status_t database_insert_url(database_t *database,
                                      const char *original_url,
                                      int64_t created_at, int64_t *id);
database_status_t database_find_url(database_t *database, int64_t id,
                                    char *original_url, size_t capacity);

#endif
