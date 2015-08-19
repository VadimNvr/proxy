#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <vector>
#include <unistd.h>
#include <stdlib.h>
#include <cstring>
#include <string>
#include <iostream>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <signal.h>
#include <list>
#include <netdb.h>
#include <ev.h>

#define BUFSIZE 1024

//CREATING A LIST OF CLIENTS
std::list<int> clients;

class my_io: public ev_io
{
public:
    int val;
};

int set_nonblock(int fd)
{
    int flags;
    #ifdef O_NONBLOCK
    if (-1 == (flags = fcntl(fd, F_GETFL, 0))) flags = 0;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    #endif
    flags = 1;
    return ioctl(fd, FIONBIO, &flags);
}

//CALLBACK ON CLIENT MESSAGE / DISCONNECT
static void read_cb(struct ev_loop *loop, struct ev_io *client, int revents)
{
    char cache[BUFSIZE];
    char *buf = (char *) malloc(BUFSIZE);

    int buf_len = BUFSIZE;
    
    if (0 == recv(client->fd, cache, BUFSIZE, 0))
    //CLIENT DISCONNECTED
    {
        clients.remove(client->fd);
        std::cout << "connection terminated\n";
        ev_io_stop(loop, client);
        delete static_cast<my_io *>(client);
        close(client->fd);
    }
    else
    //RECIEVED MESSAGE FROM CLIENT
    {
        memcpy(buf, cache, BUFSIZE);

        //reading whole message in buf
        while (recv(client->fd, cache, BUFSIZE, 0) > 0)
        {
            buf_len += BUFSIZE;
            buf = (char *) realloc(buf, buf_len); 

            memcpy(buf + (buf_len - BUFSIZE), cache, BUFSIZE);
        }

        std::cout << buf << std::endl;
        send(client->fd, buf, BUFSIZE, 0);
    }

    free(buf);
}

//CALLBACK ON CLIENT ACCEPT
static void accept_cb(struct ev_loop *loop, struct ev_io *server, int revents)
{
    int client_socket = accept(server->fd, 0, 0);
    set_nonblock(client_socket);
    clients.push_front(client_socket);

    struct ev_io *watcher = new my_io;
    ev_init(watcher, read_cb);
    ev_io_set(watcher, client_socket, EV_READ);
    ev_io_start(loop, watcher);

    std::cout << "accepted connection\n";
}

int main(int argc, char **argv)
{
    char *host, *port;
    int c;

    if (argc != 2)
        throw "Not enough arguments";

    //CREATING SERVER
    int server = socket(AF_INET, SOCK_STREAM, 0);

    //FILLING SERVER ADDRESS STRUCTURE
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(argv[1]));
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    //BINDING SERVER TO ADDRESS
    if (bind(server, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
    {
        std::cerr << "Error in bind\n";
        exit(1);
    }

    //PREPAIRING TO ACCEPT CLIENTS
    if (listen(server, SOMAXCONN) < 0)
    {
        std::cerr << "Error in listen\n";
        exit(1);
    }

    //MAKING EVENT LOOP
    struct ev_loop *loop = ev_default_loop(0);

    //MAKING EVENT FOR ACCEPT
    struct ev_io *watcher = new my_io;
    ev_init(watcher, accept_cb);
    ev_io_set(watcher, server, EV_READ);
    ev_io_start(loop, watcher);

    printf("Waiting for clients...\n");

    //RUNNING EVENT LOOP
    while (true)
    {
        ev_loop(loop, 0);
    }

    shutdown(server, SHUT_RDWR);
    close(server);
}