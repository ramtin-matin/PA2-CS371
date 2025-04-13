/*
# CS371 PA2 - Task 1: UDP Stop-and-Wait (No ARQ)
# Authors:
# - David Falade
# - Parker Nurick
# - Ramtin Seyedmatin
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <pthread.h>

#define MAX_EVENTS 64
#define MESSAGE_SIZE 16

char *server_ip;
int server_port;
int num_client_threads;
int num_requests;

typedef struct {
    int epoll_fd;
    int socket_fd;
    struct sockaddr_in server_addr;
    int client_id;
} client_thread_data_t;

void *client_thread_func(void *arg) {
    client_thread_data_t *data = (client_thread_data_t *)arg;
    struct epoll_event event, events[MAX_EVENTS];
    char send_buf[MESSAGE_SIZE] = "PingPingPingPing";
    char recv_buf[MESSAGE_SIZE];

    event.events = EPOLLIN;
    event.data.fd = data->socket_fd;
    epoll_ctl(data->epoll_fd, EPOLL_CTL_ADD, data->socket_fd, &event);

    int tx_cnt = 0, rx_cnt = 0;

    for (int i = 0; i < num_requests; i++) {
        sendto(data->socket_fd, send_buf, MESSAGE_SIZE, 0,
               (struct sockaddr *)&data->server_addr, sizeof(data->server_addr));
        tx_cnt++;

        int nfds = epoll_wait(data->epoll_fd, events, MAX_EVENTS, 200);
        if (nfds > 0) {
            recvfrom(data->socket_fd, recv_buf, MESSAGE_SIZE, 0, NULL, NULL);
            rx_cnt++;
        }
    }

    printf("[Client %d] Sent: %d, Received: %d, Lost: %d\n",
           data->client_id, tx_cnt, rx_cnt, tx_cnt - rx_cnt);

    close(data->socket_fd);
    close(data->epoll_fd);
    return NULL;
}

void run_client() {
    pthread_t threads[num_client_threads];
    client_thread_data_t thread_data[num_client_threads];

    for (int i = 0; i < num_client_threads; i++) {
        thread_data[i].socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
        thread_data[i].epoll_fd = epoll_create1(0);
        thread_data[i].client_id = i;

        memset(&thread_data[i].server_addr, 0, sizeof(struct sockaddr_in));
        thread_data[i].server_addr.sin_family = AF_INET;
        thread_data[i].server_addr.sin_port = htons(server_port);
        inet_pton(AF_INET, server_ip, &thread_data[i].server_addr.sin_addr);

        pthread_create(&threads[i], NULL, client_thread_func, &thread_data[i]);
    }

    for (int i = 0; i < num_client_threads; i++) {
        pthread_join(threads[i], NULL);
    }
}

void run_server() {
    int server_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[MESSAGE_SIZE];

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(server_port);

    bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));

    while (1) {
        int n = recvfrom(server_fd, buffer, MESSAGE_SIZE, 0,
                         (struct sockaddr *)&client_addr, &client_len);
        if (n > 0) {
            sendto(server_fd, buffer, n, 0,
                   (struct sockaddr *)&client_addr, client_len);
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 6) {
        printf("Usage: %s <server|client> <server_ip> <server_port> <num_client_threads> <num_requests>\n", argv[0]);
        return 1;
    }

    server_ip = argv[2];
    server_port = atoi(argv[3]);
    num_client_threads = atoi(argv[4]);
    num_requests = atoi(argv[5]);

    if (strcmp(argv[1], "server") == 0) {
        run_server();
    } else if (strcmp(argv[1], "client") == 0) {
        run_client();
    } else {
        printf("Invalid mode. Use 'server' or 'client'.\n");
        return 1;
    }

    return 0;
}
