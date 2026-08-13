#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <string.h>
#include <sqlite3.h> 
#include <sys/shm.h>
#include <sys/stat.h> 
#include <signal.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <errno.h>
#include "shared_defs.h"


int select_action(sqlite3 *db, SharedBuffer *sBuff);
int update_insert_city(sqlite3 *db, SharedBuffer *sBuff);
void clear_stdin(void);
int get_info(sqlite3 *db);
int delete_city(sqlite3 *db, SharedBuffer *sBuff);
void Server_shutdown(sqlite3 *db, SharedBuffer *sBuff);



void clear_stdin(void) {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

int select_action(sqlite3 *db, SharedBuffer *sBuff)
{
    int selection;
    printf("\nselect action:\n"
        "1. Query database\n"
        "2. Update or add entry\n"
        "3. Delete entry\n"
        "4. Close server"
        "any other number will exit.\n");
    while(scanf("%d", &selection) != 1)
    {
        printf("Invalid input!\n");
        clear_stdin();
    }
    clear_stdin();
    switch (selection)
    {
        case 1:
            get_info(db);
            break;
        case 2:
            update_insert_city(db, sBuff);
            break;
        case 3:
            delete_city(db, sBuff);
            break;
        case 4:
            Server_shutdown(db, sBuff);
            break;
        default:
            return -1;
    } 
    return 0;
}

int update_insert_city(sqlite3 *db, SharedBuffer *sBuff)
{
    char input[MAX_NAME_SIZE] = {0};
    int price = 0, x_cord = 0, y_cord = 0, check;
    sqlite3_stmt *stmt = NULL;
    const char *sql, *sql2, *sql3;

    sql = "SELECT x_cord, y_cord FROM cities WHERE Name = ?";
    sql2 = "INSERT INTO cities (Name, x_cord, y_cord, Price) VALUES (?, ?, ?, ?)";
    sql3 = "UPDATE cities SET Price = ? WHERE name = ?;";


    printf("\nSelect location name\n");
    fgets(input, MAX_NAME_SIZE, stdin);
    size_t len = strlen(input);
    if (len > 0 && input[len - 1] == '\n')
    {
        input[len - 1] = '\0';
    }
    
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (stmt == NULL)
    {
        printf("error binding Database");
        return -1;
    }
    sqlite3_bind_text(stmt, 1, input, -1, SQLITE_TRANSIENT);
    check = sqlite3_step(stmt);
    
    
    if (check == SQLITE_ROW)
    {
        x_cord = sqlite3_column_int(stmt, 0);
        y_cord = sqlite3_column_int(stmt, 1);
        sqlite3_finalize(stmt);
        stmt = NULL;
        printf("\nLocation exists\nSelect new price:\n");
        while(1)
        {
            check = scanf("%d", &price);
            clear_stdin();
            if(check == 1 && price >= 0)
                break;
            printf("Invalid input!\n");
        }
        sqlite3_prepare_v2(db, sql3, -1, &stmt, NULL);
        if (stmt == NULL)
        {
            printf("error binding Database\n");
            return -1;
        }
        lock_mutex(&sBuff->lock);
        sqlite3_bind_int(stmt, 1, price);
        sqlite3_bind_text(stmt, 2, input, -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        stmt = NULL;
        pthread_mutex_unlock(&sBuff->lock);
        printf("Price Updated\n");
    }
    else
    {
        sqlite3_finalize(stmt);
        stmt = NULL;
        printf("\nSelect X coordinate (int 1-5):\n");
        while(1)
        {
            check = scanf("%d", &x_cord);
            clear_stdin();
            if(check == 1 && x_cord >= 1 && x_cord <= 5)
                break;
            printf("Invalid input!\n");
        }
        printf("\nSelect Y coordinate (int 1-5):\n");
        while(1)
        {
            check = scanf("%d", &y_cord);
            clear_stdin();
            if(check == 1 && y_cord >= 1 && y_cord <= 5)
                break;
            printf("Invalid input!\n");
            
        }
        printf("\nSelect price\n");
        while(1)
        {
            check = scanf("%d", &price);
            clear_stdin();
            if(check == 1 && price >= 0)
                break;
            printf("Invalid input!\n");
        }
        sqlite3_prepare_v2(db, sql2, -1, &stmt, NULL);
        if (stmt == NULL)
        {
            printf("error binding Database\n");
            return -1;
        }
        lock_mutex(&sBuff->lock);
        sqlite3_bind_text(stmt, 1, input, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, x_cord);
        sqlite3_bind_int(stmt, 3, y_cord);
        sqlite3_bind_int(stmt, 4, price);
        check = sqlite3_step(stmt);
        if (check != SQLITE_DONE) 
        {
            printf("Error: Location already have a city");
            sqlite3_finalize(stmt);
            stmt = NULL;
            pthread_mutex_unlock(&sBuff->lock);
            return -1;
        }
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&sBuff->lock);
    }
    lock_mutex(&sBuff->lock);
    snprintf(sBuff->city, sizeof(sBuff->city), "%s", input);
    sBuff->x_cord = x_cord;
    sBuff->y_cord = y_cord;
    sBuff->price = price;
    sBuff->action = update;
    pthread_mutex_unlock(&sBuff->lock);
    stmt = NULL;

    if (sBuff->listener_pid > 0)
    {
        kill(sBuff->listener_pid, SIGUSR1);
    }

    return 0;
}

int get_info(sqlite3 *db)
{
    sqlite3_stmt *stmt = NULL;
    const char *sql, *name; 
    char pattern[MAX_NAME_SIZE + 4] = {0}, input[MAX_NAME_SIZE] = {0};
    int check, x_cord, y_cord, price;

    sql = "SELECT Name, x_cord, y_cord, COALESCE(Price, 0) "
    "FROM cities WHERE Name LIKE ? LIMIT 1";

    printf("\nselect location name:\n");
    fgets(input, MAX_NAME_SIZE, stdin);
    size_t len = strlen(input);
    if (len > 0 && input[len - 1] == '\n')
    {
        input[len - 1] = '\0';
    }
    snprintf(pattern, sizeof(pattern), "%%%s%%", input);
    
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (stmt == NULL)
    {
        printf("error binding Database");
        return -1;
    }
    sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_TRANSIENT);
    check = sqlite3_step(stmt);
    if (check == SQLITE_ROW)
    {
        name  = sqlite3_column_text(stmt, 0);
        x_cord = sqlite3_column_int(stmt, 1);
        y_cord = sqlite3_column_int(stmt, 2);
        price = sqlite3_column_int(stmt, 3);
        printf("Name | X coordinate | Y coordinate | Price\n");
        printf("%s | %d | %d | %d\n", name, x_cord, y_cord, price);
    }
    else
    {
        printf("No match found.\n");
    }
    sqlite3_finalize(stmt);
    stmt = NULL;

    return 0;
}

int delete_city(sqlite3 *db, SharedBuffer *sBuff)
{
    sqlite3_stmt *stmt = NULL;
    size_t len;
    const char *name = "SELECT Name, x_cord, y_cord FROM cities "
    "WHERE Name LIKE ? LIMIT 1";
    const char *del = "DELETE FROM cities WHERE Name = ?;";
    const char *city;
    char pattern[MAX_NAME_SIZE + 4] = {0}, input[MAX_NAME_SIZE] = {0}, verify = 0, matched_city[MAX_NAME_SIZE] = {0};
    int check, x_cord, y_cord;


    printf("\nselect location\n");
    fgets(input, MAX_NAME_SIZE, stdin);
    len = strlen(input);
    if (len > 0 && input[len - 1] == '\n')
    {
        input[len - 1] = '\0';
    }
    snprintf(pattern, sizeof(pattern), "%%%s%%", input);
    
    sqlite3_prepare_v2(db, name, -1, &stmt, NULL);
    if (stmt == NULL)
    {
        printf("error binding Database");
        return -1;
    }
    sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_TRANSIENT);
    check = sqlite3_step(stmt);
    if ( check == SQLITE_ROW)
    {  
        city  = sqlite3_column_text(stmt, 0);
        x_cord = sqlite3_column_int(stmt, 1);
        y_cord = sqlite3_column_int(stmt, 2);
        if (city != NULL) {
            snprintf(matched_city, sizeof(matched_city), "%s", city);
        }
        sqlite3_finalize(stmt);
        stmt = NULL;
        printf("You want to delete %s? Y/N\n", matched_city);
        scanf(" %c", &verify);
        clear_stdin();
        if (verify == 'y' || verify == 'Y')
        {
            
            printf("\nDeleting Entry");
            check = sqlite3_prepare_v2(db, del, -1, &stmt, NULL);
            if(check == SQLITE_OK)
            {
                lock_mutex(&sBuff->lock);
                sqlite3_bind_text(stmt, 1, (char *)matched_city, -1, SQLITE_TRANSIENT);
                sqlite3_step(stmt);

                snprintf(sBuff->city, sizeof(sBuff->city), "%s", matched_city);
                sBuff->x_cord = x_cord;
                sBuff->y_cord = y_cord;
                sBuff->price = 0;
                sBuff->action = deleted;

                pthread_mutex_unlock(&sBuff->lock);
                printf("Entry deleted\n");
            }
            else
            {
                printf("Error: could not delete\n");
            }
            if (sBuff->listener_pid > 0)
            {
                kill(sBuff->listener_pid, SIGUSR1);
            }
        }
    }
    else
    {
        printf("No match found.\n");
    }
    sqlite3_finalize(stmt);
    stmt = NULL;

    return 0;
}

void Server_shutdown(sqlite3 *db, SharedBuffer *sBuff)
{
    const char *sql;
    if (sBuff->listener_pid > 0)
            {
                lock_mutex(&sBuff->lock);
                sBuff->action = off;
                pthread_mutex_unlock(&sBuff->lock);
                kill(sBuff->listener_pid, SIGUSR2);
                printf("Server shutting down");
            }
            else
            {
                printf("Server not found");
            }
}

int main(void) {
    sqlite3 *db;
    int select = 0, check, shmid;
    char creator = 0;
    const char *sql, *sql2, *sql3, *sql4;
    shmid = shmget((key_t)SHM_KEY, sizeof(SharedBuffer), IPC_CREAT | IPC_EXCL | 0666);


    sql = "CREATE TABLE IF NOT EXISTS cities("
    "Id INTEGER PRIMARY KEY, "
    "Name TEXT UNIQUE COLLATE NOCASE, "
    "X_cord INT, Y_cord INT, Price INT, "
    "CONSTRAINT UC_Location UNIQUE(x_cord, y_cord));";

    sql2 = "CREATE TABLE IF NOT EXISTS customers("
    "Name TEXT, pay INT, location TEXT, "
    "Time DATETIME DEFAULT CURRENT_TIMESTAMP);";
    
    sql3 = "CREATE TABLE IF NOT EXISTS Log("
    "Name TEXT COLLATE NOCASE, "
    "Action TEXT DEFAULT 'start', "
    "price INT DEFAULT 0, "
    "Time DATETIME DEFAULT CURRENT_TIMESTAMP);";

    if (shmid != -1)
    {
        creator = 1;
    }
    else if (errno == EEXIST)
    {
        shmid = shmget((key_t)SHM_KEY, sizeof(SharedBuffer), 0666);
        if (shmid == -1) {
            perror("shmget attach failed");
            return 1;
        }
    }
    else
    {
        perror("shmget failed");
        return 1;
    }

    SharedBuffer *sBuff = (SharedBuffer *)shmat(shmid, NULL, 0);
    if (sBuff == (void *)-1) {
        perror("shmat failed");
        return 1;
    }

    
    if (creator) {

        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
        pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);

        pthread_mutex_init(&sBuff->lock, &attr);
        pthread_mutexattr_destroy(&attr);

        lock_mutex(&sBuff->lock);
        memset(sBuff->city, 0, MAX_NAME_SIZE);
        sBuff->x_cord = 0;
        sBuff->y_cord = 0;
        sBuff->action = none;
        sBuff->listener_pid = 0;
        sBuff->price = 0;
        sBuff->last = 0;
        pthread_mutex_unlock(&sBuff->lock);
    }

    check = sqlite3_open("test.db", &db);
    
    if (check != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }
    
    lock_mutex(&sBuff->lock);
    sBuff->last = 0;

    sqlite3_exec(db, "PRAGMA journal_mode = WAL;", 0, 0, NULL); // shared memory mode
    sqlite3_exec(db, sql, 0, 0, NULL);
    sqlite3_exec(db, sql2, 0, 0, NULL);
    sqlite3_exec(db, sql3, 0, 0, NULL);
    sqlite3_exec(db, sql4, 0, 0, NULL);
    pthread_mutex_unlock(&sBuff->lock);
    
    printf("Table created and data inserted successfully!\n");

    while (select == 0)
        select = select_action(db, sBuff);
    
    sqlite3_close(db);
    
    
    if(creator)
    {
        shmctl(shmid, IPC_RMID, NULL);
        printf("destroyed shared memory");
    }
    if(sBuff->last)
        pthread_mutex_destroy(&sBuff->lock);
    else
        sBuff->last = 1;
    shmdt(sBuff);
    return 0;
}