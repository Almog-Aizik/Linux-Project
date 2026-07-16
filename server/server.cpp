#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace std;

int main()
{
    // define an IPv4 TCP server    
    int TCPServer = socket(AF_INET, SOCK_STREAM, 0); 
    if (TCPServer < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    // listen to all addresses on port 8080
    sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(8080);
    serverAddress.sin_addr.s_addr = INADDR_ANY;

    bind(TCPServer, (struct sockaddr*)&serverAddress, sizeof(serverAddress));
    listen(TCPServer, 5);
    
    int clientSocket = accept(TCPServer, nullptr, nullptr);

    char buffer[100] = {0};
    recv(clientSocket, buffer, sizeof(buffer), 0);
    cout << "Message from client: " << buffer << endl;

    close(TCPServer);

    return 0;
}