#ifndef SHARED_DEFS_H
#define SHARED_DEFS_H

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <sqlite3.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h> // Provides usleep()

#ifdef __cplusplus
// C++ Compilation
#include <atomic>
typedef std::atomic<int> atomic_int;
#else
// C Compilation
#include <stdatomic.h>
#endif

#define MAX_NAME_SIZE 20

typedef enum
{
    none,
    update,
    deleted,
    off
} ActionType;

typedef struct
{
    pthread_mutex_t lock;
    pid_t listener_pid;
    int price, x_cord, y_cord;
    ActionType action;
    char city[MAX_NAME_SIZE];
    atomic_int init;
} SharedBuffer;

void lock_mutex(sqlite3 *db, SharedBuffer *sBuff);
void log_event(sqlite3 *db, SharedBuffer *sBuff, const char *name, const char *action);

#endif