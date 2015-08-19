#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <vector>
#include <map>
#include <unistd.h>
#include <stdlib.h>
#include <cstring>
#include <string>
#include <iostream>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <signal.h>
#include <vector>
#include <netdb.h>
#include <ev.h>
#include <fstream>
using namespace std;

#define BUFSIZE 1024

extern char *optarg;

class dst
{
public:
    struct sockaddr_in addr;
    char host[32], port[16];

    dst() {}

    dst(const char *ip)
    {
        bzero(host, 32);
        bzero(port, 16);

        char *tmp = strchr(ip, ':');
        strncpy(host, ip, tmp - ip);
        strcpy(port, tmp + 1);

        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr(host);
        addr.sin_port = htons(atoi(port));
    }

    void print() const { cout << host << " : " << port << endl; }
};

class client_io: public ev_io
{
public:
    int server;

    client_io(int s): server(s) {}
};

class server_io: public ev_io
{
public:
    int client;

    server_io(int c): client(c) {}
};

class listen_io: public ev_io
{
public:
    int proxy;
    vector<dst> dst_servs;

    listen_io(int p, vector<dst> &d): proxy(p), dst_servs(d) {}
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
static void client_cb(struct ev_loop *loop, struct ev_io *client, int revents)
{
    char cache[BUFSIZE];
    char *buf = (char *) malloc(BUFSIZE);

    int buf_len = BUFSIZE;
    
    if (0 == recv(client->fd, cache, BUFSIZE, 0))
    //CLIENT DISCONNECTED
    {
        std::cout << "client disconnected\n";
        ev_io_stop(loop, client);
        delete static_cast<client_io *>(client);
        close(client->fd);
        close(static_cast<client_io *>(client)->server);
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

        send(static_cast<client_io *>(client)->server, buf, buf_len, 0);
    }

    free(buf);
}

//CALLBACK ON SERVER MESSAGE / DISCONNECT
static void server_cb(struct ev_loop *loop, struct ev_io *server, int revents)
{
    char cache[BUFSIZE];
    char *buf = (char *) malloc(BUFSIZE);

    int buf_len = BUFSIZE;
    
    if (0 == recv(server->fd, cache, BUFSIZE, 0))
    //SERVER DISCONNECTED
    {
        std::cout << "DISCONNECT\n";
    }
    else
    //RECIEVED MESSAGE FROM SERVER
    {
        memcpy(buf, cache, BUFSIZE);

        //reading whole message in buf
        while (recv(server->fd, cache, BUFSIZE, 0) > 0)
        {
            buf_len += BUFSIZE;
            buf = (char *) realloc(buf, buf_len); 

            memcpy(buf + (buf_len - BUFSIZE), cache, BUFSIZE);
        }

        send(static_cast<server_io *>(server)->client, buf, buf_len, 0);
    }

    free(buf);
}

//CALLBACK ON CLIENT ACCEPT
static void accept_cb(struct ev_loop *loop, struct ev_io *proxy_ev, int revents)
{
    listen_io *inf = static_cast<listen_io *>(proxy_ev);
    //MAKING CLIENT SOCKET
    int client_socket = accept(inf->proxy, 0, 0);
    set_nonblock(client_socket);

    cout << "accepted client " << endl;

    //MAKING SERVER SOCKET
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    int serv_no = rand() % inf->dst_servs.size();
    struct sockaddr_in server_addr = inf->dst_servs[serv_no].addr;

    //CONNECTING TO SERVER    
    if (connect(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
        throw "Error connecting to server";
    set_nonblock(server_socket);

    //CLIENT EVENT
    struct ev_io *client_watcher = new client_io(server_socket);
    ev_init(client_watcher, client_cb);
    ev_io_set(client_watcher, client_socket, EV_READ);
    ev_io_start(loop, client_watcher);

    //SERVER EVENT
    struct ev_io *server_watcher = new server_io(client_socket);
    ev_init(server_watcher, server_cb);
    ev_io_set(server_watcher, server_socket, EV_READ);
    ev_io_start(loop, server_watcher);
}

void parse_conf(map<int, vector<dst>> &conf, const char *path)
{
    char buf[BUFSIZE];

    ifstream fin(path);
    char *token;
    int port;

    while (fin.getline(buf, BUFSIZE))
    {
        token = strtok(buf, ", ");
        port = atoi(token);

        vector<dst> dst_servs;
        while ((token = strtok(NULL, ", ")))
        {
            dst_servs.push_back(dst(token));
        }
        conf.insert( pair<int, vector<dst>>(port, dst_servs) );
    }
} 

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::cerr << "wrong arguments\n";
        exit(1);
    }

    map<int, vector<dst>> conf;
    parse_conf(conf, argv[1]);

    //MAKING EVENT LOOP
    struct ev_loop *loop = ev_default_loop(0);

    //CREATING PROXY
    struct sockaddr_in proxy_addr;
    for (auto pair:conf)
    {
        int proxy = socket(AF_INET, SOCK_STREAM, 0);
        proxy_addr.sin_family = AF_INET;
        proxy_addr.sin_port = htons(pair.first);
        proxy_addr.sin_addr.s_addr = htonl(INADDR_ANY);

        //BINDING PROXY TO ADDRESS
        if (::bind(proxy, (struct sockaddr*)&proxy_addr, sizeof(proxy_addr)) < 0)
        {
            std::cerr << "Error in bind\n";
            exit(1);
        }

        //PREPAIRING TO ACCEPT CLIENTS
        if (::listen(proxy, SOMAXCONN) < 0)
        {
            std::cerr << "Error in listen\n";
            exit(1);
        }

        //MAKING EVENT FOR ACCEPT
        struct ev_io *watcher = new listen_io(proxy, pair.second);
        ev_init(watcher, accept_cb);
        ev_io_set(watcher, proxy, EV_READ);
        ev_io_start(loop, watcher);
    }

    printf("Waiting for clients...\n");

    //RUNNING EVENT LOOP
    while (true)
    {
        ev_loop(loop, 0);
    }
}