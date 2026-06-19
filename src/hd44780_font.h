// hd44780_font.h
//
// Generates a raylib Font from the Hitachi HD44780A00 character ROM.
//
// The HD44780 stores each glyph as 8 bytes; each byte encodes one row
// of 5 pixels in bits [4:0] (MSB unused). The ROM covers codepoints
// 0x20–0x7E (printable ASCII). We skip 0x00–0x1F (control) and map
// the rest directly.
//
// Usage:
//   Font f; float scale;
//   hd44780_font_load(&f, &scale, dpi_scale);
//   // draw:
//   hd44780_draw_text(f, scale, "HELLO", x, y, color);
//   // measure:
//   int px_wide = hd44780_measure(f, scale, "HELLO");
//   // cleanup (call before CloseWindow):
//   UnloadFont(f);
//
// The glyph cell is 6×8 pixels at 1× (5px glyph + 1px gap), scaled up
// by an integer factor chosen from DPI: 1× for scale<1.5, 2× otherwise.
// That gives crisp nearest-neighbour upscaling with no anti-aliasing blur.

#ifndef HD44780_FONT_H
#define HD44780_FONT_H

#include "raylib.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// HD44780A00 ROM — 5×8 pixel patterns for codepoints 0x20–0x7E
// Each entry is 8 bytes; each byte = one row, bits [4:0] = pixels left→right.
// Source: Hitachi HD44780U datasheet, Table 4 (ROM code A00).
// ---------------------------------------------------------------------------
static const uint8_t kHD44780Rom[95][8] = {
  /* 0x20 ' '  */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
  /* 0x21 '!'  */ {0x04,0x04,0x04,0x04,0x00,0x00,0x04,0x00},
  /* 0x22 '"'  */ {0x0A,0x0A,0x0A,0x00,0x00,0x00,0x00,0x00},
  /* 0x23 '#'  */ {0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A,0x00},
  /* 0x24 '$'  */ {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04,0x00},
  /* 0x25 '%'  */ {0x18,0x19,0x02,0x04,0x08,0x13,0x03,0x00},
  /* 0x26 '&'  */ {0x0C,0x12,0x14,0x08,0x15,0x12,0x0D,0x00},
  /* 0x27 '\'' */ {0x04,0x04,0x08,0x00,0x00,0x00,0x00,0x00},
  /* 0x28 '('  */ {0x02,0x04,0x08,0x08,0x08,0x04,0x02,0x00},
  /* 0x29 ')'  */ {0x08,0x04,0x02,0x02,0x02,0x04,0x08,0x00},
  /* 0x2A '*'  */ {0x00,0x04,0x15,0x0E,0x15,0x04,0x00,0x00},
  /* 0x2B '+'  */ {0x00,0x04,0x04,0x1F,0x04,0x04,0x00,0x00},
  /* 0x2C ','  */ {0x00,0x00,0x00,0x00,0x0C,0x04,0x08,0x00},
  /* 0x2D '-'  */ {0x00,0x00,0x00,0x1F,0x00,0x00,0x00,0x00},
  /* 0x2E '.'  */ {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00},
  /* 0x2F '/'  */ {0x00,0x01,0x02,0x04,0x08,0x10,0x00,0x00},
  /* 0x30 '0'  */ {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E,0x00},
  /* 0x31 '1'  */ {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E,0x00},
  /* 0x32 '2'  */ {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F,0x00},
  /* 0x33 '3'  */ {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E,0x00},
  /* 0x34 '4'  */ {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02,0x00},
  /* 0x35 '5'  */ {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E,0x00},
  /* 0x36 '6'  */ {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E,0x00},
  /* 0x37 '7'  */ {0x1F,0x01,0x02,0x04,0x08,0x08,0x08,0x00},
  /* 0x38 '8'  */ {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E,0x00},
  /* 0x39 '9'  */ {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C,0x00},
  /* 0x3A ':'  */ {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00,0x00},
  /* 0x3B ';'  */ {0x00,0x0C,0x0C,0x00,0x0C,0x04,0x08,0x00},
  /* 0x3C '<'  */ {0x02,0x04,0x08,0x10,0x08,0x04,0x02,0x00},
  /* 0x3D '='  */ {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00,0x00},
  /* 0x3E '>'  */ {0x08,0x04,0x02,0x01,0x02,0x04,0x08,0x00},
  /* 0x3F '?'  */ {0x0E,0x11,0x01,0x02,0x04,0x00,0x04,0x00},
  /* 0x40 '@'  */ {0x0E,0x11,0x01,0x0D,0x15,0x15,0x0E,0x00},
  /* 0x41 'A'  */ {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11,0x00},
  /* 0x42 'B'  */ {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E,0x00},
  /* 0x43 'C'  */ {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E,0x00},
  /* 0x44 'D'  */ {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C,0x00},
  /* 0x45 'E'  */ {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F,0x00},
  /* 0x46 'F'  */ {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10,0x00},
  /* 0x47 'G'  */ {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F,0x00},
  /* 0x48 'H'  */ {0x11,0x11,0x11,0x1F,0x11,0x11,0x11,0x00},
  /* 0x49 'I'  */ {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E,0x00},
  /* 0x4A 'J'  */ {0x07,0x02,0x02,0x02,0x02,0x12,0x0C,0x00},
  /* 0x4B 'K'  */ {0x11,0x12,0x14,0x18,0x14,0x12,0x11,0x00},
  /* 0x4C 'L'  */ {0x10,0x10,0x10,0x10,0x10,0x10,0x1F,0x00},
  /* 0x4D 'M'  */ {0x11,0x1B,0x15,0x15,0x11,0x11,0x11,0x00},
  /* 0x4E 'N'  */ {0x11,0x11,0x19,0x15,0x13,0x11,0x11,0x00},
  /* 0x4F 'O'  */ {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E,0x00},
  /* 0x50 'P'  */ {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10,0x00},
  /* 0x51 'Q'  */ {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D,0x00},
  /* 0x52 'R'  */ {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11,0x00},
  /* 0x53 'S'  */ {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E,0x00},
  /* 0x54 'T'  */ {0x1F,0x04,0x04,0x04,0x04,0x04,0x04,0x00},
  /* 0x55 'U'  */ {0x11,0x11,0x11,0x11,0x11,0x11,0x0E,0x00},
  /* 0x56 'V'  */ {0x11,0x11,0x11,0x11,0x11,0x0A,0x04,0x00},
  /* 0x57 'W'  */ {0x11,0x11,0x11,0x15,0x15,0x15,0x0A,0x00},
  /* 0x58 'X'  */ {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11,0x00},
  /* 0x59 'Y'  */ {0x11,0x11,0x11,0x0A,0x04,0x04,0x04,0x00},
  /* 0x5A 'Z'  */ {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F,0x00},
  /* 0x5B '['  */ {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E,0x00},
  /* 0x5C '\\'*/ {0x00,0x10,0x08,0x04,0x02,0x01,0x00,0x00},
  /* 0x5D ']'  */ {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E,0x00},
  /* 0x5E '^'  */ {0x04,0x0A,0x11,0x00,0x00,0x00,0x00,0x00},
  /* 0x5F '_'  */ {0x00,0x00,0x00,0x00,0x00,0x00,0x1F,0x00},
  /* 0x60 '`'  */ {0x08,0x04,0x02,0x00,0x00,0x00,0x00,0x00},
  /* 0x61 'a'  */ {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F,0x00},
  /* 0x62 'b'  */ {0x10,0x10,0x1E,0x11,0x11,0x11,0x1E,0x00},
  /* 0x63 'c'  */ {0x00,0x00,0x0E,0x10,0x10,0x11,0x0E,0x00},
  /* 0x64 'd'  */ {0x01,0x01,0x0F,0x11,0x11,0x11,0x0F,0x00},
  /* 0x65 'e'  */ {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E,0x00},
  /* 0x66 'f'  */ {0x06,0x09,0x08,0x1C,0x08,0x08,0x08,0x00},
  /* 0x67 'g'  */ {0x00,0x00,0x0F,0x11,0x11,0x0F,0x01,0x0E},
  /* 0x68 'h'  */ {0x10,0x10,0x1E,0x11,0x11,0x11,0x11,0x00},
  /* 0x69 'i'  */ {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E,0x00},
  /* 0x6A 'j'  */ {0x02,0x00,0x06,0x02,0x02,0x12,0x0C,0x00},
  /* 0x6B 'k'  */ {0x10,0x10,0x11,0x12,0x1C,0x12,0x11,0x00},
  /* 0x6C 'l'  */ {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E,0x00},
  /* 0x6D 'm'  */ {0x00,0x00,0x1A,0x15,0x15,0x11,0x11,0x00},
  /* 0x6E 'n'  */ {0x00,0x00,0x1E,0x11,0x11,0x11,0x11,0x00},
  /* 0x6F 'o'  */ {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E,0x00},
  /* 0x70 'p'  */ {0x00,0x00,0x1E,0x11,0x11,0x1E,0x10,0x10},
  /* 0x71 'q'  */ {0x00,0x00,0x0F,0x11,0x11,0x0F,0x01,0x01},
  /* 0x72 'r'  */ {0x00,0x00,0x16,0x19,0x10,0x10,0x10,0x00},
  /* 0x73 's'  */ {0x00,0x00,0x0E,0x10,0x0E,0x01,0x1E,0x00},
  /* 0x74 't'  */ {0x08,0x08,0x1E,0x08,0x08,0x09,0x06,0x00},
  /* 0x75 'u'  */ {0x00,0x00,0x11,0x11,0x11,0x13,0x0D,0x00},
  /* 0x76 'v'  */ {0x00,0x00,0x11,0x11,0x11,0x0A,0x04,0x00},
  /* 0x77 'w'  */ {0x00,0x00,0x11,0x11,0x15,0x15,0x0A,0x00},
  /* 0x78 'x'  */ {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11,0x00},
  /* 0x79 'y'  */ {0x00,0x00,0x11,0x11,0x0F,0x01,0x0E,0x00},
  /* 0x7A 'z'  */ {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F,0x00},
  /* 0x7B '{'  */ {0x02,0x04,0x04,0x08,0x04,0x04,0x02,0x00},
  /* 0x7C '|'  */ {0x04,0x04,0x04,0x00,0x04,0x04,0x04,0x00},
  /* 0x7D '}'  */ {0x08,0x04,0x04,0x02,0x04,0x04,0x08,0x00},
  /* 0x7E '~'  */ {0x00,0x00,0x08,0x15,0x02,0x00,0x00,0x00},
};

#define HD44780_GLYPH_W   5    // pixel columns per glyph
#define HD44780_GLYPH_H   8    // pixel rows per glyph (row 7 = descender)
#define HD44780_CELL_W    6    // cell width with 1px right gap
#define HD44780_CELL_H    9    // cell height with 1px bottom gap
#define HD44780_FIRST     0x20 // first codepoint in ROM
#define HD44780_LAST      0x7E // last codepoint in ROM
#define HD44780_COUNT     (HD44780_LAST - HD44780_FIRST + 1)  // 95

// Pick an integer scale factor so the font is legible at the current DPI.
// At 1× the cell is 6×9px — too small. At 2× = 12×18px (good for 1080p).
// At 3× = 18×27px (good for 4K / high DPI).
static inline int hd44780_pick_scale(float dpi_scale) {
  if (dpi_scale >= 2.5f) return 4;
  if (dpi_scale >= 1.5f) return 3;
  return 2;
}

// Build a raylib Font from the HD44780 ROM bitmaps at the given integer scale.
// Returns the Font and writes the pixel size of one cell into *cell_w/*cell_h.
static Font hd44780_font_load(int scale, int *cell_w_out, int *cell_h_out) {
  int cw = HD44780_CELL_W  * scale;
  int ch = HD44780_CELL_H  * scale;

  // Atlas layout: one row of all 95 glyphs side by side.
  int atlas_w = cw * HD44780_COUNT;
  int atlas_h = ch;

  // Allocate RGBA image (white pixels, alpha = coverage)
  // We use white-on-transparent so DrawTextEx tinting works correctly.
  unsigned char *pixels =
    (unsigned char *)calloc((size_t)(atlas_w * atlas_h), 4);

  for (int g = 0; g < HD44780_COUNT; g++) {
    const uint8_t *glyph = kHD44780Rom[g];
    int gx = g * cw;

    for (int row = 0; row < HD44780_GLYPH_H; row++) {
      uint8_t bits = glyph[row];
      for (int col = 0; col < HD44780_GLYPH_W; col++) {
        // Bit 4 = leftmost pixel
        int lit = (bits >> (4 - col)) & 1;
        if (!lit) continue;
        // Scale up: write a scale×scale block
        for (int sy = 0; sy < scale; sy++) {
          for (int sx = 0; sx < scale; sx++) {
            int px = gx + col * scale + sx;
            int py = row * scale + sy;
            int idx = (py * atlas_w + px) * 4;
            pixels[idx + 0] = 255; // R
            pixels[idx + 1] = 255; // G
            pixels[idx + 2] = 255; // B
            pixels[idx + 3] = 255; // A
          }
        }
      }
    }
  }

  // Wrap in a raylib Image then upload to GPU
  Image img = {
    .data    = pixels,
    .width   = atlas_w,
    .height  = atlas_h,
    .mipmaps = 1,
    .format  = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
  };

  // Build GlyphInfo array (one per printable ASCII character)
  GlyphInfo *glyphs = (GlyphInfo *)calloc(HD44780_COUNT, sizeof(GlyphInfo));
  for (int g = 0; g < HD44780_COUNT; g++) {
    glyphs[g].value    = HD44780_FIRST + g;
    glyphs[g].offsetX  = 0;
    glyphs[g].offsetY  = 0;
    glyphs[g].advanceX = cw;  // includes the 1-px right gap
    glyphs[g].image    = (Image){0}; // not needed when using atlas
  }

  // Build atlas rectangle array (each glyph's source rect in the atlas)
  Rectangle *recs = (Rectangle *)calloc(HD44780_COUNT, sizeof(Rectangle));
  for (int g = 0; g < HD44780_COUNT; g++) {
    recs[g] = (Rectangle){
      .x      = (float)(g * cw),
      .y      = 0.0f,
      .width  = (float)cw,
      .height = (float)ch,
    };
  }

  Texture2D tex = LoadTextureFromImage(img);
  // Nearest-neighbour filtering — critical for pixel-perfect scaling
  SetTextureFilter(tex, TEXTURE_FILTER_POINT);
  free(pixels);

  Font font = {
    .baseSize     = ch,
    .glyphCount   = HD44780_COUNT,
    .glyphPadding = 0,
    .texture      = tex,
    .recs         = recs,
    .glyphs       = glyphs,
  };

  if (cell_w_out) *cell_w_out = cw;
  if (cell_h_out) *cell_h_out = ch;
  return font;
}

// ---------------------------------------------------------------------------
// Convenience wrappers that match the rest of skope.c's call style
// ---------------------------------------------------------------------------

// Draw text using the HD44780 font at its native pixel size (no scaling —
// use font.baseSize as fontSize, which equals cell_h * scale exactly).
static inline void hd_draw(Font f, const char *txt,
                            int x, int y, Color col) {
  DrawTextEx(f, txt, (Vector2){(float)x, (float)y},
             (float)f.baseSize, 0.0f, col);
}

// Measure text width in pixels at native size.
static inline int hd_measure(Font f, const char *txt) {
  if (!txt || !txt[0]) return 0;
  Vector2 v = MeasureTextEx(f, txt, (float)f.baseSize, 0.0f);
  return (int)v.x;
}

// Draw at an explicit scale factor (for the rare oversized readout).
// scale_factor 1.0 = native cell_h pixels tall.
static inline void hd_draw_scaled(Font f, const char *txt,
                                   int x, int y, float scale_factor,
                                   Color col) {
  float sz = (float)f.baseSize * scale_factor;
  DrawTextEx(f, txt, (Vector2){(float)x, (float)y}, sz, 0.0f, col);
}

// ---------------------------------------------------------------------------
// Vector-stroke rendering mode
//
// Instead of treating each lit pixel as a filled square (the bitmap-atlas
// path above), this walks the same ROM bit patterns at draw time and
// connects adjacent lit pixels with line segments — horizontal runs,
// vertical runs, and the two diagonal directions. This traces the strokes
// the glyph was designed around rather than rendering it as a block grid,
// giving a genuine "vector scope readout" look instead of an LCD look.
//
// This is drawn live with DrawLineEx, not baked into a texture, so it
// costs more per character than the bitmap-atlas path. It's only used
// when vector mode is toggled on, and only for on-screen UI text (never
// for anything performance-sensitive like waveform sample loops), so the
// added per-frame cost is bounded by how much text is on screen — a few
// hundred characters at most, well within budget at 30fps.
// ---------------------------------------------------------------------------

// Returns 1 if the pixel at (col,row) is lit in the given glyph's bitmap.
static inline int hd44780_pixel_lit(const uint8_t *glyph, int col, int row) {
  if (col < 0 || col >= HD44780_GLYPH_W) return 0;
  if (row < 0 || row >= HD44780_GLYPH_H) return 0;
  return (glyph[row] >> (4 - col)) & 1;
}

// Draw one glyph as connected line strokes at (ox,oy), scaled by `scale`
// (pixels per ROM cell — same convention as the bitmap path's cell size).
// thick: line stroke thickness in pixels.
static void hd44780_draw_glyph_vector(const uint8_t *glyph,
                                       float ox, float oy,
                                       float scale, float thick,
                                       Color col) {
  // Horizontal runs: for each row, connect contiguous lit pixels with one
  // line segment per run rather than per-pixel-pair, so a 5-pixel-wide
  // lit run becomes one line, not four overlapping ones.
  for (int row = 0; row < HD44780_GLYPH_H; row++) {
    int run_start = -1;
    for (int c = 0; c <= HD44780_GLYPH_W; c++) {
      int lit = (c < HD44780_GLYPH_W) && hd44780_pixel_lit(glyph, c, row);
      if (lit && run_start < 0) {
        run_start = c;
      } else if (!lit && run_start >= 0) {
        int run_end = c - 1;
        if (run_end > run_start) {
          // Multi-pixel run: draw a single horizontal stroke through it.
          Vector2 p0 = {ox + run_start * scale + scale*0.5f, oy + row*scale + scale*0.5f};
          Vector2 p1 = {ox + run_end   * scale + scale*0.5f, oy + row*scale + scale*0.5f};
          DrawLineEx(p0, p1, thick, col);
        } else {
          // Single isolated pixel: draw a short dot-stroke (tiny line so
          // it still renders at any thickness).
          Vector2 p0 = {ox + run_start*scale + scale*0.5f, oy + row*scale + scale*0.5f};
          Vector2 p1 = {p0.x + 0.01f, p0.y};
          DrawLineEx(p0, p1, thick, col);
        }
        run_start = -1;
      }
    }
  }

  // Vertical runs: same idea, column-wise. This is what gives strokes
  // like the spine of 'L' or the stem of 'T' a clean single line instead
  // of being implied only by stacked horizontal dots.
  for (int c = 0; c < HD44780_GLYPH_W; c++) {
    int run_start = -1;
    for (int row = 0; row <= HD44780_GLYPH_H; row++) {
      int lit = (row < HD44780_GLYPH_H) && hd44780_pixel_lit(glyph, c, row);
      if (lit && run_start < 0) {
        run_start = row;
      } else if (!lit && run_start >= 0) {
        int run_end = row - 1;
        if (run_end > run_start) {
          Vector2 p0 = {ox + c*scale + scale*0.5f, oy + run_start*scale + scale*0.5f};
          Vector2 p1 = {ox + c*scale + scale*0.5f, oy + run_end  *scale + scale*0.5f};
          DrawLineEx(p0, p1, thick, col);
        }
        // Single-pixel runs already drawn by the horizontal pass above —
        // skip here to avoid doubling the stroke on isolated pixels.
        run_start = -1;
      }
    }
  }

  // Diagonal connectors: catches strokes like the legs of 'X', 'K', 'Y'
  // that the horizontal/vertical passes alone leave looking disconnected
  // (a staircase of isolated pixels). For each lit pixel, if its
  // down-right or down-left neighbour is lit AND neither of the
  // "straight" neighbours that would normally connect them is lit,
  // draw the diagonal segment.
  for (int row = 0; row < HD44780_GLYPH_H - 1; row++) {
    for (int c = 0; c < HD44780_GLYPH_W; c++) {
      if (!hd44780_pixel_lit(glyph, c, row)) continue;

      // Down-right diagonal
      if (c+1 < HD44780_GLYPH_W && hd44780_pixel_lit(glyph, c+1, row+1)) {
        int straight_blocked =
          hd44780_pixel_lit(glyph, c+1, row) || hd44780_pixel_lit(glyph, c, row+1);
        if (!straight_blocked) {
          Vector2 p0 = {ox + c*scale     + scale*0.5f, oy + row*scale     + scale*0.5f};
          Vector2 p1 = {ox + (c+1)*scale + scale*0.5f, oy + (row+1)*scale + scale*0.5f};
          DrawLineEx(p0, p1, thick, col);
        }
      }
      // Down-left diagonal
      if (c-1 >= 0 && hd44780_pixel_lit(glyph, c-1, row+1)) {
        int straight_blocked =
          hd44780_pixel_lit(glyph, c-1, row) || hd44780_pixel_lit(glyph, c, row+1);
        if (!straight_blocked) {
          Vector2 p0 = {ox + c*scale     + scale*0.5f, oy + row*scale     + scale*0.5f};
          Vector2 p1 = {ox + (c-1)*scale + scale*0.5f, oy + (row+1)*scale + scale*0.5f};
          DrawLineEx(p0, p1, thick, col);
        }
      }
    }
  }
}

// Draw a full string in vector-stroke mode. cell_w/cell_h match the
// bitmap path's cell size convention (HD44780_CELL_W/H * scale_factor)
// so vector and bitmap text line up identically when swapped.
static void hd44780_draw_text_vector(const char *txt, int x, int y,
                                      int cell_w, int cell_h,
                                      Color col) {
  if (!txt) return;
  // scale = pixels per ROM grid cell (glyph is 5 wide / 8 tall within
  // the cell_w-1 x cell_h-1 drawable area, matching the bitmap path's
  // 1px gap convention).
  float scale = (float)cell_h / (float)HD44780_GLYPH_H;
  float thick = scale * 0.55f;  // stroke thickness relative to pixel pitch
  if (thick < 1.0f) thick = 1.0f;

  float cx = (float)x;
  for (const unsigned char *p = (const unsigned char *)txt; *p; p++) {
    if (*p == ' ') { cx += cell_w; continue; }
    if (*p < HD44780_FIRST || *p > HD44780_LAST) { cx += cell_w; continue; }
    const uint8_t *glyph = kHD44780Rom[*p - HD44780_FIRST];
    hd44780_draw_glyph_vector(glyph, cx, (float)y, scale, thick, col);
    cx += cell_w;
  }
}

// Measure a vector-mode string — same advance width as the bitmap path
// (one cell_w per character), so layout code doesn't need to know which
// mode is active.
static inline int hd44780_measure_vector(const char *txt, int cell_w) {
  if (!txt) return 0;
  return (int)strlen(txt) * cell_w;
}

#endif /* HD44780_FONT_H */
