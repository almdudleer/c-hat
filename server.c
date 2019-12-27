#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <zconf.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include "common.h"

// ----------------------- macros ----------------------- //

#define DEBUG_MSGS 1
#define INIT_CLIENTS_NUM 5

// ------------------ type definitions ------------------ //
typedef struct {
    struct sockaddr_in addr;
    int conn_fd;
    char* name;

} client;

typedef struct {
    client* sender;
    char* text;
    long id;
    client* receiver;
    bool server_message;
} msg;

typedef struct {
    char* cmd;
    char* arg;
    char* remainder;
} command;

// ----------------------- globals ---------------------- //
int sock_fd = 0;
int num_clients = 0;
long message_num = 0;
char* inetaddr = "127.0.0.1";
client** clients;

struct sockaddr_in servaddr;

// ------------------ utility functions ----------------- //

command* parse_cmd(char* input) {
    char *firstWord, *secondWord, *remainder, *context;

    int inputLength = strlen(input);
    char *inputCopy = (char*) calloc(inputLength + 1, sizeof(char));
    strncpy(inputCopy, input, inputLength);

    firstWord = strtok_r (inputCopy, " ", &context);
    secondWord = strtok_r (NULL, " ", &context);
    remainder = context;

    command* cmd = malloc(sizeof(cmd));

    if (firstWord != NULL) {
        int len1 = strlen(firstWord);
        cmd->cmd = malloc(len1 + 1);
        strncpy(cmd->cmd, firstWord, len1);
    } else {
        cmd->cmd = "";
    }

    if (secondWord != NULL) {
        int len2 = strlen(secondWord);
        cmd->arg = malloc(len2 + 1);
        strncpy(cmd->arg, secondWord, len2);
    } else {
        cmd->arg = "";
    }

    if (remainder != NULL) {
        int len3 = strlen(remainder);
        cmd->remainder = malloc(len3 + 1);
        strncpy(cmd->remainder, remainder, len3);
    } else {
        cmd->remainder = "";
    }

    free(inputCopy);

    return cmd;
}

int set_name(client* sender, char* new_name) { //TODO: prohibit "server"
    for (int clnt_num = 0; clnt_num < num_clients; clnt_num++) {
        if (strcmp(clients[clnt_num]->name, new_name) == 0)
            return false;
    }
    sender->name = new_name;
    return true;
}

// utilities to create server messages without working with msg
msg* make_server_private_message(client* receiver, char* text) {
    msg* message = malloc(sizeof(msg));
    message->server_message = true;
    message->receiver = receiver;
    message->sender = NULL;
    message->text = text;
    return message;
}

msg* make_server_public_message(char* text) {
    msg* message = malloc(sizeof(msg));
    message->server_message = true;
    message->receiver = NULL;
    message->sender = NULL;
    message->text = text;
    return message;
}

// ------------------ message functions ----------------- //

// thread functions to send messages
void* broadcast_msg(void* args) {
    msg* message = (msg*) args;
    message->id = message_num++;

    char* final_text = malloc(MAXBUFF + MAXNAME + 2); // TODO: unlimited final text
    if (message->server_message) {
        sprintf(final_text, "server: %s", message->text);
    } else {
        sprintf(final_text, "%s: %s", message->sender->name, message->text);
    }

    if (DEBUG_MSGS) {
        puts(final_text);
    }
    for (int clnt_num = 0; clnt_num < num_clients; clnt_num++) {
        if (message->sender == NULL || message->sender->conn_fd != clients[clnt_num]->conn_fd) {
            int num_bytes = send(clients[clnt_num]->conn_fd, final_text, strlen(final_text)+1, 0);
            if (num_bytes == -1) {
                perror("broadcast message failed");
            }
        }
    }
    free(message); //TODO: free everything
    return 0;
}

void* unicast_msg(void* args) {
    msg* message = (msg*) args;
    message->id = 0;

    char* final_text = malloc(MAXBUFF + MAXNAME + 50); // TODO: unlimited final text
    if (message->server_message) {
        sprintf(final_text, "server (to you): %s", message->text);
    } else {
        sprintf(final_text, "%s (to you): %s", message->sender->name, message->text);
    }

    if (DEBUG_MSGS) {
        puts(final_text);
    }
    int num_bytes = send(message->receiver->conn_fd, final_text, strlen(final_text)+1, 0);
    if (num_bytes == -1) {
        perror("unicast message failed");
    }
    free(message);
    return 0;
}

// create a thread and call broadcast_msg
int broadcast_parallel(msg* message) {
    pthread_t broadcast_thread;
    pthread_create(&broadcast_thread, NULL, broadcast_msg, message); // TODO: errors
    return 0;
}

// create a thread and call unicast
int unicast_parallel(msg* message) {
    pthread_t unicast_thread;
    pthread_create(&unicast_thread, NULL, unicast_msg, message); // TODO: errors
    return 0;
}

// utilities to send server messages without working with msg
int send_server_private_message(client* receiver, char* text) {
    unicast_parallel(make_server_private_message(receiver, text));
    return 0;
}

int send_server_public_message(char* text) {
    broadcast_parallel(make_server_public_message(text));
    return 0;
}

// ------------------- client thread -------------------- //
// one thread per client
void* client_worker(void* args) {
    client* clnt = args;
    char buff[MAXBUFF];
    int n;
    int cont = 1;

    char *mtext = malloc(MAXNAME + 20);
    sprintf(mtext, "%s connected", clnt->name);
    send_server_public_message(mtext);
    send_server_private_message(clnt, "type \\help to see the list of available commands");

    while (cont) {
        n = recv(clnt->conn_fd, buff, sizeof(buff), 0); // TODO: sigpipe
        if (n == -1) {
            perror("recv() failed");
            return 0;
        }
        if (n != 0) {
            msg* message = malloc(sizeof(msg));
            message->sender = clnt;
            message->text = buff;
            message->receiver = NULL;
            message->id = 0;
            message->server_message = false;

            // trim last newline
            char* end = message->text + strlen(message->text) - 1;
            if (*end == '\n') {
                end[0] = '\0';
            }

            // parse commands
            if (*(message->text) == '\\') {
                message->text++;

                if (*(message->text) != '\\') {
                    command* cmd = parse_cmd(message->text);
                    if (strcmp(cmd->cmd, "exit") == 0) {
                        cont = 0;
                        mtext = malloc(MAXNAME + 20);
                        sprintf(mtext, "%s left", clnt->name);
                        send_server_public_message(mtext);
                    }
                    else if (strcmp(cmd->cmd, "name") == 0) {
                        char* old_name = malloc(MAXNAME);
                        strncpy(old_name, message->sender->name, strlen(message->sender->name));
                        if (set_name(message->sender, cmd->arg)) {
                            send_server_private_message(clnt, "successfully changed name");
                            mtext = malloc(2*MAXNAME+50);
                            sprintf(mtext, "%s changed name to %s", old_name, message->sender->name);
                            send_server_public_message(mtext);
                        } else {
                            send_server_private_message(clnt, "error changing name, try another name");
                        }
                    } else if (strcmp(cmd->cmd, "help") == 0) {
                        send_server_private_message(clnt, "\n\t\\help -- see this help\n\t\\name <new name> -- change name\n\t\\wisp <name> message -- send private message\n\t\\file <path> <name> -- send file");
                    } else {
                        send_server_private_message(message->sender, "no such command");
                    }
                }
            } else {
                broadcast_parallel(message);
            }
        } else {
            cont = 0;
        }
        // finish business after cont = 0
    }
    return 0;
}

// --------------- main thread functions ---------------- //

int init() {
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd == -1) {
        perror("socket() failed");
        return -1;
    } else {
        printf("created socket\n");
    }
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr(inetaddr);
    servaddr.sin_port = htons(PORT);

    int enable = 1;
    if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
        perror("setsockopt(SO_REUSEADDR) failed");

    if ((bind(sock_fd, (SA*) &servaddr, sizeof(servaddr))) != 0) {
        perror("bind() failed");
        return -1;
    } else {
        printf("binded socket\n");
    }

    if ((listen(sock_fd, 5)) != 0) {
        perror("listen() failed");
        return -1;
    } else {
        printf("listening on port %d\n", PORT);
    }

    clients = malloc(INIT_CLIENTS_NUM * sizeof(client*)); //TODO: unlimited clients
    return 0;
}

int accept_clients() {
    while (1) {
        struct sockaddr_in addr;
        socklen_t len = sizeof(addr);
        int conn_fd = accept(sock_fd, (SA*) &addr, &len);
        if (conn_fd < 0) {
            perror("accept() failed");
        } else {
            printf("client accepted on %d\n", conn_fd);
        }

        client* new_client = malloc(sizeof new_client);
        new_client->conn_fd = conn_fd;
        new_client->addr = addr;
        new_client->name = malloc(MAXNAME); //TODO: if name is larger
        sprintf(new_client->name,"%d", new_client->addr.sin_port);
        pthread_t new_thread;
        if (pthread_create(&new_thread, NULL, client_worker, new_client) != 0) {
            perror("client thread creation failed");
            return -1;
        };
        clients[num_clients] = new_client;
        num_clients++;
    }
}

void terminate() {
    //TODO: shutdown hook
    //TODO: broadcast server killed
    if (sock_fd != 0) close(sock_fd);
    exit(0);
}

int main() {
    if (init() == -1) terminate();
    if (accept_clients() == -1) terminate();
    terminate();
//    command* cmd = parse_cmd("name is lesha");
//    printf("%s:%s:%s", cmd->cmd, cmd->arg, cmd->remainder);
}