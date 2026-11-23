import bdf6x12 from "../fonts/spleen-6x12.bdf" with { type: "text" }
import bdf8x16 from "../fonts/spleen-8x16.bdf" with { type: "text" }
import bdf12x24 from "../fonts/spleen-12x24.bdf" with { type: "text" }
import bdf16x32 from "../fonts/spleen-16x32.bdf" with { type: "text" }
import bdf32x64 from "../fonts/spleen-32x64.bdf" with { type: "text" }

export interface Glyph {
  readonly width: number
  readonly height: number
  readonly xOffset: number
  readonly yOffset: number
  readonly advance: number
  /** One entry per row, MSB-first, `rowBits` wide. */
  readonly rows: ReadonlyArray<number>
  readonly rowBits: number
}

export interface Font {
  readonly cellWidth: number
  readonly cellHeight: number
  readonly ascent: number
  readonly glyphs: ReadonlyMap<number, Glyph>
}

/** Parses the subset of BDF that fixed-cell bitmap fonts such as Spleen use. */
export const parseBdf = (source: string): Font => {
  const glyphs = new Map<number, Glyph>()
  let cellWidth = 0
  let cellHeight = 0
  let ascent = 0

  let encoding = -1
  let advance = 0
  let bbx: [number, number, number, number] = [0, 0, 0, 0]
  let rows: Array<number> | undefined
  let rowBits = 0

  for (const line of source.split("\n")) {
    if (rows !== undefined) {
      if (line.startsWith("ENDCHAR")) {
        glyphs.set(encoding, {
          width: bbx[0],
          height: bbx[1],
          xOffset: bbx[2],
          yOffset: bbx[3],
          advance: advance || cellWidth,
          rows,
          rowBits
        })
        rows = undefined
      } else {
        const hex = line.trim()
        rowBits = hex.length * 4
        rows.push(parseInt(hex, 16))
      }
      continue
    }
    const [keyword, ...args] = line.trim().split(/\s+/)
    const nums = args.map(Number)
    switch (keyword) {
      case "FONTBOUNDINGBOX":
        cellWidth = nums[0] ?? 0
        cellHeight = nums[1] ?? 0
        break
      case "FONT_ASCENT":
        ascent = nums[0] ?? 0
        break
      case "ENCODING":
        encoding = nums[0] ?? -1
        break
      case "DWIDTH":
        advance = nums[0] ?? 0
        break
      case "BBX":
        bbx = [nums[0] ?? 0, nums[1] ?? 0, nums[2] ?? 0, nums[3] ?? 0]
        break
      case "BITMAP":
        rows = []
        break
    }
  }
  return { cellWidth, cellHeight, ascent, glyphs }
}

export const fonts = {
  tiny: parseBdf(bdf6x12),
  small: parseBdf(bdf8x16),
  medium: parseBdf(bdf12x24),
  large: parseBdf(bdf16x32),
  huge: parseBdf(bdf32x64)
} as const

export const textWidth = (font: Font, text: string, scale = 1): number => text.length * font.cellWidth * scale
