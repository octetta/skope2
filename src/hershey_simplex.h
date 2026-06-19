// hershey_simplex.h
//
// Small Hershey-simplex-style stroke text renderer for raylib.
// Coordinates are normalized to a 10 x 14 cell. The renderer favours the
// single-line drafting look of the Hershey Simplex family over exact glyph
// archival fidelity; it is intended for scope readouts, not typography.

#ifndef HERSHEY_SIMPLEX_H
#define HERSHEY_SIMPLEX_H

#include "raylib.h"
#include <ctype.h>
#include <math.h>
#include <string.h>

#define HS_W 10.0f
#define HS_H 14.0f

static void hs_line(float x, float y, float s, float thick, Color col,
                    float x0, float y0, float x1, float y1) {
  DrawLineEx((Vector2){x + x0 * s, y + y0 * s},
             (Vector2){x + x1 * s, y + y1 * s}, thick, col);
}

static void hs_arc(float x, float y, float s, float thick, Color col,
                   float cx, float cy, float rx, float ry,
                   float a0, float a1) {
  Vector2 prev = {0};
  int have = 0;
  for (int i = 0; i <= 10; i++) {
    float t = (float)i / 10.0f;
    float a = (a0 + (a1 - a0) * t) * DEG2RAD;
    Vector2 cur = {x + (cx + cosf(a) * rx) * s,
                   y + (cy + sinf(a) * ry) * s};
    if (have) DrawLineEx(prev, cur, thick, col);
    prev = cur;
    have = 1;
  }
}

static void hs_glyph(char ch, float x, float y, float s, float thick,
                     Color col) {
  ch = (char)toupper((unsigned char)ch);

#define L(x0,y0,x1,y1) hs_line(x, y, s, thick, col, x0, y0, x1, y1)
#define A(cx,cy,rx,ry,a0,a1) hs_arc(x, y, s, thick, col, cx, cy, rx, ry, a0, a1)
  switch (ch) {
    case 'A': L(1,14,5,0); L(5,0,9,14); L(2.7f,8,7.3f,8); break;
    case 'B': L(1,0,1,14); L(1,0,6.5f,0); A(6.5f,3.5f,2.5f,3.5f,-90,90); L(1,7,6.5f,7); A(6.5f,10.5f,2.5f,3.5f,-90,90); L(1,14,6.5f,14); break;
    case 'C': A(5.5f,7,4.5f,7,55,305); break;
    case 'D': L(1,0,1,14); L(1,0,5.5f,0); A(5.5f,7,3.5f,7,-90,90); L(1,14,5.5f,14); break;
    case 'E': L(1,0,1,14); L(1,0,9,0); L(1,7,7.5f,7); L(1,14,9,14); break;
    case 'F': L(1,0,1,14); L(1,0,9,0); L(1,7,7.5f,7); break;
    case 'G': A(5.5f,7,4.5f,7,45,330); L(5.5f,8,9,8); L(9,8,9,12); break;
    case 'H': L(1,0,1,14); L(9,0,9,14); L(1,7,9,7); break;
    case 'I': L(2,0,8,0); L(5,0,5,14); L(2,14,8,14); break;
    case 'J': L(8,0,8,10.5f); A(5,10.5f,3,3.5f,0,180); break;
    case 'K': L(1,0,1,14); L(9,0,1,7); L(1,7,9,14); break;
    case 'L': L(1,0,1,14); L(1,14,9,14); break;
    case 'M': L(1,14,1,0); L(1,0,5,7); L(5,7,9,0); L(9,0,9,14); break;
    case 'N': L(1,14,1,0); L(1,0,9,14); L(9,14,9,0); break;
    case 'O': A(5,7,4,7,0,360); break;
    case 'P': L(1,0,1,14); L(1,0,6.5f,0); A(6.5f,3.5f,2.5f,3.5f,-90,90); L(1,7,6.5f,7); break;
    case 'Q': A(5,7,4,7,0,360); L(6.5f,10.5f,9,14); break;
    case 'R': L(1,0,1,14); L(1,0,6.5f,0); A(6.5f,3.5f,2.5f,3.5f,-90,90); L(1,7,6.5f,7); L(5.5f,7,9,14); break;
    case 'S': A(5,3.5f,4,3.5f,190,20); A(5,10.5f,4,3.5f,200,30); break;
    case 'T': L(1,0,9,0); L(5,0,5,14); break;
    case 'U': L(1,0,1,10); A(5,10,4,4,0,180); L(9,0,9,10); break;
    case 'V': L(1,0,5,14); L(5,14,9,0); break;
    case 'W': L(1,0,2.5f,14); L(2.5f,14,5,7); L(5,7,7.5f,14); L(7.5f,14,9,0); break;
    case 'X': L(1,0,9,14); L(9,0,1,14); break;
    case 'Y': L(1,0,5,7); L(9,0,5,7); L(5,7,5,14); break;
    case 'Z': L(1,0,9,0); L(9,0,1,14); L(1,14,9,14); break;
    case '0': A(5,7,4,7,0,360); L(8,2,2,12); break;
    case '1': L(4,3,5.5f,0); L(5.5f,0,5.5f,14); L(3,14,8,14); break;
    case '2': A(5,4,4,4,200,20); L(8.8f,5,1,14); L(1,14,9,14); break;
    case '3': A(5,3.5f,4,3.5f,210,35); A(5,10.5f,4,3.5f,325,150); L(4,7,7,7); break;
    case '4': L(8,14,8,0); L(1,9,9,9); L(1,9,8,0); break;
    case '5': L(9,0,1,0); L(1,0,1,7); A(5,10.5f,4,3.5f,210,30); break;
    case '6': A(5,9,4,5,0,360); L(8,1,2,8); break;
    case '7': L(1,0,9,0); L(9,0,3,14); break;
    case '8': A(5,3.5f,3.5f,3.5f,0,360); A(5,10.5f,3.5f,3.5f,0,360); break;
    case '9': A(5,5,4,5,0,360); L(8,6,2,14); break;
    case '.': L(5,13.2f,5.1f,13.2f); break;
    case ',': L(5,12,4,15); break;
    case ':': L(5,4,5.1f,4); L(5,10,5.1f,10); break;
    case ';': L(5,4,5.1f,4); L(5,10,4,14); break;
    case '+': L(5,3,5,11); L(1,7,9,7); break;
    case '-': L(1,7,9,7); break;
    case '/': L(9,0,1,14); break;
    case '\\': L(1,0,9,14); break;
    case '[': L(8,0,3,0); L(3,0,3,14); L(3,14,8,14); break;
    case ']': L(2,0,7,0); L(7,0,7,14); L(7,14,2,14); break;
    case '(': A(7,7,4,7,105,255); break;
    case ')': A(3,7,4,7,-75,75); break;
    case '<': L(8,2,2,7); L(2,7,8,12); break;
    case '>': L(2,2,8,7); L(8,7,2,12); break;
    case '=': L(1,5,9,5); L(1,9,9,9); break;
    case '%': A(3,3,1.8f,2,0,360); A(7,11,1.8f,2,0,360); L(9,0,1,14); break;
    case '_': L(1,14,9,14); break;
    case '|': L(5,0,5,14); break;
    case '\'': L(5,0,4,4); break;
    case '"': L(3,0,3,4); L(7,0,7,4); break;
    case ' ': break;
    default: L(2,0,8,0); L(8,0,8,14); L(8,14,2,14); L(2,14,2,0); break;
  }
#undef A
#undef L
}

static inline int hs_measure(const char *txt, int cell_h) {
  if (!txt || !*txt) return 0;
  float scale = (float)cell_h / HS_H;
  int n = (int)strlen(txt);
  return (int)((float)n * (HS_W + 2.0f) * scale + 0.5f);
}

static void hs_text(const char *txt, int x, int y, int cell_h, Color col) {
  if (!txt) return;
  float scale = (float)cell_h / HS_H;
  float thick = scale < 1.0f ? 1.0f : scale;
  float cx = (float)x;
  while (*txt) {
    hs_glyph(*txt++, cx, (float)y, scale, thick, col);
    cx += (HS_W + 2.0f) * scale;
  }
}

#endif /* HERSHEY_SIMPLEX_H */
