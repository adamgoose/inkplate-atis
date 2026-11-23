// Copy this file to config.h and fill it in. config.h is ignored by git.
#pragma once

#define WIFI_SSID "your-network"
#define WIFI_PASSWORD "your-password"

// Seconds of deep sleep between checks of atis.info, and after a failed check.
#define REFRESH_SECONDS 300
#define RETRY_SECONDS 60

// Optional: verify atis.info's TLS certificate against this root (PEM). Without
// it the connection is encrypted but the server is not authenticated, which is
// acceptable for public, read-only data.
// #define ATIS_ROOT_CA "-----BEGIN CERTIFICATE-----\n...\n-----END CERTIFICATE-----\n"
