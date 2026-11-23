#include "atis.h"

#include <algorithm>
#include <stdlib.h>
#include <string.h>

namespace
{

const size_t NONE = std::string::npos;

bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
bool isWordChar(char c) { return isDigit(c) || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }

bool allDigits(const std::string &s, size_t from, size_t to)
{
    if (from >= to)
        return false;
    for (size_t i = from; i < to; i++)
        if (!isDigit(s[i]))
            return false;
    return true;
}

/** True where a word starts or ends, like \b in a regular expression. */
bool boundary(const std::string &s, size_t at)
{
    const bool before = at > 0 && isWordChar(s[at - 1]);
    const bool after = at < s.size() && isWordChar(s[at]);
    return before != after;
}

size_t skipSpaces(const std::string &s, size_t at)
{
    while (at < s.size() && isSpace(s[at]))
        at++;
    return at;
}

bool startsWith(const std::string &s, size_t at, const char *text)
{
    return s.compare(at, strlen(text), text) == 0;
}

bool endsWith(const std::string &s, const char *text)
{
    const size_t n = strlen(text);
    return s.size() >= n && s.compare(s.size() - n, n, text) == 0;
}

std::string trim(const std::string &s)
{
    size_t from = 0, to = s.size();
    while (from < to && isSpace(s[from]))
        from++;
    while (to > from && isSpace(s[to - 1]))
        to--;
    return s.substr(from, to - from);
}

/** Whitespace-separated tokens. */
std::vector<std::string> tokens(const std::string &s)
{
    std::vector<std::string> out;
    size_t at = 0;
    while ((at = skipSpaces(s, at)) < s.size())
    {
        size_t end = at;
        while (end < s.size() && !isSpace(s[end]))
            end++;
        out.push_back(s.substr(at, end - at));
        at = end;
    }
    return out;
}

/** Drops punctuation around a token: "(A3007)" -> "A3007". */
std::string bare(const std::string &token)
{
    size_t from = 0, to = token.size();
    while (from < to && !isWordChar(token[from]))
        from++;
    while (to > from && !isWordChar(token[to - 1]))
        to--;
    return token.substr(from, to - from);
}

/** Runs of letters and digits, i.e. what \bWORD\b would match. */
std::vector<std::string> words(const std::string &s)
{
    std::vector<std::string> out;
    size_t at = 0;
    while (at < s.size())
    {
        if (!isWordChar(s[at]))
        {
            at++;
            continue;
        }
        size_t end = at;
        while (end < s.size() && isWordChar(s[end]))
            end++;
        out.push_back(s.substr(at, end - at));
        at = end;
    }
    return out;
}

template <size_t N> bool isOneOf(const std::string &word, const char *const (&list)[N])
{
    for (size_t i = 0; i < N; i++)
        if (word == list[i])
            return true;
    return false;
}

template <size_t N> bool hasWord(const std::string &text, const char *const (&list)[N])
{
    for (const std::string &word : words(text))
        if (isOneOf(word, list))
            return true;
    return false;
}

/** True when `phrase` occurs in `text` as whole words. */
bool hasPhrase(const std::string &text, const char *phrase)
{
    const size_t n = strlen(phrase);
    for (size_t at = text.find(phrase); at != NONE; at = text.find(phrase, at + 1))
        if (boundary(text, at) && boundary(text, at + n))
            return true;
    return false;
}

void addUnique(std::vector<std::string> &into, const std::vector<std::string> &items)
{
    for (const std::string &item : items)
        if (std::find(into.begin(), into.end(), item) == into.end())
            into.push_back(item);
}

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------

struct JsonReader
{
    const std::string &s;
    size_t at;

    void spaces() { at = skipSpaces(s, at); }

    bool take(char c)
    {
        spaces();
        if (at < s.size() && s[at] == c)
        {
            at++;
            return true;
        }
        return false;
    }

    bool string(std::string &out)
    {
        out.clear();
        if (!take('"'))
            return false;
        while (at < s.size())
        {
            const char c = s[at++];
            if (c == '"')
                return true;
            if (c != '\\')
            {
                out += c;
                continue;
            }
            if (at >= s.size())
                return false;
            const char escaped = s[at++];
            switch (escaped)
            {
            case 'n':
            case 'r':
            case 't':
            case 'b':
            case 'f':
                out += ' ';
                break;
            case 'u':
            {
                if (at + 4 > s.size())
                    return false;
                const long code = strtol(s.substr(at, 4).c_str(), nullptr, 16);
                at += 4;
                out += code >= 0x20 && code < 0x7f ? (char)code : '?';
                break;
            }
            default:
                out += escaped;
            }
        }
        return false;
    }

    /** Skips any value. Nesting is bounded so a hostile response cannot exhaust the stack. */
    bool skip(int depth = 0)
    {
        spaces();
        if (at >= s.size() || depth > 16)
            return false;
        const char c = s[at];
        if (c == '"')
        {
            std::string ignored;
            return string(ignored);
        }
        if (c == '{' || c == '[')
        {
            const char close = c == '{' ? '}' : ']';
            at++;
            if (take(close))
                return true;
            do
            {
                if (c == '{')
                {
                    std::string key;
                    if (!string(key) || !take(':'))
                        return false;
                }
                if (!skip(depth + 1))
                    return false;
            } while (take(','));
            return take(close);
        }
        const size_t start = at;
        while (at < s.size() && s[at] != ',' && s[at] != '}' && s[at] != ']' && !isSpace(s[at]))
            at++;
        return at > start;
    }
};

// ---------------------------------------------------------------------------
// Weather
// ---------------------------------------------------------------------------

/** Reads 2 or 3 digits at `at`; returns the position after them or NONE. */
size_t speedDigits(const std::string &t, size_t at, int &value)
{
    size_t end = at;
    while (end < t.size() && isDigit(t[end]))
        end++;
    if (end - at < 2 || end - at > 3)
        return NONE;
    value = atoi(t.substr(at, end - at).c_str());
    return end;
}

/** dddssKT, dddssGggKT or VRBssKT. */
bool parseWind(const std::string &t, Wind &wind)
{
    if (t.size() < 7)
        return false;
    Wind found;
    found.known = true;
    if (allDigits(t, 0, 3))
    {
        found.hasDirection = true;
        found.direction = atoi(t.substr(0, 3).c_str());
    }
    else if (!startsWith(t, 0, "VRB"))
        return false;
    size_t at = speedDigits(t, 3, found.speed);
    if (at == NONE)
        return false;
    if (at < t.size() && t[at] == 'G')
    {
        found.hasGust = true;
        at = speedDigits(t, at + 1, found.gust);
        if (at == NONE)
            return false;
    }
    if (t.compare(at, std::string::npos, "KT") != 0)
        return false;
    wind = found;
    return true;
}

/** "10SM", "P6SM", "1/2SM"; returns the part before SM. */
bool parseVisibility(const std::string &t, std::string &miles)
{
    if (!endsWith(t, "SM"))
        return false;
    const std::string core = t.substr(0, t.size() - 2);
    size_t at = !core.empty() && (core[0] == 'M' || core[0] == 'P') ? 1 : 0;
    const size_t slash = core.find('/', at);
    const bool whole = allDigits(core, at, slash == NONE ? core.size() : slash);
    const bool fraction = slash == NONE || allDigits(core, slash + 1, core.size());
    if (!whole || !fraction)
        return false;
    miles = core;
    return true;
}

int signedMetar(const std::string &value)
{
    return value[0] == 'M' ? -atoi(value.c_str() + 1) : atoi(value.c_str());
}

/** "04/M06". */
bool parseTemperature(const std::string &t, int &temperature, int &dewpoint)
{
    const size_t slash = t.find('/');
    if (slash == NONE)
        return false;
    const std::string left = t.substr(0, slash), right = t.substr(slash + 1);
    const auto valid = [](const std::string &v) {
        const size_t from = !v.empty() && v[0] == 'M' ? 1 : 0;
        return v.size() - from == 2 && allDigits(v, from, v.size());
    };
    if (!valid(left) || !valid(right))
        return false;
    temperature = signedMetar(left);
    dewpoint = signedMetar(right);
    return true;
}

bool isAltimeter(const std::string &t) { return t.size() == 5 && t[0] == 'A' && allDigits(t, 1, 5); }

bool isPhenomenon(const std::string &token)
{
    static const char *const DESCRIPTORS[] = {"MI", "PR", "BC", "DR", "BL", "SH", "TS", "FZ"};
    static const char *const CODES[] = {"DZ", "RA", "SN", "SG", "IC", "PL", "GR", "GS", "UP", "BR", "FG", "FU",
                                        "VA", "DU", "SA", "HZ", "PY", "PO", "SQ", "FC", "SS", "DS"};
    size_t at = !token.empty() && (token[0] == '-' || token[0] == '+') ? 1 : 0;
    if (startsWith(token, at, "VC"))
        at += 2;
    // Thunderstorms and showers are reported on their own too: "TS", "VCSH".
    if (token.compare(at, std::string::npos, "TS") == 0 || token.compare(at, std::string::npos, "SH") == 0)
        return true;
    const auto codesFrom = [&](size_t from) {
        if (from >= token.size() || (token.size() - from) % 2 != 0)
            return false;
        for (; from < token.size(); from += 2)
            if (!isOneOf(token.substr(from, 2), CODES))
                return false;
        return true;
    };
    if (at + 2 <= token.size() && isOneOf(token.substr(at, 2), DESCRIPTORS) && codesFrom(at + 2))
        return true;
    return codesFrom(at);
}

bool isCloud(const std::string &t)
{
    static const char *const COVER[] = {"FEW", "SCT", "BKN", "OVC", "VV"};
    if (t == "CLR" || t == "SKC")
        return true;
    for (const char *cover : COVER)
    {
        const size_t n = strlen(cover);
        if (!startsWith(t, 0, cover) || !allDigits(t, n, n + 3))
            continue;
        const std::string rest = t.size() > n + 3 ? t.substr(n + 3) : "";
        if (rest.empty() || rest == "CB" || rest == "TCU")
            return true;
    }
    return false;
}

/** True for the METAR sentence of a broadcast. */
bool isWeatherSentence(const std::string &sentence)
{
    for (const std::string &token : tokens(sentence))
    {
        const std::string t = bare(token);
        if (isAltimeter(t))
            return true;
        if (endsWith(t, "KT") && t.size() >= 4 && allDigits(t, t.size() - 4, t.size() - 2))
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Sentences
// ---------------------------------------------------------------------------

/** Splits on a full stop followed by whitespace or the end, so "133.57" survives. */
std::vector<std::string> splitSentences(const std::string &datis)
{
    // "...ADVS" style ellipses count as one sentence break.
    std::string text;
    for (size_t i = 0; i < datis.size();)
    {
        size_t dots = 0;
        while (i + dots < datis.size() && datis[i + dots] == '.')
            dots++;
        if (dots >= 2)
        {
            text += ". ";
            i += dots;
        }
        else
            text += datis[i++];
    }

    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= text.size(); i++)
    {
        const bool atEnd = i == text.size();
        const bool isBreak = !atEnd && text[i] == '.' && (i + 1 == text.size() || isSpace(text[i + 1]));
        if (!atEnd && !isBreak)
            continue;
        const std::string sentence = trim(text.substr(start, i - start));
        if (!sentence.empty())
            out.push_back(sentence);
        start = i + 1;
    }
    return out;
}

/** "RWY", "RWYS", "RUNWAY" or "RUNWAYS" at `at`; returns the position after it or NONE. */
size_t runwayKeyword(const std::string &s, size_t at)
{
    static const char *const KEYWORDS[] = {"RUNWAYS", "RUNWAY", "RWYS", "RWY"};
    for (const char *keyword : KEYWORDS)
        if (startsWith(s, at, keyword))
            return at + strlen(keyword);
    return NONE;
}

/**
 * One or two digits and an optional L/R/C at `at`. With `whole` the designator
 * must end at a word boundary, so "30L" matches but "300" and "4TH" do not.
 */
size_t runwayDesignator(const std::string &s, size_t at, bool whole, std::string &out)
{
    size_t digits = 0;
    while (digits < 2 && at + digits < s.size() && isDigit(s[at + digits]))
        digits++;
    for (; digits >= 1; digits--)
    {
        const size_t end = at + digits;
        const bool sided = end < s.size() && (s[end] == 'L' || s[end] == 'R' || s[end] == 'C');
        if (sided && (!whole || boundary(s, end + 1)))
        {
            out = s.substr(at, digits + 1);
            return end + 1;
        }
        if (!whole || boundary(s, end))
        {
            out = s.substr(at, digits);
            return end;
        }
    }
    return NONE;
}

bool wordAt(const std::string &s, size_t at, const char *word)
{
    return startsWith(s, at, word) && boundary(s, at) && boundary(s, at + strlen(word));
}

std::string normalizeRunway(const std::string &designator)
{
    return !designator.empty() && designator[0] == '0' ? designator.substr(1) : designator;
}

struct RunwayList
{
    size_t start;
    size_t end;
    std::vector<std::string> runways;
};

/** Finds every "RWY 30L", "RWYS 4, 22", "RUNWAY 12L AND 12R", "RWY 30L, RWY 30R" in a sentence. */
std::vector<RunwayList> findRunwayLists(const std::string &s)
{
    std::vector<RunwayList> lists;
    size_t at = 0;
    while (at < s.size())
    {
        const size_t keyword = boundary(s, at) ? runwayKeyword(s, at) : NONE;
        const size_t first = keyword == NONE ? NONE : skipSpaces(s, keyword);
        std::string designator;
        const size_t firstEnd = first == NONE || first == keyword ? NONE : runwayDesignator(s, first, false, designator);
        if (firstEnd == NONE)
        {
            at++;
            continue;
        }

        RunwayList list;
        list.start = at;
        list.end = firstEnd;
        list.runways.push_back(normalizeRunway(designator));
        for (;;)
        {
            // A separator: comma, slash, ampersand, AND, OR, or just a space.
            const size_t gap = skipSpaces(s, list.end);
            size_t next;
            if (gap < s.size() && (s[gap] == ',' || s[gap] == '/' || s[gap] == '&'))
                next = skipSpaces(s, gap + 1);
            else if (wordAt(s, gap, "AND"))
                next = skipSpaces(s, gap + 3);
            else if (wordAt(s, gap, "OR"))
                next = skipSpaces(s, gap + 2);
            else if (gap > list.end)
                next = gap;
            else
                break;
            // The keyword may be repeated: "RWY 30L, RWY 30R".
            const size_t repeated = runwayKeyword(s, next);
            if (repeated != NONE && skipSpaces(s, repeated) > repeated)
                next = skipSpaces(s, repeated);
            const size_t end = runwayDesignator(s, next, true, designator);
            if (end == NONE)
                break;
            list.runways.push_back(normalizeRunway(designator));
            list.end = end;
        }
        lists.push_back(list);
        at = list.end;
    }
    return lists;
}

/** "CLSD", "IS CLOSED", "ARE CLSD" right after a runway list. */
bool closedFollows(const std::string &after)
{
    size_t at = skipSpaces(after, 0);
    for (const char *verb : {"IS", "ARE"})
    {
        const size_t end = at + strlen(verb);
        if (startsWith(after, at, verb) && skipSpaces(after, end) > end)
        {
            at = skipSpaces(after, end);
            break;
        }
    }
    for (const char *word : {"CLSD", "CLOSED"})
        if (startsWith(after, at, word) && boundary(after, at + strlen(word)))
            return true;
    return false;
}

bool isUnserviceable(const std::string &text)
{
    for (const char *phrase : {"OTS", "U/S", "UNUSABLE", "UNAVBL", "UNAVAILABLE", "OUT OF SERVICE", "NOT AVBL"})
        if (hasPhrase(text, phrase))
            return true;
    return false;
}

const char *const DEPARTURE_WORDS[] = {"DEPG", "DEPARTING", "DEP",  "DEPS",   "DEPARTURE",
                                       "DEPARTURES", "TKOF", "TAKEOFF", "TAKEOFFS"};
const char *const ARRIVAL_WORDS[] = {"APCH",    "APCHS",    "APCHES",   "APPROACH", "APPROACHES", "LNDG",
                                     "LDG",     "LANDING",  "ARR",      "ARRS",     "ARRIVAL",    "ARRIVALS",
                                     "ARRIVING", "VISUAL",  "VISUALS",  "ILS",      "RNAV",       "RNP",
                                     "LOC",     "LDA",      "GPS",      "EXPECT",   "EXPT"};
const char *const APPROACH_TYPES[] = {"ILS", "VISUAL", "RNAV", "RNP", "LOC", "LDA", "GPS"};

bool isBoilerplate(const std::string &sentence)
{
    return sentence == "NOTICE TO AIRMEN" || sentence == "NOTICE TO AIR MISSIONS" || sentence == "NOTAM" ||
           sentence == "NOTAMS" || sentence.find("ADV YOU HAVE INFO") != NONE ||
           sentence.find("ADVS YOU HAVE INFO") != NONE || sentence.find("READBACK") != NONE ||
           sentence.find("TRANSPONDER") != NONE;
}

std::string broadcastTime(const AtisEntry &entry)
{
    if (!entry.time.empty())
        return entry.time;
    for (const std::string &token : tokens(entry.datis))
    {
        const std::string t = bare(token);
        if (t.size() == 5 && t[4] == 'Z' && allDigits(t, 0, 4))
            return t.substr(0, 4);
    }
    return "----";
}

} // namespace

bool parseAtisJson(const std::string &json, std::vector<AtisEntry> &out)
{
    out.clear();
    JsonReader reader{json, 0};
    if (!reader.take('['))
        return false;
    if (reader.take(']'))
        return false;
    do
    {
        if (!reader.take('{'))
            return false;
        AtisEntry entry;
        if (!reader.take('}'))
        {
            do
            {
                std::string key;
                if (!reader.string(key) || !reader.take(':'))
                    return false;
                reader.spaces();
                std::string *field = key == "type"    ? &entry.type
                                     : key == "code"  ? &entry.code
                                     : key == "datis" ? &entry.datis
                                     : key == "time"  ? &entry.time
                                                      : nullptr;
                const bool isString = reader.at < json.size() && json[reader.at] == '"';
                if (field != nullptr && isString ? !reader.string(*field) : !reader.skip())
                    return false;
            } while (reader.take(','));
            if (!reader.take('}'))
                return false;
        }
        if (!entry.type.empty() && !entry.datis.empty())
            out.push_back(entry);
    } while (reader.take(','));
    return reader.take(']') && !out.empty();
}

Weather parseWeather(const std::string &text)
{
    Weather weather;
    const std::vector<std::string> all = tokens(text);
    bool inRemarks = false;
    for (size_t i = 0; i < all.size(); i++)
    {
        const std::string &token = all[i];
        const std::string t = bare(token);
        if (t == "RMK")
            inRemarks = true;

        if (!weather.wind.known)
            parseWind(t, weather.wind);
        std::string miles;
        if (weather.visibility.empty() && parseVisibility(t, miles))
        {
            // "1 1/2SM" arrives as two tokens.
            const bool mixed = i > 0 && allDigits(all[i - 1], 0, all[i - 1].size());
            weather.visibility = mixed ? all[i - 1] + " " + miles : miles;
        }
        if (!weather.hasTemperature)
            weather.hasTemperature = parseTemperature(token, weather.temperature, weather.dewpoint);
        if (weather.altimeter.empty() && isAltimeter(t))
            weather.altimeter = t.substr(1, 2) + "." + t.substr(3, 2);

        if (inRemarks)
            continue;
        if (isPhenomenon(token))
            weather.phenomena.push_back(token);
        if (isCloud(t))
            weather.clouds.push_back(t);
    }
    return weather;
}

RunwayUse classifySentence(const std::string &sentence)
{
    RunwayUse out;
    const std::vector<RunwayList> lists = findRunwayLists(sentence);
    bool previousArrival = false, previousDeparture = false;
    for (size_t i = 0; i < lists.size(); i++)
    {
        const RunwayList &list = lists[i];
        const size_t beforeStart = i == 0 ? 0 : lists[i - 1].end;
        const size_t afterEnd = i + 1 < lists.size() ? lists[i + 1].start : sentence.size();
        const std::string before = sentence.substr(beforeStart, list.start - beforeStart);
        const std::string after = sentence.substr(list.end, afterEnd - list.end);

        if (closedFollows(after))
        {
            out.closed.insert(out.closed.end(), list.runways.begin(), list.runways.end());
            continue;
        }
        if (isUnserviceable(after))
            continue;

        // Trailing context stops at the next clause so "LNDG RWY 30L, DEPG RWY 30R" stays split.
        const std::string trailing = after.substr(0, after.find(','));
        bool arrival = hasWord(before, ARRIVAL_WORDS), departure = hasWord(before, DEPARTURE_WORDS);
        if (!arrival && !departure)
        {
            arrival = hasWord(trailing, ARRIVAL_WORDS);
            departure = hasWord(trailing, DEPARTURE_WORDS);
        }
        if (!arrival && !departure)
        {
            arrival = previousArrival;
            departure = previousDeparture;
        }
        previousArrival = arrival;
        previousDeparture = departure;

        if (arrival)
        {
            out.landing.insert(out.landing.end(), list.runways.begin(), list.runways.end());
            for (const std::string &word : words(before + " " + trailing))
                if (isOneOf(word, APPROACH_TYPES))
                    out.approaches.push_back(word);
        }
        if (departure)
            out.departing.insert(out.departing.end(), list.runways.begin(), list.runways.end());
    }
    return out;
}

AtisState parseAtis(std::vector<AtisEntry> entries)
{
    AtisState state;
    bool haveWeather = false;

    // Arrival first so its weather and notes win when both broadcasts exist.
    std::stable_sort(entries.begin(), entries.end(),
                     [](const AtisEntry &a, const AtisEntry &b) { return a.type < b.type; });

    for (const AtisEntry &entry : entries)
    {
        const std::vector<std::string> sentences = splitSentences(entry.datis);
        size_t weatherIndex = NONE;
        for (size_t i = 0; i < sentences.size() && weatherIndex == NONE; i++)
            if (isWeatherSentence(sentences[i]))
                weatherIndex = i;
        if (!haveWeather && weatherIndex != NONE)
        {
            state.weather = parseWeather(sentences[weatherIndex]);
            haveWeather = true;
        }

        for (size_t i = weatherIndex == NONE ? 1 : weatherIndex + 1; i < sentences.size(); i++)
        {
            const std::string &sentence = sentences[i];
            const RunwayUse found = classifySentence(sentence);
            addUnique(state.runways.closed, found.closed);
            addUnique(state.runways.approaches, found.approaches);
            // A departure-only broadcast never announces landing runways.
            if (entry.type != "dep")
                addUnique(state.runways.landing, found.landing);
            addUnique(state.runways.departing, found.departing);

            const bool isRunwayUse = !found.landing.empty() || !found.departing.empty() || !found.closed.empty();
            if (!isRunwayUse && !isBoilerplate(sentence))
                addUnique(state.notes, {sentence});
        }

        Broadcast broadcast;
        broadcast.type = entry.type;
        broadcast.code = entry.code;
        broadcast.time = broadcastTime(entry);
        state.broadcasts.push_back(broadcast);
    }
    return state;
}
