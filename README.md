# epoll-cpp

# Introduction

A simple C++17 wrapper library around the Linux epoll API.
Provides you with an easy way of asynchronously listening for a linux file descriptor events in 4 lines of C++ code.

## Usage

### 1) Create the Epoll object

We first construct the Epoll object which uses the epoll_create1() system call to make an epoll.

There is a boolean constructor parameter `isEdgeTriggered`. In this case we've created an edge triggered epoll.

The edge triggered epoll will automatically set any file descriptors added to it to a non-blocking mode.

```cpp
Epoll epoll{true};
```

### 2) Register a file descriptor
Using the `addDescriptor` method we'll register a file descriptor with this Epoll instance.

**Warning**: `addDescriptor` must always be called first. `addEventHandler` will throw an exception if it's called before the descriptor is added.

```cpp
epoll.addDescriptor(serverSocketFd);
```

### 3) Add event callback functions

Now we can add callback functions to catch events produced by a file descriptor.

The `addEventHandler` uses standard epoll event types as defined here https://man7.org/linux/man-pages/man2/epoll_ctl.2.html

```cpp
epoll.addEventHandler(clientFd, EPOLLIN, onClientWrite);
```

### 4) Set up an event loop

Epoll is an example of [event-driven programming](https://en.wikipedia.org/wiki/Event-driven_programming), so we need to add an event loop which will wait for events to occur (`waitForEvents()` is a blocking call).

```cpp
for (;;) {
    epoll.waitForEvents(); 
}
```

For a more detailed explanation see the example below. 

# Example code

The following code starts a simple TCP server on localhost:3001. Open localhost:3001 in your browser to get a hello world message. The browser's HTTP request will be printed into the console as well.

```cpp
#include "Epoll.h"
#include <string>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdexcept>
#include <iostream>
#include <csignal>

constexpr unsigned int MAX_BUF_LENGTH = 4096;
constexpr unsigned int TCP_ACCEPT_BACKLOG = 5;

int serverSocketFd;
Epoll epoll{true};

/**
 * Called when data is written to the socket (client sent data)
 */
void onClientWrite(int clientFd) {
    int bytesReceived = 0;
    std::string rcv;
    std::vector<char> buffer(MAX_BUF_LENGTH);

    bytesReceived = recv(clientFd, &buffer[0], buffer.size(), 0);
    if (bytesReceived == -1) {
        throw std::runtime_error("Failed to receive data on this socket. (FD" + std::to_string(clientFd) + ")");
    } else {
        rcv.append(buffer.cbegin(), buffer.cbegin() + bytesReceived);
        std::cout << "Received " << bytesReceived << " bytes of data from FD" << clientFd << "\nMessage content: " << rcv << std::endl;
    }

    //Send HTTP hello world to client
    const std::string &httpHello = "HTTP/1.1 200 OK\r\nContent-Length: 20\r\nContent-Type: text/html\r\n\r\n<h1>Hello world</h1>";
    send(clientFd, httpHello.c_str(), httpHello.size(), 0);
}

/**
 * Called when client terminates the TCP connection
 */
void onClientDisconnect(int clientFd) {
    std::cout << "TCP client FD" << clientFd << " has disconnected." << std::endl;

    // If handler for EPOLLRDHUP or EPOLLHUP is added, the descriptor will be removed from the Epoll instance automatically.
    // Otherwise, you must in order to free memory use the removeDescriptor method like so:
    // epoll.removeDescriptor(clientFd);
}

/**
 * Accepts new TCP connections to the server
 */
void tcpAccept(int serverFd) {
    int clientFd;                    // Client's socket - client sends requests via this socket
    struct sockaddr_in remoteAddr{}; // Client's address
    socklen_t remoteAddrLen{};         // Length of client's address

    clientFd = accept(serverFd, (struct sockaddr *) &remoteAddr, &remoteAddrLen);

    if (clientFd > 0) {
        std::cout << "A new TCP client FD" << clientFd << " connected to server FD" << serverFd << std::endl;

        // Add this client socket to the Epoll
        epoll.addDescriptor(clientFd);

        // The Epoll instance will call our handler functions once user writes something to the socket or disconnects
        epoll.addEventHandler(clientFd, EPOLLIN, onClientWrite);
        epoll.addEventHandler(clientFd, EPOLLRDHUP | EPOLLHUP, onClientDisconnect);
    } else {
        throw std::runtime_error("Fatal error in tcpAccept of server socket FD" + std::to_string(serverFd)
                                 + " TCP accept failed. " + std::to_string(clientFd));
    }
}

/**
 * Initializes the server socket and starts listening for connections on provided port + ip
 */
void startServer(const std::string &address, uint16_t port) {
    struct sockaddr_in localAddr{};
    localAddr.sin_family = AF_INET;                             // IPv4
    localAddr.sin_port = htons(port);                  // Server listens on this port
    localAddr.sin_addr.s_addr = inet_addr(address.c_str()); // Server listens on this address

    // Create the socket
    if ((serverSocketFd = socket(AF_INET, SOCK_STREAM, 0)) <= 0) {
        throw std::runtime_error("Failed to create a server socket (system resource error?)");
    }

    // Bind socket to port and ip
    if (bind(serverSocketFd, (struct sockaddr *) &localAddr, sizeof(struct sockaddr_in)) != 0) {
        throw std::runtime_error("Failed bind server socket. (FD" + std::to_string(serverSocketFd) + ") (Port: " + std::to_string(port)
                                 + ")");
    }

    // Listen on bound port and ip
    if (listen(serverSocketFd, TCP_ACCEPT_BACKLOG) != 0) {
        throw std::runtime_error("Failed listen on server socket. (FD" + std::to_string(serverSocketFd) + ")");
    }

    std::cout << "A new server socket FD" << serverSocketFd << " is now listening on port " << port << std::endl;

    // Register this server socket with the epoll
    epoll.addDescriptor(serverSocketFd);
    // Notice the use of C++11 lambda (for demonstration purposes)
    epoll.addEventHandler(serverSocketFd, EPOLLIN | EPOLLOUT, [](int serverFd) { tcpAccept(serverFd); });
}

int main(int argc, char **argv) {
    startServer("127.0.0.1", 3000);

    for (;;) {
        epoll.waitForEvents();
    }

    close(serverSocketFd);
}
```

# Additional information about the epoll system call

https://suchprogramming.com/epoll-in-3-easy-steps/

https://en.wikipedia.org/wiki/Epoll

https://www.hackingnote.com/en/versus/select-vs-poll-vs-epoll/

https://man7.org/linux/man-pages/man7/epoll.7.html

https://man7.org/linux/man-pages/man2/epoll_ctl.2.html

# Todo

* Use enums instead of linux library preprocessor macros to make public methods more user-friendly
* CMake external library example