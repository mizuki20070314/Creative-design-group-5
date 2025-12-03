#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define HTTP_PORT 8888
#define BUF_SIZE 4096
// system()呼び出し用のバッファサイズを定義 (データ長 + コマンド構造分の余裕)
#define CMD_BUFFER_SIZE (BUF_SIZE + 256) 

// --- ファイルにデータを書き込む関数 (system(echo)を使用) ---
static void write_to_command_file(const char *data, size_t len) {
    char command[CMD_BUFFER_SIZE];
    
    // データが長すぎる場合、バッファサイズに合わせて切り詰める
    size_t data_len = len;
    if (data_len > BUF_SIZE) {
        data_len = BUF_SIZE;
    }

    // snprintfでコマンド文字列を構築: echo "データ" > command.txt
    // %.*s を使用して、ヌル終端されていないデータバッファの一部を指定した長さで安全に出力
    // データが二重引用符で囲まれるため、データ内の二重引用符やバックスラッシュは適切にエスケープされない可能性がある点に注意
    int ret = snprintf(command, sizeof(command), "echo \"%.*s\" > command.txt", (int)data_len, data);

    if (ret >= sizeof(command)) {
        fprintf(stderr, "Error: Command buffer too small for data.\n");
        return;
    }

    // シェルコマンドを実行
    int sys_ret = system(command);
    if (sys_ret != 0) {
        fprintf(stderr, "system command failed (Exit Code: %d).\n", sys_ret);
    }

    printf("-> Executed shell command: %s\n", command);
}

// --- HTTPリクエストを処理する関数 ---
static void handle_http_request(int client_fd) {
    char buf[BUF_SIZE];
    ssize_t total_n = 0;
    char *body_start = NULL;
    int content_length = 0;
    const char *response_ok = 
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Access-Control-Allow-Origin: *\r\n"  // CORS対応
        "Content-Length: 4\r\n"
        "\r\n"
        "OK\n"; // レスポンスボディ    
    const char *bad_request = "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\n\r\n";

    // ヘッダー部を受信
    ssize_t n = recv(client_fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) return;
    total_n = n;
    buf[total_n] = '\0';

    // 1. POST /command リクエストかを確認
    if (strncmp(buf, "POST /command", 13) != 0) {
        send(client_fd, bad_request, strlen(bad_request), 0);
        return;
    }

    // 2. Content-Lengthを取得
    char *cl_header = strstr(buf, "Content-Length:");
    if (cl_header) {
        sscanf(cl_header, "Content-Length: %d", &content_length);
    }

    // 3. ボディの開始位置を見つける
    body_start = strstr(buf, "\r\n\r\n");
    if (body_start) {
        body_start += 4; // \r\n\r\n の分をスキップ
    }

    if (content_length > 0 && body_start) {
        size_t header_and_body_len = body_start - buf;
        size_t received_body_len = total_n - header_and_body_len;

        // ボディ全体を格納するバッファを確保
        char *full_body = (char *)malloc(content_length + 1);
        if (!full_body) {
            perror("malloc");
            send(client_fd, response_ok, strlen(response_ok), 0);
            return;
        }

        // 既に受信したボディ部分をコピー
        memcpy(full_body, body_start, received_body_len);
        
        // まだボディ全体を受信しきっていない場合、残りを読み込む
        size_t remaining_len = content_length - received_body_len;
        if (remaining_len > 0) {
            ssize_t read_len = recv(client_fd, full_body + received_body_len, remaining_len, 0);
            if (read_len > 0) {
                received_body_len += read_len;
            }
        }
        
        // C言語の文字列操作を安全に行うため、最後にヌル終端
        full_body[received_body_len] = '\0';

        // コマンドとしてファイルに書き込み
        if (received_body_len > 0) {
            write_to_command_file(full_body, received_body_len);
        }

        free(full_body);
    } else {
        printf("-> POST /command リクエストを受信しましたが、有効なボディがありません\n");
    }

    // 4. クライアントへ成功レスポンスを返す
    send(client_fd, response_ok, strlen(response_ok), 0);
}

int main(void) {
    int listen_fd;
    struct sockaddr_in srv_addr;
    int yes = 1;

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); return 1; }
    
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        perror("setsockopt"); close(listen_fd); return 1;
    }

    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_addr.s_addr = INADDR_ANY;
    srv_addr.sin_port = htons(HTTP_PORT);

    if (bind(listen_fd, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) < 0) {
        perror("bind"); close(listen_fd); return 1;
    }
    if (listen(listen_fd, 5) < 0) {
        perror("listen"); close(listen_fd); return 1;
    }

    printf("Command HTTP Server Listening on 0.0.0.0:%d\n", HTTP_PORT);

    for (;;) {
        struct sockaddr_in cli_addr;
        socklen_t cli_len = sizeof(cli_addr);
        int client_fd = accept(listen_fd, (struct sockaddr *)&cli_addr, &cli_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("accept"); break;
        }

        handle_http_request(client_fd);
        close(client_fd);
    }

    close(listen_fd);
    return 0;
}