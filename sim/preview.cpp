// Renders the screen exactly as the firmware would, into a PNG.
//
//   preview <atis.json | none> <out.png> [--low-battery] [--raw out.raw]
//
// atis.json is a saved https://atis.info/api/{ICAO} response; "none" shows the
// screen drawn when no broadcast could be fetched. --raw also writes one byte
// per pixel (the ink number), for comparing frames.
#include <fstream>
#include <iostream>
#include <sstream>
#include <string.h>

#include "render.h"

namespace
{

/** Approximate on-panel appearance of each ink. */
const uint8_t PALETTE[][3] = {{0x1c, 0x1c, 0x1c}, {0xf4, 0xf2, 0xea}, {0x2e, 0x7d, 0x4f}, {0x2b, 0x4c, 0x9b},
                              {0xb8, 0x33, 0x2e}, {0xe6, 0xc8, 0x2f}, {0xd9, 0x7a, 0x2b}};

class Bitmap : public Surface
{
  public:
    std::string pixels = std::string((size_t)SCREEN_WIDTH * SCREEN_HEIGHT, (char)INK_WHITE);
    void set(int x, int y, Ink ink) override { pixels[(size_t)y * SCREEN_WIDTH + x] = (char)ink; }
};

uint32_t crc32(const std::string &data)
{
    uint32_t crc = 0xffffffffu;
    for (const unsigned char byte : data)
    {
        crc ^= byte;
        for (int bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

std::string big32(uint32_t value)
{
    return {(char)(value >> 24), (char)(value >> 16), (char)(value >> 8), (char)value};
}

std::string chunk(const char *type, const std::string &data)
{
    const std::string body = type + data;
    return big32((uint32_t)data.size()) + body + big32(crc32(body));
}

/** zlib stream of stored (uncompressed) blocks; plenty for a preview. */
std::string zlibStored(const std::string &raw)
{
    std::string out = "\x78\x01";
    uint32_t a = 1, b = 0;
    for (size_t at = 0; at < raw.size(); at += 65535)
    {
        const size_t n = std::min<size_t>(65535, raw.size() - at);
        out += (char)(at + n == raw.size() ? 1 : 0);
        out += {(char)n, (char)(n >> 8), (char)~n, (char)(~n >> 8)};
        out.append(raw, at, n);
    }
    for (const unsigned char byte : raw)
    {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    return out + big32((b << 16) | a);
}

std::string toPng(const Bitmap &bitmap)
{
    std::string raw;
    for (int y = 0; y < SCREEN_HEIGHT; y++)
    {
        raw += '\0';
        raw.append(bitmap.pixels, (size_t)y * SCREEN_WIDTH, SCREEN_WIDTH);
    }
    const std::string header = big32(SCREEN_WIDTH) + big32(SCREEN_HEIGHT) + std::string("\x08\x03\x00\x00\x00", 5);
    const std::string palette((const char *)PALETTE, sizeof(PALETTE));
    return std::string("\x89PNG\r\n\x1a\n", 8) + chunk("IHDR", header) + chunk("PLTE", palette) +
           chunk("IDAT", zlibStored(raw)) + chunk("IEND", "");
}

bool write(const char *path, const std::string &data)
{
    std::ofstream file(path, std::ios::binary);
    file.write(data.data(), (std::streamsize)data.size());
    return file.good();
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::cerr << "usage: preview <atis.json | none> <out.png> [--low-battery] [--raw out.raw]\n";
        return 2;
    }
    bool lowBattery = false;
    const char *rawPath = nullptr;
    for (int i = 3; i < argc; i++)
    {
        if (strcmp(argv[i], "--low-battery") == 0)
            lowBattery = true;
        else if (strcmp(argv[i], "--raw") == 0 && i + 1 < argc)
            rawPath = argv[++i];
    }

    AtisState state;
    bool haveAtis = false;
    if (strcmp(argv[1], "none") != 0)
    {
        std::ifstream file(argv[1], std::ios::binary);
        std::stringstream json;
        json << file.rdbuf();
        std::vector<AtisEntry> entries;
        if (!file || !parseAtisJson(json.str(), entries))
        {
            std::cerr << argv[1] << " is not an atis.info response with at least one broadcast\n";
            return 1;
        }
        state = parseAtis(entries);
        haveAtis = true;
    }

    Bitmap bitmap;
    Canvas canvas(bitmap, SCREEN_WIDTH, SCREEN_HEIGHT);
    render(canvas, AIRPORT, haveAtis ? &state : nullptr, lowBattery);

    if (!write(argv[2], toPng(bitmap)) || (rawPath != nullptr && !write(rawPath, bitmap.pixels)))
    {
        std::cerr << "cannot write output\n";
        return 1;
    }
    return 0;
}
