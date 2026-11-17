# 01: シンプルなWebサーバー（W5100 / PlatformIO）

W5100の最初のサンプルとして、最低限のLAN通信を確認するプロジェクトです。
Arduino Uno R3（ATmega328P）とW5100 Ethernetシールドを使用します。
DHCPでIPアドレスを取得し、HTTPサーバー（ポート80）でIPアドレスと起動後の秒数を表示します。
ブラウザを再読み込みすると秒数が更新されます。SDカードは使用しません。

## 接続

1. 電源を切ったUnoにW5100シールドを装着します。
2. LANケーブルでシールドとDHCP対応ルーター／スイッチを接続します。
3. USBケーブルでUnoとPCを接続します。

標準シールドのピン割り当ては次のとおりです。

| Unoのピン | 用途 |
| --- | --- |
| D10 | W5100のCS |
| D11 / D12 / D13 | SPI MOSI / MISO / SCK（シールドの版によってICSP経由） |
| D4 | SDカードのCS（サンプルではHIGHにして無効化） |

単体モジュールの場合は、製品の電源電圧・信号電圧・ピン配置を確認して配線してください。
上の表はUno用シールドの説明です。

## ビルド・書き込み

VS CodeにPlatformIO IDE拡張をインストールし、この `W5100/01_basic_web_server` フォルダを開きます。
PlatformIOのBuild → Upload → Serial Monitorの順で実行してください。
Ethernetライブラリなどの依存関係は初回ビルド時に取得されます。

PlatformIO Coreが利用できるターミナルでは、このフォルダで以下を実行できます。

```powershell
pio run
pio run --target upload
pio device monitor --baud 115200
```

ポートの自動選択に失敗したら、`platformio.ini` の `[env:uno]` に実際のポートを指定します。

```ini
upload_port = COM3
monitor_port = COM3
```

シリアルモニターは115200bpsです。起動ログを見逃した場合はUnoのRESETボタンを押してください。
表示された `http://192.168.x.x/` を同じLANのPCのブラウザで開くと、動作確認ページが表示されます。

## ネットワーク設定

`src/main.cpp` の冒頭で設定を変更できます。

- 通常は `kUseDhcp = true` のまま使用します。
- DHCPを使わない場合は `kUseDhcp = false` にし、`staticIp`、`dnsServer`、`gateway`、`subnet` をLANに合わせて変更します。固定IPには他の機器と重複しないアドレスを割り当ててください。
- 複数台接続する場合は `mac` を機器ごとに変更してください。初期値はサンプル用のローカル管理MACです。
- CSがD10以外のモジュールでは `kEthernetCsPin` を変更します。

起動時はシールドの安定を1秒待ち、DHCPで最大約30秒間IPアドレス取得を試みます。
失敗時はシリアルに確認方法を表示し、5秒待ってから自動で再試行します。
LANケーブルを後から接続した場合も、RESETなしで再試行できます。
ルーターなしでPCのLAN端子に直結する場合、通常はDHCPサーバーがありません。
その場合はPCの有線LANとW5100に、同じサブネットの異なる固定IPを設定してください。
W5100ではライブラリのリンク状態検出が未対応のため、ケーブル接続は端子のLEDなどでも確認してください。

このサンプルはLAN内での動作確認用です。認証やHTTPSは実装していません。
使用するマイコンがUno R3以外の場合は、PlatformIOのボード設定と配線の変更が必要です。

## 参考資料

- [Arduino公式Ethernetライブラリ](https://docs.arduino.cc/libraries/ethernet/)
- [Ethernetライブラリのソース・サンプル](https://github.com/arduino-libraries/Ethernet)
- [PlatformIO Arduino Uno設定](https://docs.platformio.org/en/latest/boards/atmelavr/uno.html)
