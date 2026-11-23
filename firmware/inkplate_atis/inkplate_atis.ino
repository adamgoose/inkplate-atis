/*
 * ATIS display for the Soldered Inkplate 6COLOR.
 *
 * The board wakes, asks the server for the current frame, redraws only when the
 * frame changed (a 7-colour refresh takes ~15 s and flashes), then deep-sleeps.
 * All layout and ATIS parsing live in the server; this sketch only moves pixels.
 *
 * Board: "Soldered Inkplate 6COLOR" from the Inkplate board package.
 * Library: InkplateLibrary.
 */
#ifndef ARDUINO_INKPLATECOLOR
#error "Select the Soldered Inkplate 6COLOR board."
#endif

#include <HTTPClient.h>
#include <Inkplate.h>
#include <WiFi.h>

#include "config.h"

static const int FRAME_WIDTH = 600;
static const int FRAME_HEIGHT = 448;
static const int FRAME_BYTES = FRAME_WIDTH * FRAME_HEIGHT / 2; // 4 bits per pixel
static const int ROW_BYTES = FRAME_WIDTH / 2;
static const uint32_t WIFI_TIMEOUT_MS = 20000;
static const uint32_t STREAM_TIMEOUT_MS = 10000;
static const uint8_t FAILURES_BEFORE_NOTICE = 3;

// Wire value (one nibble) to panel colour. See Ink in server/src/Canvas.ts.
static const uint8_t PALETTE[] = {INKPLATE_BLACK, INKPLATE_WHITE,  INKPLATE_GREEN, INKPLATE_BLUE,
                                  INKPLATE_RED,   INKPLATE_YELLOW, INKPLATE_ORANGE};

// Survive deep sleep.
RTC_DATA_ATTR char lastEtag[48] = "";
RTC_DATA_ATTR uint8_t consecutiveFailures = 0;
RTC_DATA_ATTR uint32_t refreshSeconds = DEFAULT_REFRESH_SECONDS;

Inkplate display;

enum class FetchResult { Unchanged, Drawn, Failed };

static bool connectWiFi()
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    const uint32_t started = millis();
    while (WiFi.status() != WL_CONNECTED)
    {
        if (millis() - started > WIFI_TIMEOUT_MS)
            return false;
        delay(100);
    }
    return true;
}

static bool readExactly(WiFiClient &stream, uint8_t *buffer, size_t length)
{
    size_t received = 0;
    uint32_t lastProgress = millis();
    while (received < length)
    {
        const int n = stream.read(buffer + received, length - received);
        if (n > 0)
        {
            received += n;
            lastProgress = millis();
        }
        else if (!stream.connected() && !stream.available())
            return false;
        else if (millis() - lastProgress > STREAM_TIMEOUT_MS)
            return false;
        else
            delay(1);
    }
    return true;
}

// Fills the Inkplate frame buffer from the response body. The panel is only
// refreshed if the whole frame arrived, so a dropped connection leaves the
// previous image on the glass.
static bool readFrame(WiFiClient &stream)
{
    static uint8_t row[ROW_BYTES];
    for (int y = 0; y < FRAME_HEIGHT; y++)
    {
        if (!readExactly(stream, row, ROW_BYTES))
            return false;
        for (int i = 0; i < ROW_BYTES; i++)
        {
            const uint8_t left = row[i] >> 4;
            const uint8_t right = row[i] & 0x0F;
            display.drawPixel(2 * i, y, PALETTE[left < 7 ? left : 1]);
            display.drawPixel(2 * i + 1, y, PALETTE[right < 7 ? right : 1]);
        }
    }
    return true;
}

static FetchResult fetchFrame(bool force)
{
    HTTPClient http;
    const String url = String(SERVER_URL) + "/frame.bin?battery=" + String(display.readBattery(), 2);
    if (!http.begin(url))
        return FetchResult::Failed;

    const char *wanted[] = {"ETag", "X-Refresh-Seconds"};
    http.collectHeaders(wanted, 2);
    http.setTimeout(STREAM_TIMEOUT_MS);
    if (!force && lastEtag[0] != '\0')
        http.addHeader("If-None-Match", lastEtag);

    const int status = http.GET();
    Serial.printf("GET %s -> %d\n", url.c_str(), status);

    const long advised = http.header("X-Refresh-Seconds").toInt();
    if (advised > 0)
        refreshSeconds = constrain(advised, 30, 6 * 3600);

    FetchResult result = FetchResult::Failed;
    if (status == HTTP_CODE_NOT_MODIFIED)
    {
        result = FetchResult::Unchanged;
    }
    else if (status == HTTP_CODE_OK && http.getSize() == FRAME_BYTES && readFrame(*http.getStreamPtr()))
    {
        display.display();
        strlcpy(lastEtag, http.header("ETag").c_str(), sizeof(lastEtag));
        result = FetchResult::Drawn;
    }
    http.end();
    return result;
}

static void showNotice(const char *line1, const char *line2)
{
    display.clearDisplay();
    display.setTextColor(INKPLATE_BLACK);
    display.setTextSize(3);
    display.setCursor(20, 180);
    display.print(line1);
    display.setTextSize(2);
    display.setCursor(20, 230);
    display.print(line2);
    display.display();
    // Whatever was on screen is gone, so the next good frame must be drawn.
    lastEtag[0] = '\0';
}

static void sleepFor(uint32_t seconds)
{
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
    // The wake button (GPIO36, active low) forces an immediate full redraw.
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_36, 0);
    Serial.printf("Sleeping %lu s\n", (unsigned long)seconds);
    Serial.flush();
    esp_deep_sleep_start();
}

void setup()
{
    Serial.begin(115200);
    display.begin();

    const bool buttonWake = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0;

    FetchResult result = FetchResult::Failed;
    const bool online = connectWiFi();
    if (online)
        result = fetchFrame(buttonWake);

    if (result == FetchResult::Failed)
    {
        if (consecutiveFailures < 255)
            consecutiveFailures++;
        // Leave the last good frame up through brief outages; say so once it is stale.
        if (consecutiveFailures == FAILURES_BEFORE_NOTICE || (buttonWake && lastEtag[0] == '\0'))
            showNotice(online ? "ATIS server unreachable" : "No WiFi", online ? SERVER_URL : WIFI_SSID);
        sleepFor(RETRY_SECONDS);
    }

    consecutiveFailures = 0;
    sleepFor(refreshSeconds);
}

void loop()
{
    // Never reached: deep sleep restarts the sketch from setup().
}
