import { deflateSync } from "node:zlib"
import type { Font } from "./Font.ts"

/**
 * The seven inks of the Inkplate 6COLOR panel. The numeric values are the wire
 * format shared with the firmware (one nibble per pixel), and happen to match
 * the INKPLATE_* constants of the Arduino library.
 */
export const Ink = {
  Black: 0,
  White: 1,
  Green: 2,
  Blue: 3,
  Red: 4,
  Yellow: 5,
  Orange: 6
} as const
export type Ink = (typeof Ink)[keyof typeof Ink]

/** Approximate on-panel appearance, used only for the PNG preview. */
const previewRgb: ReadonlyArray<readonly [number, number, number]> = [
  [0x1c, 0x1c, 0x1c],
  [0xf4, 0xf2, 0xea],
  [0x2e, 0x7d, 0x4f],
  [0x2b, 0x4c, 0x9b],
  [0xb8, 0x33, 0x2e],
  [0xe6, 0xc8, 0x2f],
  [0xd9, 0x7a, 0x2b]
]

export interface Point {
  readonly x: number
  readonly y: number
}

export class Canvas {
  readonly pixels: Uint8Array

  constructor(readonly width: number, readonly height: number, background: Ink = Ink.White) {
    this.pixels = new Uint8Array(width * height).fill(background)
  }

  set(x: number, y: number, ink: Ink): void {
    if (x >= 0 && y >= 0 && x < this.width && y < this.height) {
      this.pixels[y * this.width + x] = ink
    }
  }

  fillRect(x: number, y: number, w: number, h: number, ink: Ink): void {
    const x0 = Math.max(0, Math.round(x))
    const y0 = Math.max(0, Math.round(y))
    const x1 = Math.min(this.width, Math.round(x + w))
    const y1 = Math.min(this.height, Math.round(y + h))
    for (let py = y0; py < y1; py++) {
      this.pixels.fill(ink, py * this.width + x0, py * this.width + Math.max(x0, x1))
    }
  }

  strokeRect(x: number, y: number, w: number, h: number, ink: Ink, thickness = 1): void {
    this.fillRect(x, y, w, thickness, ink)
    this.fillRect(x, y + h - thickness, w, thickness, ink)
    this.fillRect(x, y, thickness, h, ink)
    this.fillRect(x + w - thickness, y, thickness, h, ink)
  }

  /** Even-odd scanline fill, sampling at pixel centres. */
  fillPolygon(points: ReadonlyArray<Point>, ink: Ink): void {
    if (points.length < 3) return
    let minY = Infinity
    let maxY = -Infinity
    for (const p of points) {
      minY = Math.min(minY, p.y)
      maxY = Math.max(maxY, p.y)
    }
    const yStart = Math.max(0, Math.ceil(minY - 0.5))
    const yEnd = Math.min(this.height - 1, Math.ceil(maxY - 0.5) - 1)
    for (let py = yStart; py <= yEnd; py++) {
      const sy = py + 0.5
      const xs: Array<number> = []
      for (let i = 0; i < points.length; i++) {
        const a = points[i]!
        const b = points[(i + 1) % points.length]!
        if ((a.y <= sy && b.y > sy) || (b.y <= sy && a.y > sy)) {
          xs.push(a.x + ((sy - a.y) / (b.y - a.y)) * (b.x - a.x))
        }
      }
      xs.sort((l, r) => l - r)
      for (let i = 0; i + 1 < xs.length; i += 2) {
        const from = Math.max(0, Math.ceil(xs[i]! - 0.5))
        const to = Math.min(this.width, Math.ceil(xs[i + 1]! - 0.5))
        if (to > from) this.pixels.fill(ink, py * this.width + from, py * this.width + to)
      }
    }
  }

  line(a: Point, b: Point, ink: Ink, thickness = 1): void {
    const dx = b.x - a.x
    const dy = b.y - a.y
    const length = Math.hypot(dx, dy)
    if (length === 0) return
    const nx = (-dy / length) * (thickness / 2)
    const ny = (dx / length) * (thickness / 2)
    this.fillPolygon(
      [
        { x: a.x + nx, y: a.y + ny },
        { x: b.x + nx, y: b.y + ny },
        { x: b.x - nx, y: b.y - ny },
        { x: a.x - nx, y: a.y - ny }
      ],
      ink
    )
  }

  strokePolygon(points: ReadonlyArray<Point>, ink: Ink, thickness = 1): void {
    for (let i = 0; i < points.length; i++) {
      this.line(points[i]!, points[(i + 1) % points.length]!, ink, thickness)
    }
  }

  fillCircle(cx: number, cy: number, radius: number, ink: Ink): void {
    for (let py = Math.floor(cy - radius); py <= Math.ceil(cy + radius); py++) {
      for (let px = Math.floor(cx - radius); px <= Math.ceil(cx + radius); px++) {
        if (Math.hypot(px + 0.5 - cx, py + 0.5 - cy) <= radius) this.set(px, py, ink)
      }
    }
  }

  /** Draws `text` with its top-left corner at (x, y). Returns the x after the last glyph. */
  text(font: Font, x: number, y: number, text: string, ink: Ink, scale = 1): number {
    let penX = Math.round(x)
    const top = Math.round(y)
    for (const char of text) {
      const glyph = font.glyphs.get(char.codePointAt(0)!) ?? font.glyphs.get(0x3f)
      if (glyph === undefined) {
        penX += font.cellWidth * scale
        continue
      }
      const glyphTop = top + (font.ascent - glyph.yOffset - glyph.height) * scale
      for (let row = 0; row < glyph.height; row++) {
        const bits = glyph.rows[row] ?? 0
        for (let col = 0; col < glyph.width; col++) {
          if (((bits >>> (glyph.rowBits - 1 - col)) & 1) === 0) continue
          const px = penX + (glyph.xOffset + col) * scale
          const py = glyphTop + row * scale
          if (scale === 1) this.set(px, py, ink)
          else this.fillRect(px, py, scale, scale, ink)
        }
      }
      penX += glyph.advance * scale
    }
    return penX
  }

  /** Row-major, two pixels per byte, first pixel in the high nibble. */
  toPacked4bpp(): Uint8Array {
    const out = new Uint8Array(Math.ceil(this.pixels.length / 2))
    for (let i = 0; i < this.pixels.length; i += 2) {
      out[i >> 1] = ((this.pixels[i]! & 0x0f) << 4) | ((this.pixels[i + 1] ?? Ink.White) & 0x0f)
    }
    return out
  }

  /** Palette PNG for previewing in a browser. */
  toPng(): Uint8Array {
    const raw = new Uint8Array((this.width + 1) * this.height)
    for (let y = 0; y < this.height; y++) {
      raw.set(this.pixels.subarray(y * this.width, (y + 1) * this.width), y * (this.width + 1) + 1)
    }
    const header = new Uint8Array(13)
    const view = new DataView(header.buffer)
    view.setUint32(0, this.width)
    view.setUint32(4, this.height)
    header.set([8, 3, 0, 0, 0], 8)
    return concat([
      Uint8Array.of(0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a),
      pngChunk("IHDR", header),
      pngChunk("PLTE", Uint8Array.from(previewRgb.flat())),
      pngChunk("IDAT", deflateSync(raw)),
      pngChunk("IEND", new Uint8Array(0))
    ])
  }
}

const concat = (parts: ReadonlyArray<Uint8Array>): Uint8Array => {
  const out = new Uint8Array(parts.reduce((n, p) => n + p.length, 0))
  let offset = 0
  for (const part of parts) {
    out.set(part, offset)
    offset += part.length
  }
  return out
}

const pngChunk = (type: string, data: Uint8Array): Uint8Array => {
  const body = concat([new TextEncoder().encode(type), data])
  const out = new Uint8Array(body.length + 8)
  const view = new DataView(out.buffer)
  view.setUint32(0, data.length)
  out.set(body, 4)
  view.setUint32(body.length + 4, Bun.hash.crc32(body))
  return out
}
