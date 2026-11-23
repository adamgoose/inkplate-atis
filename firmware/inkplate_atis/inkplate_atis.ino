/*
 * ATIS display for the Soldered Inkplate 6COLOR.
 *
 * The board wakes, fetches the D-ATIS for the built-in airport from atis.info,
 * redraws only when the broadcast changed (a 7-colour refresh takes ~15 s and
 * flashes), then deep-sleeps. Parsing and layout live in src/, which also
 * builds on a computer for tests and previews (see sim/).
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
#include <WiFiClientSecure.h>

#include "config.h"
#include "src/render.h"

#ifndef ATIS_API
#define ATIS_API "https://atis.info/api/"
#endif
#ifndef REFRESH_SECONDS
#define REFRESH_SECONDS 300
#endif
#ifndef RETRY_SECONDS
#define RETRY_SECONDS 60
#endif

static const uint32_t WIFI_TIMEOUT_MS = 20000;
static const uint32_t HTTP_TIMEOUT_MS = 15000;
static const uint8_t FAILURES_BEFORE_NOTICE = 3;
// The LOW BATT flag turns on below the first voltage and off above the second,
// so a battery hovering at the threshold does not redraw the panel every wake.
static const float LOW_BATTERY_VOLTS = 3.5f;
static const float BATTERY_OK_VOLTS = 3.6f;

enum Shown : uint8_t { SHOWN_NOTHING, SHOWN_ATIS, SHOWN_PROBLEM };

// Survive deep sleep.
RTC_DATA_ATTR uint8_t shown = SHOWN_NOTHING;
RTC_DATA_ATTR uint32_t shownHash = 0;
RTC_DATA_ATTR uint8_t consecutiveFailures = 0;
RTC_DATA_ATTR bool lowBattery = false;

Inkplate display;

// Ink values match the library's INKPLATE_* colours, so pixels pass straight through.
class Panel : public Surface
{
  public:
    void set(int x, int y, Ink ink) override { display.drawPixel(x, y, ink); }
};

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

// Returns the HTTP status, or a negative HTTPClient error.
static int fetchAtis(std::string &body)
{
    WiFiClientSecure client;
#ifdef ATIS_ROOT_CA
    client.setCACert(ATIS_ROOT_CA);
#else
    client.setInsecure();
#endif
    HTTPClient http;
    const String url = String(ATIS_API) + AIRPORT.ident;
    if (!http.begin(client, url))
        return -1;
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setUserAgent("inkplate-atis");
    const int status = http.GET();
    if (status == HTTP_CODE_OK)
        body = http.getString().c_str();
    http.end();
    Serial.printf("GET %s -> %d (%u bytes)\n", url.c_str(), status, (unsigned)body.size());
    return status;
}

// FNV-1a over everything the screen is drawn from.
static uint32_t contentHash(const std::vector<AtisEntry> &entries)
{
    uint32_t hash = 2166136261u;
    const auto mix = [&hash](const std::string &text) {
        for (const char c : text)
            hash = (hash ^ (uint8_t)c) * 16777619u;
        hash = (hash ^ 0xffu) * 16777619u;
    };
    for (const AtisEntry &entry : entries)
    {
        mix(entry.type);
        mix(entry.code);
        mix(entry.datis);
        mix(entry.time);
    }
    mix(lowBattery ? "low" : "ok");
    return hash;
}

static void draw(const AtisState *atis, const char *problem)
{
    Panel panel;
    Canvas canvas(panel, SCREEN_WIDTH, SCREEN_HEIGHT);
    render(canvas, AIRPORT, atis, lowBattery, problem);
    display.display();
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

    const float volts = display.readBattery();
    if (volts > 0 && volts < LOW_BATTERY_VOLTS)
        lowBattery = true;
    else if (volts > BATTERY_OK_VOLTS)
        lowBattery = false;

    char problem[80] = "";
    std::vector<AtisEntry> entries;
    if (!connectWiFi())
    {
        snprintf(problem, sizeof(problem), "No WiFi: %s", WIFI_SSID);
    }
    else
    {
        std::string body;
        const int status = fetchAtis(body);
        if (status != HTTP_CODE_OK)
            snprintf(problem, sizeof(problem), "atis.info did not answer (%d)", status);
        else if (!parseAtisJson(body, entries))
            snprintf(problem, sizeof(problem), "No D-ATIS for %s on atis.info", AIRPORT.ident);
    }

    if (problem[0] != '\0')
    {
        Serial.println(problem);
        if (consecutiveFailures < 255)
            consecutiveFailures++;
        // Leave the last broadcast up through brief outages; say so once it is stale.
        const bool stale = consecutiveFailures >= FAILURES_BEFORE_NOTICE || buttonWake;
        if (shown == SHOWN_NOTHING || (shown == SHOWN_ATIS && stale))
        {
            draw(nullptr, problem);
            shown = SHOWN_PROBLEM;
        }
        sleepFor(RETRY_SECONDS);
    }

    consecutiveFailures = 0;
    const uint32_t hash = contentHash(entries);
    if (buttonWake || shown != SHOWN_ATIS || hash != shownHash)
    {
        const AtisState state = parseAtis(entries);
        draw(&state, "");
        shown = SHOWN_ATIS;
        shownHash = hash;
        Serial.println("Redrawn");
    }
    else
    {
        Serial.println("Unchanged");
    }
    sleepFor(REFRESH_SECONDS);
}

void loop()
{
    // Never reached: deep sleep restarts the sketch from setup().
}
