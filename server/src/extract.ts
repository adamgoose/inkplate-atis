/**
 * Extracts one airport's runway geometry from a Navigraph-style AIRAC SQLite
 * database (the navdata.s3db that SimToolkitPro ships) into data/{ICAO}.json,
 * so the server does not need the 170 MB database at runtime.
 *
 *   bun run extract [ICAO] [path/to/navdata.s3db]
 */
import { BunRuntime, BunServices } from "@effect/platform-bun"
import { Database } from "bun:sqlite"
import { Console, Effect, FileSystem, Schema } from "effect"
import { homedir } from "node:os"
import { fileURLToPath } from "node:url"
import { AirportData, AirportDataError } from "./Airport.ts"

const AirportRow = Schema.Struct({
  airport_identifier: Schema.String,
  airport_name: Schema.String,
  airport_ref_latitude: Schema.Number,
  airport_ref_longitude: Schema.Number,
  elevation: Schema.Number
})

const RunwayRow = Schema.Struct({
  runway_identifier: Schema.String,
  runway_latitude: Schema.Number,
  runway_longitude: Schema.Number,
  runway_true_bearing: Schema.Number,
  runway_magnetic_bearing: Schema.Number,
  runway_length: Schema.Number,
  runway_width: Schema.Number,
  displaced_threshold_distance: Schema.Number
})

const HeaderRow = Schema.Struct({ current_airac: Schema.String })

const openReadonly = (path: string) =>
  Effect.acquireRelease(
    Effect.try({
      try: () => new Database(path, { readonly: true }),
      catch: (cause) => new AirportDataError({ message: `Cannot open navdata at ${path}`, cause })
    }),
    (db) => Effect.sync(() => db.close())
  )

const query = <S extends Schema.Top>(db: Database, schema: S, sql: string, ...params: Array<string>) =>
  Effect.try({
    try: () => db.query(sql).all(...params),
    catch: (cause) => new AirportDataError({ message: `Query failed: ${sql}`, cause })
  }).pipe(
    Effect.flatMap(Schema.decodeUnknownEffect(Schema.Array(schema))),
    Effect.mapError((cause) =>
      cause instanceof AirportDataError ? cause : new AirportDataError({ message: `Unexpected navdata shape: ${sql}`, cause })
    )
  )

const program = Effect.gen(function*() {
  const icao = (process.argv[2] ?? "KMSP").toUpperCase()
  const navdata = process.argv[3] ?? process.env.NAVDATA_PATH ?? `${homedir()}/Documents/SimToolkitPro/navdata.s3db`

  const db = yield* openReadonly(navdata)
  const [airport] = yield* query(db, AirportRow, "select * from tbl_airports where airport_identifier = ?", icao)
  if (airport === undefined) {
    return yield* new AirportDataError({ message: `${icao} is not in ${navdata}` })
  }
  const runways = yield* query(
    db,
    RunwayRow,
    "select * from tbl_runways where airport_identifier = ? order by runway_identifier",
    icao
  )
  const [header] = yield* query(db, HeaderRow, "select current_airac from tbl_header limit 1")

  const data: AirportData = {
    ident: airport.airport_identifier,
    name: airport.airport_name,
    latitude: airport.airport_ref_latitude,
    longitude: airport.airport_ref_longitude,
    elevationFt: airport.elevation,
    airac: header?.current_airac ?? "unknown",
    runways: runways.map((row) => ({
      ident: row.runway_identifier,
      latitude: row.runway_latitude,
      longitude: row.runway_longitude,
      trueBearing: row.runway_true_bearing,
      magneticBearing: row.runway_magnetic_bearing,
      lengthFt: row.runway_length,
      widthFt: row.runway_width,
      displacedThresholdFt: row.displaced_threshold_distance
    }))
  }

  const fs = yield* FileSystem.FileSystem
  const out = fileURLToPath(new URL(`../data/${icao}.json`, import.meta.url))
  yield* fs.writeFileString(out, JSON.stringify(data, null, 2) + "\n")
  yield* Console.log(`${icao}: ${data.runways.length} runway ends, AIRAC ${data.airac} -> ${out}`)
})

program.pipe(Effect.scoped, Effect.provide(BunServices.layer), BunRuntime.runMain)
