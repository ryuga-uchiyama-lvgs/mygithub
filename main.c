#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

#define INITIAL_BUFFER_SIZE 8000

// シグナルハンドラー
void handle_sigchld(int sig) {
    while (waitpid(-1, NULL, WNOHANG) > 0);
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

ssize_t read_request(int socket, char **buffer, size_t *content_length) {
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

    *content_length = 0;
    char *content_length_str = strstr(*buffer, "Content-Length:");
    if (content_length_str) {
        *content_length = strtoul(content_length_str + 15, NULL, 10);
    }

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

int parse_request(const char *buffer, char **method, char **uri, char **protocol, char **body, size_t content_length) {
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

    if (content_length > 0) {
        *body = strstr(buffer, "\r\n\r\n");
        if (*body) {
            *body += 4;  // Skip the "\r\n\r\n"
        }
    } else {
        *body = NULL;
    }

    return 0;
}

void handle_client(int new_socket) {
    char *buffer = NULL;
    size_t content_length = 0;

    ssize_t valread = read_request(new_socket, &buffer, &content_length);
    if (valread < 0) {
        close(new_socket);
        return;
    }

    char *method, *uri, *protocol, *body;
    if (parse_request(buffer, &method, &uri, &protocol, &body, content_length) < 0) {
        send_error_response(new_socket, "400 Bad Request", "400 Bad Request");
        close(new_socket);
        free(buffer);
        return;
    }

    if (strcmp(method, "GET") != 0 && strcmp(method, "POST") != 0) {
        send_error_response(new_socket, "405 Method Not Allowed", "405 Method Not Allowed");
        close(new_socket);
        free(buffer);
        return;
    }

    char *filename = sanitize_path(uri);
    if (filename == NULL) {
        send_error_response(new_socket, "400 Bad Request", "400 Bad Request");
        close(new_socket);
        free(buffer);
        return;
    }

    if (strcmp(method, "GET") == 0) {
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
        printf("Received POST request\n");
        const char *content_type = strstr(buffer, "Content-Type:");
        if (content_type && strstr(content_type, "multipart/form-data")) {
            const char *boundary = strstr(content_type, "boundary=");
            if (boundary) {
                boundary += 9; // "boundary=" の長さ
                char *boundary_end = strstr(boundary, "\r\n");
                if (boundary_end) {
                    size_t boundary_length = boundary_end - boundary;
                    char boundary_str[boundary_length + 1];
                    strncpy(boundary_str, boundary, boundary_length);
                    boundary_str[boundary_length] = '\0';
                    printf("Boundary: %s\n", boundary_str);

                    char *body_start = strstr(buffer, "\r\n\r\n");
                    if (body_start) {
                        body_start += 4;

                        char *file_data_start = strstr(body_start, "\r\n\r\n") + 4;
                        if (!file_data_start) {
                            printf("Failed to find file data start\n");
                            send_error_response(new_socket, "400 Bad Request", "Failed to find file data start");
                            close(new_socket);
                            free(buffer);
                            return;
                        }

                        char *file_data_end = strstr(file_data_start, boundary_str) - 4;
                        if (!file_data_end) {
                            printf("Failed to find file data end\n");
                            send_error_response(new_socket, "400 Bad Request", "Failed to find file data end");
                            close(new_socket);
                            free(buffer);
                            return;
                        }
                        size_t file_data_length = file_data_end - file_data_start;

                        printf("File data start: %.*s\n", 50, file_data_start); // データの開始部分を表示
                        printf("Saving file data: start=%p, end=%p, length=%ld\n", file_data_start, file_data_end, file_data_length);

                        // ファイルパスの作成
                        char filepath[256];
                        snprintf(filepath, sizeof(filepath), "uploaded_image.jpg");
                        printf("File path: %s\n", filepath);

                        // 画像データをファイルに保存
                        FILE *fp = fopen(filepath, "wb");
                        if (fp) {
                            fwrite(file_data_start, 1, file_data_length, fp);
                            fclose(fp);
                            printf("Image saved as %s\n", filepath);

                            // 画像を表示するHTMLレスポンスを送信
                            const char *html_response_template =
                                "HTTP/1.1 200 OK\r\n"
                                "Content-Type: text/html\r\n"
                                "\r\n"
                                "<html><body><h1>Uploaded Image</h1>"
                                "<img src=\"uploaded_image.jpg\" alt=\"Uploaded Image\"/></body></html>";

                            printf("Sending HTML response:\n%s\n", html_response_template);
                            write(new_socket, html_response_template, strlen(html_response_template));
                        } else {
                            perror("File save failed");
                            send_error_response(new_socket, "500 Internal Server Error", "File save failed");
                        }
                    } else {
                        printf("Failed to find body start\n");
                        send_error_response(new_socket, "400 Bad Request", "Failed to find body start");
                    }
                } else {
                    printf("Failed to find boundary end\n");
                    send_error_response(new_socket, "400 Bad Request", "Failed to find boundary end");
                }
            } else {
                printf("Failed to find boundary\n");
                send_error_response(new_socket, "400 Bad Request", "Failed to find boundary");
            }
        } else {
            printf("Content-Type is not multipart/form-data\n");
            send_error_response(new_socket, "400 Bad Request", "Content-Type is not multipart/form-data");
        }
    }

    close(new_socket);
    free(buffer);
}

int main() {
    struct sigaction sa;
    sa.sa_handler = &handle_sigchld;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGCHLD, &sa, 0) == -1) {
        perror("sigaction");
        exit(1);
    }

    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);

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
            if (errno == EINTR) {
                continue; // accept() がシグナルで中断された場合は再試行
            } else {
                perror("Accept failed");
                continue;
            }
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
        }
    }

    close(server_fd);
    return 0;
}
