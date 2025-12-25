#ifndef PROXY_H
#define PROXY_H

#include <winsock2.h>
#include <windows.h>

#define BUFFER_SIZE 4096
typedef struct {
    int port;
    char log_path[256];
    char blocked_file[256];
} ServerConfig;

extern ServerConfig server_config;
extern HANDLE hLogMutex;

typedef struct {
    char method[16];
    char host[256];
    int port;
} ParsedRequest;

void load_config(const char *filename);
void log_request(char *client_ip, char *url, int status_code);
int parse_request(char *buffer, ParsedRequest *req);
int is_blocked(char *host);
SOCKET connect_to_upstream(char *host, int port);
void handle_https_tunnel(SOCKET client_socket, SOCKET server_socket);
DWORD WINAPI handle_client_thread(LPVOID lpParam);

#endif