#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>

#define SERVER_PORT 2121 
#define BUFFER_SIZE 1024

int data_sock; 
char transfer_type = 'I';

typedef struct {
    char username[50];
    char password[50];
} User;

User valid_users[] = {
    {"user1", "pass1"},
    {"admin", "admin123"},
    {"test", "testpass"}
};
int user_count = 3;
char current_user[50] = {0};
int is_authenticated = 0;

const char restricted_filename[] = "restricted_file.txt"; 
const char restricted_user[] = "user1"; 

void send_response(int client_sock, const char *response) {
    write(client_sock, response, strlen(response));
}

int validate_username(const char *username) {
    for (int i = 0; i < user_count; i++) {
        if (strcmp(username, valid_users[i].username) == 0) {
            strncpy(current_user, username, sizeof(current_user));
            return 1;
        }
    }
    return 0;
}

int validate_password(const char *password) {
    for (int i = 0; i < user_count; i++) {
        if (strcmp(current_user, valid_users[i].username) == 0 &&
            strcmp(password, valid_users[i].password) == 0) {
            return 1;
        }
    }
    return 0;
}

void handle_pasv_command(int client_sock) {
    struct sockaddr_in data_addr;
    socklen_t len = sizeof(data_addr);
    char server_ip[INET_ADDRSTRLEN];

    if ((data_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Data socket creation failed");
        send_response(client_sock, "425 Can't open data connection.\r\n");
        return;
    }

    data_addr.sin_family = AF_INET;
    data_addr.sin_addr.s_addr = INADDR_ANY;
    data_addr.sin_port = 0;

    if (bind(data_sock, (struct sockaddr *)&data_addr, sizeof(data_addr)) < 0) {
        perror("Data socket bind failed");
        send_response(client_sock, "425 Can't open data connection.\r\n");
        close(data_sock);
        return;
    }

    if (listen(data_sock, 1) < 0) {
        perror("Data socket listen failed");
        send_response(client_sock, "425 Can't open data connection.\r\n");
        close(data_sock);
        return;
    }

    getsockname(data_sock, (struct sockaddr *)&data_addr, &len);
    int port = ntohs(data_addr.sin_port);

    struct sockaddr_in server_addr;
    socklen_t server_len = sizeof(server_addr);
    if (getsockname(client_sock, (struct sockaddr *)&server_addr, &server_len) == 0) {
        inet_ntop(AF_INET, &server_addr.sin_addr, server_ip, sizeof(server_ip));
    } else {
        perror("Failed to retrieve server IP");
        send_response(client_sock, "425 Can't retrieve server IP.\r\n");
        close(data_sock);
        return;
    }

    char response[BUFFER_SIZE];
    int ip1, ip2, ip3, ip4;
    sscanf(server_ip, "%d.%d.%d.%d", &ip1, &ip2, &ip3, &ip4);
    snprintf(response, sizeof(response),
             "227 Entering Passive Mode (%d,%d,%d,%d,%d,%d).\r\n",
             ip1, ip2, ip3, ip4, port / 256, port % 256);
    send_response(client_sock, response);
}


int accept_data_connection() {
    struct sockaddr_in client_addr;
    socklen_t len = sizeof(client_addr);

    printf("Waiting for data connection...\n");
    int conn_sock = accept(data_sock, (struct sockaddr *)&client_addr, &len);
    if (conn_sock >= 0) {
        printf("Data connection established.\n");
    } else {
        perror("Data connection accept failed");
    }
    return conn_sock;
}

void handle_list_command(int client_sock) {
    send_response(client_sock, "150 Opening data connection for directory listing.\r\n");

    int data_conn = accept_data_connection();
    if (data_conn < 0) {
        send_response(client_sock, "425 Can't open data connection.\r\n");
        return;
    }

    DIR *dir;
    struct dirent *ent;
    char buffer[BUFFER_SIZE];

    if ((dir = opendir(".")) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            snprintf(buffer, sizeof(buffer), "%s\r\n", ent->d_name);
            write(data_conn, buffer, strlen(buffer));
        }
        closedir(dir);
    } else {
        perror("Directory listing error");
    }

    close(data_conn);
    send_response(client_sock, "226 Directory send OK.\r\n");
}

void handle_retr_command(int client_sock, char *filename) {
    if (strcmp(filename, restricted_filename) == 0 && strcmp(current_user, restricted_user) != 0) {
        send_response(client_sock, "550 Permission denied.\r\n");
        return;
    }

    char buffer[BUFFER_SIZE];
    int file_fd;

    send_response(client_sock, "150 Opening data connection for file transfer.\r\n");

    int data_conn = accept_data_connection();
    if (data_conn < 0) {
        send_response(client_sock, "425 Can't open data connection.\r\n");
        return;
    }

    if ((file_fd = open(filename, O_RDONLY)) < 0) {
        perror("File open error");
        send_response(client_sock, "550 File not found.\r\n");
        close(data_conn);
        return;
    }

    ssize_t n;
    if (transfer_type == 'A') { 
        while ((n = read(file_fd, buffer, BUFFER_SIZE)) > 0) {
            for (ssize_t i = 0; i < n; i++) {
                if (buffer[i] == '\n') {
                    write(data_conn, "\r", 1);
                }
                write(data_conn, &buffer[i], 1);
            }
        }
    } else { 
        while ((n = read(file_fd, buffer, BUFFER_SIZE)) > 0) {
            write(data_conn, buffer, n);
        }
    }

    close(file_fd);
    close(data_conn);
    send_response(client_sock, "226 Transfer complete.\r\n");
}

void handle_stor_command(int client_sock, char *filename) {
    if (strcmp(filename, restricted_filename) == 0 && strcmp(current_user, restricted_user) != 0) {
        send_response(client_sock, "550 Permission denied.\r\n");
        return;
    }

    char buffer[BUFFER_SIZE];
    int file_fd;

    send_response(client_sock, "150 Opening data connection for file upload.\r\n");

    int data_conn = accept_data_connection();
    if (data_conn < 0) {
        send_response(client_sock, "425 Can't open data connection.\r\n");
        return;
    }

    if ((file_fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0) {
        perror("File open error");
        send_response(client_sock, "550 Can't create file.\r\n");
        close(data_conn);
        return;
    }

    ssize_t n;
    if (transfer_type == 'A') { 
        while ((n = read(data_conn, buffer, BUFFER_SIZE)) > 0) {
            for (ssize_t i = 0; i < n; i++) {
                if (buffer[i] == '\r') continue; 
                write(file_fd, &buffer[i], 1);
            }
        }
    } else { 
        while ((n = read(data_conn, buffer, BUFFER_SIZE)) > 0) {
            write(file_fd, buffer, n);
        }
    }

    close(file_fd);
    close(data_conn);
    send_response(client_sock, "226 Transfer complete.\r\n");
}

void handle_dele_command(int client_sock, char *filename) {
    if (strcmp(filename, restricted_filename) == 0 && strcmp(current_user, restricted_user) != 0) {
        send_response(client_sock, "550 Permission denied.\r\n");
        return;
    }

    if (unlink(filename) == 0) {
        send_response(client_sock, "250 File deleted successfully.\r\n");
    } else {
        perror("File delete error");
        send_response(client_sock, "550 File not found or cannot delete.\r\n");
    }
}

void handle_rename_command(int client_sock, char *command, char *argument) {
    static char source_file[BUFFER_SIZE];

    if (strncmp(command, "RNFR", 4) == 0) {
        strncpy(source_file, argument, sizeof(source_file) - 1);
        source_file[sizeof(source_file) - 1] = '\0';
        if (access(source_file, F_OK) == 0) {
            send_response(client_sock, "350 Ready for destination name.\r\n");
        } else {
            perror("Source file not found");
            send_response(client_sock, "550 Source file not found.\r\n");
        }
    } else if (strncmp(command, "RNTO", 4) == 0) {
        if (rename(source_file, argument) == 0) {
            send_response(client_sock, "250 File renamed successfully.\r\n");
        } else {
            perror("File rename error");
            send_response(client_sock, "550 Rename failed.\r\n");
        }
    } else {
        send_response(client_sock, "502 Command not implemented.\r\n");
    }
}

void handle_client(int client_sock) {
    char buffer[BUFFER_SIZE], command[BUFFER_SIZE], argument[BUFFER_SIZE];
    ssize_t n;

    send_response(client_sock, "220 Welcome to Simple FTP Server\r\n");

    while ((n = read(client_sock, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[n] = '\0';
        printf("Received command: %s", buffer);

        sscanf(buffer, "%s %s", command, argument);

        if (strncmp(command, "USER", 4) == 0) {
            if (validate_username(argument)) {
                strncpy(current_user, argument, sizeof(current_user));
                send_response(client_sock, "331 Username OK, need password.\r\n");
            } else {
                send_response(client_sock, "530 Invalid username.\r\n");
            }
        } else if (strncmp(command, "PASS", 4) == 0) {
            if (validate_password(argument)) {
                is_authenticated = 1;
                send_response(client_sock, "230 User logged in, proceed.\r\n");
            } else {
                send_response(client_sock, "530 Invalid password.\r\n");
            }
        } else if (strncmp(command, "TYPE", 4) == 0) {
            if (strcmp(argument, "A") == 0) {
                transfer_type = 'A';
                send_response(client_sock, "200 Type set to ASCII.\r\n");
            } else if (strcmp(argument, "I") == 0) {
                transfer_type = 'I';
                send_response(client_sock, "200 Type set to Binary.\r\n");
            } else {
                send_response(client_sock, "501 Invalid type. Use 'A' or 'I'.\r\n");
            }
        } else if (strncmp(command, "PASV", 4) == 0) {
            handle_pasv_command(client_sock);
        } else if (strncmp(command, "LIST", 4) == 0) {
            handle_list_command(client_sock);
        } else if (strncmp(command, "RETR", 4) == 0) {
            handle_retr_command(client_sock, argument);
        } else if (strncmp(command, "STOR", 4) == 0) {
            handle_stor_command(client_sock, argument);
        } else if (strncmp(command, "DELE", 4) == 0) {
            handle_dele_command(client_sock, argument);
        } else if (strncmp(command, "RNFR", 4) == 0 || strncmp(command, "RNTO", 4) == 0) {
            handle_rename_command(client_sock, command, argument);
        } else if (strncmp(command, "QUIT", 4) == 0) {
            send_response(client_sock, "221 Goodbye.\r\n");
            break;
        } else {
            send_response(client_sock, "502 Command not implemented.\r\n");
        }
    }

    close(client_sock);
    is_authenticated = 0;
    memset(current_user, 0, sizeof(current_user));
}

int main() {
    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    if ((server_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    int opt = 1;
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        close(server_sock);
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);

    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_sock);
        exit(1);
    }

    if (listen(server_sock, 5) < 0) {
        perror("Listen failed");
        close(server_sock);
        exit(1);
    }

    printf("FTP server started on port %d\n", SERVER_PORT);

    while ((client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_addr_len)) > 0) {
        printf("Client connected\n");
        handle_client(client_sock);
        printf("Client disconnected\n");
    }

    close(server_sock);
    return 0;
}