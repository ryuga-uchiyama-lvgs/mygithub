#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <sys/stat.h>

#define INITIAL_BUFFER_SIZE 8000

/* ファイルの内容を読み取る関数 */
char *read_file(const char *filename, size_t *length) {
    struct stat file_stat;
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
    }
    return "application/octet-stream";
}

/* リクエストラインを完全に読み取る関数（動的バッファ拡張付き） */
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

        // リクエストの終端を検出
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

/* エラーレスポンスを送信する関数 */
void send_error_response(int socket, const char *status, const char *message) {
    char response[256];
    snprintf(response, sizeof(response), "HTTP/1.1 %s\r\nContent-Length: %ld\r\n\r\n%s", status, strlen(message), message);
    write(socket, response, strlen(response));
}

/* パスをサニタイズする関数 */
char *sanitize_path(const char *uri) {
    if (strcmp(uri, "/") == 0) {
        uri = "/index.html";
    }

    // 先頭の'/'を除去
    char *path = strdup(uri + 1);

    // パスに不正な文字列が含まれていないかチェック
    if (strstr(path, "..")) {
        free(path);
        return NULL;
    }

    return path;
}

/* HTTPリクエストのメソッド、URI、プロトコルをパースする関数 */
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

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char *buffer = NULL;

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
            perror("Accept failed");
            continue;
        }

        ssize_t valread = read_request(new_socket, &buffer);
        if (valread < 0) {
            close(new_socket);
            continue;
        }

        char *method, *uri, *protocol;
        if (parse_request(buffer, &method, &uri, &protocol) < 0) {
            send_error_response(new_socket, "400 Bad Request", "400 Bad Request");
            close(new_socket);
            free(buffer);
            continue;
        }

        if (strcmp(method, "GET") != 0) {
            send_error_response(new_socket, "405 Method Not Allowed", "405 Method Not Allowed");
            close(new_socket);
            free(buffer);
            continue;
        }

        char *filename = sanitize_path(uri);
        if (filename == NULL) {
            send_error_response(new_socket, "400 Bad Request", "400 Bad Request");
            close(new_socket);
            free(buffer);
            continue;
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
            if (errno == EACCES) {
                send_error_response(new_socket, "403 Forbidden", "403 Forbidden");
            } else {
                send_error_response(new_socket, "404 Not Found", "404 Not Found");
            }
        }

        close(new_socket);
        free(buffer);
    }

    close(server_fd);
    return 0;
}
