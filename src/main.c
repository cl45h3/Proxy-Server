#include <stdio.h>
#include <time.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string.h>
#include <stdlib.h>
#include "../include/proxy.h"

#pragma comment(lib, "Ws2_32.lib")


HANDLE hLogMutex;
ServerConfig server_config;

void get_timestamp(char *buf) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buf, 64, "%Y-%m-%d %H:%M:%S", t);
}

void log_request(char *client_ip, char *url, int status_code) {
    WaitForSingleObject(hLogMutex, INFINITE);
    
    FILE *fp = fopen(server_config.log_path, "a");
    if (fp) {
        char time_buf[64];
        get_timestamp(time_buf);
        fprintf(fp, "[%s] Client: %s | Request: %s | Status: %d\n", time_buf, client_ip, url, status_code);
        fclose(fp);
    }
    
    ReleaseMutex(hLogMutex);
}


int parse_request(char *buffer, ParsedRequest *req) {
    char buf_copy[BUFFER_SIZE];
    strcpy(buf_copy, buffer); 

    char url[2048], protocol[16];
    if (sscanf(buf_copy, "%s %s %s", req->method, url, protocol) < 3) return -1;

    char *host_start = url;
    if (strstr(url, "http://") == url) host_start += 7;
    
    char *path_start = strchr(host_start, '/');
    if (path_start) *path_start = '\0';

    char *port_ptr = strchr(host_start, ':');
    if (port_ptr) {
        *port_ptr = '\0';
        req->port = atoi(port_ptr + 1);
        strncpy(req->host, host_start, 255);
    } else {
        req->port = (strcmp(req->method, "CONNECT") == 0) ? 443 : 80;
        strncpy(req->host, host_start, 255);
    }
    return 0;
}


void trim(char *s) {
    char *p = s;
    int l = strlen(p);
    while(l > 0 && isspace(p[l - 1])) p[--l] = 0;
    while(*p && isspace(*p)) p++;
    memmove(s, p, l + 1);
}

void load_config(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        printf("Could not open config file %s. Using defaults.\n", filename);
        server_config.port = 8888;
        strcpy(server_config.log_path, "logs/proxy.log");
        strcpy(server_config.blocked_file, "../config/blocked.txt");
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '#' || strlen(line) < 3) continue;

        char *key = strtok(line, "=");
        char *val = strtok(NULL, "\n");
        if (key && val) {
            trim(key);
            trim(val);
            
            if (strcmp(key, "PORT") == 0) server_config.port = atoi(val);
            else if (strcmp(key, "LOG_PATH") == 0) strcpy(server_config.log_path, val);
            else if (strcmp(key, "BLOCKED_LIST") == 0) strcpy(server_config.blocked_file, val);
        }
    }
    fclose(file);
}

int is_blocked(char *host) {
    FILE *file = fopen(server_config.blocked_file, "r");
    if (!file) return 0;
    
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\r\n")] = 0; 
        if (strlen(line) > 0 && strstr(host, line) != NULL) {
            fclose(file);
            return 1; 
        }
    }
    fclose(file);
    return 0;
}

SOCKET connect_to_upstream(char *host, int port) {
    struct addrinfo hints, *res, *ptr;
    SOCKET sock;
    char port_str[10];
    sprintf(port_str, "%d", port);

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;       
    hints.ai_socktype = SOCK_STREAM; 
    hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo(host, port_str, &hints, &res) != 0) return INVALID_SOCKET;

    for (ptr = res; ptr != NULL; ptr = ptr->ai_next) {
        sock = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (sock == INVALID_SOCKET) continue;
        if (connect(sock, ptr->ai_addr, (int)ptr->ai_addrlen) == SOCKET_ERROR) {
            closesocket(sock);
            sock = INVALID_SOCKET;
            continue;
        }
        break;
    }
    freeaddrinfo(res);
    return sock;
}

void handle_https_tunnel(SOCKET client_socket, SOCKET server_socket) {
    char buffer[BUFFER_SIZE];
    fd_set readfds;
    int max_sd, activity;
    char *success_msg = "HTTP/1.1 200 Connection Established\r\n\r\n";
    send(client_socket, success_msg, strlen(success_msg), 0);

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(client_socket, &readfds);
        FD_SET(server_socket, &readfds);
        
        max_sd = (client_socket > server_socket) ? client_socket : server_socket;

        activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);
        if (activity < 0) break; 

        if (FD_ISSET(client_socket, &readfds)) {
            int valread = recv(client_socket, buffer, BUFFER_SIZE, 0);
            if (valread <= 0) break; 
            send(server_socket, buffer, valread, 0);
        }
        if (FD_ISSET(server_socket, &readfds)) {
            int valread = recv(server_socket, buffer, BUFFER_SIZE, 0);
            if (valread <= 0) break; 
            send(client_socket, buffer, valread, 0);
        }
    }
}

DWORD WINAPI handle_client_thread(LPVOID lpParam) {
    SOCKET client_socket = (SOCKET)lpParam;
    struct sockaddr_in addr;
    int len = sizeof(addr);
    getpeername(client_socket, (struct sockaddr*)&addr, &len);
    char *client_ip = inet_ntoa(addr.sin_addr);

    char buffer[BUFFER_SIZE];
    ParsedRequest req;

    int bytes_read = recv(client_socket, buffer, BUFFER_SIZE, 0);
    if (bytes_read <= 0) {
        closesocket(client_socket);
        return 0;
    }
    
    if (parse_request(buffer, &req) < 0) {
        closesocket(client_socket);
        return 0;
    }
    
    if (is_blocked(req.host)) {
        log_request(client_ip, req.host, 403);
        char *forbidden_msg = "HTTP/1.1 403 Forbidden\r\nContent-Type: text/plain\r\n\r\nAccess Denied.";
        send(client_socket, forbidden_msg, strlen(forbidden_msg), 0);
        closesocket(client_socket);
        return 0;
    }

    printf("[Thread %lu] %s request to: %s\n", GetCurrentThreadId(), req.method, req.host);
    log_request(client_ip, req.host, 200);

    SOCKET server_socket = connect_to_upstream(req.host, req.port);
    if (server_socket == INVALID_SOCKET) {
        log_request(client_ip, req.host, 502);
        closesocket(client_socket);
        return 0;
    }

    if (strcmp(req.method, "CONNECT") == 0) {
        handle_https_tunnel(client_socket, server_socket);
    } else {
        send(server_socket, buffer, bytes_read, 0);
        while (1) {
            int n = recv(server_socket, buffer, BUFFER_SIZE, 0);
            if (n <= 0) break; 
            send(client_socket, buffer, n, 0);
        }
    }

    closesocket(server_socket);
    closesocket(client_socket);
    return 0;
}


int main() {
    load_config("../config/server.conf");

    WSADATA wsaData;
    SOCKET server_fd, client_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return 1;

    CreateDirectory("logs", NULL); 
    hLogMutex = CreateMutex(NULL, FALSE, NULL);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    
    address.sin_port = htons(server_config.port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR) {
        printf("Bind failed on port %d. Error: %d\n", server_config.port, WSAGetLastError());
        return 1;
    }
    listen(server_fd, 10);

    printf("Modular Proxy Server Active on Port %d\n", server_config.port);
    printf("Logging to: %s\n", server_config.log_path);
    printf("Blocked List: %s\n", server_config.blocked_file);

    while (1) {
        client_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        if (client_socket != INVALID_SOCKET) {
            HANDLE hThread = CreateThread(NULL, 0, handle_client_thread, (LPVOID)client_socket, 0, NULL);
            if (hThread) CloseHandle(hThread);
        }
    }

    CloseHandle(hLogMutex);
    closesocket(server_fd);
    WSACleanup();
    return 0;
}