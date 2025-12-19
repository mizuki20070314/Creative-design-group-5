/* simple_tcp_server with response */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define LISTEN_PORT 12345
#define BACKLOG 5
#define BUF_SIZE 4096

static int listen_fd = -1;

static void handle_sigint(int sig) {
    (void)sig;
    if (listen_fd != -1) {
        close(listen_fd);
        listen_fd = -1;
    }
    fprintf(stderr, "\nterminated by SIGINT\n");
    exit(0);
}

static void parse_json_data(const char *data, int *swing, int *rssi, int *found) {
    if (sscanf(data, "{\"swing\":%d,\"rssi\":%d,\"found\":%d}", swing, rssi, found) == 3) {
        printf("Parsed values -> swing=%d, rssi=%d, found=%d\n", *swing, *rssi, *found);
    } else {
        fprintf(stderr, "Failed to parse JSON: %s\n", data);
    }
}

// 追加関数: command.txt の先頭有効文字を読み取って返す ('A'/'B' など)、読み取れなければ '\0'
static char read_command_char(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return '\0';
    int c = fgetc(f);
    // skip initial CR/LF
    while (c != EOF && (c == '\n' || c == '\r')) c = fgetc(f);
    fclose(f);
    if (c == EOF) return '\0';
    return (char)c;
}

int main(void) {
    struct sockaddr_in srv_addr;
    int yes = 1;

    signal(SIGINT, handle_sigint);

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }

    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        perror("setsockopt");
        close(listen_fd);
        return 1;
    }

    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_addr.s_addr = INADDR_ANY;
    srv_addr.sin_port = htons(LISTEN_PORT);

    if (bind(listen_fd, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) < 0) {
        perror("bind");
        close(listen_fd);
        return 1;
    }

    if (listen(listen_fd, BACKLOG) < 0) {
        perror("listen");
        close(listen_fd);
        return 1;
    }

    printf("Listening on 0.0.0.0:%d\n", LISTEN_PORT);

    // 前回の command.txt の値を保持
    char prev_cmd = '\0';
    const char *cmd_path = "c:\\\\cygwin64\\\\home\\\\USER\\\\Creative-design-group-5\\\\command.txt";

    for (;;) {
        struct sockaddr_in cli_addr;
        socklen_t cli_len = sizeof(cli_addr);
        int client_fd = accept(listen_fd, (struct sockaddr *)&cli_addr, &cli_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }

        char cli_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &cli_addr.sin_addr, cli_ip, sizeof(cli_ip));
        printf("Accepted connection from %s:%d\n", cli_ip, ntohs(cli_addr.sin_port));

        char buf[BUF_SIZE];
        size_t buf_len = 0;

        for (;;) {
            ssize_t n = recv(client_fd, buf + buf_len, sizeof(buf) - buf_len - 1, 0);
            if (n <= 0) {
                printf("Client %s disconnected\n", cli_ip);
                break;
            }

            buf_len += n;
            buf[buf_len] = '\0';

            char *line_start = buf;
            char *newline;
            while ((newline = strchr(line_start, '\n')) != NULL) {
                *newline = '\0';

                if (line_start[0] != '\0') {
                    printf("%s\n", line_start);
                    char command[256];
                    snprintf(command, sizeof(command), "echo \"%s\" > received_data.txt", line_start);
                    system(command);
                    int swing = 0, rssi = 0, found = 0;
                    parse_json_data(line_start, &swing, &rssi, &found);

                    // 追加部分: command.txt の読み取りと変化検出
                    char cur_cmd = read_command_char(cmd_path);
                    int send_value = -1;
                    if (cur_cmd != '0' && prev_cmd == '0') {
                        // 値変化があれば優先して 3(A) または 4(B) を返す
                        if (cur_cmd == 'A') {
                            send_value = 4;
                        } else if (cur_cmd == 'B') {
                            send_value = 5;
                        }
                        printf("command.txt changed: '%c' -> '%c', override send_value=%d\n", prev_cmd, cur_cmd, send_value);
                    } else {
                        // 既存の判定ロジック
                        if (found && rssi > -40) {
                            send_value = 1;
                        } else if (found && rssi > -53) {
                            send_value = 2;
                        } else if (found && rssi > -128) {
                            send_value = 3;
                        }
                    }
                    // prev_cmd を更新（読み取れた場合のみ）
                    if (cur_cmd != '\0') prev_cmd = cur_cmd;

                    char resbuf[8];
                    snprintf(resbuf, sizeof(resbuf), "%d\n", send_value);

                    // ESP32へ返信
                    send(client_fd, resbuf, strlen(resbuf), 0);
                    printf("Returned -> %d\n", send_value);
                }

                line_start = newline + 1;
            }

            size_t remaining = buf + buf_len - line_start;
            memmove(buf, line_start, remaining);
            buf_len = remaining;
        }

        close(client_fd);
    }

    close(listen_fd);
    return 0;
}
