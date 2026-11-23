// Tests for the firmware's ATIS parser. Run with `make test`.
#include <iostream>
#include <sstream>

#include "atis.h"

namespace
{

int failures = 0;

typedef std::vector<std::string> Strings;

std::string show(const Strings &items)
{
    std::string out = "[";
    for (size_t i = 0; i < items.size(); i++)
        out += (i == 0 ? "" : ", ") + items[i];
    return out + "]";
}
std::string show(const std::string &value) { return "\"" + value + "\""; }
std::string show(int value) { return std::to_string(value); }
std::string show(bool value) { return value ? "true" : "false"; }
std::string show(const char *value) { return show(std::string(value)); }

template <typename A, typename B> void expectEqual(const char *what, const A &actual, const B &expected)
{
    if (show(actual) == show(expected))
        return;
    failures++;
    std::cerr << "FAIL " << what << "\n  expected " << show(expected) << "\n  actual   " << show(actual) << "\n";
}

#define EXPECT(actual, expected) expectEqual(#actual, actual, expected)

const char *const ARR =
    "MSP ARR INFO F 1853Z. 30015G23KT 10SM BKN041 04/M06 A3007 (THREE ZERO ZERO SEVEN) RMK AO2 SLP190 T00391061. "
    "SILMULTANEOUS DEPENDENT, ILS RWY 30L APCH IN USE, ILS RWY 30R APCH IN USE. NOTICE TO AIRMEN. RWYS 4, 22 CLSD. "
    "CTN, BIRDS NEAR MSP. ALL ACFT READBACK HLDSHRT INSTRUCTIONS AND ASSIGNED ALTITUDES. ALL ACFT TAXI WITH "
    "TRANSPONDER AND ALTITUDE ENCODING ON. RWY 12L AND 12R INNER MARKERS OTS. ...ADVS YOU HAVE INFO F.";
const char *const DEP =
    "MSP DEP INFO S 1853Z. 30015G23KT 10SM BKN041 04/M06 A3007 (THREE ZERO ZERO SEVEN) RMK AO2 SLP190 T00391061. "
    "DEPARTING RWY 30L, RWY 30R. NOTICE TO AIRMEN. ALL ACFT CTC METERING ON 133.57 WHEN READY TO PUSHBACK OR TAXI. "
    "CTN, BIRDS NEAR MSP. ALL ACFT READBACK HLDSHRT INSTRUCTIONS AND ASSIGNED ALTITUDES. ALL ACFT TAXI WITH "
    "TRANSPONDER AND ALTITUDE ENCODING ON. RUNWAY 4/22 CLOSED. ...ADVS YOU HAVE INFO S.";

AtisEntry entry(const char *type, const char *code, const char *datis, const char *time = "")
{
    AtisEntry out;
    out.type = type;
    out.code = code;
    out.datis = datis;
    out.time = time;
    return out;
}

void testParseAtis()
{
    const AtisState state = parseAtis({entry("dep", "S", DEP, "1853"), entry("arr", "F", ARR, "1853")});

    // Arrival is ordered before departure.
    EXPECT(state.broadcasts.size() == 2, true);
    EXPECT(state.broadcasts[0].type + ":" + state.broadcasts[0].code, "arr:F");
    EXPECT(state.broadcasts[1].type + ":" + state.broadcasts[1].code, "dep:S");

    EXPECT(state.runways.landing, (Strings{"30L", "30R"}));
    EXPECT(state.runways.departing, (Strings{"30L", "30R"}));
    EXPECT(state.runways.closed, (Strings{"4", "22"}));
    EXPECT(state.runways.approaches, (Strings{"ILS"}));

    // Operational notes are kept, boilerplate is dropped.
    EXPECT(state.notes, (Strings{"CTN, BIRDS NEAR MSP", "RWY 12L AND 12R INNER MARKERS OTS",
                                 "ALL ACFT CTC METERING ON 133.57 WHEN READY TO PUSHBACK OR TAXI"}));

    const Weather &weather = state.weather;
    EXPECT(weather.wind.known, true);
    EXPECT(weather.wind.hasDirection, true);
    EXPECT(weather.wind.direction, 300);
    EXPECT(weather.wind.speed, 15);
    EXPECT(weather.wind.hasGust, true);
    EXPECT(weather.wind.gust, 23);
    EXPECT(weather.visibility, "10");
    EXPECT(weather.phenomena, Strings{});
    EXPECT(weather.clouds, (Strings{"BKN041"}));
    EXPECT(weather.hasTemperature, true);
    EXPECT(weather.temperature, 4);
    EXPECT(weather.dewpoint, -6);
    EXPECT(weather.altimeter, "30.07");
}

void testBroadcastTime()
{
    EXPECT(parseAtis({entry("arr", "F", ARR)}).broadcasts[0].time, "1853");
    EXPECT(parseAtis({entry("arr", "F", "MSP ARR INFO F. 30015KT 10SM CLR 04/M06 A3007.")}).broadcasts[0].time, "----");
}

void testClassifySentence()
{
    // Mixed landing and departing clauses are split.
    RunwayUse found = classifySentence("LNDG RWY 30L, DEPG RWY 30R");
    EXPECT(found.landing, (Strings{"30L"}));
    EXPECT(found.departing, (Strings{"30R"}));

    // The approach type may follow the runway.
    found = classifySentence("RWY 35 VISUAL APCH IN USE");
    EXPECT(found.landing, (Strings{"35"}));
    EXPECT(found.approaches, (Strings{"VISUAL"}));

    // Simultaneous landing and departing.
    found = classifySentence("LNDG AND DEPG RWYS 12L AND 12R");
    EXPECT(found.landing, (Strings{"12L", "12R"}));
    EXPECT(found.departing, (Strings{"12L", "12R"}));

    // A later list inherits the use of the one before it.
    found = classifySentence("DEPG RWY 17 AND ALSO RWY 12R");
    EXPECT(found.departing, (Strings{"17", "12R"}));

    // A runway mentioned in a taxiway closure is not closed.
    EXPECT(classifySentence("TWY A CLSD BTN RWY 30L AND TWY A4").closed, Strings{});
    EXPECT(classifySentence("RWY 17 IS CLOSED").closed, (Strings{"17"}));
    EXPECT(classifySentence("RWYS 12L & 12R ARE CLSD").closed, (Strings{"12L", "12R"}));

    // Unserviceable equipment is not runway use.
    EXPECT(classifySentence("ILS RWY 12L GLIDESLOPE OTS").landing, Strings{});

    // Leading zeros are stripped.
    EXPECT(classifySentence("RWY 04/22 CLSD").closed, (Strings{"4", "22"}));

    // Numbers that are not runways do not join the list.
    EXPECT(classifySentence("ILS RWY 30L APCH IN USE 300 FT CEILING").landing, (Strings{"30L"}));
}

void testParseWeather()
{
    // Variable wind, fractional visibility, precipitation and layers.
    Weather weather = parseWeather("VRB04KT 1 1/2SM -SN BR FEW008 OVC015 M02/M04 A2992 RMK AO2");
    EXPECT(weather.wind.known, true);
    EXPECT(weather.wind.hasDirection, false);
    EXPECT(weather.wind.speed, 4);
    EXPECT(weather.wind.hasGust, false);
    EXPECT(weather.visibility, "1 1/2");
    EXPECT(weather.phenomena, (Strings{"-SN", "BR"}));
    EXPECT(weather.clouds, (Strings{"FEW008", "OVC015"}));
    EXPECT(weather.temperature, -2);
    EXPECT(weather.dewpoint, -4);

    weather = parseWeather("00000KT P6SM +TSRA VCSH SCT030CB VV002 21/19 A2984");
    EXPECT(weather.wind.speed, 0);
    EXPECT(weather.visibility, "P6");
    EXPECT(weather.phenomena, (Strings{"+TSRA", "VCSH"}));
    EXPECT(weather.clouds, (Strings{"SCT030CB", "VV002"}));

    weather = parseWeather("NO METAR HERE");
    EXPECT(weather.wind.known, false);
    EXPECT(weather.visibility, "");
    EXPECT(weather.hasTemperature, false);
    EXPECT(weather.altimeter, "");
}

void testParseJson()
{
    std::vector<AtisEntry> entries;
    const std::string json =
        "[{\"airport\":\"KMSP\",\"type\":\"arr\",\"code\":\"M\",\"datis\":\"MSP ARR INFO M. \\\"QUOTED\\\" A\\u0026B\","
        "\"time\":\"1753\",\"updatedAt\":\"2026-11-23T18:13:03Z\",\"extra\":{\"nested\":[1,2,{\"a\":null}]}},\n"
        " {\"airport\":\"KMSP\",\"type\":\"dep\",\"code\":\"R\",\"datis\":\"MSP DEP INFO R.\"}]";
    EXPECT(parseAtisJson(json, entries), true);
    EXPECT(entries.size() == 2, true);
    EXPECT(entries[0].type, "arr");
    EXPECT(entries[0].code, "M");
    EXPECT(entries[0].datis, "MSP ARR INFO M. \"QUOTED\" A&B");
    EXPECT(entries[0].time, "1753");
    EXPECT(entries[1].time, "");

    // Not a list of broadcasts.
    EXPECT(parseAtisJson("{\"error\":\"KXYZ is not a D-ATIS airport\"}", entries), false);
    EXPECT(parseAtisJson("[]", entries), false);
    EXPECT(parseAtisJson("[{\"type\":\"arr\",\"datis\":\"TRUNCATED", entries), false);
    EXPECT(parseAtisJson("<html>502 Bad Gateway</html>", entries), false);
    EXPECT(parseAtisJson("", entries), false);
}

} // namespace

int main()
{
    testParseAtis();
    testBroadcastTime();
    testClassifySentence();
    testParseWeather();
    testParseJson();
    if (failures > 0)
    {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all parser tests passed\n";
    return 0;
}
