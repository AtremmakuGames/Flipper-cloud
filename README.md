# Flipper Cloud: облачная синхронизация для Flipper Zero

Приложение для Flipper Zero, которое через WiFi-модуль на ESP32 загружает файлы с SD-карты
на [filebin.net](https://filebin.net) и скачивает их обратно.
Поддерживаемые модули: **ESP32 Marauder Compact C5** (flipper.market, WiFi 2.4 и 5 ГГц),
официальная **WiFi Dev Board** (ESP32-S2) и любой ESP32, ESP32-S3 или ESP32-C3, подключённый к UART Flipper.

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
| `esp32_firmware/` | прошивка для ESP32 (Arduino) |
| `docs/PROTOCOL.md` | протокол обмена Flipper ↔ ESP32 |
| `.github/workflows/build.yml` | CI: собирает `.fap` и прошивки ESP32 |

## 1. Прошивка WiFi-модуля (ESP32)

Модулю нужна своя прошивка из `esp32_firmware/`. **Она заменит Marauder**, который стоит на модуле сейчас.
Вернуть Marauder можно в любой момент, прошив его тем же способом
(файл `…_esp32c5_devkit.bin` из [релизов Marauder](https://github.com/justcallmekoko/ESP32Marauder/releases)
или прошивку от продавца модуля).

Готовые файлы собирает CI: вкладка **Actions** → последняя сборка → артефакт `esp32-marauder_c5`
(или `esp32-wifi_devboard` для Dev Board). В артефакте есть:
- папка `flipper_cloud_<плата>/` с файлами `bootloader.bin`, `partitions.bin`, `boot_app0.bin` и `firmware.bin`
  для приложения ESP Flasher;
- `flipper_cloud_<плата>_merged.bin`: один файл для прошивки с компьютера с адреса `0x0`.

### ESP32 Marauder Compact C5: прошивка прямо с Flipper
Модуль садится на пины 9–18 Flipper, общается по UART (пины 13/14) и питается от 3.3V (пин 9).
Компьютер не нужен: прошивку заливает сам Flipper через приложение **ESP Flasher**.

1. Установите **ESP Flasher** (Apps → GPIO) из каталога приложений Flipper или через qFlipper / мобильное приложение.
2. Скопируйте четыре файла из `flipper_cloud_marauder_c5/` на SD-карту в папку `apps_data/esp_flasher/flipper_cloud/`.
3. Переведите модуль в режим загрузчика: зажмите кнопку **BOOT** на модуле, вставьте модуль во Flipper и отпустите кнопку.
   Так модуль прошивают и для Marauder: смотрите инструкцию продавца.
4. В ESP Flasher откройте **Flash ESP** и отметьте **Select for ESP32-C5**. Затем выберите файлы:
   - `Bootloader (0x2000)` → `bootloader.bin`;
   - `Part Table (0x8000)` → `partitions.bin`;
   - `boot_app0 (0xE000)` → `boot_app0.bin`;
   - `FirmwareA (0x10000)` → `firmware.bin`.
5. Нажмите **[>] FLASH - fast (C5)**; если не получилось, используйте **FLASH - slow**.
   Когда прошивка закончится, выньте модуль и вставьте его обратно без кнопки BOOT.

Если ESP Flasher не может подключиться, значит модуль не в режиме загрузчика: повторите шаг 3.

Второй способ, с компьютера: на Flipper включите **GPIO → USB-UART Bridge** (пины 13/14, 115200).
Переведите модуль в режим загрузчика и прошейте `flipper_cloud_marauder_c5_merged.bin` с адреса **0x0**
через <https://espressif.github.io/esptool-js/> (Chrome или Edge).

### WiFi Dev Board (ESP32-S2)
1. Отключите плату от Flipper. Зажмите кнопку **BOOT**, подключите плату к компьютеру по USB и отпустите BOOT.
2. Откройте <https://espressif.github.io/esptool-js/>, нажмите **Connect** и выберите порт.
3. Добавьте файл `flipper_cloud_wifi_devboard_merged.bin` с адресом **0x0** и нажмите **Program**. Потом нажмите **RESET**.

### Сборка самому (Arduino IDE или arduino-cli)
1. Установите ядро **esp32** от Espressif версии 3.3 или новее (Boards Manager). ESP32-C5 поддерживается с версии 3.3.
2. Установите библиотеку **ArduinoJson** версии 7.
3. Откройте `esp32_firmware/FlipperCloudESP32/FlipperCloudESP32.ino` и выберите плату:
   **ESP32C5 Dev Module** для Marauder Compact C5, **ESP32S2 Dev Module** для Dev Board.

```bash
arduino-cli compile -b esp32:esp32:esp32c5 --output-dir build esp32_firmware/FlipperCloudESP32
```

Для своих плат выводы UART задаются флагами. Пример для обычного ESP32 (RX=GPIO16, TX=GPIO17):
```bash
arduino-cli compile -b esp32:esp32:esp32 \
  --build-property "compiler.cpp.extra_flags=-DFLIPPER_RX_PIN=16 -DFLIPPER_TX_PIN=17" \
  esp32_firmware/FlipperCloudESP32
```
CI собирает так же варианты для ESP32-S3 (RX=44, TX=43) и ESP32-C3 (RX=20, TX=21).

### Подключение своего ESP32
| Flipper | ESP32 |
|---|---|
| 13 (TX) | RX |
| 14 (RX) | TX |
| 1 (5V) или 9 (3.3V) | 5V/VIN или 3V3 |
| 8, 11 или 18 (GND) | GND |

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

1. Вставьте WiFi-модуль во Flipper и запустите **Cloud Sync**. Для Dev Board приложение само включает 5V на GPIO.
2. **Test module** — проверка, что модуль отвечает.
3. **WiFi** → **Scan networks** → выберите сеть → введите пароль → **SAVE**.
   Сети 5 ГГц помечены `5G` (их видит только модуль на ESP32-C5). Если сеть скрыта, выберите **Enter SSID manually**.
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
| `WiFi module not responding` | на модуле стоит Marauder или другая прошивка: прошейте Flipper Cloud (раздел 1). Проверьте, что модуль вставлен до конца и не остался в режиме загрузчика (выньте и вставьте без кнопки BOOT) |
| `USART is busy` | закройте другие приложения, которые используют UART, и запустите Cloud Sync заново |
| `WiFi error: Wrong password?` | проверьте пароль; 5 ГГц поддерживает только ESP32-C5, остальные модули работают только в сетях 2.4 ГГц |
| `Bin is empty or does not exist` | в этом bin ещё нет файлов, или он истёк |
| `Forbidden …` | лимит скачиваний filebin или bin требует подтверждения |
