import { Config, Context, Effect, FileSystem, Layer, Schema } from "effect"
import { fileURLToPath } from "node:url"
import { normalizeRunway } from "./AtisParser.ts"

export const RunwayEnd = Schema.Struct({
  /** ARINC ident, e.g. "RW30L". */
  ident: Schema.String,
  /** Landing threshold position. */
  latitude: Schema.Number,
  longitude: Schema.Number,
  trueBearing: Schema.Number,
  magneticBearing: Schema.Number,
  lengthFt: Schema.Number,
  widthFt: Schema.Number,
  displacedThresholdFt: Schema.Number
})
export type RunwayEnd = typeof RunwayEnd.Type

export const AirportData = Schema.Struct({
  ident: Schema.String,
  name: Schema.String,
  latitude: Schema.Number,
  longitude: Schema.Number,
  elevationFt: Schema.Number,
  airac: Schema.String,
  runways: Schema.Array(RunwayEnd)
})
export type AirportData = typeof AirportData.Type

export class AirportDataError extends Schema.TaggedError<AirportDataError>()("AirportDataError", {
  message: Schema.String,
  cause: Schema.optional(Schema.Defect())
}) {}

/** A point in feet east (x) and north (y) of the airport reference point. */
export interface LocalPoint {
  readonly x: number
  readonly y: number
}

export interface PavementEnd {
  /** Ident as spoken on the ATIS: "4", "30L". */
  readonly name: string
  /** Physical end of the pavement behind this end's threshold. */
  readonly position: LocalPoint
}

export interface Pavement {
  readonly ends: readonly [PavementEnd, PavementEnd]
  readonly widthFt: number
}

const FEET_PER_DEGREE_LATITUDE = 364_567

const reciprocal = (name: string): string => {
  const match = /^(\d{1,2})([LRC]?)$/.exec(name)
  if (!match) return name
  const number = ((Number(match[1]) + 17) % 36) + 1
  const side = match[2] === "L" ? "R" : match[2] === "R" ? "L" : match[2]
  return `${number}${side}`
}

/**
 * Projects the runway thresholds onto a flat plane around the airport and pairs
 * opposite ends into strips of pavement. Displaced thresholds are moved back to
 * the pavement end so the strip is drawn at its full length.
 */
export const pavements = (airport: AirportData): Array<Pavement> => {
  const feetPerDegreeLongitude = FEET_PER_DEGREE_LATITUDE * Math.cos((airport.latitude * Math.PI) / 180)
  const ends = airport.runways.map((runway) => {
    const bearing = (runway.trueBearing * Math.PI) / 180
    const back = runway.displacedThresholdFt
    return {
      name: normalizeRunway(runway.ident),
      widthFt: runway.widthFt,
      position: {
        x: (runway.longitude - airport.longitude) * feetPerDegreeLongitude - Math.sin(bearing) * back,
        y: (runway.latitude - airport.latitude) * FEET_PER_DEGREE_LATITUDE - Math.cos(bearing) * back
      }
    }
  })

  const out: Array<Pavement> = []
  const used = new Set<string>()
  for (const end of ends) {
    if (used.has(end.name)) continue
    const other = ends.find((candidate) => candidate.name === reciprocal(end.name))
    if (other === undefined) continue
    used.add(end.name).add(other.name)
    out.push({
      ends: [{ name: end.name, position: end.position }, { name: other.name, position: other.position }],
      widthFt: end.widthFt
    })
  }
  return out
}

/** Runway geometry extracted from the AIRAC database by `bun run extract`. */
export class Airport extends Context.Service<Airport, AirportData>()("inkplate-atis/Airport") {
  static readonly layer = Layer.effect(
    Airport,
    Effect.gen(function*() {
      const fs = yield* FileSystem.FileSystem
      const icao = yield* Config.String("ICAO").pipe(Config.withDefault("KMSP"))
      const path = fileURLToPath(new URL(`../data/${icao}.json`, import.meta.url))
      const text = yield* fs.readFileString(path).pipe(
        Effect.mapError((cause) =>
          new AirportDataError({ message: `No runway data at ${path}. Run "bun run extract ${icao}" first.`, cause })
        )
      )
      const data = yield* Schema.decodeUnknownEffect(Schema.fromJsonString(AirportData))(text).pipe(
        Effect.mapError((cause) => new AirportDataError({ message: `Invalid runway data in ${path}`, cause }))
      )
      return Airport.of(data)
    })
  )
}
