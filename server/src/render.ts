import { type AirportData, type LocalPoint, type Pavement, pavements } from "./Airport.ts"
import type { AtisState, Broadcast, Wind } from "./AtisParser.ts"
import { Canvas, Ink, type Point } from "./Canvas.ts"
import { type Font, fonts, textWidth } from "./Font.ts"

export const WIDTH = 600
export const HEIGHT = 448

export interface Screen {
  readonly airport: AirportData
  /** Undefined when no broadcast has ever been received. */
  readonly atis: AtisState | undefined
  readonly lowBattery: boolean
}

const HEADER_HEIGHT = 40
const DIAGRAM = { x: 0, y: HEADER_HEIGHT, width: 300, height: 304 }
const PANEL = { x: 306, y: HEADER_HEIGHT + 8, width: 288 }
const NOTES_TOP = 350

export const render = (screen: Screen): Canvas => {
  const canvas = new Canvas(WIDTH, HEIGHT)
  drawHeader(canvas, screen)
  drawDiagram(canvas, screen)
  canvas.fillRect(DIAGRAM.width, HEADER_HEIGHT, 1, NOTES_TOP - 6 - HEADER_HEIGHT, Ink.Black)
  if (screen.atis === undefined) {
    canvas.text(fonts.medium, PANEL.x, PANEL.y + 40, "ATIS UNAVAILABLE", Ink.Red)
    canvas.text(fonts.small, PANEL.x, PANEL.y + 72, "Waiting for atis.info", Ink.Black)
  } else {
    drawBroadcasts(canvas, screen.atis)
    drawWeather(canvas, screen.atis)
    drawRunwayUse(canvas, screen.atis)
    drawNotes(canvas, screen.atis)
  }
  return canvas
}

// ---------------------------------------------------------------------------
// Header
// ---------------------------------------------------------------------------

const drawHeader = (canvas: Canvas, { airport, atis, lowBattery }: Screen): void => {
  canvas.fillRect(0, 0, WIDTH, HEADER_HEIGHT, Ink.Black)
  const afterIdent = canvas.text(fonts.large, 10, 4, airport.ident, Ink.White)

  const time = atis?.broadcasts[0]?.time
  const right = time === undefined ? "" : `${time}Z`
  const rightX = WIDTH - 10 - textWidth(fonts.large, right)
  canvas.text(fonts.large, rightX, 4, right, Ink.White)

  if (lowBattery) {
    const label = "LOW BATT"
    const x = rightX - 16 - textWidth(fonts.small, label) - 8
    canvas.fillRect(x, 10, textWidth(fonts.small, label) + 8, 20, Ink.Red)
    canvas.text(fonts.small, x + 4, 12, label, Ink.White)
  }

  const nameRoom = Math.floor((rightX - afterIdent - 28 - (lowBattery ? 90 : 0)) / fonts.small.cellWidth)
  canvas.text(fonts.small, afterIdent + 12, 13, airport.name.replace(/[-/\s]+$/, "").slice(0, nameRoom), Ink.White)
}

// ---------------------------------------------------------------------------
// Runway diagram
// ---------------------------------------------------------------------------

const DIAGRAM_MARGIN = 52
const ARROW_GAP = 5
const ARROW_LENGTH = 24

const drawDiagram = (canvas: Canvas, { airport, atis }: Screen): void => {
  const strips = pavements(airport)
  if (strips.length === 0) return

  const all = strips.flatMap((strip) => strip.ends.map((end) => end.position))
  const minX = Math.min(...all.map((p) => p.x))
  const maxX = Math.max(...all.map((p) => p.x))
  const minY = Math.min(...all.map((p) => p.y))
  const maxY = Math.max(...all.map((p) => p.y))
  const scale = Math.min(
    (DIAGRAM.width - 2 * DIAGRAM_MARGIN) / Math.max(1, maxX - minX),
    (DIAGRAM.height - 2 * DIAGRAM_MARGIN) / Math.max(1, maxY - minY)
  )
  const centreX = DIAGRAM.x + DIAGRAM.width / 2
  const centreY = DIAGRAM.y + DIAGRAM.height / 2
  // North up: screen y grows downwards.
  const project = (p: LocalPoint): Point => ({
    x: centreX + (p.x - (minX + maxX) / 2) * scale,
    y: centreY - (p.y - (minY + maxY) / 2) * scale
  })

  const landing = new Set(atis?.landing)
  const departing = new Set(atis?.departing)
  const closed = new Set(atis?.closed)
  const isClosed = (strip: Pavement) => strip.ends.some((end) => closed.has(end.name))
  const isActive = (strip: Pavement) => strip.ends.some((end) => landing.has(end.name) || departing.has(end.name))

  // Idle strips first so the runways in use are drawn over their intersections.
  const rank = (strip: Pavement) => (isClosed(strip) ? 1 : isActive(strip) ? 2 : 0)
  for (const strip of [...strips].sort((a, b) => rank(a) - rank(b))) {
    const a = project(strip.ends[0].position)
    const b = project(strip.ends[1].position)
    const width = Math.max(7, strip.widthFt * scale)
    if (isClosed(strip)) {
      canvas.line(a, b, Ink.Red, width)
      canvas.line(a, b, Ink.White, width - 4)
      drawClosedMarks(canvas, a, b, width)
    } else if (isActive(strip)) {
      canvas.line(a, b, Ink.Black, width)
    } else {
      canvas.line(a, b, Ink.Black, width)
      canvas.line(shift(a, b, 1), shift(b, a, 1), Ink.White, width - 2)
    }
  }

  // The north mark and AIRAC caption are fixed; labels must stay clear of them.
  const occupied: Array<Box> = [
    { x: DIAGRAM.x + DIAGRAM.width - 30, y: DIAGRAM.y + 8, width: 24, height: 50 },
    { x: DIAGRAM.x + 4, y: DIAGRAM.y + DIAGRAM.height - 18, width: 70, height: 16 }
  ]
  const labels: Array<Label> = []
  for (const strip of strips) {
    for (const [end, other] of [[strip.ends[0], strip.ends[1]], [strip.ends[1], strip.ends[0]]] as const) {
      const here = project(end.position)
      const there = project(other.position)
      const length = Math.hypot(there.x - here.x, there.y - here.y)
      // Unit vector pointing away from the pavement at this end.
      const out = { x: (here.x - there.x) / length, y: (here.y - there.y) / length }
      const along = (distance: number): Point => ({ x: here.x + out.x * distance, y: here.y + out.y * distance })

      const lands = landing.has(end.name) && !closed.has(end.name)
      const departsOver = departing.has(other.name) && !closed.has(other.name)
      if (lands) drawArrow(canvas, along(ARROW_GAP + ARROW_LENGTH), along(ARROW_GAP), Ink.Green)
      else if (departsOver) drawArrow(canvas, along(ARROW_GAP), along(ARROW_GAP + ARROW_LENGTH), Ink.Blue)
      if (lands || departsOver) {
        const from = along(ARROW_GAP)
        const to = along(ARROW_GAP + ARROW_LENGTH)
        occupied.push({
          x: Math.min(from.x, to.x) - 6,
          y: Math.min(from.y, to.y) - 6,
          width: Math.abs(from.x - to.x) + 12,
          height: Math.abs(from.y - to.y) + 12
        })
      }

      const boxWidth = textWidth(fonts.small, end.name) + 6
      const boxHeight = fonts.small.cellHeight + 2
      const clearance = (lands || departsOver ? ARROW_GAP + ARROW_LENGTH : 0) + 5 +
        (Math.abs(out.x) * boxWidth + Math.abs(out.y) * boxHeight) / 2
      const inUse = landing.has(end.name) || departing.has(end.name)
      labels.push({
        name: end.name,
        anchor: along(clearance),
        out,
        side: { x: -out.y, y: out.x },
        width: boxWidth,
        height: boxHeight,
        style: closed.has(end.name) ? "closed" : inUse ? "active" : "idle"
      })
    }
  }

  // Labels of runways in use keep their natural spot; the rest step sideways out of the way.
  const priority = { active: 0, closed: 1, idle: 2 } as const
  for (const label of labels.sort((a, b) => priority[a.style] - priority[b.style])) {
    const candidates = NUDGES.map(([sideways, outwards]): Box => ({
      x: clamp(
        Math.round(label.anchor.x + label.side.x * sideways + label.out.x * outwards - label.width / 2),
        DIAGRAM.x + 2,
        DIAGRAM.x + DIAGRAM.width - label.width - 3
      ),
      y: clamp(
        Math.round(label.anchor.y + label.side.y * sideways + label.out.y * outwards - label.height / 2),
        DIAGRAM.y + 2,
        DIAGRAM.y + DIAGRAM.height - label.height - 2
      ),
      width: label.width,
      height: label.height
    }))
    const box = candidates.find((candidate) => !occupied.some((other) => overlaps(candidate, other))) ?? candidates[0]!
    occupied.push(box)
    if (label.style === "active") canvas.fillRect(box.x, box.y, box.width, box.height, Ink.Black)
    const ink = label.style === "active" ? Ink.White : label.style === "closed" ? Ink.Red : Ink.Black
    canvas.text(fonts.small, box.x + 3, box.y + 1, label.name, ink)
  }

  canvas.text(fonts.tiny, DIAGRAM.x + 6, DIAGRAM.y + DIAGRAM.height - 16, `AIRAC ${airport.airac}`, Ink.Black)
  drawNorthMark(canvas, DIAGRAM.x + DIAGRAM.width - 18, DIAGRAM.y + 24)
}

interface Box {
  readonly x: number
  readonly y: number
  readonly width: number
  readonly height: number
}

interface Label {
  readonly name: string
  readonly anchor: Point
  /** Unit vectors away from and across the runway, used to step out of collisions. */
  readonly out: Point
  readonly side: Point
  readonly width: number
  readonly height: number
  readonly style: "active" | "closed" | "idle"
}

/** Label offsets (sideways, outwards) in pixels, nearest first. */
const NUDGES: ReadonlyArray<readonly [number, number]> = [0, 12, 24, 36]
  .flatMap((outwards) => [0, 14, -14, 28, -28, 42, -42].map((sideways) => [sideways, outwards] as const))
  .sort((a, b) => Math.hypot(...a) - Math.hypot(...b))

const clamp = (value: number, min: number, max: number): number => Math.max(min, Math.min(max, value))

/** True when the boxes come within `gap` pixels of each other. */
const overlaps = (a: Box, b: Box, gap = 3): boolean =>
  a.x < b.x + b.width + gap && b.x < a.x + a.width + gap && a.y < b.y + b.height + gap && b.y < a.y + a.height + gap

/** Moves `from` a few pixels towards `to`. */
const shift = (from: Point, to: Point, distance: number): Point => {
  const length = Math.hypot(to.x - from.x, to.y - from.y)
  return { x: from.x + ((to.x - from.x) / length) * distance, y: from.y + ((to.y - from.y) / length) * distance }
}

const drawClosedMarks = (canvas: Canvas, a: Point, b: Point, width: number): void => {
  const size = Math.max(5, width / 2 + 3)
  for (const t of [0.2, 0.5, 0.8]) {
    const x = a.x + (b.x - a.x) * t
    const y = a.y + (b.y - a.y) * t
    canvas.line({ x: x - size, y: y - size }, { x: x + size, y: y + size }, Ink.Red, 3)
    canvas.line({ x: x - size, y: y + size }, { x: x + size, y: y - size }, Ink.Red, 3)
  }
}

const drawArrow = (canvas: Canvas, tail: Point, tip: Point, ink: Ink, shaft = 5, head = 15, headLength = 12): void => {
  const length = Math.hypot(tip.x - tail.x, tip.y - tail.y)
  const ux = (tip.x - tail.x) / length
  const uy = (tip.y - tail.y) / length
  const at = (distance: number, side: number): Point => ({
    x: tail.x + ux * distance - uy * side,
    y: tail.y + uy * distance + ux * side
  })
  const neck = length - headLength
  canvas.fillPolygon(
    [at(0, shaft / 2), at(neck, shaft / 2), at(neck, head / 2), at(length, 0), at(neck, -head / 2), at(neck, -shaft / 2), at(0, -shaft / 2)],
    ink
  )
}

const drawNorthMark = (canvas: Canvas, x: number, y: number): void => {
  drawArrow(canvas, { x, y: y + 12 }, { x, y: y - 12 }, Ink.Black, 3, 11, 10)
  canvas.text(fonts.small, x - 4, y + 14, "N", Ink.Black)
}

// ---------------------------------------------------------------------------
// Right-hand panel
// ---------------------------------------------------------------------------

const BOX = { width: 92, height: 92, gap: 6 }

const broadcastStyle = (type: Broadcast["type"]): { label: string; ink: Ink } =>
  type === "arr" ? { label: "ARR", ink: Ink.Green } : type === "dep" ? { label: "DEP", ink: Ink.Blue } : { label: "ATIS", ink: Ink.Black }

const drawBroadcasts = (canvas: Canvas, atis: AtisState): void => {
  atis.broadcasts.slice(0, 2).forEach((broadcast, i) => {
    const x = PANEL.x + i * (BOX.width + BOX.gap)
    const { ink, label } = broadcastStyle(broadcast.type)
    canvas.strokeRect(x, PANEL.y, BOX.width, BOX.height, ink, 2)
    canvas.fillRect(x, PANEL.y, BOX.width, 20, ink)
    canvas.text(fonts.small, x + (BOX.width - textWidth(fonts.small, label)) / 2, PANEL.y + 2, label, Ink.White)
    const code = broadcast.code.slice(0, 1)
    canvas.text(fonts.huge, x + (BOX.width - fonts.huge.cellWidth) / 2, PANEL.y + 24, code, Ink.Black)
  })
  drawWindRose(canvas, PANEL.x + 2 * (BOX.width + BOX.gap) + BOX.width / 2, PANEL.y + BOX.height / 2, atis.weather.wind)
}

const drawWindRose = (canvas: Canvas, cx: number, cy: number, wind: Wind | undefined): void => {
  const radius = 44
  canvas.fillCircle(cx, cy, radius, Ink.Black)
  canvas.fillCircle(cx, cy, radius - 2, Ink.White)
  for (let degrees = 0; degrees < 360; degrees += 30) {
    const radians = (degrees * Math.PI) / 180
    const inner = degrees % 90 === 0 ? radius - 9 : radius - 6
    canvas.line(
      { x: cx + Math.sin(radians) * inner, y: cy - Math.cos(radians) * inner },
      { x: cx + Math.sin(radians) * (radius - 1), y: cy - Math.cos(radians) * (radius - 1) },
      Ink.Black,
      degrees % 90 === 0 ? 3 : 1
    )
  }
  if (wind === undefined) return
  if (wind.speed === 0) {
    canvas.text(fonts.small, cx - 16, cy - 8, "CALM", Ink.Black)
    return
  }
  if (wind.direction === undefined) {
    canvas.text(fonts.small, cx - 12, cy - 8, "VRB", Ink.Black)
    return
  }
  // The arrow flies with the wind: tail at the direction it blows from.
  const radians = (wind.direction * Math.PI) / 180
  const reach = radius - 12
  const from = { x: cx + Math.sin(radians) * reach, y: cy - Math.cos(radians) * reach }
  const to = { x: cx - Math.sin(radians) * reach, y: cy + Math.cos(radians) * reach }
  drawArrow(canvas, from, to, Ink.Black, 9, 23, 18)
  drawArrow(canvas, shift(from, to, 2), shift(to, from, 3), Ink.Orange, 5, 17, 13)
}

const ROW = { top: PANEL.y + BOX.height + 8, pitch: 25, valueX: PANEL.x + 48 }

const formatWind = (wind: Wind | undefined): string => {
  if (wind === undefined) return "--"
  if (wind.speed === 0) return "CALM"
  const direction = wind.direction === undefined ? "VRB" : String(wind.direction).padStart(3, "0")
  return `${direction}/${wind.speed}${wind.gust === undefined ? "" : `G${wind.gust}`}KT`
}

const formatTemperature = (value: number | undefined): string => (value === undefined ? "--" : String(value))

/** Picks the largest font in which `text` fits `width`, truncating as a last resort. */
const fit = (text: string, width: number, candidates: ReadonlyArray<Font>): { font: Font; text: string } => {
  for (const font of candidates) {
    if (textWidth(font, text) <= width) return { font, text }
  }
  const font = candidates[candidates.length - 1]!
  return { font, text: text.slice(0, Math.floor(width / font.cellWidth)) }
}

const drawWeather = (canvas: Canvas, { weather }: AtisState): void => {
  const visibility = [weather.visibility === undefined ? "--" : `${weather.visibility}SM`, ...weather.phenomena].join(" ")
  const rows: ReadonlyArray<readonly [string, string]> = [
    ["WIND", formatWind(weather.wind)],
    ["VIS", visibility],
    ["SKY", weather.clouds.length === 0 ? "--" : weather.clouds.join(" ")],
    ["TEMP", `${formatTemperature(weather.temperature)}/${formatTemperature(weather.dewpoint)} C`],
    ["ALTM", weather.altimeter ?? "--"]
  ]
  rows.forEach(([label, value], i) => {
    const y = ROW.top + i * ROW.pitch
    canvas.text(fonts.small, PANEL.x, y + 6, label, Ink.Black)
    const fitted = fit(value, PANEL.x + PANEL.width - ROW.valueX, [fonts.medium, fonts.small, fonts.tiny])
    canvas.text(fitted.font, ROW.valueX, y + (fonts.medium.cellHeight - fitted.font.cellHeight) / 2, fitted.text, Ink.Black)
  })
}

const USE = { top: ROW.top + 5 * ROW.pitch + 4, pitch: 20, tagWidth: 42 }

const drawRunwayUse = (canvas: Canvas, atis: AtisState): void => {
  const approaches = atis.approaches.length === 0 ? "" : `  ${atis.approaches.join("/")}`
  const rows: ReadonlyArray<readonly [string, Ink, string]> = [
    ["LDG", Ink.Green, atis.landing.length === 0 ? "--" : atis.landing.join(" ") + approaches],
    ["DEP", Ink.Blue, atis.departing.length === 0 ? "--" : atis.departing.join(" ")],
    ...(atis.closed.length === 0 ? [] : [["CLSD", Ink.Red, atis.closed.join(" ")] as const])
  ]
  rows.forEach(([tag, ink, value], i) => {
    const y = USE.top + i * USE.pitch
    canvas.fillRect(PANEL.x, y, USE.tagWidth, 18, ink)
    canvas.text(fonts.small, PANEL.x + (USE.tagWidth - textWidth(fonts.small, tag)) / 2, y + 1, tag, Ink.White)
    const fitted = fit(value, PANEL.width - USE.tagWidth - 8, [fonts.small, fonts.tiny])
    canvas.text(fitted.font, PANEL.x + USE.tagWidth + 8, y + 1 + (16 - fitted.font.cellHeight) / 2, fitted.text, Ink.Black)
  })
}

// ---------------------------------------------------------------------------
// Notes
// ---------------------------------------------------------------------------

export const wrap = (text: string, columns: number): Array<string> => {
  const lines: Array<string> = []
  let line = ""
  for (const word of text.split(/\s+/)) {
    if (line.length > 0 && line.length + 1 + word.length > columns) {
      lines.push(line)
      line = word
    } else {
      line = line.length === 0 ? word : `${line} ${word}`
    }
  }
  if (line.length > 0) lines.push(line)
  return lines
}

const drawNotes = (canvas: Canvas, atis: AtisState): void => {
  const margin = 8
  canvas.fillRect(0, NOTES_TOP - 6, WIDTH, 2, Ink.Black)
  if (atis.notes.length === 0) return
  const text = atis.notes.map((note) => `${note}.`).join(" ")
  const room = HEIGHT - NOTES_TOP - 2

  // Prefer the larger font; drop to the small one before truncating.
  for (const font of [fonts.small, fonts.tiny]) {
    const columns = Math.floor((WIDTH - 2 * margin) / font.cellWidth)
    const maxLines = Math.floor(room / font.cellHeight)
    const lines = wrap(text, columns)
    const isLast = font === fonts.tiny
    if (lines.length > maxLines && !isLast) continue
    const shown = lines.slice(0, maxLines)
    if (lines.length > maxLines) {
      shown[maxLines - 1] = `${shown[maxLines - 1]!.slice(0, columns - 4)} ...`
    }
    shown.forEach((line, i) => canvas.text(font, margin, NOTES_TOP + i * font.cellHeight, line, Ink.Black))
    return
  }
}
