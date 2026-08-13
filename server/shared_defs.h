#ifndef SHARED_DEFS_H
#define SHARED_DEFS_H

#define SHM_KEY 0x54321
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
    char city[MAX_NAME_SIZE], last;
} SharedBuffer;

void lock_mutex(sqlite3 *db, SharedBuffer *sBuff);
void log_event(sqlite3 *db, SharedBuffer *sBuff, const char *name, const char *action);

#endif