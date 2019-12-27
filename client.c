#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <zconf.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "common.h"

// ----------------------- globals ---------------------- //

int sockfd;
char* inetaddr = "127.0.0.1";

// ------------------ receiver thread ------------------- //

void* receive_msgs(void* args) {
    char buff[MAXBUFF];
    int n = 1;

    while (n != 0) {
        n = recv(sockfd, buff, sizeof(buff), 0);
        if (n == -1) {
            perror("recv() failed");
            return 0;
        }
        if (n != 0) {
            printf("\r%s\n> ", buff);
            fflush(stdout);
        }
    }
    return 0;
}

// --------------- main thread functions ---------------- //

void send_msgs()
{
    printf("> ");
    char buff[MAXBUFF];
    for (;;) {
        if (fgets(buff, MAXBUFF, stdin) == NULL) break;
        printf("> ");
        send(sockfd, buff, sizeof(buff), 0);
        if ((strncmp(buff, "\\exit", 4)) == 0) {
            printf("exiting...\n");
            break;
        }
    }
}

int main()
{
    struct sockaddr_in servaddr;

    // socket create and varification
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        printf("socket creation failed...\n");
        exit(0);
    }
    else
        printf("Socket successfully created..\n");
    bzero(&servaddr, sizeof(servaddr));

    // assign IP, PORT
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr(inetaddr);
    servaddr.sin_port = htons(PORT);

    // connect the client socket to server socket
    if (connect(sockfd, (SA*)&servaddr, sizeof(servaddr)) != 0) {
        printf("connection with the server failed...\n");
        exit(0);
    }
    else
        printf("connected to the server..\n");

    pthread_t receiver_thread;
    pthread_create(&receiver_thread, NULL, receive_msgs, NULL);
    // function for chat
    send_msgs();

    // close the socket
    close(sockfd);
}

// TODO: server disconnection