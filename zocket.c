#include "zocket.h"
#include <stdlib.h>

//
// data specific functions
//

zkt_data* zkt_data_compress(const void* buf, const size_t size, int compression) {
    zkt_data* data = malloc(sizeof(zkt_data));
    if(!data) {
        zkt_err("could not allocate memory for zkt_data");
        return NULL;
    }

    size_t estimate = ZSTD_compressBound(size);
    data->buffer = malloc(estimate);
    if(!data->buffer) {
        zkt_err("could not allocate memory for zkt_data->buffer");
        return NULL;
    }

    size_t compressed_size = ZSTD_compress(data->buffer, estimate, buf, size, compression);
    if(ZSTD_isError(compressed_size)) {
        zkt_err("could not compress provided buffer");
        return NULL;
    }

    void* new_pointer = realloc(data->buffer, compressed_size);
    if(!new_pointer) {
        zkt_err("failed to compress and realloc data. returning data without compression");
        data->size = estimate;
        return data;
    }

    data->buffer = new_pointer;
    data->size = compressed_size;
    return data;
}

zkt_data* zkt_data_decompress(const void* buf, const size_t size) {
    zkt_data* data = malloc(sizeof(zkt_data));
    if(!data) {
        zkt_err("could not allocate memory for zkt_data");
        return NULL;
    }

    const size_t estimate = ZSTD_findFrameCompressedSize(buf, size);
    data->buffer = malloc(estimate);
    if(!data->buffer) {
        zkt_err("could not allocate memory for zkt_data->buffer");
        return NULL;
    }

    size_t decompressed_size = ZSTD_decompress(data->buffer, estimate, buf, size);
    if(ZSTD_isError(decompressed_size)) {
        zkt_err("could not compress provided buffer");
        return NULL;
    }

    data->size = estimate;

    return data;
}

void zkt_data_clean(zkt_data* data) {
    free(data->buffer);
    free(data);
}

int zkt_data_send(int fd, zkt_data* data) {
    int ret = zkt_send(fd, &data->size, sizeof(size_t));
    if(ret == -1) return -1;
    return zkt_send(fd, data->buffer, data->size);
}

int zkt_data_send_compress(int fd, const void* buf, const size_t size, int compression) {
    zkt_data* data = zkt_data_compress(buf, size, compression);
    if(!data) return -1;
    int ret = zkt_data_send(fd, data);
    zkt_data_clean(data);
    return ret;
}

zkt_data* zkt_data_recv(int fd) {
    zkt_data* temp_data = malloc(sizeof(zkt_data));

    if(!temp_data) {
        zkt_err("could not allocate memory for zkt_data");
        return NULL;
    }

    if(zkt_recv(fd, &temp_data->size, sizeof(size_t)) == -1) {
        free(temp_data);
        return NULL;
    }

    if(zkt_recv(fd, &temp_data->buffer, temp_data->size) == -1) {
        free(temp_data);
        return NULL;
    }

    zkt_data* data = zkt_data_decompress(temp_data->buffer, temp_data->size);

    if(!data) {
        zkt_data_clean(temp_data);
        return NULL;
    }

    zkt_data_clean(temp_data);
    return data;
}

//
// send and recv
//

int zkt_send(int fd, const void* buf, const size_t size) {
    size_t total = 0, left = size, ret = 0;

    while(total < size) {
        ret = send(fd, buf+total, left, 0);
        if(ret == -1) {
            perror("zkt:err:send:");
            return -1;
        }
        total += ret;
        left -= ret;
    }

    return total;
}

int zkt_recv(int fd, void* buf, const size_t size) {
    int ret = recv(fd, buf, size, 0);
    if(ret == -1) perror("zkt:err:recv:");
    return ret;
}

// 
// socket connection handling helpers
//

static void sigchld_handler(int s) {
    int t_errno = errno;
    while(waitpid(-1, NULL, WNOHANG) > 0);
    errno = t_errno;
}

static int kill_dead() {
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if(sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("zkt:err:sigaction:");
        return -1;
    }
    return 1;
}

static void* get_in_addr(struct sockaddr* sa) {
    if(sa->sa_family == AF_INET) return &(((struct sockaddr_in*)sa)->sin_addr);
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

static struct addrinfo* init_addrinfo(const char* host, const char* port, const struct addrinfo* hints) {
    int res;
    struct addrinfo* info;

    if((res = getaddrinfo(host, port, hints, &info)) != 0) {
        zkt_verr("getaddrinfo:%s", gai_strerror(res));
        return NULL;
    }

    return info;
}

//
// server specific functions
//

static zkt_server* bind_server(const char* port) {
    zkt_server* server = malloc(sizeof(zkt_server));
    if(!server) {
        zkt_err("could not allocate memory for zkt_server");
        return NULL;
    }

    struct addrinfo hints = {
        .ai_family      = AF_UNSPEC,
        .ai_socktype    = SOCK_STREAM,
        .ai_flags       = AI_PASSIVE,
    };
    struct addrinfo* info = init_addrinfo(NULL, port, &hints);

    inet_ntop(info->ai_family, get_in_addr(info->ai_addr), server->ip, sizeof(server->ip));
    zkt_vlog("starting server with address: %s:%s", server->ip, port);

    int opt = 1;

    for(server->sai = info; server->sai != NULL; server->sai = server->sai->ai_next) {
        if((server->fd = socket(info->ai_family, info->ai_socktype, info->ai_protocol)) == -1) {
            perror("zkt:err:socket:");
            continue;
        }

        if(setsockopt(server->fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
            perror("zkt:err:setsockopt:");
            return NULL;
        }

        if(bind(server->fd, info->ai_addr, info->ai_addrlen) == -1) {
            close(server->fd);
            perror("zkt:err:bind:");
            continue;
        }
    break;
    }

    freeaddrinfo(info);
    return server;
}

zkt_server* zkt_server_init(const char* port) {
    zkt_server* server = bind_server(port);
    if(!server) {
        zkt_err("server failed to bind");
        return NULL;
    }

    if(listen(server->fd, 10) == -1) {
        perror("zkt:err:listen:");
        return NULL;
    }

    if(kill_dead() == -1) {
        zkt_err("server failed to kill unused children");
        return NULL;
    }

    zkt_log("server awaiting connections...");

    return server;
}

int zkt_server_accept(zkt_server* server, void (*func)(int)) {
    socklen_t ra_len = sizeof(server->ra);

    if((server->rfd = accept(server->fd, (struct sockaddr*)&server->ra, &ra_len)) == -1) {
        perror("zkt:err:accept:");
        return -1;
    }

    inet_ntop(server->ra.ss_family, get_in_addr((struct sockaddr*)&server->ra), server->rip, sizeof(server->rip));
    zkt_vlog("server connected with ip: %s", server->rip);

    if(!fork()) {
        int rfd = server->rfd;
        if(func) func(rfd);
        close(rfd);
        return 0;
    }

    close(server->rfd);
    return 0;
}

//
// client specific functions
//

static zkt_client* connect_client(const char* host, const char* port) {
    zkt_client* client = malloc(sizeof(zkt_client));
    if(!client) {
        zkt_err("could not allocate memory for zkt_client");
        return NULL;
    }

    struct addrinfo hints = {
        .ai_family      = AF_UNSPEC,
        .ai_socktype    = SOCK_STREAM,
    };
    struct addrinfo* info = init_addrinfo(host, port, &hints);

    for(client->ai = info; client->ai != NULL; client->ai = client->ai->ai_next) {
        if((client->fd = socket(info->ai_family, info->ai_socktype, info->ai_protocol)) == -1) {
            perror("zkt:err:socket:");
            continue;
        }

        if(connect(client->fd, info->ai_addr, info->ai_addrlen) == -1) {
            close(client->fd);
            perror("zkt:err:connect:");
            continue;
        }

        break;
    }

    freeaddrinfo(info);

    return client;
}

zkt_client* zkt_client_init(const char* host, const char* port) {
    zkt_client* client = connect_client(host, port);
    if(!client) {
        zkt_err("client failed to connect");
        return NULL;
    }

    char rip[INET6_ADDRSTRLEN];
    inet_ntop(client->ai->ai_family, get_in_addr((struct sockaddr*)&client->ai->ai_addr), rip, sizeof(rip));
    zkt_vlog("client connected to address %s:%s", rip, port);

    return client;
}
