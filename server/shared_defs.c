#define _POSIX_C_SOURCE 200809L

#include "shared_defs.h"

#include <errno.h>
#include <pthread.h>
#include <sqlite3.h>
#include <sys/types.h>

void lock_mutex(sqlite3 *db, SharedBuffer *sBuff)
{
    int check;
    check = pthread_mutex_lock(&sBuff->lock);
    if (check == EOWNERDEAD)
    {
        pthread_mutex_consistent(&sBuff->lock);
        printf("Server crashed!");
        log_event(db, sBuff, "Mutex creator not running, recovering mutex", "Error");
    }
}

/**
 * @brief Helper function to log events to the SQLite database.
 */
void log_event(sqlite3 *db, SharedBuffer *sBuff, const char *name, const char *action)
{
    if (db == NULL)
        return;

    sqlite3_stmt *stmt = NULL;
    const char *log = "INSERT INTO Log(Name, Action) VALUES (?, ?);";

    if (sqlite3_prepare_v2(db, log, -1, &stmt, NULL) == SQLITE_OK)
    {
        sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, action, -1, SQLITE_TRANSIENT);

        pthread_mutex_lock(&sBuff->lock);
        sqlite3_step(stmt);
        pthread_mutex_unlock(&sBuff->lock);

        sqlite3_finalize(stmt);
        stmt = NULL;
    }
    else
    {
        printf("Failed to prepare log statement\n");
    }
}