#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "shared_defs.h"
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <unistd.h>

int select_action(sqlite3 *db, SharedBuffer *sBuff);
int update_insert_city(sqlite3 *db, SharedBuffer *sBuff);
void clear_stdin(void);
int get_info(sqlite3 *db, SharedBuffer *sBuff);
int delete_city(sqlite3 *db, SharedBuffer *sBuff);
void Server_shutdown(sqlite3 *db, SharedBuffer *sBuff);
void server_start(sqlite3 *db, SharedBuffer *sBuff);
int import_cities(sqlite3 *db, SharedBuffer *sBuff);
int get_city_info(sqlite3 *db);
int get_trans(sqlite3 *db);
int get_log(sqlite3 *db);

/**
 * @brief
 * clears the stdin stream to insure no unwanted input is left in the buffer
 */
void clear_stdin(void)
{
    int c;
    while ((c = getchar()) != '\n' && c != EOF)
        ;
}

/**
 * @brief
 * intended to allow the user to select the action it wants
 * @param db
 * database to be used
 * @param sBuff
 * shared buffer for synching
 * @return int
 * exit code
 * tells the program the user wants to exit or not
 */
int select_action(sqlite3 *db, SharedBuffer *sBuff)
{
    int selection, check;
    system("clear");
    printf("\nselect action:\n"
           "1. Query database\n"
           "2. Update or add entry\n"
           "3. Delete entry\n"
           "4. Import from cities.txt\n"
           "5. Start server\n"
           "6. Shutdown the server\n"
           "any other number will exit.\n");
    check = scanf("%d", &selection);
    while (check != 1)
    {
        if (check == -1)
            // check for end of stream
            return -1;
        printf("Invalid input!\n");
        clear_stdin();
        check = scanf("%d", &selection);
    }
    clear_stdin();
    switch (selection)
    {
    case 1:
        get_info(db, sBuff);
        break;
    case 2:
        update_insert_city(db, sBuff);
        break;
    case 3:
        delete_city(db, sBuff);
        break;
    case 4:
        import_cities(db, sBuff);
        break;
    case 5:
        server_start(db, sBuff);
        break;
    case 6:
        Server_shutdown(db, sBuff);
        break;
    default:
        return -1;
    }
    return 0;
}

int get_info(sqlite3 *db, SharedBuffer *sBuff)
{
    int selection, check;
    system("clear");
    printf("\nselect what to quary:\n"
           "1. Get city info\n"
           "2. Get transactions\n"
           "3. Get log\n"
           "any other number will exit.\n");
    check = scanf("%d", &selection);
    while (check != 1)
    {
        if (check == -1)
            // check for end of stream
            return -1;
        printf("Invalid input!\n");
        clear_stdin();
        check = scanf("%d", &selection);
    }
    clear_stdin();
    switch (selection)
    {
    case 1:
        get_city_info(db);
        break;
    case 2:
        get_trans(db);
        break;
    case 3:
        get_log(db);
        break;
    default:
        return -1;
    }
    return 0;
}

/**
 * @brief
 * tries to insert a new city to the database
 * update its price if already exists
 *
 * @param db
 * the database with the city
 * @param sBuff
 * shared buffer for syncing
 * @return int
 * exit code
 */
int update_insert_city(sqlite3 *db, SharedBuffer *sBuff)
{
    char input[MAX_NAME_SIZE] = {0};
    int price = 0, x_cord = 0, y_cord = 0, check;
    sqlite3_stmt *stmt = NULL;
    const char *sql, *sql2, *sql3;

    sql = "SELECT x_cord, y_cord FROM cities WHERE Name = ?";
    sql2 = "INSERT INTO cities (Name, x_cord, y_cord, Price) VALUES (?, ?, ?, ?)";
    sql3 = "UPDATE cities SET Price = ? WHERE name = ?;";

    system("clear");
    printf("\nSelect location name\n");
    fgets(input, MAX_NAME_SIZE, stdin);
    size_t len = strlen(input);
    if (len > 0 && input[len - 1] == '\n')
    {
        input[len - 1] = '\0';
    }
    // look for a city at that location
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (stmt == NULL)
    {
        printf("Error binding statement");
        log_event(db, sBuff, "Error binding statement", "Error");
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
        while (1)
        {
            check = scanf("%d", &price);
            clear_stdin();
            if (check == 1 && price >= 0)
                break;
            printf("Invalid input!\n");
            log_event(db, sBuff, "Invalid input", "Error");
        }
        sqlite3_prepare_v2(db, sql3, -1, &stmt, NULL);
        if (stmt == NULL)
        {
            printf("Error binding statement\n");
            log_event(db, sBuff, "Error binding statement", "Error");
            return -1;
        }
        lock_mutex(db, sBuff);
        sqlite3_bind_int(stmt, 1, price);
        sqlite3_bind_text(stmt, 2, input, -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        stmt = NULL;
        pthread_mutex_unlock(&sBuff->lock);
        printf("Price uSpdated\n");
        log_event(db, sBuff, "Price updated", "Info");
    }
    else
    {
        sqlite3_finalize(stmt);
        stmt = NULL;
        printf("\nSelect X coordinate (int 1-5):\n");
        while (1)
        {
            check = scanf("%d", &x_cord);
            clear_stdin();
            if (check == 1 && x_cord >= 1 && x_cord <= 5)
                break;
            printf("Invalid input!\n");
            log_event(db, sBuff, "Invalid input", "Error");
        }
        printf("\nSelect Y coordinate (int 1-5):\n");
        while (1)
        {
            check = scanf("%d", &y_cord);
            clear_stdin();
            if (check == 1 && y_cord >= 1 && y_cord <= 5)
                break;
            printf("Invalid input!\n");
            log_event(db, sBuff, "Invalid input", "Error");
        }
        printf("\nSelect price\n");
        while (1)
        {
            check = scanf("%d", &price);
            clear_stdin();
            if (check == 1 && price >= 0)
                break;
            printf("Invalid input!\n");
            log_event(db, sBuff, "Invalid input", "Error");
        }
        sqlite3_prepare_v2(db, sql2, -1, &stmt, NULL);
        if (stmt == NULL)
        {
            printf("Error binding Database\n");
            log_event(db, sBuff, "Error binding Database", "Error");
            return -1;
        }
        lock_mutex(db, sBuff);
        sqlite3_bind_text(stmt, 1, input, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, x_cord);
        sqlite3_bind_int(stmt, 3, y_cord);
        sqlite3_bind_int(stmt, 4, price);
        check = sqlite3_step(stmt);
        if (check != SQLITE_DONE)
        {
            printf("Error: Insert failed");
            log_event(db, sBuff, "Insert failed", "Error");
            sqlite3_finalize(stmt);
            stmt = NULL;
            pthread_mutex_unlock(&sBuff->lock);
            return -1;
        }
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&sBuff->lock);
    }
    lock_mutex(db, sBuff);
    snprintf(sBuff->city, sizeof(sBuff->city), "%s", input);
    sBuff->x_cord = x_cord;
    sBuff->y_cord = y_cord;
    sBuff->price = price;
    sBuff->action = update;
    if (sBuff->listener_pid > 0)
    {
        kill(sBuff->listener_pid, SIGUSR1);
    }
    pthread_mutex_unlock(&sBuff->lock);
    stmt = NULL;

    printf("Press any key to continue");
    getchar();
    return 0;
}

/**
 * @brief
 * simple SQL look up for the cities
 *
 * @param db
 * City storage database
 * @return int
 * exit code
 */
int get_city_info(sqlite3 *db)
{
    sqlite3_stmt *stmt = NULL;
    const char *sql, *sql2;
    unsigned const char *name;
    char pattern[MAX_NAME_SIZE + 4] = {0}, input[MAX_NAME_SIZE] = {0};
    int check, x_cord, y_cord, price, found = 0;

    sql = "SELECT Name FROM cities";

    sql2 = "SELECT Name, x_cord, y_cord, COALESCE(Price, 0) "
           "FROM cities WHERE Name LIKE ? LIMIT 1";

    // get list of cities available
    system("clear");
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (stmt == NULL)
    {
        printf("error binding Database");
        return -1;
    }
    check = sqlite3_step(stmt);
    while (check == SQLITE_ROW)
    {
        if (found == 0)
        {
            printf("Cities available: ");
            found = 1;
        }

        name = sqlite3_column_text(stmt, 0);

        printf("%s, ", name);
        check = sqlite3_step(stmt);
    }
    if (found == 0)
    {
        printf("No cities found.\n");
        sqlite3_finalize(stmt);
        stmt = NULL;
        return -1;
    }
    sqlite3_finalize(stmt);
    stmt = NULL;

    // select city
    printf("\nselect location name:\n");
    fgets(input, MAX_NAME_SIZE, stdin);
    size_t len = strlen(input);
    if (len > 0 && input[len - 1] == '\n')
    {
        input[len - 1] = '\0';
    }
    snprintf(pattern, sizeof(pattern), "%%%s%%", input);

    sqlite3_prepare_v2(db, sql2, -1, &stmt, NULL);
    if (stmt == NULL)
    {
        printf("error binding Database");
        return -1;
    }
    sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_TRANSIENT);
    check = sqlite3_step(stmt);
    if (check == SQLITE_ROW)
    {
        name = sqlite3_column_text(stmt, 0);
        x_cord = sqlite3_column_int(stmt, 1);
        y_cord = sqlite3_column_int(stmt, 2);
        price = sqlite3_column_int(stmt, 3);
        printf("Name | X coordinate | Y coordinate | Price\n");
        printf("%s | %d | %d | %d\n", name, x_cord, y_cord, price);
    }
    sqlite3_finalize(stmt);
    stmt = NULL;
    printf("Press any key to continue");
    getchar();
    return 0;
}

/**
 * @brief
 * get the last 200 entries in the transaction log
 *
 * @param db
 * database to look up from
 * @return int
 * return code
 */
int get_trans(sqlite3 *db)
{
    sqlite3_stmt *stmt = NULL;
    const char *sql;
    unsigned const char *name, *location, *time;
    int check, pay, found = 0;

    sql = "SELECT * FROM ( "
          "SELECT DATETIME(time, 'localtime') AS local_time , Name, location, pay "
          "FROM customers "
          "ORDER BY time DESC "
          "limit 200 "
          ") ORDER BY local_time ASC";

    // get list of cities available
    system("clear");
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (stmt == NULL)
    {
        printf("error binding Database");
        return -1;
    }
    check = sqlite3_step(stmt);
    while (check == SQLITE_ROW)
    {
        if (found == 0)
        {
            printf("log:\n"
                   "Time | Identifier | Location | payment\n");
            found = 1;
        }

        time = sqlite3_column_text(stmt, 0);
        name = sqlite3_column_text(stmt, 1);
        location = sqlite3_column_text(stmt, 2);
        pay = sqlite3_column_int(stmt, 3);

        printf("%s | %s | %s | %d\n", time, name, location, pay);
        check = sqlite3_step(stmt);
    }
    if (found == 0)
    {
        printf("No transactions happened.\n");
        sqlite3_finalize(stmt);
        stmt = NULL;
        return -1;
    }
    sqlite3_finalize(stmt);
    stmt = NULL;
    printf("Press any key to continue");
    getchar();
    return 0;
}

/**
 * @brief
 * Get the last 200 entries in the log
 *
 * @param db
 * database to search
 * @return int
 * return code
 */
int get_log(sqlite3 *db)
{
    sqlite3_stmt *stmt = NULL;
    const char *sql;
    unsigned const char *name, *type, *time;
    int check, pay, found = 0;

    sql = "SELECT * FROM ("
          "SELECT DATETIME(time, 'localtime') AS local_time, Name, action, price "
          "FROM Log "
          "ORDER BY time DESC "
          "LIMIT 200 "
          ") ORDER BY local_time ASC";

    // get list of cities available
    system("clear");
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (stmt == NULL)
    {
        printf("error binding Database");
        return -1;
    }
    check = sqlite3_step(stmt);
    while (check == SQLITE_ROW)
    {
        if (found == 0)
        {
            printf("log:\n"
                   "Time | Identifier | type | payment\n");
            found = 1;
        }

        time = sqlite3_column_text(stmt, 0);
        name = sqlite3_column_text(stmt, 1);
        type = sqlite3_column_text(stmt, 2);
        pay = sqlite3_column_int(stmt, 3);

        printf("%s | %s | %s | %d\n", time, name, type, pay);
        check = sqlite3_step(stmt);
    }
    if (found == 0)
    {
        printf("No transactions happened.\n");
        sqlite3_finalize(stmt);
        stmt = NULL;
        return -1;
    }
    sqlite3_finalize(stmt);
    stmt = NULL;
    printf("Press any key to continue");
    getchar();
    return 0;
}

/**
 * @brief
 * deletes a city entry from the database
 *
 * @param db
 * database storage for the city
 * @param sBuff
 * sync database use
 * @return int
 */
int delete_city(sqlite3 *db, SharedBuffer *sBuff)
{
    sqlite3_stmt *stmt = NULL;
    size_t len;
    const char *name = "SELECT Name, x_cord, y_cord FROM cities "
                       "WHERE Name LIKE ? LIMIT 1";
    const char *del = "DELETE FROM cities WHERE Name = ?;";
    unsigned const char *city;
    char pattern[MAX_NAME_SIZE + 4] = {0}, input[MAX_NAME_SIZE] = {0}, verify = 0, matched_city[MAX_NAME_SIZE] = {0};
    int check, x_cord, y_cord;

    system("clear");
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
    if (check == SQLITE_ROW)
    {
        city = sqlite3_column_text(stmt, 0);
        x_cord = sqlite3_column_int(stmt, 1);
        y_cord = sqlite3_column_int(stmt, 2);
        if (city != NULL)
        {
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
            if (check == SQLITE_OK)
            {
                lock_mutex(db, sBuff);
                sqlite3_bind_text(stmt, 1, (char *) matched_city, -1, SQLITE_TRANSIENT);
                sqlite3_step(stmt);

                snprintf(sBuff->city, sizeof(sBuff->city), "%s", matched_city);
                sBuff->x_cord = x_cord;
                sBuff->y_cord = y_cord;
                sBuff->price = 0;
                sBuff->action = deleted;
                if (sBuff->listener_pid > 0)
                {
                    kill(sBuff->listener_pid, SIGUSR1);
                }
                pthread_mutex_unlock(&sBuff->lock);
                printf("Entry deleted\n");
            }
            else
            {
                printf("Error: could not delete\n");
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

/**
 * @brief
 * shut down command for the server
 *
 * @param db
 * @param sBuff
 */
void Server_shutdown(sqlite3 *db, SharedBuffer *sBuff)
{
    system("clear");
    const char *sql;
    lock_mutex(db, sBuff);
    if (sBuff->listener_pid > 0)
    {
        sBuff->action = off;
        pthread_mutex_unlock(&sBuff->lock);
        kill(sBuff->listener_pid, SIGUSR2);
        log_event(db, sBuff, "Server shutdown command sent", "info");
    }
    else
    {
        pthread_mutex_unlock(&sBuff->lock);
        log_event(db, sBuff, "Server not found", "Error");
    }
}

/**
 * @brief
 * starts the server
 *
 * @param db
 * database for logging
 * @param sBuff
 * shared memory for syncing
 */
void server_start(sqlite3 *db, SharedBuffer *sBuff)
{
    pid_t pid = fork();

    if (pid < 0)
    {
        // fork failed
        printf("fork failed\n");
        log_event(db, sBuff, "fork failed", "Error");
    }
    else if (pid == 0)
    {
        // child process
        // Replace child process with server
        char *argv[] = {NULL};
        char *envp[] = {NULL};

        execve("./server", argv, envp);

        // runs only if it failed
        printf("Server couldn't start\n");
        log_event(db, sBuff, "Server couldn't start", "Error");
        exit(EXIT_FAILURE);
    }
    else
    {
        // parent process
        printf("Server started\n");
        log_event(db, sBuff, "Server started", "Info");
    }
}

/**
 * @brief
 * imports cities from a file named "cities.txt"
 * goes line by line and formats them before entering into the database
 *
 * @param db
 * database to insert into
 * @param sBuff
 * shared memory to syncronize
 * @return int
 * exit code
 */
int import_cities(sqlite3 *db, SharedBuffer *sBuff)
{
    const char *sql;
    char line[100], name[MAX_NAME_SIZE], *verify;
    int x_cord, y_cord, price, check, error = 0;
    sqlite3_stmt *stmt = NULL;
    FILE *file = fopen("cities.txt", "r");
    if (file == NULL)
    {
        printf("Error opening file");
        log_event(db, sBuff, "Error opening file", "Error");
        return -1;
    }

    sql = "INSERT INTO cities (Name, X_cord, Y_cord, Price) "
          "VALUES (?, ?, ?, ?) "
          "ON CONFLICT(X_cord, Y_cord) DO UPDATE SET "
          "Name = excluded.Name, "
          "Price = excluded.Price;";

    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (stmt == NULL)
    {
        printf("Error binding Database\n");
        log_event(db, sBuff, "Error binding Database", "Error");
        fclose(file);
        return -1;
    }

    // Read the file line by line
    verify = fgets(line, sizeof(line), file);
    while (verify != NULL)
    {
        // Strip trailing newline characters (\n or \r)
        check = strcspn(line, "\r\n");
        line[check] = '\0';

        if (line[0] == '\0')
        {
            verify = fgets(line, sizeof(line), file);
            continue;
        }

        // %[^,] reads all non-comma characters into name
        // spaces included
        check = sscanf(line, " %49[^,],%d, %d, %d", name, &x_cord, &y_cord, &price);
        if (check == 4)
        {
            lock_mutex(db, sBuff);
            // reset statement to starting point
            sqlite3_reset(stmt);
            sqlite3_clear_bindings(stmt);

            sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 2, x_cord);
            sqlite3_bind_int(stmt, 3, y_cord);
            sqlite3_bind_int(stmt, 4, price);
            check = sqlite3_step(stmt);
            pthread_mutex_unlock(&sBuff->lock);
            if (check != SQLITE_DONE)
            {
                printf("Insert failed");
                log_event(db, sBuff, "Insert failed", "Error");
            }
        }
        else
        {
            log_event(db, sBuff, "Failed to parse line", "Error");
            error++;
        }
        verify = fgets(line, sizeof(line), file);
    }
    sqlite3_finalize(stmt);
    stmt = NULL;
    printf("Cities imported from file\n");
    if (error > 0)
        printf("%d lines were skipped\n", error);
    log_event(db, sBuff, "Cities imported from file", "Info");
    fclose(file);
    printf("Press any key to continue");
    getchar();
    return 0;
}

int main(void)
{
    sqlite3 *db;
    int select = 0, check, shmid;
    char creator = 0;
    const char *sql, *sql2, *sql3;
    shmid = shmget((key_t) SHM_KEY, sizeof(SharedBuffer), IPC_CREAT | IPC_EXCL | 0666);

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
        shmid = shmget((key_t) SHM_KEY, sizeof(SharedBuffer), 0666);
        if (shmid == -1)
        {
            perror("shmget attach failed");
            return 1;
        }
    }
    else
    {
        perror("shmget failed");
        return 1;
    }

    SharedBuffer *sBuff = (SharedBuffer *) shmat(shmid, NULL, 0);
    if (sBuff == (void *) -1)
    {
        perror("shmat failed");
        return 1;
    }

    // make sure that shared memory is defined properly if the process started it
    if (creator)
    {
        sBuff->init = 0;
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
        pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);

        pthread_mutex_init(&sBuff->lock, &attr);
        pthread_mutexattr_destroy(&attr);

        pthread_mutex_lock(&sBuff->lock);
        memset(sBuff->city, 0, MAX_NAME_SIZE);
        sBuff->x_cord = 0;
        sBuff->y_cord = 0;
        sBuff->action = none;
        sBuff->listener_pid = 0;
        sBuff->price = 0;
        sBuff->init = 1;
        pthread_mutex_unlock(&sBuff->lock);
    }

    check = sqlite3_open("test.db", &db);

    if (check != SQLITE_OK)
    {
        printf("Cannot open database\n");
        sqlite3_close(db);
        return 1;
    }

    lock_mutex(db, sBuff);
    sqlite3_exec(db, "PRAGMA journal_mode = WAL;", 0, 0, NULL); // shared memory mode

    // create all relavent tables
    sqlite3_exec(db, sql, 0, 0, NULL);
    sqlite3_exec(db, sql2, 0, 0, NULL);
    sqlite3_exec(db, sql3, 0, 0, NULL);
    pthread_mutex_unlock(&sBuff->lock);

    log_event(db, sBuff, "Databases verified to exist", "info");

    while (select == 0)
        select = select_action(db, sBuff);

    if (creator)
    {
        shmctl(shmid, IPC_RMID, NULL);
        printf("destroyed shared memory");
    }
    sqlite3_close(db);
    shmdt(sBuff);
    return 0;
}