import { Schema } from "effect"

/** One element of the array returned by https://atis.info/api/{ICAO}. */
export const AtisEntry = Schema.Struct({
  airport: Schema.String,
  type: Schema.Literals(["arr", "dep", "combined"]),
  code: Schema.String,
  datis: Schema.String,
  time: Schema.optional(Schema.String),
  updatedAt: Schema.optional(Schema.String)
})
export type AtisEntry = typeof AtisEntry.Type

export interface Wind {
  /** Degrees the wind is blowing from, or undefined when variable. */
  readonly direction: number | undefined
  readonly speed: number
  readonly gust: number | undefined
}

export interface Weather {
  readonly wind: Wind | undefined
  readonly visibility: string | undefined
  readonly phenomena: ReadonlyArray<string>
  readonly clouds: ReadonlyArray<string>
  readonly temperature: number | undefined
  readonly dewpoint: number | undefined
  readonly altimeter: string | undefined
}

export interface Broadcast {
  readonly type: AtisEntry["type"]
  readonly code: string
  readonly time: string
}

export interface AtisState {
  readonly broadcasts: ReadonlyArray<Broadcast>
  readonly weather: Weather
  /** Runway idents without leading zeros, e.g. "4", "30L". */
  readonly landing: ReadonlyArray<string>
  readonly departing: ReadonlyArray<string>
  readonly closed: ReadonlyArray<string>
  readonly approaches: ReadonlyArray<string>
  readonly notes: ReadonlyArray<string>
}

const RUNWAY_LIST =
  /\b(?:RWYS?|RUNWAYS?)\s+(\d{1,2}[LRC]?(?:(?:\s*(?:,|\/|&|\bAND\b|\bOR\b)\s*|\s+)(?:(?:RWYS?|RUNWAYS?)\s+)?\d{1,2}[LRC]?\b)*)/g
const DEPARTURE_WORDS = /\b(?:DEPG|DEPARTING|DEPS?|DEPARTURES?|TKOF|TAKEOFFS?)\b/
const ARRIVAL_WORDS =
  /\b(?:APCHS?|APCHES|APPROACH(?:ES)?|LNDG|LDG|LANDING|ARRS?|ARRIVALS?|ARRIVING|VISUALS?|ILS|RNAV|RNP|LOC|LDA|GPS|EXPECT|EXPT)\b/
const APPROACH_TYPES = /\b(ILS|VISUAL|RNAV|RNP|LOC|LDA|GPS)\b/g
const CLOSED_AFTER = /^\s*(?:IS\s+|ARE\s+)?(?:CLSD|CLOSED)\b/
const UNSERVICEABLE = /\b(?:OTS|U\/S|UNUSABLE|UNAVBL|UNAVAILABLE|OUT OF SERVICE|NOT AVBL)\b/
const BOILERPLATE = [
  /^NOTICE TO AIR(?:MEN| MISSIONS)$/,
  /^NOTAMS?$/,
  /ADVS? YOU HAVE INFO/,
  /READBACK/,
  /TRANSPONDER/
]
const PHENOMENON =
  /^[-+]?(?:VC)?(?:MI|PR|BC|DR|BL|SH|TS|FZ)?(?:DZ|RA|SN|SG|IC|PL|GR|GS|UP|BR|FG|FU|VA|DU|SA|HZ|PY|PO|SQ|FC|SS|DS)+$|^TS$/

export const normalizeRunway = (ident: string): string => ident.replace(/^RW/, "").replace(/^0/, "")

const splitSentences = (datis: string): Array<string> =>
  datis
    .replace(/\.{2,}/g, ". ")
    .split(/\.(?:\s+|$)/)
    .map((s) => s.trim())
    .filter((s) => s.length > 0)

const signed = (metar: string): number => (metar.startsWith("M") ? -Number(metar.slice(1)) : Number(metar))

export const parseWeather = (text: string): Weather => {
  const wind = /\b(\d{3}|VRB)(\d{2,3})(?:G(\d{2,3}))?KT\b/.exec(text)
  const visibility = /\b((?:\d+ )?(?:M|P)?\d+(?:\/\d+)?)SM\b/.exec(text)
  const temperature = /(?:^|\s)(M?\d{2})\/(M?\d{2})(?:\s|$)/.exec(text)
  const altimeter = /\bA(\d{2})(\d{2})\b/.exec(text)
  const body = text.split(/\bRMK\b/)[0] ?? text
  return {
    wind: wind
      ? {
        direction: wind[1] === "VRB" ? undefined : Number(wind[1]),
        speed: Number(wind[2]),
        gust: wind[3] === undefined ? undefined : Number(wind[3])
      }
      : undefined,
    visibility: visibility?.[1],
    phenomena: body.split(/\s+/).filter((token) => PHENOMENON.test(token)),
    clouds: body.match(/\b(?:(?:FEW|SCT|BKN|OVC|VV)\d{3}(?:CB|TCU)?|CLR|SKC)\b/g) ?? [],
    temperature: temperature ? signed(temperature[1]!) : undefined,
    dewpoint: temperature ? signed(temperature[2]!) : undefined,
    altimeter: altimeter ? `${altimeter[1]}.${altimeter[2]}` : undefined
  }
}

interface SentenceRunways {
  readonly landing: Array<string>
  readonly departing: Array<string>
  readonly closed: Array<string>
  readonly approaches: Array<string>
}

/**
 * Classifies every runway list in a sentence by the words around it. A list
 * directly followed by CLSD/CLOSED is a closure; otherwise the words leading up
 * to it decide (DEPG ..., ILS ..., LNDG ...), then the words after it
 * (... APCH IN USE), and finally the previous list in the same sentence
 * ("DEPARTING RWY 30L, RWY 30R").
 */
export const classifySentence = (sentence: string): SentenceRunways => {
  const out: SentenceRunways = { landing: [], departing: [], closed: [], approaches: [] }
  const matches = [...sentence.matchAll(RUNWAY_LIST)]
  let previous: { arrival: boolean; departure: boolean } = { arrival: false, departure: false }
  matches.forEach((match, i) => {
    const start = match.index
    const end = start + match[0].length
    const before = sentence.slice(i === 0 ? 0 : matches[i - 1]!.index + matches[i - 1]![0].length, start)
    const after = sentence.slice(end, matches[i + 1]?.index ?? sentence.length)
    const runways = (match[1]!.match(/\d{1,2}[LRC]?/g) ?? []).map(normalizeRunway)

    if (CLOSED_AFTER.test(after)) {
      out.closed.push(...runways)
      return
    }
    if (UNSERVICEABLE.test(after)) return

    let use = { arrival: ARRIVAL_WORDS.test(before), departure: DEPARTURE_WORDS.test(before) }
    if (!use.arrival && !use.departure) {
      // Trailing context stops at the next clause so "LNDG RWY 30L, DEPG RWY 30R" stays split.
      const trailing = after.split(",")[0] ?? ""
      use = { arrival: ARRIVAL_WORDS.test(trailing), departure: DEPARTURE_WORDS.test(trailing) }
    }
    if (!use.arrival && !use.departure) use = previous
    previous = use

    if (use.arrival) {
      out.landing.push(...runways)
      out.approaches.push(...(`${before} ${after.split(",")[0] ?? ""}`.match(APPROACH_TYPES) ?? []))
    }
    if (use.departure) out.departing.push(...runways)
  })
  return out
}

const unique = <A>(items: ReadonlyArray<A>): Array<A> => [...new Set(items)]

export const parseAtis = (entries: ReadonlyArray<AtisEntry>): AtisState => {
  const landing: Array<string> = []
  const departing: Array<string> = []
  const closed: Array<string> = []
  const approaches: Array<string> = []
  const notes: Array<string> = []
  let weather: Weather | undefined

  // Arrival first so its weather and notes win when both broadcasts exist.
  const ordered = [...entries].sort((a, b) => a.type.localeCompare(b.type))

  for (const entry of ordered) {
    const sentences = splitSentences(entry.datis)
    const weatherIndex = sentences.findIndex((s) => /\bA\d{4}\b/.test(s) || /\d{2,3}KT\b/.test(s))
    if (weather === undefined && weatherIndex >= 0) weather = parseWeather(sentences[weatherIndex]!)

    for (const sentence of sentences.slice(weatherIndex >= 0 ? weatherIndex + 1 : 1)) {
      const found = classifySentence(sentence)
      closed.push(...found.closed)
      approaches.push(...found.approaches)
      // A departure-only broadcast never announces landing runways and vice versa.
      if (entry.type !== "dep") landing.push(...found.landing)
      if (entry.type !== "arr") departing.push(...found.departing)
      if (entry.type === "arr" && found.departing.length > 0) departing.push(...found.departing)

      const isRunwayUse = found.landing.length + found.departing.length + found.closed.length > 0
      if (!isRunwayUse && !BOILERPLATE.some((pattern) => pattern.test(sentence))) notes.push(sentence)
    }
  }

  return {
    broadcasts: ordered.map((entry) => ({
      type: entry.type,
      code: entry.code,
      time: entry.time ?? /\b(\d{4})Z\b/.exec(entry.datis)?.[1] ?? "----"
    })),
    weather: weather ?? parseWeather(""),
    landing: unique(landing),
    departing: unique(departing),
    closed: unique(closed),
    approaches: unique(approaches),
    notes: unique(notes)
  }
}
