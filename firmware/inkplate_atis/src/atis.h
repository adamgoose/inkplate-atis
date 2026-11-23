// D-ATIS parsing: turns the atis.info response into weather and runway use.
// Portable C++11 with no Arduino dependencies so it also builds for the
// simulator and tests in sim/.
#pragma once
#include <string>
#include <vector>

/** One element of the array returned by https://atis.info/api/{ICAO}. */
struct AtisEntry
{
    /** "arr", "dep" or "combined". */
    std::string type;
    std::string code;
    std::string datis;
    /** Empty when the response carries no time. */
    std::string time;
};

struct Wind
{
    bool known = false;
    /** False when the direction is variable. */
    bool hasDirection = false;
    /** Degrees the wind is blowing from. */
    int direction = 0;
    int speed = 0;
    bool hasGust = false;
    int gust = 0;
};

struct Weather
{
    Wind wind;
    /** Statute miles as written ("10", "1 1/2"); empty when unknown. */
    std::string visibility;
    std::vector<std::string> phenomena;
    std::vector<std::string> clouds;
    bool hasTemperature = false;
    int temperature = 0;
    int dewpoint = 0;
    /** Inches of mercury ("30.07"); empty when unknown. */
    std::string altimeter;
};

struct Broadcast
{
    std::string type;
    std::string code;
    std::string time;
};

struct RunwayUse
{
    /** Runway idents without leading zeros, e.g. "4", "30L". */
    std::vector<std::string> landing;
    std::vector<std::string> departing;
    std::vector<std::string> closed;
    std::vector<std::string> approaches;
};

struct AtisState
{
    std::vector<Broadcast> broadcasts;
    Weather weather;
    RunwayUse runways;
    std::vector<std::string> notes;
};

/**
 * Reads the atis.info JSON array. Returns false when the text is not an array
 * of objects or holds no broadcast (the API answers with an error object when
 * an airport has no D-ATIS).
 */
bool parseAtisJson(const std::string &json, std::vector<AtisEntry> &out);

Weather parseWeather(const std::string &text);

/**
 * Classifies every runway list in a sentence by the words around it. A list
 * directly followed by CLSD/CLOSED is a closure; otherwise the words leading up
 * to it decide (DEPG ..., ILS ..., LNDG ...), then the words after it
 * (... APCH IN USE), and finally the previous list in the same sentence.
 */
RunwayUse classifySentence(const std::string &sentence);

AtisState parseAtis(std::vector<AtisEntry> entries);
