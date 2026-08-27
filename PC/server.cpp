#define _POSIX_C_SOURCE 200809L
#include "shared_defs.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <pthread.h>
#include <sqlite3.h>
#include <string>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace std;

typedef struct
{
    int Socket;
    SharedBuffer *sBuff;
} ClientContext;

struct ServerConfig
{
    int port = 8080;
    int max_connections = 5;
    char db_path[256] = "test.db";
    int recv_timeout_sec = 1;
    int shared_key = 344865;
};

volatile sig_atomic_t sig_update = 0;
volatile sig_atomic_t sig_shutdown = 0;
pthread_mutex_t thread_mutex;
atomic_int active_threads = 0;
struct ServerConfig config;

void handle_sigusr1(int sig);
void handle_sigusr2(int sig);
void *client_handler(void *arg);
int listen_function(int TCPServer, SharedBuffer *sBuff);
void load_config(const string &path);
void save_config(const std::string &path);

/**
 * @brief
 * update signal handler
 */
void handle_sigusr1(int sig)
{
    sig_update++;
}

/**
 * @brief
 * shutdown signal handler
 */
void handle_sigusr2(int sig)
{
    sig_shutdown = 1;
}

/**
 * @brief
 * save the currently loaded config to file
 * should only be called if no such config exists
 *
 * @param path
 * the path for the config file
 */
void save_config(const std::string &path)
{
    std::ofstream file(path);
    if (!file.is_open())
    {
        std::cout << "Warning: could not create config file at " << path << std::endl;
        return;
    }

    file << "# Server configuration\n";
    file << "port=" << config.port << "\n";
    file << "max_connections=" << config.max_connections << "\n";
    file << "recv_timeout_sec=" << config.recv_timeout_sec << "\n";
    file << "# need to be synced with SQLite\n";
    file << "shared_key=" << config.shared_key << "\n";
    file << "db_path=" << config.db_path << "\n";
}

/**
 * @brief
 * loads a config file
 * save currenly loaded to file if it's not found
 *
 * @param path
 * file name
 */
void load_config(const string &path)
{
    ifstream file(path);
    string line, key, value;
    size_t equal;
    if (!file.is_open())
    {
        cout << "Config file not found, creating default at " << path << endl;
        save_config(path); // write defaults so the file exists next time
        return;
    }

    while (getline(file, line))
    {
        if (line.empty() || line[0] == '#') // # for comments in the config file
            continue;

        equal = line.find('=');
        if (equal == string::npos)
            continue;

        key = line.substr(0, equal);
        value = line.substr(equal + 1);

        if (key == "port")
            config.port = stoi(value);
        else if (key == "max_connections")
            config.max_connections = stoi(value);
        else if (key == "db_path")
        {
            strncpy(config.db_path, value.c_str(), sizeof(config.db_path) - 1);
            config.db_path[sizeof(config.db_path) - 1] = '\0'; // guarantee null termination
        }
        else if (key == "recv_timeout_sec")
            config.recv_timeout_sec = stoi(value);
        else if (key == "shared_key")
            config.shared_key = stoi(value);
    }
}

/**
 * @brief
 * handler function for clients, creates a thread seperate for each client once it recieves one.
 * gives each thread the shared buffer for processing.
 *
 * @param TCPServer server identifier
 * @param sBuff shared memory
 * @return int success code
 */
int listen_function(int TCPServer, SharedBuffer *sBuff)
{
    int error;
    pthread_t thread_id;
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;

    error = sqlite3_open(config.db_path, &db);
    if (error)
    {
        cout << "Failed to open DB" << endl;
        return -1;
    }

    // keep listening until shut down
    while (sig_shutdown == 0)
    {
        int clientSocket = accept(TCPServer, NULL, NULL);
        if (clientSocket < 0)
        {
            // retry if interrupted by a sigal
            if (errno == EINTR)
                continue;

            log_event(db, sBuff, "Accept failed", "Error");
            continue;
        }

        // prevent unwanted interaction between clients by splitting them
        ClientContext *ctx = (ClientContext *) malloc(sizeof(ClientContext));
        if (ctx == NULL)
        {
            log_event(db, sBuff, "Client context allocation failed", "Error");

            close(clientSocket);
            continue;
        }

        ctx->sBuff = sBuff;
        ctx->Socket = clientSocket;

        error = pthread_create(&thread_id, NULL, client_handler, ctx);
        if (error != 0)
        {
            log_event(db, sBuff, "Failed to create thread", "Error");

            close(clientSocket);
            free(ctx);
        }
        else
        {
            // Detach thread so it is automically cleaned upon exit
            pthread_detach(thread_id);
        }
    }
    sqlite3_close(db);
    return 0;
}

/**
 * @brief
 * client handler for each client.
 * records transactions and log whenever a client enters or leaves an area.
 *
 * @param arg
 * the client context to be able to listen and get data from
 */
void *client_handler(void *arg)
{
    struct timeval tv = {.tv_sec = config.recv_timeout_sec, .tv_usec = 0};
    ClientContext *ctx = (ClientContext *) arg;
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    SharedBuffer *sBuff = ctx->sBuff;
    time_t start = 0, stop;
    sig_atomic_t last_update = sig_update;
    int clientSocket = ctx->Socket, check, x_cord, y_cord;
    long elapsed, price; // 64 bits returned so avoiding int
    char buffer[100], user[50];
    string city;
    const char *sql2, *last_act, *temp;
    const char *sql = "SELECT action, strftime('%s', time) FROM Log "
                      "WHERE action != 'Error' AND action != 'Info' AND name = ? "
                      "ORDER BY Time DESC "
                      "LIMIT 1";
    const char *sql3 = "INSERT INTO Log(Name, Action, Price) VALUES (?, ?, ?);";

    check = sqlite3_open(config.db_path, &db);
    if (check)
    {
        cout << "Thread failed to open DB" << endl;
        close(clientSocket);
        free(ctx);
        return NULL;
    }

    log_event(db, sBuff, "Connection recieved, thread started", "Info");
    setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    active_threads++;

    while (!sig_shutdown)
    {
        memset(buffer, 0, sizeof(buffer)); // make sure all requests are saved to a clean buffer
        ssize_t bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        last_update = sig_update;

        if (bytesReceived < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            {
                // Ignore signal interrupts
                // And wake up every second
                continue;
            }
            break;
        }
        if (bytesReceived == 0)
        {
            log_event(db, sBuff, "Client disconnected", "Info");
            break;
        }

        // get client info, coordinates and an identifier
        // [^\n] looks at all characters but new line
        // letters and spaces may appear in the identifier
        check = sscanf(buffer, "%d %d %49[^\n]", &x_cord, &y_cord, user);

        if (check == 3)
        {
            sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
            if (stmt == NULL)
            {
                cout << "Error preparing statement" << endl;
                log_event(db, sBuff, "Error preparing statement", "Error");
                continue;
            }
            lock_mutex(db, sBuff);
            sqlite3_bind_text(stmt, 1, user, -1, SQLITE_TRANSIENT);
            check = sqlite3_step(stmt);
            pthread_mutex_unlock(&sBuff->lock);
            // use the log to check what was the last action the client took
            if (check == SQLITE_ROW)
            {
                last_act = (const char *) sqlite3_column_text(stmt, 0);
                start = sqlite3_column_int64(stmt, 1);
            }
            else
            {
                last_act = "stop"; // pretend it stopped prior to the first entry
            }

            //-------client starting-----
            // reminder, last_act is the last action client took
            // not current action he's taking
            if (strncmp(last_act, "stop", 5) == 0)
            {
                sqlite3_finalize(stmt);
                stmt = NULL;

                log_event(db, sBuff, user, "start");
            }
            //-------client stopping--------
            // reminder, last_act is the last action client took
            // not current action he's taking
            else if (strncmp(last_act, "start", 5) == 0)
            {
                sqlite3_finalize(stmt);
                stmt = NULL;
                // need to get the time spent to get the proper price
                // happens in seconds
                stop = time(NULL);
                elapsed = difftime(stop, start);
                // signal to check update recieved, handle that
                if (sig_update != last_update)
                {
                    if (x_cord == sBuff->x_cord && y_cord == sBuff->y_cord)
                    {
                        lock_mutex(db, sBuff);
                        city = sBuff->city;
                        price = sBuff->price;
                        pthread_mutex_unlock(&sBuff->lock);
                        check = SQLITE_OK;
                    }
                }
                else
                {
                    sql2 = "SELECT COALESCE(Price, 0), Name FROM cities "
                           "WHERE x_cord = ? AND y_cord = ?;";
                    check = sqlite3_prepare_v2(db, sql2, -1, &stmt, NULL);
                }
                if (check == SQLITE_OK)
                {
                    if (sig_update == last_update)
                    {
                        sqlite3_bind_int(stmt, 1, x_cord);
                        sqlite3_bind_int(stmt, 2, y_cord);
                        check = sqlite3_step(stmt);
                    }
                    else
                    {
                        check = SQLITE_ROW;
                    }
                    // handle the city request
                    if (check == SQLITE_ROW)
                    {
                        if (sig_update == last_update)
                        {
                            price = sqlite3_column_int(stmt, 0);
                            temp = (const char *) sqlite3_column_text(stmt, 1);
                            if (temp != nullptr)
                                city = temp;
                            else
                                city = "null";
                        }
                        price = price * elapsed;

                        sqlite3_finalize(stmt);
                        stmt = NULL;

                        sql2 = "INSERT INTO customers(Name, pay, location) VALUES (?, ?, ?);";
                        check = sqlite3_prepare_v2(db, sql2, -1, &stmt, NULL);
                        if (check == SQLITE_OK)
                        {
                            sqlite3_bind_text(stmt, 1, user, -1, SQLITE_TRANSIENT);
                            sqlite3_bind_text(stmt, 3, city.c_str(), -1, SQLITE_TRANSIENT);
                            sqlite3_bind_int(stmt, 2, price);

                            lock_mutex(db, sBuff);
                            sqlite3_step(stmt);
                            pthread_mutex_unlock(&sBuff->lock);

                            sqlite3_finalize(stmt);
                            stmt = NULL;
                        }
                        // save the user and price he paid in the log;
                        check = sqlite3_prepare_v2(db, sql3, -1, &stmt, NULL);
                        if (check == SQLITE_OK)
                        {
                            sqlite3_bind_text(stmt, 1, user, -1, SQLITE_TRANSIENT);
                            sqlite3_bind_text(stmt, 2, "stop", -1, SQLITE_TRANSIENT);
                            sqlite3_bind_int(stmt, 3, price);

                            lock_mutex(db, sBuff);
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
                        // need to prevent duplicate attempts at stopping
                        // also handle the error of city not found
                        check = sqlite3_prepare_v2(db, sql3, -1, &stmt, NULL);
                        if (check == SQLITE_OK)
                        {
                            sqlite3_bind_text(stmt, 1, user, -1, SQLITE_TRANSIENT);
                            sqlite3_bind_text(stmt, 2, "stop", -1, SQLITE_TRANSIENT);
                            sqlite3_bind_int(stmt, 3, 0);

                            lock_mutex(db, sBuff);
                            sqlite3_step(stmt);
                            pthread_mutex_unlock(&sBuff->lock);

                            sqlite3_reset(stmt);
                            sqlite3_clear_bindings(stmt);

                            sqlite3_bind_text(stmt, 1, "Location Not Found", -1, SQLITE_TRANSIENT);
                            sqlite3_bind_text(stmt, 2, "Error", -1, SQLITE_TRANSIENT);
                            sqlite3_bind_int(stmt, 3, 0);

                            lock_mutex(db, sBuff);
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

                log_event(db, sBuff, "Invalid Input", "Error");
            }
        }
        else
        {
            log_event(db, sBuff, "Invalid Input", "Error");
        }
    }

    log_event(db, sBuff, "Thread shutting down", "Info");
    sqlite3_close(db);
    close(clientSocket);
    free(ctx);
    active_threads--;
    return NULL;
}

/**
 * @brief main process
 *  set the server up then splits the clients into their own threads as needed.
 *  closes the server properly at the end.
 *
 * @return int exit code
 */
int main()
{
    struct sockaddr_in serverAddress;
    int error, check, shmid;
    sqlite3 *db;
    char creator = 0;
    struct sigaction sa1 = {0}, sa2 = {0};

    load_config("server.conf");

    check = sqlite3_open(config.db_path, &db);
    if (check != SQLITE_OK)
    {
        cout << "Cannot open database" << endl;
        sqlite3_close(db);
        return 1;
    }

    // define the signal handlers
    // sigusr1 for update notifications, so restart syscalls is enabled.
    // sigusr2 for shutdown
    sa1.sa_handler = handle_sigusr1;
    sigemptyset(&sa1.sa_mask);
    sa1.sa_flags = SA_RESTART;
    sigaction(SIGUSR1, &sa1, NULL);

    sa2.sa_handler = handle_sigusr2;
    sigemptyset(&sa2.sa_mask);
    sa2.sa_flags = 0;
    sigaction(SIGUSR2, &sa2, NULL);

    shmid = shmget((key_t) config.shared_key, sizeof(SharedBuffer), IPC_CREAT | IPC_EXCL | 0666);
    if (shmid != -1)
    {
        creator = 1;
    }
    else if (errno == EEXIST)
    {
        shmid = shmget((key_t) config.shared_key, sizeof(SharedBuffer), 0666);
        if (shmid == -1)
        {
            cout << "shmget attach failed" << endl;
            return 1;
        }
    }
    else
    {
        cout << "shmget failed" << endl;
        return 1;
    }

    SharedBuffer *sBuff = (SharedBuffer *) shmat(shmid, NULL, 0);
    if (sBuff == (void *) -1)
    {
        cout << "shmat failed" << endl;
        return 1;
    }

    pthread_mutex_init(&thread_mutex, NULL);

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
        sBuff->x_cord = 0;
        sBuff->y_cord = 0;
        sBuff->action = none;
        sBuff->price = 0;
        sBuff->listener_pid = 0;
        sBuff->init = 1;
        pthread_mutex_unlock(&sBuff->lock);
    }

    sqlite3_exec(db, "PRAGMA journal_mode = WAL;", 0, 0, NULL); // shared memory mode

    log_event(db, sBuff, "Server started", "Info");

    // define an IPv4 TCP server
    int TCPServer = socket(AF_INET, SOCK_STREAM, 0);
    if (TCPServer < 0)
    {
        cout << "Socket creation failed" << endl;
        sqlite3_close(db);
        shmdt(sBuff);
        exit(EXIT_FAILURE);
    }
    lock_mutex(db, sBuff);
    sBuff->listener_pid = getpid();
    pthread_mutex_unlock(&sBuff->lock);

    // listen to all addresses on port from the config
    memset(&serverAddress, 0, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(config.port);
    serverAddress.sin_addr.s_addr = INADDR_ANY;

    error = bind(TCPServer, (struct sockaddr *) &serverAddress, sizeof(serverAddress));
    if (error == -1)
    {
        log_event(db, sBuff, "Server address bind failed", "Error");
        close(TCPServer);
        sqlite3_close(db);
        pthread_mutex_destroy(&thread_mutex);
        shmdt(sBuff);
        return 1;
    }
    listen(TCPServer, config.max_connections);
    listen_function(TCPServer, sBuff);

    log_event(db, sBuff, "Server stopped", "Info");

    while (active_threads > 0)
    {
        usleep(10000); // Check every 10ms
    }

    lock_mutex(db, sBuff);
    sBuff->listener_pid = 0;
    pthread_mutex_unlock(&sBuff->lock);
    close(TCPServer);

    if (creator)
    {
        shmctl(shmid, IPC_RMID, NULL);
        cout << "destroyed shared memory" << endl;
    }
    sqlite3_close(db);
    shmdt(sBuff);
    pthread_mutex_destroy(&thread_mutex);
    return 0;
}
