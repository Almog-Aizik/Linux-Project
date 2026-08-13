#ifndef SHARED_DEFS_H
#define SHARED_DEFS_H

#include <pthread.h>
#include <sys/types.h>

#define SHM_KEY 0x54321
#define MAX_NAME_SIZE 20

typedef enum {
    none = 0,
    update,
    deleted,
    off
} ActionType;

typedef struct {
    pthread_mutex_t lock;
    pid_t listener_pid;
    int price, x_cord, y_cord;
    ActionType action;
    char city[MAX_NAME_SIZE], last;
} SharedBuffer;

void lock_mutex(pthread_mutex_t *lock);

void lock_mutex(pthread_mutex_t *lock)
{
    int check;
    check = pthread_mutex_lock(lock);
    if (check == EOWNERDEAD)
    {
        pthread_mutex_consistent(lock);
        printf("Server crashed!");
        // add log about a crash
    }
}

#endif