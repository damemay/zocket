# zocket
boilerplate socket communication library with zstd compression

# documentation
## zkt_data
- `zkt_data* zkt_data_compress(const void* buf, const size_t size, int compression)` - zstd compression wrapper with error checking
- `zkt_data* zkt_data_decompress(const void* buf, const size_t size)` - zstd decompression wrapper with error checking
- `int zkt_data_send(int fd, zkt_data* data)` - send `zkt_data*` over socket file descriptor
- `int zkt_data_compress_send(int fd, const void* buf, const size_t size, int compression)` - compress data with zstd and send over socket file descriptor
- `zkt_data* zkt_data_recv(int fd)` - receive compressed `zkt_data*` and decompress it into new `zkt_data*`
- `void zkt_data_clean(zkt_data* data)` - free zkt_data
## socket high level interface
### data handling
- `int zkt_send(int fd, const void* buf, const size_t size)` - `send` wrapper that sends `buf` of `size` over socket file descriptor
- `int zkt_recv(int fd, void* buf, const size_t size)` - `recv` wrapper that receives data into `buf` of `size` over socket file descriptor
### zkt_server
- `zkt_server* zkt_server_init(const char* port)` - bind server on `NULL` (0.0.0.0) with port `port`
- `int zkt_server_accept(zkt_server* server, void (*func)(int))` - `accept` wrapper that calls user-defined `void callback(int fd)` on connection
#### example
```c
zkt_server* server = zkt_server_init("8888");
if(!server) exit(EXIT_FAILURE);
while(1) if(zkt_server_accept(server, &callback) == -1) continue;
return EXIT_SUCCESS;
```
### zkt_client
- `zkt_client* zkt_client_init(const char* host, const char* port)` - connect to server of `host`:`port`
#### example
```c
zkt_client* client = zkt_client_init("127.0.0.1", "8888");
if(!client) exit(EXIT_FAILURE);
zkt_data* data = zkt_data_recv(client->fd);
if(!data) zkt_err("could not receive data"), exit(EXIT_FAILURE);
zkt_data_clean(data);
return EXIT_SUCCESS;
```
