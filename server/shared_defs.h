#ifndef SHARED_DEFS_H
#define SHARED_DEFS_H

#include <semaphore.h>
#include <sys/types.h>

#define SHM_KEY 0x54321

typedef enum {
    none = 0,
    update,
    deleted
} ActionType;

typedef struct {
    sem_t lock;
    pid_t listener_pid;
    char city[20];
    int price;
    ActionType action;
} SharedBuffer;

#endif