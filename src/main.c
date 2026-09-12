#include "database.h"
#include "server.h"
#include "shortener.h"

#include <stdio.h>

int main(void)
{
    database_t *database = NULL;
    shortener_t shortener;
    int server_status;

    if (database_open("url_shortener.db", &database) != DATABASE_OK) {
        (void)fprintf(stderr, "failed to open or initialize url_shortener.db\n");
        return 1;
    }
    if (shortener_init(&shortener, database) != SHORTENER_OK) {
        (void)fprintf(stderr, "failed to initialize URL shortener\n");
        database_close(database);
        return 1;
    }

    server_status = server_run(&shortener);
    database_close(database);
    return server_status;
}
