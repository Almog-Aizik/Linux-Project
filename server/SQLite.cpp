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

void clear_stdin(void) {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

int select_action(sqlite3 *db, SharedBuffer *sBuff)
{
    int selection;
    printf("\nselect action\n1. Query database\n2. Update or add entry\n 3. Delete entry\n any other number will exit.");
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
        default:
            return -1;
    } 
    return 0;
}

int update_insert_city(sqlite3 *db, SharedBuffer *sBuff)
{
    char input[20] = {0};
    int price = 0;
    sqlite3_stmt *stmt = NULL;
    const char *sql = "INSERT INTO cities (Name, Price) VALUES (?, ?)"
        " ON CONFLICT(Name) DO UPDATE SET Price = EXCLUDED.Price;";


    printf("\nselect location\n");
    fgets(input, 20, stdin);
    size_t len = strlen(input);
    if (len > 0 && input[len - 1] == '\n')
    {
        input[len - 1] = '\0';
    }
    printf("\nselect price\n");
    while(scanf("%d", &price) != 1)
    {
        printf("Invalid input!\n");
        clear_stdin();
    }

    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, input, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, price);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    stmt = NULL;

    sem_wait(&sBuff->lock);
    snprintf(sBuff->city, sizeof(sBuff->city), "%s", input);
    sBuff->price = price;
    sBuff->action = update;
    sem_post(&sBuff->lock);

    if (sBuff->listener_pid > 0)
    {
        kill(sBuff->listener_pid, SIGUSR1);
    }

    return 0;
}

int get_info(sqlite3 *db)
{
    sqlite3_stmt *stmt = NULL;
    const char *sql = "SELECT Name, COALESCE(Price, 'Null') FROM cities WHERE Name LIKE ? LIMIT 1";
    char pattern[24] = {0}, input[20] = {0};


    printf("\nselect location\n");
    fgets(input, 20, stdin);
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
    
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        const unsigned char *name  = sqlite3_column_text(stmt, 0);
        const unsigned char *price = sqlite3_column_text(stmt, 1);
        printf("Name | Price\n");
        printf("%s | %s\n", name, price);
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
    const char *name = "SELECT Name FROM cities WHERE Name LIKE ? LIMIT 1";
    const char *del = "DELETE FROM cities WHERE Name = ?;";
    char pattern[24] = {0}, input[20] = {0}, verify = 0, matched_city[20] = {0};


    printf("\nselect location\n");
    fgets(input, 20, stdin);
    size_t len = strlen(input);
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
    
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {  
        const unsigned char *city  = sqlite3_column_text(stmt, 0);
        if (city != NULL) {
            snprintf(matched_city, sizeof(matched_city), "%s", city);
        }
        

        printf("You want to delete %s? Y/N\n", matched_city);
        scanf(" %c", &verify);
        clear_stdin();
        if (verify == 'y' || verify == 'Y')
        {
            sqlite3_finalize(stmt);
            printf("\nDeleting Entry");
            sqlite3_prepare_v2(db, del, -1, &stmt, NULL);
            sqlite3_bind_text(stmt, 1, (char *)matched_city, -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);

            sem_wait(&sBuff->lock);
            
            snprintf(sBuff->city, sizeof(sBuff->city), "%s", matched_city);
            sBuff->price = 0;
            sBuff->action = deleted;

            sem_post(&sBuff->lock);
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

int main(void) {
    sqlite3 *db;
    int select = 0;
    char creator = 0;
    int shmid = shmget((key_t)SHM_KEY, sizeof(SharedBuffer), IPC_CREAT | IPC_EXCL | 0666);
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
        printf("[SHM] Created new shared memory segment (SHMID: %d)\n", shmid);

        if (sem_init(&sBuff->lock, 1, 1) == -1) {
            printf("sem_init failed");
        }

        memset(sBuff->city, 0, sizeof(sBuff->city));
        sBuff->action = none;
        sBuff->price = 0;
        sBuff->listener_pid = 0;
    }

    int rc = sqlite3_open("test.db", &db);
    
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }
    
    // sqlite3_exec(db, "PRAGMA mmap_size = 268435456;", 0, 0, NULL); // shared memory for the database
    sqlite3_exec(db, "PRAGMA journal_mode = WAL;", 0, 0, NULL); // allow reading and writing at the same time

    const char *sql = "CREATE TABLE IF NOT EXISTS cities(Id INTEGER PRIMARY KEY, Name TEXT UNIQUE, Price INT);";
    const char *sql2 = "CREATE TABLE IF NOT EXISTS customers(Name TEXT, pay INT, location TEXT, Time DATETIME DEFAULT CURRENT_TIMESTAMP);"
                "INSERT INTO customers(Name, pay, location) VALUES('Jay', 100, 'jerusalem');";
    sqlite3_exec(db, sql, 0, 0, NULL);
    sqlite3_exec(db, sql2, 0, 0, NULL);

    
    printf("Table created and data inserted successfully!\n");

    while (select == 0)
        select = select_action(db, sBuff);
    


    sqlite3_close(db);
    shmdt(sBuff);

    return 0;
}