#include "database.h"

#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>

struct database {
    sqlite3 *connection;
};

database_status_t database_open(const char *path, database_t **database) {
    static const char schema_sql[] =
        "CREATE TABLE IF NOT EXISTS urls ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "original_url TEXT NOT NULL,"
        "created_at INTEGER NOT NULL"
        ");";
    database_t *opened_database = NULL;
    int sqlite_status = SQLITE_OK;

    if (path == NULL || database == NULL) {
        return DATABASE_INVALID_ARGUMENT;
    }
    *database = NULL;

    opened_database = malloc(sizeof(*opened_database));
    if (opened_database == NULL) {
        return DATABASE_INTERNAL_ERROR;
    }
    opened_database->connection = NULL;

    sqlite_status = sqlite3_open_v2(path, &opened_database->connection,
                                    SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                                    NULL);
    if (sqlite_status != SQLITE_OK) {
        if (opened_database->connection != NULL) {
            (void)sqlite3_close(opened_database->connection);
        }
        free(opened_database);
        return DATABASE_INTERNAL_ERROR;
    }

    sqlite_status = sqlite3_exec(opened_database->connection, schema_sql, NULL,
                                 NULL, NULL);
    if (sqlite_status != SQLITE_OK) {
        (void)sqlite3_close(opened_database->connection);
        free(opened_database);
        return DATABASE_INTERNAL_ERROR;
    }

    *database = opened_database;
    return DATABASE_OK;
}

void database_close(database_t *database) {
    if (database == NULL) {
        return;
    }

    (void)sqlite3_close(database->connection);
    free(database);
}

database_status_t database_insert_url(database_t *database,
                                      const char *original_url,
                                      int64_t created_at, int64_t *id) {
    static const char insert_sql[] =
        "INSERT INTO urls (original_url, created_at) VALUES (?1, ?2);";
    sqlite3_stmt *statement = NULL;
    sqlite3_int64 inserted_id = 0;
    int sqlite_status = SQLITE_OK;

    if (database == NULL || original_url == NULL || id == NULL) {
        return DATABASE_INVALID_ARGUMENT;
    }

    sqlite_status = sqlite3_prepare_v2(database->connection, insert_sql, -1,
                                       &statement, NULL);
    if (sqlite_status != SQLITE_OK) {
        return DATABASE_INTERNAL_ERROR;
    }

    sqlite_status = sqlite3_bind_text(statement, 1, original_url, -1,
                                      SQLITE_TRANSIENT);
    if (sqlite_status == SQLITE_OK) {
        sqlite_status = sqlite3_bind_int64(statement, 2, created_at);
    }
    if (sqlite_status == SQLITE_OK) {
        sqlite_status = sqlite3_step(statement);
    }
    if (sqlite_status != SQLITE_DONE) {
        (void)sqlite3_finalize(statement);
        return DATABASE_INTERNAL_ERROR;
    }

    inserted_id = sqlite3_last_insert_rowid(database->connection);
    sqlite_status = sqlite3_finalize(statement);
    if (sqlite_status != SQLITE_OK || inserted_id <= 0) {
        return DATABASE_INTERNAL_ERROR;
    }

    *id = (int64_t)inserted_id;
    return DATABASE_OK;
}

database_status_t database_find_url(database_t *database, int64_t id,
                                    char *original_url, size_t capacity) {
    static const char find_sql[] =
        "SELECT original_url FROM urls WHERE id = ?1;";
    sqlite3_stmt *statement = NULL;
    const unsigned char *stored_url = NULL;
    size_t stored_url_length = 0;
    int sqlite_status = SQLITE_OK;
    int stored_bytes = 0;

    if (database == NULL || id <= 0 || original_url == NULL || capacity == 0) {
        return DATABASE_INVALID_ARGUMENT;
    }

    sqlite_status = sqlite3_prepare_v2(database->connection, find_sql, -1,
                                       &statement, NULL);
    if (sqlite_status != SQLITE_OK) {
        return DATABASE_INTERNAL_ERROR;
    }

    sqlite_status = sqlite3_bind_int64(statement, 1, id);
    if (sqlite_status != SQLITE_OK) {
        (void)sqlite3_finalize(statement);
        return DATABASE_INTERNAL_ERROR;
    }

    sqlite_status = sqlite3_step(statement);
    if (sqlite_status == SQLITE_DONE) {
        (void)sqlite3_finalize(statement);
        return DATABASE_NOT_FOUND;
    }
    if (sqlite_status != SQLITE_ROW) {
        (void)sqlite3_finalize(statement);
        return DATABASE_INTERNAL_ERROR;
    }

    stored_url = sqlite3_column_text(statement, 0);
    stored_bytes = sqlite3_column_bytes(statement, 0);
    if (stored_url == NULL || stored_bytes < 0) {
        (void)sqlite3_finalize(statement);
        return DATABASE_INTERNAL_ERROR;
    }
    stored_url_length = (size_t)stored_bytes;
    if (stored_url_length >= capacity) {
        (void)sqlite3_finalize(statement);
        return DATABASE_BUFFER_TOO_SMALL;
    }

    (void)memcpy(original_url, stored_url, stored_url_length);
    original_url[stored_url_length] = '\0';
    sqlite_status = sqlite3_finalize(statement);
    if (sqlite_status != SQLITE_OK) {
        return DATABASE_INTERNAL_ERROR;
    }

    return DATABASE_OK;
}
