#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "shared_defs.h"

#include <errno.h>
#include <pthread.h>
#include <sqlite3.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

void lock_mutex(sqlite3 *db, SharedBuffer *sBuff)
{
    int check;
    while (sBuff->init != 1)
    {
        sched_yield(); // yield the process if the mutex haven't been initialized yet
        usleep(1000);  // wait 1ms before trying again
    }
    check = pthread_mutex_lock(&sBuff->lock);
    if (check == EOWNERDEAD)
    {
        pthread_mutex_consistent(&sBuff->lock);
        printf("Server crashed!");
        log_event(db, sBuff, "Mutex stopped mid-operation, recovering mutex", "Error");
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

        sqlite3_step(stmt);

        sqlite3_finalize(stmt);
        stmt = NULL;
    }
    else
    {
        printf("Failed to prepare log statement\n");
    }
}