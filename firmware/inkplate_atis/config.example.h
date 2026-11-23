// Copy this file to config.h and fill it in. config.h is ignored by git.
#pragma once

#define WIFI_SSID "your-network"
#define WIFI_PASSWORD "your-password"

// Base URL of the Bun server, without a trailing slash. Plain HTTP on your LAN.
#define SERVER_URL "http://192.168.1.10:8080"

// Used when the server does not send X-Refresh-Seconds, and after a failed fetch.
#define DEFAULT_REFRESH_SECONDS 300
#define RETRY_SECONDS 60
