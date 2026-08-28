#include <arpa/inet.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <syslog.h>
#include <unistd.h>

void save_config(const char *path);
void load_config(const char *path);
void I2C(int fd);
void ethernet(int fd);

typedef struct
{
    char ip[16];
    char I2C_bus[256];
    int port;
    int slave_address;
} Config;

Config config = {.ip = "192.168.7.3", .port = 8080, .I2C_bus = "/dev/i2c-2", .slave_address = 42}; // defaults

/**
 * @brief
 * save the currently loaded config to file
 * should only be called if no such config exists
 *
 * @param path
 * the path for the config file
 */
void save_config(const char *path)
{
    FILE *file = fopen(path, "w");
    if (file == NULL)
    {
        syslog(LOG_WARNING, "could not create config file at %s", path);
        return;
    }
    fprintf(file, "# Server configuration\n");
    fprintf(file, "ip=%s\n", config.ip);
    fprintf(file, "port=%d\n", config.port);
    fprintf(file, "I2C_bus=%s\n", config.I2C_bus);
    fprintf(file, "slave_address=%d\n", config.slave_address);
    fclose(file);
}

/**
 * @brief
 * loads a config file
 * save currenly loaded to file if it's not found
 *
 * @param path
 * file name
 */
void load_config(const char *path)
{
    char line[256], key[64], value[192];
    FILE *file = fopen(path, "r");
    if (file == NULL)
    {
        syslog(LOG_INFO, "Config file not found, creating default at %s", path);
        save_config(path);
        return;
    }
    while (fgets(line, sizeof(line), file) != NULL)
    {
        if (line[0] == '\0' || line[0] == '#' || line[0] == '\n')
            continue;
        if (sscanf(line, "%63[^=]=%191[^\n]", key, value) != 2) // scan everything up to =
            continue;

        if (strcmp(key, "port") == 0)
            config.port = atoi(value);
        else if (strcmp(key, "slave_address") == 0)
            config.slave_address = atoi(value);
        else if (strcmp(key, "I2C_bus") == 0)
        {
            strncpy(config.I2C_bus, value, sizeof(config.I2C_bus) - 1);
            config.I2C_bus[sizeof(config.I2C_bus) - 1] = '\0';
        }
        else if (strcmp(key, "ip") == 0)
        {
            strncpy(config.ip, value, sizeof(config.ip) - 1);
            config.ip[sizeof(config.ip) - 1] = '\0';
        }
    }
    fclose(file);
}

/**
 * @brief
 * handles the I2C for the deamon
 * gets the info and sends it to the ethernet process
 *
 * @param fd
 * pipe to send the data to
 */
void I2C(int fd)
{
    int i2c = -1, check = 0;
    uint8_t pipeBuff[3] = {0};

    // config I2C
    i2c = open(config.I2C_bus, O_RDWR);
    if (i2c < 0)
    {
        syslog(LOG_ERR, "Failed to open I2C bus");
        return;
    }

    check = ioctl(i2c, I2C_SLAVE, config.slave_address);
    if (check < 0)
    {
        syslog(LOG_ERR, "Failed to set I2C slave address");
        close(i2c);
        return;
    }

    // loop to continuesly call the STM32
    while (1)
    {
        check = read(i2c, pipeBuff, sizeof(pipeBuff));
        if (check == sizeof(pipeBuff))
        {
            check = write(fd, pipeBuff, sizeof(pipeBuff));
            if (check < 0)
            {
                syslog(LOG_ERR, "Pipe write failed (Ethernet reader process died)");
                break;
            }
        }
        else
        {
            syslog(LOG_WARNING, "I2C recieve failed");
        }
        usleep(333000); // 3 requests a second
    }
    close(i2c);
    return;
}

/**
 * @brief
 * handles the ethernet connection
 * takes data from pipe and sends it to the target
 *
 * @param fd
 * pipe to get info from
 */
void ethernet(int fd)
{
    int sock = 0, check = 0;
    uint8_t inBuff[3] = {0};
    struct sockaddr_in serv_addr;
    char outBuff[100] = {0};

    // outer loop to make sure the daemon tries to reconnect if it fails
    while (1)
    {
        // config the internet
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0)
        {
            syslog(LOG_ERR, "Socket creation error: %m");
            sleep(1);
            continue;
        }

        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(config.port);

        check = inet_pton(AF_INET, config.ip, &serv_addr.sin_addr);
        if (check <= 0)
        {
            syslog(LOG_ERR, "Invalid address");
            close(sock);
            return;
        }

        check = connect(sock, (struct sockaddr *) &serv_addr, sizeof(serv_addr));
        if (check < 0)
        {
            syslog(LOG_WARNING, "Connection failed");
            close(sock);
            sleep(1);
            continue;
        }
        // inner loop
        // continuesly sends data once connected
        while (1)
        {
            check = read(fd, inBuff, sizeof(inBuff)); // atomic write
            if (check == sizeof(inBuff))
            {
                snprintf(outBuff, sizeof(outBuff), "%d %d %d\n", inBuff[0], inBuff[1], inBuff[2]);
                check = send(sock, outBuff, strlen(outBuff),
                             MSG_NOSIGNAL); // Flag to avoid the OS killing the process on connection disconnect
                if (check < 0)
                {
                    syslog(LOG_WARNING, "Send failed or connection closed");
                    break;
                }
            }
            else
            {
                syslog(LOG_ERR, "pipe broke");
                close(sock);
                return;
            }
        }
        close(sock);
    }
}

int main()
{
    int fd[2] = {0}, check = 0;

    openlog("bb_daemon", LOG_PID, LOG_USER);
    syslog(LOG_INFO, "process started");

    load_config("BB.conf");

    signal(SIGPIPE, SIG_IGN); // prevent signal from killing processes on pipe break

    check = pipe(fd);
    if (check == -1)
    {
        syslog(LOG_ERR, "Pipe creation failed");
        return 1;
    }

    pid_t pid = fork();

    if (pid < 0)
    {
        syslog(LOG_ERR, "Fork failed");
        return 1;
    }

    if (pid == 0)
    {
        // --- CHILD PROCESS (I2C) ---
        syslog(LOG_INFO, "child process started");
        close(fd[0]);

        I2C(fd[1]);

        close(fd[1]);
    }
    else
    {
        // --- PARENT PROCESS (Ethernet) ---
        syslog(LOG_INFO, "fork successful");
        close(fd[1]);

        ethernet(fd[0]);

        close(fd[0]);
    }
    closelog();
    return 0;
}