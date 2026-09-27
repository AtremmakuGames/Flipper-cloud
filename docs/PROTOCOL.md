# Протокол Flipper ↔ ESP32

UART 115200 8N1. Flipper: пин 13 (TX) → RX ESP32, пин 14 (RX) ← TX ESP32, GND ↔ GND.
Питание: 5V (пин 1) у WiFi Dev Board, 3.3V (пин 9) у Marauder Compact C5.

Каждая команда и каждый ответ — одна текстовая строка, которая заканчивается `\n`.
Поля разделяются символом табуляции `\t` (дальше в таблице он обозначен как `→`).
Строки, которых Flipper не ждёт (например, мусор при загрузке ESP32), он пропускает.
Неизвестные команды ESP32 молча игнорирует.

| Команда (Flipper → ESP) | Ответы (ESP → Flipper) |
|---|---|
| `PING` | `PONG→<версия>` |
| `SCAN` | `AP→<rssi>→<open 0/1>→<канал>→<ssid>` × N, затем `SCAN_END→<N>` или `ERR→<текст>`. Канал выше 14 означает сеть 5 ГГц |
| `CONNECT→<ssid>→<пароль>` | `OK→<ip>` или `ERR→<текст>` |
| `STATUS` | `OK→<ip>` или `ERR→not connected` |
| `LIST→<bin>` | `FILE→<байт>→<имя>` × N, затем `OK→<N>` или `ERR→<текст>` |
| `PUT→<bin>→<имя>→<размер>` | см. ниже |
| `GET→<bin>→<имя>` | см. ниже |

## Загрузка (PUT)

```
Flipper: PUT→mybin123→test.sub→2048
ESP:     NEXT→1024            ← ESP открыл HTTPS и ждёт данные
Flipper: <1024 байта>
ESP:     NEXT→1024
Flipper: <1024 байта>
ESP:     OK→201               ← ответ filebin (или ERR→<текст>)
```

Если Flipper перестаёт присылать данные (пользователь нажал «Назад»),
ESP через 5 секунд прерывает загрузку и отвечает `ERR→Cancelled`.

## Скачивание (GET)

```
Flipper: GET→mybin123→test.sub
ESP:     SIZE→2048            ← -1, если размер неизвестен
ESP:     DATA→1024\n<1024 байта>
Flipper: ACK                  ← или CANCEL, чтобы прервать
ESP:     DATA→1024\n<1024 байта>
Flipper: ACK
ESP:     OK→2048              ← или ERR→<текст>
```

ESP ждёт `ACK` после каждого блока, поэтому Flipper успевает записать данные на SD-карту
и буфер UART не переполняется.

## Запросы к filebin.net

- список файлов: `GET https://filebin.net/<bin>` с заголовком `Accept: application/json`;
- загрузка: `POST https://filebin.net/<bin>/<имя>` с телом файла и `Content-Length`;
- скачивание: `GET https://filebin.net/<bin>/<имя>` → редирект 302 на хранилище.
  User-Agent содержит `curl`, поэтому filebin не показывает страницу-предупреждение для браузеров.

Код bin: 8–60 символов `A-Z a-z 0-9 - _`. Если bin не существует, он создаётся при первой загрузке.
