/*HTTPサーバー作成2024/05/15*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>/*ソケット生成*/
#include <netinet/in.h>
#include <errno.h>/*内山エラー確認用*/

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
        // ファイルの内容を読み取る
        fread(data, 1, length, file);
        data[length] = '\0';  // 文字列として扱うためにヌル終端を追加
    }
    fclose(file);  // ファイルを閉じる
    return data;   // 読み取ったデータを返す
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[1024] = {0};

    // ソケットを作成
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Socket creation failed");  // ソケット作成に失敗した場合のエラー処理
        exit(EXIT_FAILURE);
    }

    // ソケットアドレス構造体を設定
    address.sin_family = AF_INET;  // アドレスファミリー（IPv4）
    address.sin_addr.s_addr = INADDR_ANY;  // すべてのインターフェースで受け付ける
    address.sin_port = htons(8080);  // ポート番号を設定

    // ソケットにアドレスをくっつける(バインドする)
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");  // バインドに失敗した場合のエラー処理
        exit(EXIT_FAILURE);
    }

    // ソケットを接続待ち状態に設定
    if (listen(server_fd, 10) < 0) {
        perror("Listen failed");  // リッスンに失敗した場合のエラー処理
        exit(EXIT_FAILURE);
    }

    // メインのサーバー　サーバーは常に繰り返し
    while (1) {
        printf("Waiting for connections...\n");

        // 接続要求を受け付ける
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("Accept failed");  // 受付に失敗した場合のエラー処理
            continue;  // 失敗した場合は次のループへ続行
        }

        // クライアントからのリクエストメッセージを受信
        read(new_socket, buffer, 1024);//
        char *method = strtok(buffer, " ");  // メソッドを取得
        char *uri = strtok(NULL, " ");  // リクエストURIを取得
        
        if (strcmp(uri, "/") == 0){
            uri = "/index.html";  // ルートが要求された場合はindex.htmlに置き換え
        }

        char *filename = uri + 1;  // 先頭の'/'を除去してファイル名を取得
        char *response_data = read_file(filename);  // ファイルの内容を読み取る

        if (response_data) {

            // ファイルが存在する場合にレスポンスメッセージを作成
            char header[1024];
            sprintf(header, "HTTP/1.1 200 OK\nContent-Length: %ld\n\n", strlen(response_data));
            write(new_socket, header, strlen(header));  // ヘッダーを送信
            write(new_socket, response_data, strlen(response_data));  // ボディを送信
            free(response_data);  // 動的に確保したメモリを解放

        } 
        else {
            // ファイルが存在しない場合のエラーレスポンスメッセージを作成
            char *error_message = "HTTP/1.1 404 Not Found\nContent-Length: 13\n\n404 Not Found";
            write(new_socket, error_message, strlen(error_message));  // エラーメッセージを送信
        }

        // クライアントとの接続を閉じる
        close(new_socket);
    }

    return 0;
}