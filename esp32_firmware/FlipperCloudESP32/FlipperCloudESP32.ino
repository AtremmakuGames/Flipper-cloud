/*
 * Flipper Cloud - firmware for the ESP32 WiFi module (Flipper Zero WiFi Dev Board,
 * ESP32 Marauder Compact C5 and other ESP32 boards wired to the Flipper UART).
 *
 * Receives text commands from the Flipper over UART and talks to
 * https://filebin.net over WiFi. See docs/PROTOCOL.md for the protocol.
 *
 * Dependencies: ESP32 Arduino core (2.x or 3.x), ArduinoJson 7.
 */
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#define FIRMWARE_VERSION "FlipperCloud-ESP32 1.0"

#ifndef FLIPPER_BAUD
#define FLIPPER_BAUD 115200
#endif

// UART wired to the Flipper. The WiFi Dev Board (ESP32-S2, GPIO43/44) and the
// Marauder Compact C5 (ESP32-C5, GPIO11/12) use UART0 on its default pins.
// Other boards can pick any pins with -DFLIPPER_RX_PIN=.. -DFLIPPER_TX_PIN=..
#if defined(FLIPPER_RX_PIN) && defined(FLIPPER_TX_PIN)
#define FLIPPER Serial1
#elif ARDUINO_USB_CDC_ON_BOOT
#define FLIPPER Serial0
#else
#define FLIPPER Serial
#endif

static const char* FILEBIN_HOST = "filebin.net";
// filebin skips its browser warning page for curl-like user agents.
static const char* USER_AGENT = "curl/8.5.0 (FlipperCloud)";

static const size_t CHUNK_SIZE = 1024;
static const uint32_t FLIPPER_RAW_TIMEOUT_MS = 5000;
static const uint32_t FLIPPER_ACK_TIMEOUT_MS = 10000;
static const uint32_t HTTP_TIMEOUT_MS = 20000;
static const uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;
static const size_t MAX_LINE = 400;

static uint8_t chunk[CHUNK_SIZE];
static String rxLine;

// ---------------------------------------------------------------- helpers

static void reply(const char* type, const String& args = String()) {
    FLIPPER.print(type);
    if(args.length()) {
        FLIPPER.print('\t');
        FLIPPER.print(args);
    }
    FLIPPER.print('\n');
    FLIPPER.flush();
}

static String clean(String text) {
    // Tabs and newlines would break the line protocol.
    text.replace('\t', ' ');
    text.replace('\r', ' ');
    text.replace('\n', ' ');
    return text;
}

static String urlEncode(const String& text) {
    static const char hex[] = "0123456789ABCDEF";
    String out;
    out.reserve(text.length() * 3);
    for(size_t i = 0; i < text.length(); i++) {
        uint8_t c = text[i];
        if(isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0x0F];
        }
    }
    return out;
}

static String fileUrl(const String& bin, const String& name) {
    return String("https://") + FILEBIN_HOST + "/" + urlEncode(bin) + "/" + urlEncode(name);
}

/** Splits a tab separated command line into at most `max` fields. */
static int splitFields(const String& line, String* fields, int max) {
    int count = 0;
    int start = 0;
    while(count < max) {
        int tab = line.indexOf('\t', start);
        if(tab < 0 || count == max - 1) {
            fields[count++] = line.substring(start);
            break;
        }
        fields[count++] = line.substring(start, tab);
        start = tab + 1;
    }
    return count;
}

static bool readFlipperLine(String& line, uint32_t timeoutMs) {
    line = "";
    uint32_t start = millis();
    while(millis() - start < timeoutMs) {
        while(FLIPPER.available()) {
            char c = FLIPPER.read();
            if(c == '\n') return true;
            if(c != '\r' && line.length() < MAX_LINE) line += c;
        }
        delay(1);
    }
    return false;
}

static bool readFlipperRaw(uint8_t* buffer, size_t size, uint32_t timeoutMs) {
    size_t received = 0;
    uint32_t start = millis();
    while(received < size) {
        if(millis() - start > timeoutMs) return false;
        int available = FLIPPER.available();
        if(available > 0) {
            received += FLIPPER.readBytes(buffer + received, min((size_t)available, size - received));
        } else {
            delay(1);
        }
    }
    return true;
}

static bool writeAll(WiFiClient& client, const uint8_t* data, size_t size) {
    size_t written = 0;
    uint32_t start = millis();
    while(written < size) {
        size_t n = client.write(data + written, size - written);
        if(n > 0) {
            written += n;
            start = millis();
        } else if(!client.connected() || millis() - start > HTTP_TIMEOUT_MS) {
            return false;
        } else {
            delay(1);
        }
    }
    return true;
}

static String httpError(int code) {
    switch(code) {
    case 400:
        return "Bad request (check bin code)";
    case 403:
        return "Forbidden (limit or bin needs approval)";
    case 404:
        return "Not found";
    case 405:
        return "Bin is locked or expired";
    case 413:
        return "File too large";
    case 429:
        return "Too many requests, wait";
    default:
        if(code < 0) return "Connection failed: " + HTTPClient::errorToString(code);
        return "HTTP " + String(code);
    }
}

/** Starts the station; dual band chips (ESP32-C5) also look at 5 GHz networks. */
static void wifiStart() {
    WiFi.mode(WIFI_STA);
#if SOC_WIFI_SUPPORT_5G && ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 2)
    WiFi.setBandMode(WIFI_BAND_MODE_AUTO);
#endif
}

static bool wifiReady() {
    if(WiFi.status() == WL_CONNECTED) return true;
    reply("ERR", "WiFi not connected");
    return false;
}

// ---------------------------------------------------------------- commands

static void cmdScan() {
    wifiStart();
    int count = WiFi.scanNetworks();
    if(count < 0) {
        reply("ERR", "scan failed");
        return;
    }

    int sent = 0;
    for(int i = 0; i < count; i++) {
        String ssid = clean(WiFi.SSID(i));
        if(ssid.length() == 0) continue;

        // Results are sorted by signal; skip duplicates of the same network name.
        bool duplicate = false;
        for(int j = 0; j < i && !duplicate; j++) duplicate = WiFi.SSID(j) == WiFi.SSID(i);
        if(duplicate) continue;

        bool open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
        reply(
            "AP",
            String(WiFi.RSSI(i)) + "\t" + (open ? "1" : "0") + "\t" + String(WiFi.channel(i)) +
                "\t" + ssid);
        sent++;
    }
    WiFi.scanDelete();
    reply("SCAN_END", String(sent));
}

static void cmdConnect(const String& ssid, const String& password) {
    wifiStart();
    WiFi.disconnect();
    delay(100);
    WiFi.setAutoReconnect(true);
    if(password.length()) {
        WiFi.begin(ssid.c_str(), password.c_str());
    } else {
        WiFi.begin(ssid.c_str());
    }

    uint32_t start = millis();
    wl_status_t status = WiFi.status();
    while(status != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
        delay(200);
        status = WiFi.status();
    }

    if(status == WL_CONNECTED) {
        reply("OK", WiFi.localIP().toString());
        return;
    }

    WiFi.disconnect();
    if(status == WL_NO_SSID_AVAIL) {
        reply("ERR", "Network not found");
    } else if(status == WL_CONNECT_FAILED) {
        reply("ERR", "Wrong password?");
    } else {
        reply("ERR", "Timeout (wrong password?)");
    }
}

static void cmdStatus() {
    if(WiFi.status() == WL_CONNECTED) {
        reply("OK", WiFi.localIP().toString());
    } else {
        reply("ERR", "not connected");
    }
}

static void cmdList(const String& bin) {
    if(!wifiReady()) return;

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.useHTTP10(true); // no chunked encoding, so the JSON can be streamed
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.setUserAgent(USER_AGENT);
    if(!http.begin(client, String("https://") + FILEBIN_HOST + "/" + urlEncode(bin))) {
        reply("ERR", "bad URL");
        return;
    }
    http.addHeader("Accept", "application/json");

    int code = http.GET();
    if(code == 404) {
        http.end();
        reply("ERR", "Bin not found or expired");
        return;
    }
    if(code != 200) {
        http.end();
        reply("ERR", httpError(code));
        return;
    }

    JsonDocument filter;
    filter["files"][0]["filename"] = true;
    filter["files"][0]["bytes"] = true;
    JsonDocument doc;
    DeserializationError error =
        deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();
    if(error) {
        reply("ERR", String("Bad answer: ") + error.c_str());
        return;
    }

    int count = 0;
    for(JsonObject file : doc["files"].as<JsonArray>()) {
        String name = clean(file["filename"] | "");
        if(name.length() == 0) continue;
        reply("FILE", String(file["bytes"].as<uint32_t>()) + "\t" + name);
        count++;
    }
    reply("OK", String(count));
}

static void cmdPut(const String& bin, const String& name, uint32_t size) {
    if(!wifiReady()) return;

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(HTTP_TIMEOUT_MS / 1000);
    if(!client.connect(FILEBIN_HOST, 443)) {
        reply("ERR", "Cannot connect to filebin.net");
        return;
    }

    String request = String("POST /") + urlEncode(bin) + "/" + urlEncode(name) + " HTTP/1.1\r\n" +
                     "Host: " + FILEBIN_HOST + "\r\n" + "User-Agent: " + USER_AGENT + "\r\n" +
                     "Accept: application/json\r\n" +
                     "Content-Type: application/octet-stream\r\n" +
                     "Content-Length: " + String(size) + "\r\n" + "Connection: close\r\n\r\n";
    if(!writeAll(client, (const uint8_t*)request.c_str(), request.length())) {
        client.stop();
        reply("ERR", "Send failed");
        return;
    }

    uint32_t remaining = size;
    while(remaining > 0) {
        size_t n = min((uint32_t)CHUNK_SIZE, remaining);
        reply("NEXT", String(n));
        if(!readFlipperRaw(chunk, n, FLIPPER_RAW_TIMEOUT_MS)) {
            client.stop();
            reply("ERR", "Cancelled");
            return;
        }
        if(!writeAll(client, chunk, n)) {
            client.stop();
            reply("ERR", "Connection lost");
            return;
        }
        remaining -= n;
    }

    // "HTTP/1.1 201 Created"
    uint32_t start = millis();
    while(!client.available() && client.connected() && millis() - start < HTTP_TIMEOUT_MS) {
        delay(10);
    }
    String status = client.readStringUntil('\n');
    int code = status.length() > 12 ? status.substring(9, 12).toInt() : -1;
    client.stop();

    if(code == 200 || code == 201) {
        reply("OK", String(code));
    } else {
        reply("ERR", code < 0 ? String("No answer from filebin") : httpError(code));
    }
}

static void cmdGet(const String& bin, const String& name) {
    if(!wifiReady()) return;

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.useHTTP10(true);
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.setUserAgent(USER_AGENT);
    // filebin answers with a redirect to the storage server.
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    if(!http.begin(client, fileUrl(bin, name))) {
        reply("ERR", "bad URL");
        return;
    }

    int code = http.GET();
    if(code != 200) {
        http.end();
        reply("ERR", httpError(code));
        return;
    }

    int size = http.getSize();
    reply("SIZE", String(size));

    WiFiClient* stream = http.getStreamPtr();
    uint32_t total = 0;
    uint32_t lastData = millis();
    String answer;

    while(size < 0 || total < (uint32_t)size) {
        size_t available = stream->available();
        if(available == 0) {
            if(!stream->connected() || millis() - lastData > HTTP_TIMEOUT_MS) break;
            delay(2);
            continue;
        }

        size_t want = min(available, CHUNK_SIZE);
        if(size >= 0) want = min(want, (size_t)(size - total));
        size_t n = stream->readBytes(chunk, want);
        if(n == 0) continue;
        lastData = millis();

        FLIPPER.print("DATA\t");
        FLIPPER.print(n);
        FLIPPER.print('\n');
        FLIPPER.write(chunk, n);
        FLIPPER.flush();
        total += n;

        if(!readFlipperLine(answer, FLIPPER_ACK_TIMEOUT_MS) || answer != "ACK") {
            http.end();
            reply("ERR", "Cancelled");
            return;
        }
    }
    http.end();

    if(size >= 0 && total < (uint32_t)size) {
        reply("ERR", "Connection lost");
    } else {
        reply("OK", String(total));
    }
}

static void handleCommand(const String& line) {
    String f[4];
    int count = splitFields(line, f, 4);
    const String& cmd = f[0];

    if(cmd == "PING") {
        reply("PONG", FIRMWARE_VERSION);
    } else if(cmd == "SCAN") {
        cmdScan();
    } else if(cmd == "CONNECT" && count >= 2) {
        cmdConnect(f[1], count >= 3 ? f[2] : String());
    } else if(cmd == "STATUS") {
        cmdStatus();
    } else if(cmd == "LIST" && count >= 2) {
        cmdList(f[1]);
    } else if(cmd == "PUT" && count >= 4) {
        cmdPut(f[1], f[2], strtoul(f[3].c_str(), NULL, 10));
    } else if(cmd == "GET" && count >= 3) {
        cmdGet(f[1], f[2]);
    }
    // Anything else (stray ACK/CANCEL, noise) is ignored on purpose: an
    // unexpected reply could be mistaken for the answer to the next command.
}

// ---------------------------------------------------------------- setup/loop

void setup() {
    FLIPPER.setRxBufferSize(4096);
#if defined(FLIPPER_RX_PIN) && defined(FLIPPER_TX_PIN)
    FLIPPER.begin(FLIPPER_BAUD, SERIAL_8N1, FLIPPER_RX_PIN, FLIPPER_TX_PIN);
#else
    FLIPPER.begin(FLIPPER_BAUD);
#endif
    wifiStart();
    WiFi.setSleep(false);
    reply("PONG", FIRMWARE_VERSION); // lets the Flipper know we (re)booted
}

void loop() {
    while(FLIPPER.available()) {
        char c = FLIPPER.read();
        if(c == '\n') {
            // No trim(): passwords may legitimately end with spaces.
            String line = rxLine;
            rxLine = "";
            if(line.length()) handleCommand(line);
        } else if(c != '\r' && rxLine.length() < MAX_LINE) {
            rxLine += c;
        }
    }
    delay(1);
}
