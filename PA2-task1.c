/*
# Copyright 2025 University of Kentucky
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# SPDX-License-Identifier: Apache-2.0
# Group Members:
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
#include <sys/time.h>
#include <pthread.h>
#include <errno.h>

#define MAX_EVENTS 64
#define MESSAGE_SIZE 16
#define DEFAULT_CLIENT_THREADS 4

char *server_ip = "127.0.0.1";
int server_port = 12345;
int num_client_threads = DEFAULT_CLIENT_THREADS;
int num_requests = 1000000;

typedef struct {
    int epoll_fd;
    int socket_fd;
    long long total_rtt;
    long total_messages;
    long tx_cnt;
    long rx_cnt;
    float request_rate;
    struct sockaddr_in server_addr;
    socklen_t addr_len;
} client_thread_data_t;

void *client_thread_func(void *arg) {
    client_thread_data_t *data = (client_thread_data_t *)arg;
    struct epoll_event event, events[MAX_EVENTS];
    char send_buf[MESSAGE_SIZE] = "ABCDEFGHIJKMLNOP";
    char recv_buf[MESSAGE_SIZE + 1];
    struct timeval start, end;

    event.events = EPOLLIN;
    event.data.fd = data->socket_fd;
    epoll_ctl(data->epoll_fd, EPOLL_CTL_ADD, data->socket_fd, &event);

    data->total_rtt = 0;
    data->total_messages = 0;
    data->tx_cnt = 0;
    data->rx_cnt = 0;

    for (long i = 0; i < num_requests; i++) {
        gettimeofday(&start, NULL);

        if (sendto(data->socket_fd, send_buf, MESSAGE_SIZE, 0,
                   (struct sockaddr *)&data->server_addr, data->addr_len) == -1) {
            perror("sendto");
            continue;
        }
        data->tx_cnt++;

        int nfds = epoll_wait(data->epoll_fd, events, MAX_EVENTS, 2000);
        if (nfds == -1) {
            perror("epoll_wait");
            continue;
        }

        for (int j = 0; j < nfds; j++) {
            if (events[j].data.fd == data->socket_fd) {
                struct sockaddr_in from_addr;
                socklen_t from_len = sizeof(from_addr);
                int bytes_received = recvfrom(data->socket_fd, recv_buf, MESSAGE_SIZE, 0,
                                              (struct sockaddr *)&from_addr, &from_len);
                if (bytes_received > 0) {
                    recv_buf[bytes_received] = '\0';
                    gettimeofday(&end, NULL);

                    long long rtt = (end.tv_sec - start.tv_sec) * 1000000LL +
                                    (end.tv_usec - start.tv_usec);
                    data->total_rtt += rtt;
                    data->total_messages++;
                    data->rx_cnt++;
                }
            }
        }
    }

    if (data->total_messages > 0) {
        data->request_rate = (float)data->total_messages / (data->total_rtt / 1000000.0);
    } else {
        data->request_rate = 0;
    }

    close(data->socket_fd);
    close(data->epoll_fd);
    return NULL;
}

void run_client() {
    pthread_t threads[num_client_threads];
    client_thread_data_t thread_data[num_client_threads];
    long long total_rtt = 0;
    long total_messages = 0;
    float total_request_rate = 0.0;
    long total_tx = 0, total_rx = 0;

    for (int i = 0; i < num_client_threads; i++) {
        thread_data[i].socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (thread_data[i].socket_fd == -1) {
            perror("socket");
            exit(EXIT_FAILURE);
        }

        thread_data[i].epoll_fd = epoll_create1(0);
        if (thread_data[i].epoll_fd == -1) {
            perror("epoll_create1");
            close(thread_data[i].socket_fd);
            exit(EXIT_FAILURE);
        }

        memset(&thread_data[i].server_addr, 0, sizeof(thread_data[i].server_addr));
        thread_data[i].server_addr.sin_family = AF_INET;
        thread_data[i].server_addr.sin_port = htons(server_port);
        if (inet_pton(AF_INET, server_ip, &thread_data[i].server_addr.sin_addr) <= 0) {
            perror("inet_pton");
            exit(EXIT_FAILURE);
        }
        thread_data[i].addr_len = sizeof(thread_data[i].server_addr);

        pthread_create(&threads[i], NULL, client_thread_func, &thread_data[i]);
    }

    for (int i = 0; i < num_client_threads; i++) {
        pthread_join(threads[i], NULL);
        total_rtt += thread_data[i].total_rtt;
        total_messages += thread_data[i].total_messages;
        total_request_rate += thread_data[i].request_rate;
        total_tx += thread_data[i].tx_cnt;
        total_rx += thread_data[i].rx_cnt;
    }

    printf("Total Messages: %ld, Total RTT: %lld us\n", total_messages, total_rtt);
    printf("Average RTT: %lld us\n", (total_messages > 0) ? (total_rtt / total_messages) : 0);
    printf("Total Request Rate: %.2f messages/s\n", total_request_rate);
    printf("TX: %ld, RX: %ld, Lost: %ld packets\n", total_tx, total_rx, total_tx - total_rx);
}

void run_server() {
    int server_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_fd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr, client_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(server_port);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    char buf[MESSAGE_SIZE];
    socklen_t client_len = sizeof(client_addr);

    while (1) {
        int bytes_received = recvfrom(server_fd, buf, MESSAGE_SIZE, 0,
                                      (struct sockaddr *)&client_addr, &client_len);
        if (bytes_received > 0) {
            sendto(server_fd, buf, bytes_received, 0,
                   (struct sockaddr *)&client_addr, client_len);
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "server") == 0) {
        run_server();
    } else if (argc > 1 && strcmp(argv[1], "client") == 0) {
        run_client();
    } else {
        printf("Usage: %s <server|client>\n", argv[0]);
    }
    return 0;
}
