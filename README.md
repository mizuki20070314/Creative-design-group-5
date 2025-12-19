# 創造設計 5班 - IoT Monitoring & Remote Control System

このリポジトリは、IoTデバイスや外部システムから受信したデータの可視化・遠隔コマンド管理を目的としたプロジェクトです。

## 概要

- TCP経由でデータ（JSON: swing, rssi, found等）を受信、判定し、ファイルに保存・応答。
- Webインターフェース（`index.html`）からリアルタイムでデータを可視化。
- HTTP経由でデバイス等へコマンドを送信可能。
- C言語サーバー＋HTML/JavaScriptによるフロントエンドで構成。

## 主要ファイルと役割

| ファイル                       | 説明 |
|-------------------------------|------|
| `server.c`                    | IoT機器や外部システムからTCPでデータを受信し、パース・判定・応答・ファイル保存 |
| `http_command_server.c`        | HTTP POSTコマンドの受信（/command）・ファイル保存・CORS許可（Web連携用API） |
| `index.html`                   | 受信データ（received_data.txt）を1秒未満周期で自動読込・可視化、コマンド送信UI |

## システム構成例

```
IoTデバイス/ESP32
        │(TCP/JSONデータ送信)
        ▼
  [ server.c - TCPサーバ ]
        │
  received_data.txt  →  [ index.html - Webでリアルタイム表示 ]
        │
HTTPコマンドリクエスト ← コマンド送信ボタン（index.html）
        ▼
 [ http_command_server.c - HTTPサーバ ]
        │
  command.txt
```

## セットアップ方法

1. リポジトリのクローン

    ```sh
    git clone https://github.com/mizuki20070314/Creative-design-group-5.git
    cd Creative-design-group-5
    ```

2. Cプログラムのビルド

    ```sh
    gcc server.c -o server
    gcc http_command_server.c -o http_command_server
    ```

3. サーバの起動例

    ```sh
    ./server            # 12345ポートでTCPサーバ (データ受信)
    ./http_command_server # 8888ポートでHTTPコマンドサーバ
    ```

4. Webビューアの実行

    - Python HTTPサーバ等でローカル配信（例: `python3 -m http.server`）
    - ブラウザで`index.html`を表示  
    - `received_data.txt`の自動読込＆解析、「コマンド送信」でAPIリクエスト可能

## 特徴と使い方

- `server.c`：IoT機器等から送られてくるJSONデータ（swing, rssi, found等）をファイル保存し、判定に応じて機器側へ値を返信
- `http_command_server.c`：Webからのコマンドを受け取り、ファイル保存して他プロセスで利用可能
- `index.html`：データのリアルタイム状態表示 / デバイス制御コマンド送信ボタン付き / rssiに応じた距離グラフ可視化

## 注意点

- セキュリティや排他制御は最小限のため、実運用では追加が必要

## 開発メンバー

- mizuki20070314(s22123-create) 薗田瑞希
- moyashi2048 風間 悠太
- know-amb 栗原 実珠
- aka2357 大津 亜果莉
- nishi072006 西澤 凌介
- hira355 平松 鼓太郎

## ライセンス

MIT License

---

ご質問・追加希望があればIssueやPRでどうぞ
