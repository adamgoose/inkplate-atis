import { BunHttpServer, BunRuntime } from "@effect/platform-bun"
import { Config, Effect, Layer, Option } from "effect"
import { HttpRouter, HttpServerRequest, HttpServerResponse } from "effect/http"
import { Airport } from "./Airport.ts"
import { AtisFeed } from "./AtisFeed.ts"
import { HEIGHT, render, WIDTH } from "./render.ts"

/** Below this the firmware's reported battery voltage draws a warning. */
const LOW_BATTERY_VOLTS = 3.5

const previewPage = `<!doctype html>
<meta charset="utf-8">
<meta http-equiv="refresh" content="60">
<title>Inkplate ATIS preview</title>
<style>
  body { margin: 0; min-height: 100vh; display: grid; place-items: center; background: #2a2a2a; }
  img { width: ${WIDTH * 2}px; max-width: 96vw; image-rendering: pixelated; border: 14px solid #111; border-radius: 6px; }
</style>
<img src="/frame.png" alt="Current Inkplate frame">
`

const Routes = HttpRouter.use(Effect.fn(function*(router) {
  const airport = yield* Airport
  const feed = yield* AtisFeed
  const refreshSeconds = yield* Config.Int("REFRESH_SECONDS").pipe(Config.withDefault(300))

  const frame = Effect.fn("frame")(function*(request: HttpServerRequest.HttpServerRequest) {
    const atis = yield* feed.current.pipe(
      Effect.tapError((error) => Effect.logWarning("No ATIS available", error)),
      Effect.option
    )
    const battery = Option.flatMapNullishOr(HttpServerRequest.toURL(request), (url) => url.searchParams.get("battery"))
    const lowBattery = Option.exists(battery, (volts) => Number(volts) > 0 && Number(volts) < LOW_BATTERY_VOLTS)
    const canvas = render({ airport, atis: Option.getOrUndefined(atis), lowBattery })
    const etag = `"${Bun.hash(canvas.pixels).toString(16)}"`
    return { canvas, etag, atis }
  })

  // Raw frame for the firmware: 4 bits per pixel, see Canvas.toPacked4bpp.
  yield* router.add("GET", "/frame.bin", (request) =>
    Effect.gen(function*() {
      const { canvas, etag } = yield* frame(request)
      const headers = { etag, "x-refresh-seconds": String(refreshSeconds), "cache-control": "no-cache" }
      if (request.headers["if-none-match"] === etag) {
        return HttpServerResponse.empty({ status: 304, headers })
      }
      return HttpServerResponse.uint8Array(canvas.toPacked4bpp(), { contentType: "application/octet-stream", headers })
    }))

  yield* router.add("GET", "/frame.png", (request) =>
    Effect.gen(function*() {
      const { canvas, etag } = yield* frame(request)
      return HttpServerResponse.uint8Array(canvas.toPng(), {
        contentType: "image/png",
        headers: { etag, "cache-control": "no-cache" }
      })
    }))

  yield* router.add("GET", "/state.json", (request) =>
    Effect.gen(function*() {
      const { atis, etag } = yield* frame(request)
      return HttpServerResponse.jsonUnsafe({ airport, atis: Option.getOrNull(atis), etag, size: [WIDTH, HEIGHT] })
    }))

  yield* router.add("GET", "/", HttpServerResponse.html(previewPage))
}))

const ServerLayer = Layer.unwrap(
  Effect.gen(function*() {
    const port = yield* Config.Port("PORT").pipe(Config.withDefault(8080))
    return HttpRouter.serve(Routes).pipe(
      Layer.provide([Airport.layer, AtisFeed.layer]),
      Layer.provide(BunHttpServer.layer({ port, hostname: "0.0.0.0" }))
    )
  })
)

Layer.launch(ServerLayer).pipe(BunRuntime.runMain)
