import { describe, expect, test } from "bun:test"
import { classifySentence, parseAtis, parseWeather } from "./AtisParser.ts"

const arr =
  "MSP ARR INFO F 1853Z. 30015G23KT 10SM BKN041 04/M06 A3007 (THREE ZERO ZERO SEVEN) RMK AO2 SLP190 T00391061. SILMULTANEOUS DEPENDENT, ILS RWY 30L APCH IN USE, ILS RWY 30R APCH IN USE. NOTICE TO AIRMEN. RWYS 4, 22 CLSD. CTN, BIRDS NEAR MSP. ALL ACFT READBACK HLDSHRT INSTRUCTIONS AND ASSIGNED ALTITUDES. ALL ACFT TAXI WITH TRANSPONDER AND ALTITUDE ENCODING ON. RWY 12L AND 12R INNER MARKERS OTS. ...ADVS YOU HAVE INFO F."
const dep =
  "MSP DEP INFO S 1853Z. 30015G23KT 10SM BKN041 04/M06 A3007 (THREE ZERO ZERO SEVEN) RMK AO2 SLP190 T00391061. DEPARTING RWY 30L, RWY 30R. NOTICE TO AIRMEN. ALL ACFT CTC METERING ON 133.57 WHEN READY TO PUSHBACK OR TAXI. CTN, BIRDS NEAR MSP. ALL ACFT READBACK HLDSHRT INSTRUCTIONS AND ASSIGNED ALTITUDES. ALL ACFT TAXI WITH TRANSPONDER AND ALTITUDE ENCODING ON. RUNWAY 4/22 CLOSED. ...ADVS YOU HAVE INFO S."

describe("parseAtis", () => {
  const state = parseAtis([
    { airport: "KMSP", type: "dep", code: "S", datis: dep, time: "1853" },
    { airport: "KMSP", type: "arr", code: "F", datis: arr, time: "1853" }
  ])

  test("orders arrival before departure", () => {
    expect(state.broadcasts.map((b) => `${b.type}:${b.code}`)).toEqual(["arr:F", "dep:S"])
  })

  test("finds the runway configuration", () => {
    expect(state.landing).toEqual(["30L", "30R"])
    expect(state.departing).toEqual(["30L", "30R"])
    expect(state.closed).toEqual(["4", "22"])
    expect(state.approaches).toEqual(["ILS"])
  })

  test("keeps operational notes and drops boilerplate", () => {
    expect(state.notes).toEqual([
      "CTN, BIRDS NEAR MSP",
      "RWY 12L AND 12R INNER MARKERS OTS",
      "ALL ACFT CTC METERING ON 133.57 WHEN READY TO PUSHBACK OR TAXI"
    ])
  })

  test("decodes the weather", () => {
    expect(state.weather).toEqual({
      wind: { direction: 300, speed: 15, gust: 23 },
      visibility: "10",
      phenomena: [],
      clouds: ["BKN041"],
      temperature: 4,
      dewpoint: -6,
      altimeter: "30.07"
    })
  })
})

describe("classifySentence", () => {
  test("splits mixed landing and departing clauses", () => {
    const found = classifySentence("LNDG RWY 30L, DEPG RWY 30R")
    expect(found.landing).toEqual(["30L"])
    expect(found.departing).toEqual(["30R"])
  })

  test("reads approach type after the runway", () => {
    const found = classifySentence("RWY 35 VISUAL APCH IN USE")
    expect(found.landing).toEqual(["35"])
    expect(found.approaches).toEqual(["VISUAL"])
  })

  test("handles simultaneous landing and departing", () => {
    const found = classifySentence("LNDG AND DEPG RWYS 12L AND 12R")
    expect(found.landing).toEqual(["12L", "12R"])
    expect(found.departing).toEqual(["12L", "12R"])
  })

  test("does not close a runway mentioned in a taxiway closure", () => {
    expect(classifySentence("TWY A CLSD BTN RWY 30L AND TWY A4").closed).toEqual([])
  })

  test("ignores unserviceable equipment", () => {
    expect(classifySentence("ILS RWY 12L GLIDESLOPE OTS").landing).toEqual([])
  })

  test("strips leading zeros", () => {
    expect(classifySentence("RWY 04/22 CLSD").closed).toEqual(["4", "22"])
  })
})

describe("parseWeather", () => {
  test("variable wind, fractional visibility, precipitation and layers", () => {
    const weather = parseWeather("VRB04KT 1 1/2SM -SN BR FEW008 OVC015 M02/M04 A2992 RMK AO2")
    expect(weather.wind).toEqual({ direction: undefined, speed: 4, gust: undefined })
    expect(weather.visibility).toBe("1 1/2")
    expect(weather.phenomena).toEqual(["-SN", "BR"])
    expect(weather.clouds).toEqual(["FEW008", "OVC015"])
    expect(weather.temperature).toBe(-2)
  })
})
