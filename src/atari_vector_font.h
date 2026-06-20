// atari_vector_font.h
//
// Compact Atari-vector-arcade-style stroke text renderer for raylib.
// The glyphs are original normalized stroke definitions, designed to evoke
// the angular Tempest/Atari vector display readout without embedding ROM data.

#ifndef ATARI_VECTOR_FONT_H
#define ATARI_VECTOR_FONT_H

#include "raylib.h"
#include <ctype.h>
#include <math.h>
#include <string.h>

#define AVF_W 10.0f
#define AVF_H 16.0f
#define AVF_RENDER_SCALE 0.89f

static Color avf_alpha(Color col, float alpha) {
  if (alpha < 0.0f) alpha = 0.0f;
  if (alpha > 1.0f) alpha = 1.0f;
  col.a = (unsigned char)((float)col.a * alpha);
  return col;
}

static void avf_line(float x, float y, float s, float thick, Color col,
                     float x0, float y0, float x1, float y1) {
  Vector2 p0 = {
    floorf(x + x0 * s) + 0.5f,
    floorf(y + y0 * s) + 0.5f
  };
  Vector2 p1 = {
    floorf(x + x1 * s) + 0.5f,
    floorf(y + y1 * s) + 0.5f
  };
  float core = thick;
  if (core < 1.0f) core = 1.0f;

  DrawLineEx(p0, p1, core * 3.0f, avf_alpha(col, 0.07f));
  DrawLineEx(p0, p1, core * 1.8f, avf_alpha(col, 0.16f));
  DrawLineEx(p0, p1, core, col);

  float dot = core * 0.45f;
  DrawCircleV(p0, dot, avf_alpha(col, 0.35f));
  DrawCircleV(p1, dot, avf_alpha(col, 0.35f));
}

static void avf_dot(float x, float y, float s, float thick, Color col,
                    float cx, float cy) {
  Vector2 p = {
    floorf(x + cx * s) + 0.5f,
    floorf(y + cy * s) + 0.5f
  };
  float core = thick * 0.82f;
  if (core < 1.15f) core = 1.15f;
  DrawCircleV(p, core * 2.1f, avf_alpha(col, 0.08f));
  DrawCircleV(p, core * 1.35f, avf_alpha(col, 0.20f));
  DrawCircleV(p, core, col);
}

static void avf_glyph(char ch, float x, float y, float s, float thick,
                      Color col) {
#define L(x0,y0,x1,y1) avf_line(x, y, s, thick, col, x0, y0, x1, y1)
#define D(cx,cy) avf_dot(x, y, s, thick, col, cx, cy)
  switch (ch) {
    case 'a': L(2,8,8,8); L(8,8,8,14); L(8,14,2,14); L(2,14,2,11); L(2,11,8,11); break;
    case 'b': L(1,2,1,14); L(1,8,8,8); L(8,8,8,14); L(8,14,1,14); break;
    case 'c': L(9,8,1,8); L(1,8,1,14); L(1,14,9,14); break;
    case 'd': L(9,2,9,14); L(9,8,2,8); L(2,8,2,14); L(2,14,9,14); break;
    case 'e': L(1,11,9,11); L(9,11,9,8); L(9,8,1,8); L(1,8,1,14); L(1,14,9,14); break;
    case 'f': L(8,2,5,2); L(5,2,5,14); L(2,7,8,7); break;
    case 'g': L(9,8,2,8); L(2,8,2,14); L(2,14,9,14); L(9,8,9,16); L(9,16,2,16); break;
    case 'h': L(1,2,1,14); L(1,8,8,8); L(8,8,8,14); break;
    case 'i': D(5,4); L(5,8,5,14); break;
    case 'j': D(7,4); L(7,8,7,16); L(7,16,2,16); break;
    case 'k': L(1,2,1,14); L(8,8,1,12); L(4,11,9,14); break;
    case 'l': L(4,2,4,14); L(4,14,8,14); break;
    case 'm': L(1,14,1,8); L(1,8,4,8); L(4,8,4,14); L(4,8,8,8); L(8,8,8,14); break;
    case 'n': L(1,14,1,8); L(1,8,8,8); L(8,8,8,14); break;
    case 'o': L(2,8,8,8); L(8,8,8,14); L(8,14,2,14); L(2,14,2,8); break;
    case 'p': L(1,8,1,16); L(1,8,8,8); L(8,8,8,14); L(8,14,1,14); break;
    case 'q': L(9,8,9,16); L(9,8,2,8); L(2,8,2,14); L(2,14,9,14); break;
    case 'r': L(1,14,1,8); L(1,8,8,8); break;
    case 's': L(9,8,1,8); L(1,8,1,11); L(1,11,9,11); L(9,11,9,14); L(9,14,1,14); break;
    case 't': L(5,4,5,14); L(2,8,8,8); L(5,14,9,14); break;
    case 'u': L(1,8,1,14); L(1,14,8,14); L(8,8,8,14); break;
    case 'v': L(1,8,5,14); L(5,14,9,8); break;
    case 'w': L(1,8,2,14); L(2,14,5,11); L(5,11,8,14); L(8,14,9,8); break;
    case 'x': L(1,8,9,14); L(9,8,1,14); break;
    case 'y': L(1,8,5,14); L(9,8,5,14); L(5,14,3,16); L(3,16,1,16); break;
    case 'z': L(1,8,9,8); L(9,8,1,14); L(1,14,9,14); break;
    case 'A': L(1,14,1,6); L(1,6,5,2); L(5,2,9,6); L(9,6,9,14); L(1,9,9,9); break;
    case 'B': L(1,2,1,14); L(1,2,7,2); L(7,2,9,4); L(9,4,9,7); L(9,7,7,8); L(1,8,7,8); L(7,8,9,10); L(9,10,9,12); L(9,12,7,14); L(1,14,7,14); break;
    case 'C': L(9,2,1,2); L(1,2,1,14); L(1,14,9,14); break;
    case 'D': L(1,2,1,14); L(1,2,6,2); L(6,2,9,5); L(9,5,9,11); L(9,11,6,14); L(1,14,6,14); break;
    case 'E': L(9,2,1,2); L(1,2,1,14); L(1,8,7,8); L(1,14,9,14); break;
    case 'F': L(1,2,1,14); L(1,2,9,2); L(1,8,7,8); break;
    case 'G': L(9,2,1,2); L(1,2,1,14); L(1,14,9,14); L(9,14,9,8); L(9,8,5,8); break;
    case 'H': L(1,2,1,14); L(9,2,9,14); L(1,8,9,8); break;
    case 'I': L(3,2,7,2); L(5,2,5,14); L(3,14,7,14); break;
    case 'J': L(9,2,9,12); L(9,12,7,14); L(7,14,3,14); L(3,14,1,12); break;
    case 'K': L(1,2,1,14); L(9,2,1,9); L(3,8,9,14); break;
    case 'L': L(1,2,1,14); L(1,14,9,14); break;
    case 'M': L(1,14,1,2); L(1,2,5,7); L(5,7,9,2); L(9,2,9,14); break;
    case 'N': L(1,14,1,2); L(1,2,9,14); L(9,14,9,2); break;
    case 'O': L(1,2,9,2); L(9,2,9,14); L(9,14,1,14); L(1,14,1,2); break;
    case 'P': L(1,2,1,14); L(1,2,8,2); L(8,2,9,3); L(9,3,9,7); L(9,7,8,8); L(1,8,8,8); break;
    case 'Q': L(1,2,9,2); L(9,2,9,14); L(9,14,1,14); L(1,14,1,2); L(6,11,9,14); break;
    case 'R': L(1,2,1,14); L(1,2,8,2); L(8,2,9,3); L(9,3,9,7); L(9,7,8,8); L(1,8,8,8); L(5,8,9,14); break;
    case 'S': L(9,2,1,2); L(1,2,1,8); L(1,8,9,8); L(9,8,9,14); L(9,14,1,14); break;
    case 'T': L(1,2,9,2); L(5,2,5,14); break;
    case 'U': L(1,2,1,14); L(1,14,9,14); L(9,14,9,2); break;
    case 'V': L(1,2,5,14); L(5,14,9,2); break;
    case 'W': L(1,2,2,14); L(2,14,5,9); L(5,9,8,14); L(8,14,9,2); break;
    case 'X': L(1,2,9,14); L(9,2,1,14); break;
    case 'Y': L(1,2,5,8); L(9,2,5,8); L(5,8,5,14); break;
    case 'Z': L(1,2,9,2); L(9,2,1,14); L(1,14,9,14); break;
    case '0': L(1,2,9,2); L(9,2,9,14); L(9,14,1,14); L(1,14,1,2); L(8,3,2,13); break;
    case '1': L(2,14,8,14); L(5,2,5,14); L(2,5,5,2); break;
    case '2': L(1,2,9,2); L(9,2,9,7); L(9,7,1,14); L(1,14,9,14); break;
    case '3': L(1,2,9,2); L(9,2,5,8); L(5,8,9,8); L(9,8,9,14); L(9,14,1,14); break;
    case '4': L(1,10,8,2); L(8,2,8,14); L(1,10,9,10); break;
    case '5': L(9,2,1,2); L(1,2,1,8); L(1,8,9,8); L(9,8,9,14); L(9,14,1,14); break;
    case '6': L(9,2,1,9); L(1,9,1,14); L(1,14,9,14); L(9,14,9,8); L(9,8,1,8); break;
    case '7': L(1,2,9,2); L(9,2,3,14); break;
    case '8': L(1,2,9,2); L(9,2,9,14); L(9,14,1,14); L(1,14,1,2); L(1,8,9,8); break;
    case '9': L(9,8,1,8); L(1,8,1,2); L(1,2,9,2); L(9,2,9,14); L(9,14,1,14); break;
    case '.': D(5,13); break;
    case ',': L(6,12,4,15); break;
    case ':': D(5,4); D(5,10); break;
    case ';': D(5,4); L(6,10,4,15); break;
    case '+': L(5,3,5,11); L(1,7,9,7); break;
    case '-': L(1,7,9,7); break;
    case '/': L(9,0,1,14); break;
    case '\\': L(1,0,9,14); break;
    case '(': L(7,0,3,4); L(3,4,3,10); L(3,10,7,14); break;
    case ')': L(3,0,7,4); L(7,4,7,10); L(7,10,3,14); break;
    case '[': L(8,0,3,0); L(3,0,3,14); L(3,14,8,14); break;
    case ']': L(2,0,7,0); L(7,0,7,14); L(7,14,2,14); break;
    case '<': L(8,2,2,7); L(2,7,8,12); break;
    case '>': L(2,2,8,7); L(8,7,2,12); break;
    case '=': L(1,5,9,5); L(1,9,9,9); break;
    case '%': L(2,4,4,2); L(4,2,5,3); L(5,3,3,5); L(3,5,2,4); L(9,0,1,14); L(6,11,8,9); L(8,9,9,10); L(9,10,7,12); L(7,12,6,11); break;
    case '_': L(1,14,9,14); break;
    case '|': L(5,0,5,14); break;
    case '\'': L(5,0,4,4); break;
    case '"': L(3,0,3,4); L(7,0,7,4); break;
    case ' ': break;
    default: L(2,0,8,0); L(8,0,8,14); L(8,14,2,14); L(2,14,2,0); break;
  }
#undef D
#undef L
}

static inline int avf_measure(const char *txt, int cell_h) {
  if (!txt || !*txt) return 0;
  float scale = ((float)cell_h * AVF_RENDER_SCALE) / AVF_H;
  return (int)((float)strlen(txt) * (AVF_W + 2.0f) * scale + 0.5f);
}

static void avf_text(const char *txt, int x, int y, int cell_h, Color col) {
  if (!txt) return;
  float scale = ((float)cell_h * AVF_RENDER_SCALE) / AVF_H;
  float thick = scale * 0.64f;
  if (thick < 1.0f) thick = 1.0f;
  float y0 = (float)y + ((float)cell_h - AVF_H * scale) * 0.5f;

  float cx = (float)x;
  while (*txt) {
    avf_glyph(*txt++, cx, y0, scale, thick, col);
    cx += (AVF_W + 2.0f) * scale;
  }
}

#endif /* ATARI_VECTOR_FONT_H */
