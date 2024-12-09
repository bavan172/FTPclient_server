#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>

#define SERVER_PORT 21 
#define BUFFER_SIZE 1024


void send_command(int sockfd, const char *command) {
    printf("Sending command: %s", command);
    write(sockfd, command, strlen(command));
}

void read_response(int sockfd, char *response) {
    int n = read(sockfd, response, BUFFER_SIZE - 1);
    if (n > 0) {
        response[n] = '\0';
        printf("Server response: %s\n", response);
    }
}

int authenticate(int sockfd, const char *username, const char *password) {
    char command[BUFFER_SIZE], response[BUFFER_SIZE];

    snprintf(command, sizeof(command), "USER %s\r\n", username);
    send_command(sockfd, command);
    read_response(sockfd, response);

    if (strstr(response, "331") == NULL) {
        fprintf(stderr, "Invalid username.\n");
        return -1;
    }

    snprintf(command, sizeof(command), "PASS %s\r\n", password);
    send_command(sockfd, command);
    read_response(sockfd, response);

    if (strstr(response, "230") == NULL) {
        fprintf(stderr, "Invalid password.\n");
        return -1;
    }

    printf("Authentication successful.\n");
    return 0;
}

int connect_data_socket(const char *server_ip, const char *response) {
    int ip1, ip2, ip3, ip4, port1, port2;
    int data_sock;
    struct sockaddr_in data_addr;

    if (sscanf(response, "227 Entering Passive Mode (%d,%d,%d,%d,%d,%d)",
               &ip1, &ip2, &ip3, &ip4, &port1, &port2) != 6) {
        fprintf(stderr, "Failed to parse PASV response.\n");
        return -1;
    }

    char ip[BUFFER_SIZE];
    snprintf(ip, sizeof(ip), "%d.%d.%d.%d", ip1, ip2, ip3, ip4);
    int port = (port1 * 256) + port2;

    printf("Connecting to data socket on %s:%d\n", ip, port);

    if ((data_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Data socket creation failed");
        return -1;
    }

    data_addr.sin_family = AF_INET;
    data_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &data_addr.sin_addr) <= 0) {
        perror("Invalid address");
        close(data_sock);
        return -1;
    }

    if (connect(data_sock, (struct sockaddr *)&data_addr, sizeof(data_addr)) < 0) {
        perror("Data connection failed");
        close(data_sock);
        return -1;
    }

    return data_sock;
}

void handle_list(int sockfd, const char *server_ip) {
    char command[BUFFER_SIZE], response[BUFFER_SIZE];
    int data_sock;

    snprintf(command, sizeof(command), "PASV\r\n");
    send_command(sockfd, command);
    read_response(sockfd, response);

    data_sock = connect_data_socket(server_ip, response);
    if (data_sock < 0) return;

    snprintf(command, sizeof(command), "LIST\r\n");
    send_command(sockfd, command);
    read_response(sockfd, response);

    char buffer[BUFFER_SIZE];
    int n;
    printf("Directory listing:\n");
    while ((n = read(data_sock, buffer, BUFFER_SIZE - 1)) > 0) {
        buffer[n] = '\0';
        printf("%s", buffer);
    }

    close(data_sock);
    read_response(sockfd, response); 
}

void handle_get(int sockfd, const char *server_ip, const char *filename) {
    char command[BUFFER_SIZE], response[BUFFER_SIZE], buffer[BUFFER_SIZE];
    int data_sock, file_fd;

    snprintf(command, sizeof(command), "PASV\r\n");
    send_command(sockfd, command);
    read_response(sockfd, response);

    data_sock = connect_data_socket(server_ip, response);
    if (data_sock < 0) return;

    snprintf(command, sizeof(command), "RETR %s\r\n", filename);
    send_command(sockfd, command);
    read_response(sockfd, response);

    file_fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (file_fd < 0) {
        perror("File open error");
        close(data_sock);
        return;
    }

    int n;
    while ((n = read(data_sock, buffer, BUFFER_SIZE)) > 0) {
        write(file_fd, buffer, n);
    }

    close(file_fd);
    close(data_sock);
    read_response(sockfd, response); 
}

void handle_put(int sockfd, const char *server_ip, const char *filename) {
    char command[BUFFER_SIZE], response[BUFFER_SIZE], buffer[BUFFER_SIZE];
    int data_sock, file_fd;

    snprintf(command, sizeof(command), "PASV\r\n");
    send_command(sockfd, command);
    read_response(sockfd, response);

    data_sock = connect_data_socket(server_ip, response);
    if (data_sock < 0) return;

    snprintf(command, sizeof(command), "STOR %s\r\n", filename);
    send_command(sockfd, command);
    read_response(sockfd, response);

    file_fd = open(filename, O_RDONLY);
    if (file_fd < 0) {
        perror("File open error");
        close(data_sock);
        return;
    }

    int n;
    while ((n = read(file_fd, buffer, BUFFER_SIZE)) > 0) {
        write(data_sock, buffer, n);
    }

    close(file_fd);
    close(data_sock);
    read_response(sockfd, response); 
}

void handle_delete(int sockfd, const char *filename) {
    char command[BUFFER_SIZE], response[BUFFER_SIZE];

    snprintf(command, sizeof(command), "DELE %s\r\n", filename);
    send_command(sockfd, command);
    read_response(sockfd, response);
}

void handle_rename(int sockfd) {
    char source[BUFFER_SIZE], target[BUFFER_SIZE];
    char command[BUFFER_SIZE], response[BUFFER_SIZE];

    printf("Enter source file name: ");
    scanf("%s", source);
    snprintf(command, sizeof(command), "RNFR %s\r\n", source);
    send_command(sockfd, command);
    read_response(sockfd, response);

    if (strstr(response, "350") == NULL) {
        printf("Source file not ready for rename.\n");
        return;
    }

    printf("Enter target file name: ");
    scanf("%s", target);
    snprintf(command, sizeof(command), "RNTO %s\r\n", target);
    send_command(sockfd, command);
    read_response(sockfd, response);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server_ip> <username>\n", argv[0]);
        exit(1);
    }

    char *server_ip = argv[1];
    char *username = argv[2];
    char password[50], command[BUFFER_SIZE], response[BUFFER_SIZE];
    int sockfd;
    struct sockaddr_in server_addr;

    printf("Enter password: ");
    scanf("%s", password);

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("Invalid address");
        exit(1);
    }

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        exit(1);
    }

    read_response(sockfd, response);

    if (authenticate(sockfd, username, password) < 0) {
        close(sockfd);
        exit(1);
    }

    while (1) {
        printf("FTP Commands: TYPE<A|I>, LIST, GET <filename>, PUT <filename>, DELETE <filename>, RENAME, QUIT\n");
        printf("Enter command: ");
        char cmd[BUFFER_SIZE], arg[BUFFER_SIZE];
        scanf("%s", cmd);

        if (strcasecmp(cmd, "TYPE") == 0) {
        scanf("%s", arg);
        snprintf(command, sizeof(command), "TYPE %s\r\n", arg);
        send_command(sockfd, command);
        read_response(sockfd, response);
        } else if (strcasecmp(cmd, "LIST") == 0) {
            handle_list(sockfd, server_ip);
        } else if (strcasecmp(cmd, "GET") == 0) {
            scanf("%s", arg);
            handle_get(sockfd, server_ip, arg);
        } else if (strcasecmp(cmd, "PUT") == 0) {
            scanf("%s", arg);
            handle_put(sockfd, server_ip, arg);
        } else if (strcasecmp(cmd, "DELETE") == 0) {
            scanf("%s", arg);
            handle_delete(sockfd, arg);
        } else if (strcasecmp(cmd, "RENAME") == 0) {
            handle_rename(sockfd);
        } else if (strcasecmp(cmd, "QUIT") == 0) {
            snprintf(command, sizeof(command), "QUIT\r\n");
            send_command(sockfd, command);
            read_response(sockfd, response);
            break;
        } else {
            printf("Unknown command.\n");
        }
    }

    close(sockfd);
    return 0;
}