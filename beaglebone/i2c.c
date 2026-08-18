#include <arpa/inet.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#define IP "192.168.7.3"
#define PORT 8080
#define I2C_BUS "/dev/i2c-2"
#define SLAVE_ADDR 0x2A

int main()
{
    int fd = -1, sock = 0, check = 0;
    uint8_t inBuff[3] = {0};
    struct sockaddr_in serv_addr;
    char outBuff[100] = {0};

    // config I2C
    fd = open(I2C_BUS, O_RDWR);
    if (fd < 0)
    {
        printf("Failed to open I2C bus\n");
        return 1;
    }

    check = ioctl(fd, I2C_SLAVE, SLAVE_ADDR);
    if (check < 0)
    {
        printf("Failed to set I2C slave address\n");
        close(fd);
        return 1;
    }
    // outer loop to make sure the daemon tries to reconnect if it fails
    while (1)
    {
        // config the internet
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0)
        {
            printf("Socket creation error\n");
            sleep(1);
            continue;
        }

        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(PORT);

        check = inet_pton(AF_INET, IP, &serv_addr.sin_addr);
        if (check <= 0)
        {
            printf("Invalid address\n");
            close(sock);
            sleep(1);
            continue;
        }

        check = connect(sock, (struct sockaddr *) &serv_addr, sizeof(serv_addr));
        if (check < 0)
        {
            printf("Connection failed\n");
            close(sock);
            sleep(1);
            continue;
        }
        // inner loop
        // continuesly sends data once connected
        while (1)
        {
            check = read(fd, inBuff, 3);
            if (check == 3)
            {
                snprintf(outBuff, sizeof(outBuff), "%d %d %d\n", inBuff[0], inBuff[1], inBuff[2]);
                check = send(sock, outBuff, strlen(outBuff),
                             MSG_NOSIGNAL); // Flag to avoid the OS killing the process on connection disconnect
                if (check < 0)
                {
                    printf("Send failed or connection closed\n");
                    break;
                }
            }
            else
            {
                printf("Failed to read data from STM32\n");
            }

            usleep(333000); // 3 requests a second
        }
        close(sock);
    }

    close(fd);
    close(sock);
    return 0;
}