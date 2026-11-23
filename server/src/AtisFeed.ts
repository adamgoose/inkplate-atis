import { Config, Context, Effect, Layer, Option, Ref, Schedule, Schema } from "effect"
import { FetchHttpClient, HttpClient, HttpClientResponse } from "effect/http"
import { AtisEntry, type AtisState, parseAtis } from "./AtisParser.ts"

export class AtisUnavailable extends Schema.TaggedError<AtisUnavailable>()("AtisUnavailable", {
  cause: Schema.Defect()
}) {}

/**
 * Fetches and parses the D-ATIS for the configured airport. Upstream results
 * are cached briefly, and the last good broadcast is kept so a blip at
 * atis.info does not blank the display.
 */
export class AtisFeed extends Context.Service<AtisFeed, {
  readonly current: Effect.Effect<AtisState, AtisUnavailable>
}>()("inkplate-atis/AtisFeed") {
  static readonly layer = Layer.effect(
    AtisFeed,
    Effect.gen(function*() {
      const icao = yield* Config.String("ICAO").pipe(Config.withDefault("KMSP"))
      const baseUrl = yield* Config.String("ATIS_API").pipe(Config.withDefault("https://atis.info/api"))
      const ttl = yield* Config.Duration("ATIS_CACHE").pipe(Config.withDefault("60 seconds"))

      const client = (yield* HttpClient.HttpClient).pipe(
        HttpClient.filterStatusOk,
        HttpClient.retryTransient({ schedule: Schedule.exponential(250), times: 2 })
      )

      const fetch = client.get(`${baseUrl}/${icao}`).pipe(
        Effect.flatMap(HttpClientResponse.schemaBodyJson(Schema.Array(AtisEntry))),
        Effect.timeout("15 seconds"),
        Effect.map(parseAtis),
        Effect.mapError((cause) => new AtisUnavailable({ cause })),
        Effect.withSpan("AtisFeed.fetch", { attributes: { icao } })
      )

      const cached = yield* Effect.cachedWithTTL(fetch, ttl)
      const lastGood = yield* Ref.make(Option.none<AtisState>())

      const current = cached.pipe(
        Effect.tap((state) => Ref.set(lastGood, Option.some(state))),
        Effect.catch((error) =>
          Ref.get(lastGood).pipe(
            Effect.flatMap(Option.match({
              onNone: () => Effect.fail(error),
              onSome: (state) => Effect.logWarning("ATIS fetch failed, serving last good broadcast", error).pipe(Effect.as(state))
            }))
          )
        )
      )

      return AtisFeed.of({ current })
    })
  ).pipe(Layer.provide(FetchHttpClient.layer))
}
