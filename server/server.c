#define _POSIX_C_SOURCE 200809L

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sqlite3.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include "shared_defs.h"
#include <time.h>

#define CONNECTIONS 5

typedef struct {
    int Socket;
    SharedBuffer *sBuff;
} ClientContext;


volatile sig_atomic_t sig_update = 0;
volatile sig_atomic_t sig_shutdown = 0;
pthread_mutex_t thread_mutex;

void handle_sigusr1(int sig);
void handle_sigusr2(int sig);
void *client_handler(void *arg);
int listen_function(int TCPServer, SharedBuffer *sBuff);

void handle_sigusr1(int sig)
{
    sig_update = 1; 
}

void handle_sigusr2(int sig)
{
    sig_shutdown = 1; 
}

int listen_function(int TCPServer, SharedBuffer *sBuff) 
{
    int error;
    pthread_t thread_id;
    

    while (sig_shutdown == 0) 
    {
        int clientSocket = accept(TCPServer, NULL, NULL);
        if (clientSocket < 0)
        {
            if (errno == EINTR)
                continue;
            perror("accept failed");
            break;
        }
        ClientContext *ctx = (ClientContext *)malloc(sizeof(ClientContext));
        
        if (ctx == NULL)
        {
            perror("Context allocation failed");
            close(clientSocket);
            continue;
        }

        ctx->sBuff = sBuff;
        ctx->Socket = clientSocket;
        
        
        error = pthread_create(&thread_id, NULL, client_handler, ctx);
        if(error != 0)
        {
            perror("Failed to create thread");
            close(clientSocket);
            free(ctx);
        } 
        else
        {
            pthread_detach(thread_id);
        }
    }

    return 0;
}

void *client_handler(void *arg) 
{
    ClientContext *ctx = (ClientContext *)arg;
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    SharedBuffer *sBuff = ctx->sBuff;
    time_t start, stop;
    int clientSocket = ctx->Socket, check, price, x_cord, y_cord;
    long elapsed; // 64 bits returned so avoiding int
    char buffer[100], user[50], city[20], update = 0;
    const char *sql2, *status, *temp;
    const char *log = "INSERT INTO Log(Name, Action) VALUES (?, ?);";
    const char *sql = "SELECT action, unixepoch(time) FROM Log "
                      "WHERE action IS NOT 'Error' AND action IS NOT 'Info' AND name = ? "
                      "ORDER BY Time DESC "
                      "LIMIT 1";
    const char *sql3 = "INSERT INTO Log(Name, Action, Price) VALUES (?, ?, ?);";

    
    check = sqlite3_open("test.db", &db);
    if (check)
    {
        fprintf(stderr, "Thread failed to open DB: %s\n", sqlite3_errmsg(db));
        close(clientSocket);
        free(ctx);
        return NULL;
    }

    check = sqlite3_prepare_v2(db, log, -1, &stmt, NULL);
    if (check == SQLITE_OK)
    {
        sqlite3_bind_text(stmt, 1, "Connection recieved, thread started", -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, "Info", -1, SQLITE_TRANSIENT);
        
        lock_mutex(&sBuff->lock);
        sqlite3_step(stmt);
        pthread_mutex_unlock(&sBuff->lock);
        
        sqlite3_finalize(stmt);
        stmt = NULL;
    }

    while (!sig_shutdown) 
    {
        memset(buffer, 0, sizeof(buffer));
        ssize_t bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        update = 0;
        

        if (bytesReceived < 0) {
            if (errno == EINTR)
                continue; // Ignore signal interrupts
            perror("recv error");
            break;
        }
        if (bytesReceived == 0) {
            check = sqlite3_prepare_v2(db, log, -1, &stmt, NULL);
            if (check == SQLITE_OK)
            {
                sqlite3_bind_text(stmt, 1, "Client disconnected", -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, "Info", -1, SQLITE_TRANSIENT);

                lock_mutex(&sBuff->lock);
                sqlite3_step(stmt);
                pthread_mutex_unlock(&sBuff->lock);

                sqlite3_finalize(stmt);
                stmt = NULL;
            }
            break;
        }

        if (strncmp(buffer, "exit", 4) == 0) {
            printf("Exit command received from client.\n");
            break;
        }

        check = sscanf(buffer, "%d %d %49[^\n]", &x_cord, &y_cord, user);

        if (check == 3)
        {
            sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
            if (stmt == NULL)
            {
                printf("error binding Database");
                break;
            }
            lock_mutex(&sBuff->lock);
            sqlite3_bind_text(stmt, 1, user, -1, SQLITE_TRANSIENT);
            check = sqlite3_step(stmt);
            pthread_mutex_unlock(&sBuff->lock);
            if (check == SQLITE_ROW)
            {   
                status  = sqlite3_column_text(stmt, 0);
                start  = sqlite3_column_int64(stmt, 1);
            }
            else
            {
                status =  "stop"; // pretend it stopped prior to the first entry
            }
            if (strncmp(status, "stop", 5) == 0)
            {
                sqlite3_finalize(stmt);
                stmt = NULL;
                // sql2 = "INSERT INTO TranLog (Name, Action) VALUES (?, ?)";
                sqlite3_prepare_v2(db, log, -1, &stmt, NULL);
                if (stmt == NULL)
                {
                    printf("error binding Database");
                    break;
                }
                lock_mutex(&sBuff->lock);
                sqlite3_bind_text(stmt, 1, user, -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, "start", -1, SQLITE_TRANSIENT);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
                stmt = NULL;
                pthread_mutex_unlock(&sBuff->lock);
            }
            else if (strncmp(status, "start", 4) == 0)
            {
                sqlite3_finalize(stmt);
                stmt = NULL;
                stop = time(NULL);
                elapsed = difftime(stop, start);
                if(sig_update)
                {
                    if(x_cord == sBuff->x_cord && y_cord == sBuff->y_cord)
                    {
                        lock_mutex(&sBuff->lock);
                        snprintf(city, sizeof(city), "%s", sBuff->city);
                        price = sBuff->price;
                        pthread_mutex_unlock(&sBuff->lock);
                        update = 1;
                        check = SQLITE_OK;
                    }
                    pthread_mutex_lock(&thread_mutex);
                    sig_update = 0;
                    pthread_mutex_unlock(&thread_mutex);
                }
                if(update != 1)
                {
                    sql2 = "SELECT COALESCE(Price, 0), Name FROM cities "
                    "WHERE x_cord = ? AND y_cord = ?;";
                    check = sqlite3_prepare_v2(db, sql2, -1, &stmt, NULL);
                }
                if (check == SQLITE_OK)
                {
                    if(update != 1)
                    {
                        sqlite3_bind_int(stmt, 1, x_cord);
                        sqlite3_bind_int(stmt, 2, y_cord);
                        check = sqlite3_step(stmt);
                    }
                    else
                    {
                        check = SQLITE_ROW;
                    }
                    if (check == SQLITE_ROW)
                    {
                        if(update != 1)
                        {
                            price = sqlite3_column_int(stmt, 0);
                            temp = sqlite3_column_text(stmt, 1);
                            snprintf(city, sizeof(city), "%s", temp);
                        }
                        price = price * elapsed;
                        
                        sqlite3_finalize(stmt);
                        stmt = NULL;

                        sql2 = "INSERT INTO customers(Name, pay, location) VALUES (?, ?, ?);";
                        check = sqlite3_prepare_v2(db, sql2, -1, &stmt, NULL);
                        if (check == SQLITE_OK)
                        {
                            sqlite3_bind_text(stmt, 1, user, -1, SQLITE_TRANSIENT);
                            sqlite3_bind_text(stmt, 3, city, -1, SQLITE_TRANSIENT);
                            sqlite3_bind_int(stmt, 2, price);

                            lock_mutex(&sBuff->lock);
                            sqlite3_step(stmt);
                            pthread_mutex_unlock(&sBuff->lock);

                            sqlite3_finalize(stmt);
                            stmt = NULL;
                        }
                        // sql3 = "INSERT INTO TranLog(Name, Action, Price) VALUES (?, ?, ?);";
                        check = sqlite3_prepare_v2(db, sql3, -1, &stmt, NULL);
                        if (check == SQLITE_OK)
                        {
                            sqlite3_bind_text(stmt, 1, user, -1, SQLITE_TRANSIENT);
                            sqlite3_bind_text(stmt, 2, "stop", -1, SQLITE_TRANSIENT);
                            sqlite3_bind_int(stmt, 3, price);

                            lock_mutex(&sBuff->lock);
                            sqlite3_step(stmt);
                            pthread_mutex_unlock(&sBuff->lock);

                            sqlite3_finalize(stmt);
                            stmt = NULL;
                        }

                    }
                    else
                    {
                        sqlite3_finalize(stmt);
                        stmt = NULL;
                        // sql3 = "INSERT INTO TranLog(Name, Action, Price) VALUES (?, ?, ?);";
                        check = sqlite3_prepare_v2(db, sql3, -1, &stmt, NULL);
                        if (check == SQLITE_OK)
                        {
                            sqlite3_bind_text(stmt, 1, user, -1, SQLITE_TRANSIENT);
                            sqlite3_bind_text(stmt, 2, "stop", -1, SQLITE_TRANSIENT);
                            sqlite3_bind_int(stmt, 3, 0);

                            lock_mutex(&sBuff->lock);
                            sqlite3_step(stmt);
                            pthread_mutex_unlock(&sBuff->lock);

                            sqlite3_finalize(stmt);
                            stmt = NULL;
                        }
                        check = sqlite3_prepare_v2(db, sql3, -1, &stmt, NULL);
                        if (check == SQLITE_OK)
                        {
                            sqlite3_bind_text(stmt, 1, "Location Not Found", -1, SQLITE_TRANSIENT);
                            sqlite3_bind_text(stmt, 2, "Error", -1, SQLITE_TRANSIENT);
                            sqlite3_bind_int(stmt, 3, 0);

                            lock_mutex(&sBuff->lock);
                            sqlite3_step(stmt);
                            pthread_mutex_unlock(&sBuff->lock);

                            sqlite3_finalize(stmt);
                            stmt = NULL;
                        }
                    }
                }
            }
            else
            {
            sqlite3_finalize(stmt);
            stmt = NULL;
            // sql2 = "INSERT INTO TranLog(Name, Action) VALUES (?, ?);";
            check = sqlite3_prepare_v2(db, log, -1, &stmt, NULL);
            if (check == SQLITE_OK)
            {
                sqlite3_bind_text(stmt, 1, "Invalid Input", -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, "Error", -1, SQLITE_TRANSIENT);

                lock_mutex(&sBuff->lock);
                sqlite3_step(stmt);
                pthread_mutex_unlock(&sBuff->lock);

                sqlite3_finalize(stmt);
                stmt = NULL;
            }
            }
        }
        else
        {
            // sql2 = "INSERT INTO TranLog(Name, Action) VALUES (?, ?);";
            check = sqlite3_prepare_v2(db, log, -1, &stmt, NULL);
            if (check == SQLITE_OK)
            {
                sqlite3_bind_text(stmt, 1, "Invalid Input", -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, "Error", -1, SQLITE_TRANSIENT);

                lock_mutex(&sBuff->lock);
                sqlite3_step(stmt);
                pthread_mutex_unlock(&sBuff->lock);

                sqlite3_finalize(stmt);
                stmt = NULL;
            }
        }
    }

    check = sqlite3_prepare_v2(db, log, -1, &stmt, NULL);
    if (check == SQLITE_OK)
    {
        sqlite3_bind_text(stmt, 1, "Thread shutting down", -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, "Info", -1, SQLITE_TRANSIENT);
        
        lock_mutex(&sBuff->lock);
        sqlite3_step(stmt);
        pthread_mutex_unlock(&sBuff->lock);
        
        sqlite3_finalize(stmt);
        stmt = NULL;
    }
    sqlite3_close(db);
    close(clientSocket);
    free(ctx); 
    return NULL;
}

int main()
{
    struct sockaddr_in serverAddress;
    int error, check, shmid;
    sqlite3 *db;
    sqlite3_stmt *stmt = NULL;
    char creator = 0;
    const char *log = "INSERT INTO Log(Name, Action) VALUES (?, ?);";
    struct sigaction sa1 = {0}, sa2 = {0};

    sa1.sa_handler = handle_sigusr1;
    sigemptyset(&sa1.sa_mask);
    sa1.sa_flags = SA_RESTART; 
    sigaction(SIGUSR1, &sa1, NULL);

    sa2.sa_handler = handle_sigusr2;
    sigemptyset(&sa2.sa_mask);
    sa2.sa_flags = 0;         
    sigaction(SIGUSR2, &sa2, NULL);
    
    shmid = shmget((key_t)SHM_KEY, sizeof(SharedBuffer), IPC_CREAT | IPC_EXCL | 0666);
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

    pthread_mutexattr_init(&thread_mutex);
    
    if (creator) {
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
        pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);

        pthread_mutex_init(&sBuff->lock, &attr);
        pthread_mutexattr_destroy(&attr);
        
        lock_mutex(&sBuff->lock);
        sBuff->x_cord = 0;
        sBuff->y_cord = 0;
        sBuff->action = none;
        sBuff->price = 0;
        sBuff->listener_pid = 0;
        pthread_mutex_unlock(&sBuff->lock);
    }
    lock_mutex(&sBuff->lock);
    sBuff->last = 0;
    check = sqlite3_open("test.db", &db);
    pthread_mutex_unlock(&sBuff->lock);

    if (check != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }
    
    sqlite3_exec(db, "PRAGMA journal_mode = WAL;", 0, 0, NULL); // shared memory mode


    check = sqlite3_prepare_v2(db, log, -1, &stmt, NULL);
    if (check == SQLITE_OK)
    {
        sqlite3_bind_text(stmt, 1, "Server started", -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, "Info", -1, SQLITE_TRANSIENT);
        
        lock_mutex(&sBuff->lock);
        sqlite3_step(stmt);
        pthread_mutex_unlock(&sBuff->lock);
        
        sqlite3_finalize(stmt);
        stmt = NULL;
    }

    // define an IPv4 TCP server    
    int TCPServer = socket(AF_INET, SOCK_STREAM, 0); 
    if (TCPServer < 0) {
        perror("Socket creation failed");
        sqlite3_close(db);
        shmdt(sBuff);
        exit(EXIT_FAILURE);
    }
    lock_mutex(&sBuff->lock);
    sBuff->listener_pid = getpid();
    sBuff->last = 0;
    pthread_mutex_unlock(&sBuff->lock);

    // listen to all addresses on port 8080
    memset(&serverAddress, 0, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(8080);
    serverAddress.sin_addr.s_addr = INADDR_ANY;

    error = bind(TCPServer, (struct sockaddr*)&serverAddress, sizeof(serverAddress));
    if (error == -1)
    {
        printf("Bind failed");
        close(TCPServer);
        sqlite3_close(db);
        shmdt(sBuff);
        return 1;
    }
    listen(TCPServer, CONNECTIONS);
    listen_function(TCPServer, sBuff);
    
    check = sqlite3_prepare_v2(db, log, -1, &stmt, NULL);
    if (check == SQLITE_OK)
    {
        sqlite3_bind_text(stmt, 1, "Server stopped", -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, "Info", -1, SQLITE_TRANSIENT);
        
        lock_mutex(&sBuff->lock);
        sqlite3_step(stmt);
        pthread_mutex_unlock(&sBuff->lock);
        
        sqlite3_finalize(stmt);
        stmt = NULL;
    }
    lock_mutex(&sBuff->lock);
    sBuff->listener_pid = 0;
    pthread_mutex_unlock(&sBuff->lock);
    close(TCPServer);
    sqlite3_close(db);
    shmdt(sBuff);
    pthread_mutex_destroy(&thread_mutex);
    if(sBuff->last)
        pthread_mutex_destroy(&sBuff->lock);
    else
        sBuff->last = 1;
    return 0;
}

