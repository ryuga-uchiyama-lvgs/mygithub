#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#define PORT 8080
#define BUFSIZE 8192
#define INITIAL_BUFFER_SIZE 8000

void error(const char *msg) {
    perror(msg);
    exit(1);
}

void handle_request(int newsockfd) {
    char buffer[BUFSIZE];
    int n;

    n = read(newsockfd, buffer, BUFSIZE - 1);
    if (n < 0) error("ERROR reading from socket");
    buffer[n] = 0;

    printf("Here is the message: %s\n", buffer);

    if (strncmp(buffer, "GET ", 4) == 0) {
        // Handle GET request
        char *filepath = strtok(buffer + 4, " ");
        if (strcmp(filepath, "/") == 0) {
            filepath = "/index.html";
        }

        char fullpath[BUFSIZE] = ".";
        strncat(fullpath, filepath, BUFSIZE - strlen(fullpath) - 1);

        int filefd = open(fullpath, O_RDONLY);
        if (filefd < 0) {
            char *not_found = "HTTP/1.1 404 Not Found\r\nContent-Length: 13\r\n\r\n404 Not Found";
            write(newsockfd, not_found, strlen(not_found));
        } else {
            char response[BUFSIZE];
            sprintf(response, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n");
            write(newsockfd, response, strlen(response));

            while ((n = read(filefd, buffer, BUFSIZE)) > 0) {
                write(newsockfd, buffer, n);
            }
            close(filefd);
        }
    } else if (strncmp(buffer, "POST ", 5) == 0) {
        // Handle POST request
        char *content_length_str = strstr(buffer, "Content-Length: ");
        int content_length = 0;
        if (content_length_str) {
            sscanf(content_length_str, "Content-Length: %d", &content_length);
        }

        char *content = strstr(buffer, "\r\n\r\n");
        if (content) {
            content += 4; // Skip past \r\n\r\n
            int content_size = n - (content - buffer);

            FILE *file = fopen("upload.jpg", "wb");
            if (file) {
                fwrite(content, 1, content_size, file);
                while (content_size < content_length) {
                    n = read(newsockfd, buffer, BUFSIZE);
                    if (n <= 0) break;
                    fwrite(buffer, 1, n, file);
                    content_size += n;
                }
                fclose(file);

                char *ok_response = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK";
                write(newsockfd, ok_response, strlen(ok_response));
            } else {
                char *internal_error = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 21\r\n\r\n500 Internal Server Error";
                write(newsockfd, internal_error, strlen(internal_error));
            }
        } else {
            char *bad_request = "HTTP/1.1 400 Bad Request\r\nContent-Length: 15\r\n\r\n400 Bad Request";
            write(newsockfd, bad_request, strlen(bad_request));
        }
    } else {
        char *bad_request = "HTTP/1.1 400 Bad Request\r\nContent-Length: 15\r\n\r\n400 Bad Request";
        write(newsockfd, bad_request, strlen(bad_request));
    }
}

char *read_file(const char *filename, size_t *length) {
    struct stat file_stat;
    printf("Trying to read file: %s\n", filename);

    if (stat(filename, &file_stat) < 0) {
        perror("File stat failed");
        return NULL;
    }

    *length = file_stat.st_size;
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        perror("File opening failed");
        return NULL;
    }

    char *data = malloc(*length);
    if (data) {
        fread(data, 1, *length, file);
    }
    fclose(file);
    return data;
}

const char *get_content_type(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if (ext) {
        if (strcmp(ext, ".html") == 0) return "text/html";
        if (strcmp(ext, ".jpg") == 0) return "image/jpeg";
        if (strcmp(ext, ".jpeg") == 0) return "image/jpeg";
        if (strcmp(ext, ".png") == 0) return "image/png";
        if (strcmp(ext, ".gif") == 0) return "image/gif";
        if (strcmp(ext, ".css") == 0) return "text/css";
    }
    return "application/octet-stream";
}

ssize_t read_request(int socket, char **buffer) {
    size_t total_read = 0;
    ssize_t bytes_read;
    size_t buffer_size = INITIAL_BUFFER_SIZE;
    char c;
    int end_of_request = 0;

    *buffer = malloc(buffer_size);
    if (*buffer == NULL) {
        perror("Malloc failed");
        return -1;
    }

    while (!end_of_request) {
        bytes_read = read(socket, &c, 1);
        if (bytes_read == -1) {
            perror("Read failed");
            free(*buffer);
            return -1;
        }
        if (bytes_read == 0) {
            break;
        }
        if (total_read >= buffer_size - 1) {
            buffer_size *= 2;
            char *new_buffer = realloc(*buffer, buffer_size);
            if (new_buffer == NULL) {
                perror("Realloc failed");
                free(*buffer);
                return -1;
            }
            *buffer = new_buffer;
        }
        (*buffer)[total_read++] = c;
        if (total_read >= 4 &&
            (*buffer)[total_read - 1] == '\n' &&
            (*buffer)[total_read - 2] == '\r' &&
            (*buffer)[total_read - 3] == '\n' &&
            (*buffer)[total_read - 4] == '\r') {
            end_of_request = 1;
        }
    }
    (*buffer)[total_read] = '\0';
    return total_read;
}

void send_error_response(int socket, const char *status, const char *message) {
    char response[256];
    snprintf(response, sizeof(response), "HTTP/1.1 %s\r\nContent-Length: %ld\r\n\r\n%s", status, strlen(message), message);
    write(socket, response, strlen(response));
}

char *sanitize_path(const char *uri) {
    if (strcmp(uri, "/") == 0) {
        uri = "/index.html";
    }
    char *path = strdup(uri + 1);
    if (strstr(path, "..")) {
        free(path);
        return NULL;
    }
    printf("Sanitized path: %s\n", path);
    return path;
}

int parse_request(const char *buffer, char **method, char **uri, char **protocol) {
    char *buf_copy = strdup(buffer);
    if (buf_copy == NULL) {
        perror("strdup failed");
        return -1;
    }

    *method = strtok(buf_copy, " ");
    *uri = strtok(NULL, " ");
    *protocol = strtok(NULL, "\r\n");

    if (*method == NULL || *uri == NULL || *protocol == NULL) {
        free(buf_copy);
        return -1;
    }

    return 0;
}

void handle_client(int new_socket) {
    char *buffer = NULL;

    ssize_t valread = read_request(new_socket, &buffer);
    if (valread < 0) {
        close(new_socket);
        return;
    }

    char *method, *uri, *protocol;
    if (parse_request(buffer, &method, &uri, &protocol) < 0) {
        send_error_response(new_socket, "400 Bad Request", "400 Bad Request");
        close(new_socket);
        free(buffer);
        return;
    }

    if (strcmp(method, "GET") == 0) {
        char *filename = sanitize_path(uri);
        if (filename == NULL) {
            send_error_response(new_socket, "400 Bad Request", "400 Bad Request");
            close(new_socket);
            free(buffer);
            return;
        }

        size_t length;
        char *response_data = read_file(filename, &length);
        if (response_data) {
            const char *content_type = get_content_type(filename);
            char header[8000];
            snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nContent-Length: %ld\r\nContent-Type: %s\r\n\r\n", length, content_type);
            write(new_socket, header, strlen(header));
            write(new_socket, response_data, length);
            free(response_data);
        } else {
            perror("File read failed");
            send_error_response(new_socket, "404 Not Found", "404 Not Found");
        }
    } else if (strcmp(method, "POST") == 0) {
        // Handle POST request
        char *content_length_str = strstr(buffer, "Content-Length: ");
        int content_length = 0;
        if (content_length_str) {
            sscanf(content_length_str, "Content-Length: %d", &content_length);
        }

        char *content = strstr(buffer, "\r\n\r\n");
        if (content) {
            content += 4; // Skip past \r\n\r\n
            int content_size = valread - (content - buffer);

            FILE *file = fopen("upload.jpg", "wb");
            if (file) {
                fwrite(content, 1, content_size, file);
                while (content_size < content_length) {
                    valread = read(new_socket, buffer, BUFSIZE);
                    if (valread <= 0) break;
                    fwrite(buffer, 1, valread, file);
                    content_size += valread;
                }
                fclose(file);

                char *ok_response = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK";
                write(new_socket, ok_response, strlen(ok_response));
            } else {
                char *internal_error = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 21\r\n\r\n500 Internal Server Error";
                write(new_socket, internal_error, strlen(internal_error));
            }
        } else {
            char *bad_request = "HTTP/1.1 400 Bad Request\r\nContent-Length: 15\r\n\r\n400 Bad Request";
            write(new_socket, bad_request, strlen(bad_request));
        }
    } else {
        send_error_response(new_socket, "405 Method Not Allowed", "405 Method Not Allowed");
    }

    close(new_socket);
    free(buffer);
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 10) < 0) {
        perror("Listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Server is listening on port 8080\n");

    while (1) {
        printf("Waiting for connections...\n");

        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("Accept failed");
            continue;
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("Fork failed");
            close(new_socket);
            continue;
        }

        if (pid == 0) {
            close(server_fd);
            handle_client(new_socket);
            exit(0);
        } else {
            close(new_socket);
            waitpid(-1, NULL, WNOHANG); // 子プロセスのゾンビ化を防ぐ
        }
    }

    close(server_fd);
    return 0;
}
