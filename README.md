# Flipper Cloud: облачная синхронизация для Flipper Zero

Приложение для Flipper Zero, которое через WiFi-модуль на ESP32 (официальная WiFi Dev Board
или любой ESP32) загружает файлы с SD-карты на [filebin.net](https://filebin.net) и скачивает их обратно.

- подключение к WiFi: поиск сетей, ввод пароля (клавиатура со всеми символами);
- **код filebin (bin)** — общая «папка» в облаке, например `myflipper2024`;
- **Upload file** — выбрать любой файл на SD-карте и загрузить его в bin;
- **Download file** — список файлов в bin, выбрать нужный и скачать на Flipper;
- настройки (WiFi, пароль, bin) сохраняются, повторно вводить их не нужно.

```
Flipper Zero  ──UART──►  ESP32 (WiFi)  ──HTTPS──►  filebin.net/<bin>/<файл>
 приложение               прошивка
 flipper_app/             esp32_firmware/
```

## Структура репозитория

| Папка | Что внутри |
|---|---|
| `flipper_app/` | приложение для Flipper (`.fap`, язык C) |
| `esp32_firmware/` | прошивка для ESP32 (Arduino / PlatformIO) |
| `docs/PROTOCOL.md` | протокол обмена Flipper ↔ ESP32 |
| `.github/workflows/build.yml` | CI: собирает `.fap` и прошивки ESP32 |

## 1. Прошивка WiFi-модуля (ESP32)

Готовые файлы собирает CI: вкладка **Actions** → последняя сборка → артефакт `esp32-wifi_devboard`.

### Вариант А: через браузер (проще всего)
1. Отключите плату от Flipper. Зажмите кнопку **BOOT** на плате, подключите её к компьютеру по USB и отпустите BOOT.
2. Откройте <https://espressif.github.io/esptool-js/> в Chrome или Edge, нажмите **Connect** и выберите порт.
3. Добавьте файл `flipper_cloud_wifi_devboard_merged.bin` с адресом **0x0** и нажмите **Program**.
4. Нажмите **RESET** на плате.

### Вариант Б: Arduino IDE
1. Установите поддержку ESP32 (Boards Manager → `esp32` от Espressif, версии 2.x или 3.x).
2. Установите библиотеку **ArduinoJson** (версия 7) через Library Manager.
3. Откройте `esp32_firmware/FlipperCloudESP32/FlipperCloudESP32.ino`.
4. Плата: **ESP32S2 Dev Module**. Переведите плату в режим загрузки (BOOT + USB) и нажмите Upload, затем RESET.

### Вариант В: PlatformIO
```bash
cd esp32_firmware
pio run -e wifi_devboard -t upload     # WiFi Dev Board (ESP32-S2)
pio run -e esp32dev -t upload          # обычный ESP32: RX=GPIO16, TX=GPIO17
```
Есть также окружения `esp32s3` (RX=44, TX=43) и `esp32c3` (RX=20, TX=21).

### Подключение своего ESP32 (если это не Dev Board)
| Flipper | ESP32 |
|---|---|
| 13 (TX) | RX (см. выше) |
| 14 (RX) | TX |
| 1 (5V) | 5V / VIN |
| 8 или 18 (GND) | GND |

## 2. Установка приложения на Flipper

Нужна официальная прошивка Flipper 1.x. Готовый `.fap` лежит в артефакте CI `flipper_cloud-release-…`.
Скопируйте `flipper_cloud.fap` на SD-карту в `apps/GPIO/` (через qFlipper или картридер).

Собрать самому:
```bash
pip install ufbt
cd flipper_app
ufbt            # сборка → dist/flipper_cloud.fap
ufbt launch     # сборка, установка и запуск на подключённом Flipper
```

Приложение появится в меню **Apps → GPIO → Cloud Sync**.

## 3. Использование

1. Вставьте WiFi-модуль во Flipper и запустите **Cloud Sync**. Приложение само включает питание 5V на GPIO.
2. **Test module** — проверка, что модуль отвечает.
3. **WiFi** → **Scan networks** → выберите сеть → введите пароль → **SAVE**.
   Если сеть скрыта, выберите **Enter SSID manually**.
4. **Bin** → введите код filebin: 8–60 символов, `a-z A-Z 0-9 - _`.
   Такой же код введите на другом устройстве или откройте `https://filebin.net/<код>` в браузере.
5. **Upload file** → выберите файл. Он появится по адресу `https://filebin.net/<код>/<имя файла>`.
6. **Download file** → выберите файл из списка.
   - **Save to: auto by type**: `.sub` → `subghz/`, `.ir` → `infrared/`, `.nfc` → `nfc/`,
     `.rfid` → `lfrfid/`, `.ibtn` → `ibutton/`, `.fmf` → `music_player/`, `.js` → `apps/Scripts/`,
     `.fap` → `apps/Misc/`, остальные файлы → `cloud_sync/`;
   - **Save to: /cloud_sync**: все файлы сохраняются в `cloud_sync/`.
   Существующие файлы не перезаписываются: новый файл получает имя `имя_1.ext`.

Во время загрузки или скачивания кнопка **Назад** отменяет операцию.

### Клавиатура
| Кнопка | Действие |
|---|---|
| стрелки | выбор символа |
| OK | ввести символ |
| удерживать OK | ввести заглавную букву |
| `Aa` | переключить регистр |
| `#?` | символы `!@#$%^&*()` и другие |
| `space` / `del` | пробел / удалить (удерживать `del`, чтобы стереть всё) |
| Назад | удалить символ; удерживать, чтобы выйти без сохранения |
| `SAVE` | сохранить |

## Безопасность и ограничения

- **Bin на filebin.net публичный**: любой, кто знает код, может скачать файлы. Используйте длинный,
  сложный код и не храните там секреты. Файлы удаляются автоматически примерно через неделю.
- Пароль от WiFi хранится открытым текстом в `/ext/apps_data/flipper_cloud/settings.txt`.
- ESP32 не проверяет TLS-сертификат filebin.net (`setInsecure()`): так не нужно обновлять сертификаты в прошивке.
- Скорость ограничена UART 115200 бод, это примерно 10 КБ/с. Для файлов Flipper этого достаточно.
- Filebin может ограничивать число скачиваний и требовать подтверждения для некоторых bin (ошибка 403).

## Если что-то не работает

| Сообщение | Что сделать |
|---|---|
| `WiFi module not responding` | прошейте ESP32 прошивкой из `esp32_firmware/`; проверьте, что плата вставлена до конца; отключите другие GPIO-приложения |
| `USART is busy` | закройте другие приложения, которые используют UART, и запустите Cloud Sync заново |
| `WiFi error: Wrong password?` | проверьте пароль; ESP32 работает только в сетях 2.4 ГГц |
| `Bin is empty or does not exist` | в этом bin ещё нет файлов, или он истёк |
| `Forbidden …` | лимит скачиваний filebin или bin требует подтверждения |
