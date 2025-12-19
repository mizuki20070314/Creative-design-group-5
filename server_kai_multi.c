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
#include <sys/select.h>

#define LISTEN_PORT 12345
#define BACKLOG 5
#define BUF_SIZE 4096
#define MAX_CLIENTS 16
#define MAX_DEVICES 8

static int listen_fd = -1;
static int client_fds[MAX_CLIENTS];

static void handle_sigint(int sig) {
    (void)sig;
    if (listen_fd != -1) close(listen_fd);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_fds[i] != -1) close(client_fds[i]);
    }
    fprintf(stderr, "\nterminated by SIGINT\n");
    exit(0);
}

// 複数デバイス情報をパース
static int parse_json_multi(const char *data, int *swing, int *found_arr, int *rssi_arr, char mac_arr[][20], int *num_devices) {
    *swing = 0;
    *num_devices = 0;
    const char *p = strstr(data, "\"swing\":");
    if (p) sscanf(p, "\"swing\":%d", swing);

    const char *devs = strstr(data, "\"devices\":[");
    if (!devs) return 0;
    devs += strlen("\"devices\":[");
    // 1台ずつパース
    while (*devs && *devs != ']') {
        char mac[20] = {0};
        int rssi = -127, found = 0;
        int n = sscanf(devs, "{\"mac\":\"%19[^\"]\",\"rssi\":%d,\"found\":%d}", mac, &rssi, &found);
        if (n == 3) {
            strncpy(mac_arr[*num_devices], mac, 19);
            rssi_arr[*num_devices] = rssi;
            found_arr[*num_devices] = found;
            (*num_devices)++;
        }
        // 次のデバイスへ
        devs = strchr(devs, '}');
        if (!devs) break;
        devs++;
        if (*devs == ',') devs++;
    }
    return *num_devices;
}

static char read_command_char(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return '\0';
    int c = fgetc(f);
    while (c != EOF && (c == '\n' || c == '\r')) c = fgetc(f);
    fclose(f);
    if (c == EOF) return '\0';
    return (char)c;
}

int main(void) {
    struct sockaddr_in srv_addr;
    int yes = 1;
    fd_set readfds;
    int maxfd;
    char prev_cmd = '\0';
    const char *cmd_path = "c:\\\\cygwin64\\\\home\\\\USER\\\\Creative-design-group-5\\\\command.txt";

    signal(SIGINT, handle_sigint);

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); return 1; }
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        perror("setsockopt"); close(listen_fd); return 1;
    }
    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_addr.s_addr = INADDR_ANY;
    srv_addr.sin_port = htons(LISTEN_PORT);
    if (bind(listen_fd, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) < 0) {
        perror("bind"); close(listen_fd); return 1;
    }
    if (listen(listen_fd, BACKLOG) < 0) {
        perror("listen"); close(listen_fd); return 1;
    }
    printf("Listening on 0.0.0.0:%d\n", LISTEN_PORT);

    for (int i = 0; i < MAX_CLIENTS; i++) client_fds[i] = -1;

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(listen_fd, &readfds);
        maxfd = listen_fd;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_fds[i] != -1) {
                FD_SET(client_fds[i], &readfds);
                if (client_fds[i] > maxfd) maxfd = client_fds[i];
            }
        }

        int ready = select(maxfd + 1, &readfds, NULL, NULL, NULL);
        if (ready < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        // 新規接続
        if (FD_ISSET(listen_fd, &readfds)) {
            struct sockaddr_in cli_addr;
            socklen_t cli_len = sizeof(cli_addr);
            int newfd = accept(listen_fd, (struct sockaddr *)&cli_addr, &cli_len);
            if (newfd >= 0) {
                int added = 0;
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (client_fds[i] == -1) {
                        client_fds[i] = newfd;
                        added = 1;
                        break;
                    }
                }
                if (!added) {
                    close(newfd);
                    printf("Too many clients\n");
                } else {
                    char cli_ip[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &cli_addr.sin_addr, cli_ip, sizeof(cli_ip));
                    printf("Accepted connection from %s:%d\n", cli_ip, ntohs(cli_addr.sin_port));
                }
            }
        }

        // クライアントからのデータ
        for (int i = 0; i < MAX_CLIENTS; i++) {
            int fd = client_fds[i];
            if (fd != -1 && FD_ISSET(fd, &readfds)) {
                char buf[BUF_SIZE];
                ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
                if (n <= 0) {
                    close(fd);
                    client_fds[i] = -1;
                    printf("Client %d disconnected\n", fd);
                    continue;
                }
                buf[n] = '\0';
                char *line = strtok(buf, "\n");
                while (line) {
                    if (line[0] != '\0') {
                        printf("%s\n", line);
                        char command[256];
                        snprintf(command, sizeof(command), "echo \"%s\" > received_data.txt", line);
                        system(command);

                        // JSONパース
                        int swing = 0, num_devices = 0;
                        int found_arr[MAX_DEVICES] = {0};
                        int rssi_arr[MAX_DEVICES] = {0};
                        char mac_arr[MAX_DEVICES][20] = {{0}};
                        parse_json_multi(line, &swing, found_arr, rssi_arr, mac_arr, &num_devices);

                        // 代表値の決定（例: 最も近いデバイスのfound/rssiで判定）
                        int send_value = -1;
                        int best_idx = -1;
                        int best_rssi = -128;
                        for (int d = 0; d < num_devices; d++) {
                            if (found_arr[d] && rssi_arr[d] > best_rssi) {
                                best_rssi = rssi_arr[d];
                                best_idx = d;
                            }
                        }

                        char cur_cmd = read_command_char(cmd_path);
                        if (cur_cmd != '0' && prev_cmd == '0') {
                            if (cur_cmd == 'A') send_value = 4;
                            else if (cur_cmd == 'B') send_value = 5;
                            printf("command.txt changed: '%c' -> '%c', override send_value=%d\n", prev_cmd, cur_cmd, send_value);
                        } else {
                            if (best_idx != -1) {
                                if (best_rssi > -40) send_value = 1;
                                else if (best_rssi > -53) send_value = 2;
                                else if (best_rssi > -128) send_value = 3;
                            } else {
                                send_value = 0; // どれも見つからない場合
                            }
                        }
                        if (cur_cmd != '\0') prev_cmd = cur_cmd;

                        char resbuf[8];
                        snprintf(resbuf, sizeof(resbuf), "%d\n", send_value);

                        // 全クライアントに送信
                        for (int j = 0; j < MAX_CLIENTS; j++) {
                            if (client_fds[j] != -1) {
                                send(client_fds[j], resbuf, strlen(resbuf), 0);
                            }
                        }
                        printf("Broadcasted -> %d\n", send_value);
                    }
                    line = strtok(NULL, "\n");
                }
            }
        }
    }

    close(listen_fd);
    return 0;
}