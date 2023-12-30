#pragma once
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <zstd.h>

//
// logging macros
//

#define zkt_verr(FMT, ...)  fprintf(stderr, "zkt:err:" FMT "!\n", __VA_ARGS__)
#define zkt_err(MSG)        fprintf(stderr, "zkt:err:" MSG "!\n")
#define zkt_vlog(FMT, ...)  printf("zkt:log:" FMT "\n", __VA_ARGS__)
#define zkt_log(MSG)        printf("zkt:log:" MSG "\n")

//
// data
//

typedef struct zkt_data {
    void* buffer;
    size_t size;
} zkt_data;

zkt_data* zkt_data_compress(const void* buf, const size_t size, int compression);
zkt_data* zkt_data_decompress(const void* buf, const size_t size);
void zkt_data_clean(zkt_data* data);

int zkt_data_send(int fd, zkt_data* data);
int zkt_data_send_compress(int fd, const void* buf, const size_t size, int compression);
zkt_data* zkt_data_recv(int fd);

//
// send and recv
//

int zkt_send(int fd, const void* buf, const size_t size);
int zkt_recv(int fd, void* buf, const size_t size);

//
// server
//

typedef struct zkt_server {
    struct addrinfo* sai;
    int fd;
    char ip[INET6_ADDRSTRLEN];

    struct sockaddr_storage ra;
    int rfd;
    char rip[INET6_ADDRSTRLEN];
} zkt_server;

zkt_server* zkt_server_init(const char* port);
int zkt_server_accept(zkt_server* server, void (*func)(int));

//
// client
//

typedef struct zkt_client {
    const char* host;
    const char* port;
    struct addrinfo* ai;
    int fd;
} zkt_client;

zkt_client* zkt_client_init(const char* host, const char* port);
void zkt_client_reconnect(zkt_client** client);
