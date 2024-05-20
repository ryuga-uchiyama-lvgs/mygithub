/*HTTPサーバー作成2024/05/20 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h> /*ソケット生成用*/
#include <netinet/in.h>
#include <errno.h> /*エラー確認用*/

/* ファイルの内容を読み取る関数 */
char *read_file(const char *filename) {
    // ファイルをバイナリモードで開く
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        perror("File opening failed");  // ファイルが開けなかった場合のエラー処理
        return NULL;
    }

    // ファイルのサイズを取得
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);

    // ファイルの内容を格納するためのバッファを確保
    char *data = malloc(length + 1);
    if (data) {
        fread(data, 1, length, file);  // ファイルの内容を読み取る
        data[length] = '\0';  // 文字列として扱うためにヌル終端を追加
    }
    fclose(file);  // ファイルを閉じる
    return data;  // 読み取ったデータを返す
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[8000] = {0};

    // ソケットを作成
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Socket creation failed");  // ソケット作成に失敗した場合のエラー処理
        exit(EXIT_FAILURE);
    }

    // ソケットアドレス構造体を設定
    address.sin_family = AF_INET;  // アドレスファミリー（IPv4）
    address.sin_addr.s_addr = INADDR_ANY;  // すべてのインターフェースで受け付ける
    address.sin_port = htons(8080);  // ポート番号を設定

    // ソケットにアドレスをバインド
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");  // バインドに失敗した場合のエラー処理
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // ソケットを接続待ち状態に設定
    if (listen(server_fd, 10) < 0) {
        perror("Listen failed");  // リッスンに失敗した場合のエラー処理
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // メインのサーバーループ
    while (1) {
        printf("Waiting for connections...\n");

        // 接続要求を受け付ける
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("Accept failed");  // 受付に失敗した場合のエラー処理
            continue;  // 失敗した場合は次のループへ続行
        }

        // クライアントからのリクエストメッセージを受信
        int valread = read(new_socket, buffer, sizeof(buffer) - 1);
        if (valread < 0) {
            perror("Read failed");  // 読み取りに失敗した場合のエラー処理
            close(new_socket);
            continue;
        }

        buffer[valread] = '\0';  // 受信したデータをヌル終端で文字列化

        // HTTPメソッド、URI、プロトコルをパース
        char *method = strtok(buffer, " ");
        char *uri = strtok(NULL, " ");
        char *protocol = strtok(NULL, "\r\n");

        // リクエストの形式が無効な場合は400エラーを返す
        if (method == NULL || uri == NULL || protocol == NULL) {
            char *error_message = "HTTP/1.1 400 Bad Request\r\nContent-Length: 15\r\n\r\n400 Bad Request";
            write(new_socket, error_message, strlen(error_message));
            close(new_socket);
            continue;
        }

        // サポートされていないHTTPメソッドの場合は405エラーを返す
        if (strcmp(method, "GET") != 0) {
            char *error_message = "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 23\r\n\r\n405 Method Not Allowed";
            write(new_socket, error_message, strlen(error_message));
            close(new_socket);
            continue;
        }

        // ルートが要求された場合はindex.htmlに置き換え
        if (strcmp(uri, "/") == 0) {
            uri = "/index.html";
        }

        // 先頭の'/'を除去してファイル名を取得
        char *filename = uri + 1;
        char *response_data = read_file(filename);  // ファイルを読み取る

        // ファイルが存在する場合にレスポンスメッセージを作成
        if (response_data) {
            char header[8000];
            sprintf(header, "HTTP/1.1 200 OK\r\nContent-Length: %ld\r\n\r\n", strlen(response_data));
            write(new_socket, header, strlen(header));  // ヘッダーを送信
            write(new_socket, response_data, strlen(response_data));  // ボディを送信
            free(response_data);  // 動的に確保したメモリを解放
        } else {
            // ファイルが存在しない場合のエラーレスポンスメッセージを作成
            char *error_message;
            if (errno == EACCES) {
                error_message = "HTTP/1.1 403 Forbidden\r\nContent-Length: 13\r\n\r\n403 Forbidden";
            } else {
                error_message = "HTTP/1.1 404 Not Found\r\nContent-Length: 13\r\n\r\n404 Not Found";
            }
            write(new_socket, error_message, strlen(error_message));  // エラーメッセージを送信
        }

        // クライアントとの接続を閉じる
        close(new_socket);
    }

    // サーバーソケットを閉じる
    close(server_fd);
    return 0;
}
