// skope.c
//
// Audio oscilloscope for the skred scope-ipc shared memory ring buffer.
// Built on raylib. Three view modes:
//
//   STACKED    — one horizontal band per stereo pair. L and R waveforms
//                share the band; the region between them is filled with a
//                translucent tint showing stereo width. A thin correlation
//                meter sits on the right edge of each band.
//
//   OVERLAY    — all enabled pairs share one plot area. L drawn solid,
//                R drawn at 60% alpha; between-fill shows stereo spread.
//
//   LISSAJOUS  — X-Y phase plot per pair (L=X, R=Y). Classic phase-scope
//                view; great for checking mono compatibility, stereo
//                imaging, and mid-side balance.
//
// Channel layout from synth-types.h (RECORD_CHANNELS = 10):
//   ch 0,1  = Main bus (L/R)   — master stereo output
//   ch 2,3  = Track 1 (L/R)
//   ch 4,5  = Track 2 (L/R)
//   ch 6,7  = Track 3 (L/R)
//   ch 8,9  = Track 4 (L/R)
//
// CPU / refresh: the loop polls write_frame in shared memory; if it has
// not advanced, the loop sleeps and skips drawing. CPU use drops to ~0
// when skred is idle or skope is paused.

#if !defined(_WIN32) && !defined(_WIN64)
#  define _POSIX_C_SOURCE 200809L
#endif

#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#  include <windows.h>
#endif

#include "raylib.h"
#include <scope-ipc.h>
#include "atari_vector_font.h"
#include "hd44780_font.h"

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

#define SKOPE_NUM_PAIRS       5           // RECORD_CHANNELS / 2
#define SKOPE_CAPTURE_FRAMES  8192
#define SKOPE_TRACE_HISTORY   4           // phosphor layers
#define SKOPE_RECONNECT_S     1.0

// Render rate limits.
// We drive the loop ourselves rather than using SetTargetFPS so that we
// only draw when something actually changed.
//
// MAX_RENDER_HZ: fastest we'll redraw even if audio is flooding in.
//   44100 / 128 frames-per-block = ~344 blocks/sec. Capping at 30fps means
//   we draw ~1 in every 11 blocks — plenty for a scope display.
//
// POLL_INTERVAL_S: how long to sleep between write_frame checks when idle.
//   5ms gives ~200 checks/sec — fast enough to notice audio starting within
//   one block, cheap enough to barely register on a CPU usage meter.
//
// BLINK_INTERVAL_S: minimum redraw interval even when nothing changed,
//   so blinking indicators (HOLD, etc.) stay alive.
#define SKOPE_MAX_RENDER_HZ   30.0
#define SKOPE_MIN_FRAME_S     (1.0 / SKOPE_MAX_RENDER_HZ)  // ~33ms
#define SKOPE_POLL_INTERVAL_S 0.005   // 5ms between idle write_frame checks
#define SKOPE_BLINK_INTERVAL_S 0.1    // force redraw at 10Hz for blink

static const char *kDefaultName = SKRED_SCOPE_DEFAULT_NAME;

// Pair names shown in the UI
static const char *kDefaultPairName[SKOPE_NUM_PAIRS] = { "Main", "Tr1", "Tr2", "Tr3", "Tr4" };

// ---------------------------------------------------------------------------
// Theme — dark (phosphor) and light (paper/daylight) modes
// Toggle with L key.
// ---------------------------------------------------------------------------

typedef struct {
  Color crt_bg;          // CRT face background
  Color bezel;           // outer bezel / window background
  Color bezel_inner;     // inside of softkey buttons etc
  Color hud_bg;          // HUD panel background
  Color hud_sep;         // HUD separator line
  Color btn_outline;     // inactive softkey border
  Color btn_active;      // active softkey border + bright text
  Color grid_minor;      // minor graticule lines
  Color grid_major;      // major graticule lines (center axes)
  Color grid_tick;       // subdivision ticks on centre cross
  Color p31_bright;      // primary readout / trace bright
  Color p31_mid;         // secondary readout
  Color p31_dim;         // dim readout / labels
  Color amber;           // trigger / warning
  Color amber_dim;       // dimmed warning
  Color disconnected;    // disconnected status text
} skope_theme_t;

// Dark theme — classic P31 phosphor green on near-black CRT
static const skope_theme_t kThemeDark = {
  .crt_bg       = { 8,   13,  10,  255},
  .bezel        = {28,   30,  27,  255},
  .bezel_inner  = {16,   18,  15,  255},
  .hud_bg       = {20,   22,  19,  255},
  .hud_sep      = {55,   60,  50,  255},
  .btn_outline  = {70,   80,  65,  255},
  .btn_active   = {57,  255, 120,  255},
  .grid_minor   = {200, 210, 200,   28},
  .grid_major   = {220, 230, 220,   52},
  .grid_tick    = {220, 230, 220,   80},
  .p31_bright   = { 57, 255, 120,  255},
  .p31_mid      = { 32, 180,  75,  255},
  .p31_dim      = { 18, 100,  45,  255},
  .amber        = {255, 190,  40,  255},
  .amber_dim    = {170, 110,  18,  255},
  .disconnected = {200, 100,  55,  255},
};

// Light theme — dark ink on warm off-white, like a Tek screengrab or
// a scope with the graticule filter out. Traces stay vivid (same colors).
static const skope_theme_t kThemeLight = {
  .crt_bg       = {232, 235, 224,  255},  // warm off-white screen
  .bezel        = {155, 160, 148,  255},  // medium grey bezel
  .bezel_inner  = {175, 180, 168,  255},
  .hud_bg       = {165, 170, 158,  255},
  .hud_sep      = { 90,  95,  82,  255},
  .btn_outline  = { 70,  78,  62,  255},
  .btn_active   = { 20,  80,  35,  255},  // dark green active state
  .grid_minor   = {  0,   0,   0,   35},  // dark ink at low alpha
  .grid_major   = {  0,   0,   0,   70},
  .grid_tick    = {  0,   0,   0,  100},
  .p31_bright   = { 12,  60,  22,  255},  // dark forest green (readable on paper)
  .p31_mid      = { 30,  85,  45,  255},
  .p31_dim      = { 55, 110,  70,  255},
  .amber        = {140,  65,   0,  255},  // dark amber/brown
  .amber_dim    = {110,  50,   0,  255},
  .disconnected = {140,  40,  20,  255},
};

// Global font state — initialised in skope_init_display(), used everywhere.
static Font   g_hd_font;
static int    g_hd_cell_w;   // pixel width of one character cell (at chosen scale)
static int    g_hd_cell_h;   // pixel height of one character cell
typedef enum {
  TEXT_LCD = 0,
  TEXT_ATARI,
  TEXT_COUNT
} text_mode_t;

static text_mode_t g_text_mode; // mirrors s->text_mode; synced once per frame
                                // in skope_draw so tek_text/tek_measure (which
                                // don't take skope_t*) can see the current mode

// ---------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------

typedef enum {
  VIZ_TIME = 0,
  VIZ_XY,
  VIZ_XY_ROT,
  VIZ_COUNT
} pair_viz_mode_t;

typedef enum {
  VIEW_STACKED = 0,   // one band per pair, L/R with stereo fill
  VIEW_OVERLAY,       // all pairs on one grid
  VIEW_GRID,          // side-by-side/top-to-bottom squares
  VIEW_COUNT
} view_mode_t;

typedef enum {
  TRIG_AUTO = 0,
  TRIG_NORMAL,
  TRIG_SINGLE,
  TRIG_COUNT
} trig_mode_t;

typedef enum {
  EDGE_RISING = 0,
  EDGE_FALLING,
  EDGE_COUNT
} trig_edge_t;

// ---------------------------------------------------------------------------
// Per-pair state
// ---------------------------------------------------------------------------

typedef struct {
  int     enabled;
  float   volts_per_div;   // amplitude scale factor (normalised float units per division)
  float   offset_div;      // vertical centre offset in divisions
  pair_viz_mode_t viz_mode;
} pair_state_t;

// ---------------------------------------------------------------------------
// Trace (one snapshot from the ring)
// ---------------------------------------------------------------------------

typedef struct {
  float   *samples;        // [frame * RECORD_CHANNELS + ch]
  int      frame_count;
  uint64_t first_frame;
  double   captured_at;
  int      valid;
  float    trig_fract;     // Fractional sample offset for anti-ghosting alignment
  int      trig_idx;       // Index of trigger within this trace's samples
} trace_t;

// ---------------------------------------------------------------------------
// App state
// ---------------------------------------------------------------------------

typedef struct {
  // connection
  skred_scope_reader_t reader;
  int     connected;
  char    name[SKRED_SCOPE_NAME_MAX];
  double  last_connect_attempt;
  uint64_t last_write_frame;
  uint64_t last_generation;

  // buffers
  float  *scratch;
  trace_t history[SKOPE_TRACE_HISTORY];
  int     history_head;
  int     history_count;
  uint32_t sample_capacity;
  uint64_t view_offset_frames; // 0 = newest window; positive = scrolled older
  int     view_dirty;

  // pairs
  pair_state_t pairs[SKOPE_NUM_PAIRS];
  int          selected_pair;   // which pair [ ] / scale / position acts on

  // trigger (operates on the L channel of the selected pair)
  trig_mode_t  trig_mode;
  trig_edge_t  trig_edge;
  int          trig_pair;       // which pair is the trigger source
  float        trig_level;
  float        trig_holdoff_s;
  double       last_trig_time;
  int          armed;

  // view
  view_mode_t  view_mode;
  int          persistence;
  float        persist_s;
  float        time_per_div_s;
  int          show_grid;
  int          show_hud;
  int          show_help;
  int          paused;
  int          dark_mode;       // 1 = dark (phosphor), 0 = light (paper)
  const skope_theme_t *theme;  // pointer into kThemeDark or kThemeLight
  text_mode_t  text_mode;      // LCD bitmap or Atari vector-style stroke text

  // window
  int    base_width, base_height;
  float  dpi_x, dpi_y;

  // render timing — we drive the loop ourselves, no SetTargetFPS
  double last_render_time;  // when we last called BeginDrawing
  int    needs_redraw;      // set by input handler or new data
} skope_t;

// ---------------------------------------------------------------------------
// Tektronix-style color palette — accessed via s->theme pointer
//
// All draw code uses TH(s, field) to look up the current theme color.
// This makes light/dark switching a single pointer swap with no other
// changes needed in drawing code.
// ---------------------------------------------------------------------------

#define TH(s, field)   ((s)->theme->field)

// Per-pair trace colors: same in both themes (the waveforms stay vivid).
// L channel = full color, R = dimmed version (see pair_color_r).
static const Color kPairColor[SKOPE_NUM_PAIRS] = {
  { 57, 255, 120, 255},   // Main — P31 green
  {255, 200,  50, 255},   // Tr1  — amber
  { 80, 210, 255, 255},   // Tr2  — cyan
  {255, 100, 170, 255},   // Tr3  — pink
  {180, 140, 255, 255},   // Tr4  — violet
};

// All color access uses TH(s, field) directly in draw functions.
// kPairColor is the only remaining static array (same in both themes).

// Derive an R-channel color: same hue, lower brightness
static Color pair_color_r(int p) {
  Color c = kPairColor[p];
  c.r = (unsigned char)(c.r * 0.55f);
  c.g = (unsigned char)(c.g * 0.55f);
  c.b = (unsigned char)(c.b * 0.55f);
  return c;
}

// Fill color for stereo-width region (translucent pair color)
static Color pair_color_fill(int p, unsigned char alpha) {
  Color c = kPairColor[p];
  c.a = alpha;
  return c;
}

// Apply alpha to a color
static Color color_alpha(Color c, float a) {
  c.a = (unsigned char)(c.a * a);
  return c;
}

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

static void   skope_init(skope_t *s, const char *name);
static void   skope_shutdown(skope_t *s);
static int    skope_try_connect(skope_t *s);
static void   skope_disconnect(skope_t *s);
static int    skope_poll(skope_t *s);
static int    skope_handle_input(skope_t *s);  // returns 1 if any input consumed
static void   skope_draw(skope_t *s);
static void   skope_draw_grid(Rectangle plot, int divs_x, int divs_y,
                               float dpi_scale, const skope_theme_t *th);
static int    frames_for_window(skope_t *s, int available);
static void   skope_draw_stacked(skope_t *s, const trace_t *t,
                                  Rectangle plot, float alpha);
static void   skope_draw_overlay(skope_t *s, const trace_t *t,
                                  Rectangle plot, float alpha);
static void draw_lissajous_cell(skope_t *s, const trace_t *t, Rectangle cell, int p, float alpha, float rot);

static void   skope_draw_hud(skope_t *s, float hud_y, float hud_h);
static void   skope_draw_help(skope_t *s);
static void   skope_draw_buffer_overview(skope_t *s, Rectangle r);
static int    skope_find_trigger(skope_t *s, const float *samples,
                                  int frames, int *out_idx, float *out_fract);
static int    skope_ensure_sample_capacity(skope_t *s, uint32_t frames);
static int    skope_reader_window(const skred_scope_reader_t *reader,
                                  uint64_t first_frame, uint32_t count,
                                  float *output);
static const char *skope_pair_name(skope_t *s, int pair);
static float  skope_pair_db(skope_t *s, int pair);
static const char *text_mode_name(text_mode_t mode);
static double skope_now(void);
static void   skope_sleep(double s);
static uint64_t shm_load64(const volatile uint64_t *v);

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char **argv) {
  const char *name = argc > 1 ? argv[1] : kDefaultName;

  skope_t s;
  skope_init(&s, name);

  SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI |
                 FLAG_VSYNC_HINT);
  // No MSAA — scope lines are mostly axis-aligned, MSAA costs GPU for nothing.
  // No SetTargetFPS — we drive timing ourselves so we only draw when needed.
  InitWindow(s.base_width, s.base_height, "skope  |  octetta");
  SetWindowMinSize(520, 360);
  SetTargetFPS(0);  // disable raylib's internal frame throttle entirely
  
  // Enable MSAA 4X hint BEFORE init window? No, raylib requires ConfigFlags before InitWindow.
  // Instead we just rely on our multi-pass analog glow which acts as antialiasing.

  Vector2 dpi = GetWindowScaleDPI();
  s.dpi_x = dpi.x > 0 ? dpi.x : 1.0f;
  s.dpi_y = dpi.y > 0 ? dpi.y : 1.0f;

  // Load the HD44780 bitmap font — must happen after InitWindow (GPU ready)
  int hd_scale = hd44780_pick_scale(s.dpi_y);
  g_hd_font    = hd44780_font_load(hd_scale, &g_hd_cell_w, &g_hd_cell_h);

  s.last_render_time = skope_now() - SKOPE_MIN_FRAME_S;  // allow first draw immediately
  s.needs_redraw = 1;

  while (!WindowShouldClose()) {
    double now = skope_now();

    // --- Handle input. Any keypress or window event sets needs_redraw. ---
    int had_input = skope_handle_input(&s);
    if (had_input) s.needs_redraw = 1;

    // --- Poll for new audio data. ---
    int got_new = skope_poll(&s);
    if (got_new) s.needs_redraw = 1;

    // --- Force a redraw every BLINK_INTERVAL_S so blinking indicators
    //     (HOLD, single-shot armed) stay live even when audio is silent. ---
    if (s.paused || s.trig_mode == TRIG_SINGLE) {
      if (now - s.last_render_time >= SKOPE_BLINK_INTERVAL_S)
        s.needs_redraw = 1;
    }

    // --- Render only if something changed AND enough time has passed. ---
    double since_last = now - s.last_render_time;
    if (s.needs_redraw && since_last >= SKOPE_MIN_FRAME_S) {
      BeginDrawing();
      ClearBackground(s.theme->bezel);
      skope_draw(&s);
      EndDrawing();
      s.last_render_time = skope_now();
      s.needs_redraw = 0;
    } else {
      // Nothing to draw yet — sleep for one poll interval so we don't spin.
      // Cap sleep to remaining time until next allowed render in case we're
      // just waiting out the frame interval after a burst of audio.
      double sleep_s = SKOPE_POLL_INTERVAL_S;
      if (s.needs_redraw) {
        // We want to draw but haven't hit the frame interval yet — sleep
        // only the remaining interval, not a full poll period.
        double remaining = SKOPE_MIN_FRAME_S - since_last;
        if (remaining > 0 && remaining < sleep_s) sleep_s = remaining;
      }
      skope_sleep(sleep_s);
      // Process window events while sleeping so resize/close stay responsive.
      PollInputEvents();
    }
  }

  CloseWindow();
  skope_shutdown(&s);
  return 0;
}

// ---------------------------------------------------------------------------
// Init / shutdown
// ---------------------------------------------------------------------------

static void skope_init(skope_t *s, const char *name) {
  memset(s, 0, sizeof(*s));
  snprintf(s->name, sizeof(s->name), "%s", name);
  s->reader.fd = -1;

  for (int p = 0; p < SKOPE_NUM_PAIRS; p++) {
    s->pairs[p].enabled      = 1;
    s->pairs[p].volts_per_div = 0.2f;
    s->pairs[p].offset_div   = 0.0f;
    s->pairs[p].viz_mode     = VIZ_TIME;
  }
  s->selected_pair = 0;

  s->trig_mode     = TRIG_AUTO;
  s->trig_edge     = EDGE_RISING;
  s->trig_pair     = 0;
  s->trig_level    = 0.0f;
  s->trig_holdoff_s= 0.0f;
  s->last_trig_time= -1e9;
  s->armed         = 1;

  s->view_mode     = VIEW_STACKED;
  s->persistence   = 0;
  s->persist_s     = 0.6f;
  s->time_per_div_s= 0.005f;
  s->show_grid     = 1;
  s->show_hud      = 1;
  s->show_help     = 0;
  s->paused        = 0;
  s->dark_mode     = 1;
  s->theme         = &kThemeDark;
  s->text_mode     = TEXT_LCD;  // K cycles LCD and Atari vector-style text

  s->base_width    = 1100;
  s->base_height   = 720;
  s->dpi_x         = 1.0f;
  s->dpi_y         = 1.0f;

  s->scratch = calloc((size_t)SKOPE_CAPTURE_FRAMES * RECORD_CHANNELS,
                      sizeof(float));
  for (int i = 0; i < SKOPE_TRACE_HISTORY; i++) {
    s->history[i].samples = calloc(
      (size_t)SKOPE_CAPTURE_FRAMES * RECORD_CHANNELS, sizeof(float));
  }
  s->history_head  = -1;
  s->history_count = 0;
  s->sample_capacity = SKOPE_CAPTURE_FRAMES;
  s->view_offset_frames = 0;
  s->view_dirty = 1;
  s->last_connect_attempt = -1e9;
}

static void skope_shutdown(skope_t *s) {
  skope_disconnect(s);
  UnloadFont(g_hd_font);
  free(s->scratch);
  for (int i = 0; i < SKOPE_TRACE_HISTORY; i++) free(s->history[i].samples);
}

// ---------------------------------------------------------------------------
// Connection
// ---------------------------------------------------------------------------

static uint64_t shm_load64(const volatile uint64_t *v) {
#if defined(_WIN32) || defined(_WIN64)
  return (uint64_t)InterlockedOr64((volatile LONG64 *)v, 0);
#else
  return __atomic_load_n(v, __ATOMIC_ACQUIRE);
#endif
}

static int skope_ensure_sample_capacity(skope_t *s, uint32_t frames) {
  if (frames <= s->sample_capacity) return 1;

  float *new_scratch = realloc(s->scratch,
                               (size_t)frames * RECORD_CHANNELS * sizeof(float));
  if (!new_scratch) return 0;
  
  float *new_history[SKOPE_TRACE_HISTORY];
  for (int i = 0; i < SKOPE_TRACE_HISTORY; i++) {
    new_history[i] = realloc(s->history[i].samples,
                             (size_t)frames * RECORD_CHANNELS * sizeof(float));
    if (!new_history[i]) {
      // On failure, we've already realloc'd some buffers to larger sizes, but
      // we haven't updated sample_capacity. This is safe, as realloc leaves
      // the original pointer valid (or replaces it with a larger block). 
      // We must just ensure we don't lose the pointers that did succeed.
      s->scratch = new_scratch;
      for (int j = 0; j < i; j++) {
        s->history[j].samples = new_history[j];
      }
      return 0;
    }
  }

  // All reallocs succeeded, update all pointers and capacity safely.
  s->scratch = new_scratch;
  for (int i = 0; i < SKOPE_TRACE_HISTORY; i++) {
    s->history[i].samples = new_history[i];
  }
  s->sample_capacity = frames;
  return 1;
}

static int skope_reader_window(const skred_scope_reader_t *reader,
                               uint64_t first_frame, uint32_t count,
                               float *output) {
  if (!reader || !reader->header || !output || count == 0) return -1;
  uint32_t capacity = reader->header->capacity_frames;
  if (capacity == 0 || count > capacity) return -1;

  for (int attempt = 0; attempt < 8; attempt++) {
    uint64_t sequence_before = shm_load64(&reader->header->sequence);
    if (sequence_before & 1) continue;

    uint64_t write_frame = shm_load64(&reader->header->write_frame);
    uint32_t available = write_frame < capacity
      ? (uint32_t)write_frame : capacity;
    uint64_t oldest = write_frame - available;
    if (first_frame < oldest || first_frame + count > write_frame) return 0;

    uint32_t offset = (uint32_t)(first_frame % capacity);
    uint32_t first_count = capacity - offset;
    if (first_count > count) first_count = count;
    memcpy(output, reader->frames + (size_t)offset * SKRED_SCOPE_CHANNELS,
           (size_t)first_count * SKRED_SCOPE_CHANNELS * sizeof(float));
    if (first_count < count) {
      memcpy(output + (size_t)first_count * SKRED_SCOPE_CHANNELS,
             reader->frames,
             (size_t)(count - first_count) * SKRED_SCOPE_CHANNELS * sizeof(float));
    }

    uint64_t sequence_after = shm_load64(&reader->header->sequence);
    if (sequence_before == sequence_after && !(sequence_after & 1))
      return (int)count;
  }
  return 0;
}

static const char *skope_pair_name(skope_t *s, int pair) {
  if (s->connected && s->reader.header &&
      pair >= 0 && pair < (int)s->reader.header->track_count &&
      s->reader.header->track_name[pair][0]) {
    return s->reader.header->track_name[pair];
  }
  return kDefaultPairName[pair];
}

static float skope_pair_db(skope_t *s, int pair) {
  if (s->connected && s->reader.header &&
      pair >= 0 && pair < (int)s->reader.header->track_count) {
    return s->reader.header->track_volume_db[pair];
  }
  return NAN;
}

static const char *text_mode_name(text_mode_t mode) {
  switch (mode) {
    case TEXT_LCD:     return "LCD";
    case TEXT_ATARI:   return "ATARI";
    default:           return "?";
  }
}

static int skope_try_connect(skope_t *s) {
  if (scope_ipc_reader_open(&s->reader, s->name) != 0) return 0;
  s->connected     = 1;
  s->last_write_frame = shm_load64(&s->reader.header->write_frame);
  s->last_generation  = s->reader.header->generation;
  skope_ensure_sample_capacity(s, s->reader.header->capacity_frames);
  return 1;
}

static void skope_disconnect(skope_t *s) {
  if (s->connected) {
    scope_ipc_reader_close(&s->reader);
    s->connected = 0;
  }
}

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------

static double skope_now(void) {
#if defined(_WIN32)
  static LARGE_INTEGER freq = {0};
  if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
  LARGE_INTEGER now; QueryPerformanceCounter(&now);
  return (double)now.QuadPart / (double)freq.QuadPart;
#else
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + ts.tv_nsec * 1e-9;
#endif
}

static void skope_sleep(double sec) {
  if (sec <= 0) return;
#if defined(_WIN32)
  Sleep((DWORD)(sec * 1000.0));
#else
  struct timespec ts;
  ts.tv_sec  = (time_t)sec;
  ts.tv_nsec = (long)((sec - ts.tv_sec) * 1e9);
  nanosleep(&ts, NULL);
#endif
}

// ---------------------------------------------------------------------------
// Poll / acquire
// ---------------------------------------------------------------------------

static int skope_find_auto_sync(skope_t *s, const float *samples, int frames, int *out_idx, float *out_fract) {
  if (frames < 2) return 0;
  int ch = s->trig_pair * 2;
  float max_slope = 0.0f;
  int best_idx = -1;
  float best_fract = 0.0f;
  
  float max_val = 0.0f;
  for (int i = 0; i < frames; i++) {
      float v = fabsf(samples[(size_t)i * RECORD_CHANNELS + ch]);
      if (v > max_val) max_val = v;
  }
  if (max_val < 0.001f) return 0;
  
  int search_start = frames / 3;
  if (search_start < 1) search_start = 1;
  for (int i = search_start; i < frames; i++) {
      float prev = samples[(size_t)(i-1) * RECORD_CHANNELS + ch];
      float cur  = samples[(size_t)i     * RECORD_CHANNELS + ch];
      if (prev <= 0.0f && cur >= 0.0f) {
          float slope = cur - prev;
          if (slope > max_slope) {
              max_slope = slope;
              best_idx = i;
              best_fract = (0.0f - prev) / (slope + 1e-6f);
          }
      }
  }
  
  if (best_idx > 0) {
      if (out_idx) *out_idx = best_idx;
      if (out_fract) *out_fract = best_fract;
      return 1;
  }
  return 0;
}

static int skope_find_trigger(skope_t *s, const float *samples,
                               int frames, int *out_idx, float *out_fract) {
  if (frames < 2) return 0;
  int ch = s->trig_pair * 2;   // L channel of trigger pair
  float level = s->trig_level;
  int search_start = frames / 3;
  if (search_start < 1) search_start = 1;
  for (int i = search_start; i < frames; i++) {
    float prev = samples[(size_t)(i-1) * RECORD_CHANNELS + ch];
    float cur  = samples[(size_t)i     * RECORD_CHANNELS + ch];
    if (s->trig_edge == EDGE_RISING  && prev <= level && cur >= level) {
      if (out_idx) *out_idx = i;
      if (out_fract) *out_fract = (level - prev) / (cur - prev + 1e-6f);
      return 1;
    }
    if (s->trig_edge == EDGE_FALLING && prev >= level && cur <= level) {
      if (out_idx) *out_idx = i;
      if (out_fract) *out_fract = (prev - level) / (prev - cur + 1e-6f);
      return 1;
    }
  }
  return 0;
}

static int skope_poll(skope_t *s) {
  double now = skope_now();

  if (!s->connected) {
    if (now - s->last_connect_attempt < SKOPE_RECONNECT_S) return 0;
    s->last_connect_attempt = now;
    if (!skope_try_connect(s)) return 0;
  }

  if (!s->reader.header) { skope_disconnect(s); return 0; }

  if (shm_load64(&s->reader.header->active) == 0) {
    skope_disconnect(s);
    s->last_connect_attempt = -1e9;
    return 0;
  }

  uint64_t gen = s->reader.header->generation;
  if (gen != s->last_generation) {
    skope_disconnect(s);
    s->last_connect_attempt = -1e9;
    return 0;
  }

  if (s->paused && !s->view_dirty) return 0;

  uint64_t wf = shm_load64(&s->reader.header->write_frame);
  if (wf == s->last_write_frame && !s->view_dirty) return 0;
  s->last_write_frame = wf;

  uint32_t capacity = s->reader.header->capacity_frames;
  if (!skope_ensure_sample_capacity(s, capacity)) return 0;

  uint32_t available = wf < capacity ? (uint32_t)wf : capacity;
  if (available < 2) return 0;
  uint64_t oldest = wf - available;

  int win_i = frames_for_window(s, (int)available);
  uint32_t win = (uint32_t)win_i;
  uint64_t max_offset = available > win ? (uint64_t)(available - win) : 0;
  if (s->view_offset_frames > max_offset)
    s->view_offset_frames = max_offset;

  // We need to capture more than 'win' frames so we have room to shift the window
  // for trigger alignment. We grab win * 2 frames if possible.
  uint32_t fetch = win * 2;
  if (fetch > capacity) fetch = capacity;
  
  uint64_t visible_end = wf - s->view_offset_frames;
  if (visible_end < oldest + fetch) visible_end = oldest + fetch;
  if (visible_end > wf) visible_end = wf;
  uint64_t first = visible_end - fetch;

  int count = skope_reader_window(&s->reader, first, fetch, s->scratch);
  if (count <= 0) return 0;

  // Trigger alignment only applies to the live edge of the buffer. Once the
  // user scrolls back, the visible window is treated as a memory view.
  int trig_idx = 0;
  float trig_fract = 0.0f;
  int have_trig = 0;
  
  if (s->trig_mode == TRIG_AUTO) {
      have_trig = skope_find_auto_sync(s, s->scratch, count, &trig_idx, &trig_fract);
  } else {
      have_trig = skope_find_trigger(s, s->scratch, count, &trig_idx, &trig_fract);
  }

  if (s->view_offset_frames == 0 &&
      (s->trig_mode == TRIG_NORMAL || s->trig_mode == TRIG_SINGLE)) {
    if (!have_trig) return 0;
    if (s->trig_mode == TRIG_SINGLE && !s->armed) return 0;
    if (now - s->last_trig_time < (double)s->trig_holdoff_s) return 0;
    s->last_trig_time = now;
    if (s->trig_mode == TRIG_SINGLE) s->armed = 0;
  }

  int src_offset = 0;
  if (s->view_offset_frames == 0 && have_trig) {
    // Anchor trigger at 20% of the display window (0.2 * win)
    int lead = (int)(0.2f * win);
    src_offset = trig_idx - lead;
    if (src_offset < 0) src_offset = 0;
  }
  
  // We only ever need to copy exactly 'win' frames to the trace
  int copy_count = count - src_offset;
  if (copy_count > win) copy_count = win;

  int next = (s->history_head + 1) % SKOPE_TRACE_HISTORY;
  trace_t *dst = &s->history[next];
  if (copy_count > 0) {
    memcpy(dst->samples,
           s->scratch + (size_t)src_offset * RECORD_CHANNELS,
           (size_t)copy_count * RECORD_CHANNELS * sizeof(float));
  }
  dst->frame_count = copy_count;
  dst->first_frame = first + (uint64_t)src_offset;
  dst->captured_at = now;
  dst->valid = 1;
  dst->trig_fract = have_trig ? trig_fract : 0.0f;
  dst->trig_idx = have_trig ? (trig_idx - src_offset) : 0;

  s->history_head = next;
  if (s->history_count < SKOPE_TRACE_HISTORY) s->history_count++;
  s->view_dirty = 0;
  return 1;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

static int skope_handle_input(skope_t *s) {
  // Detect any activity up front. IsKeyDown covers held keys (arrows, comma,
  // period). For the rest, individual IsKeyPressed calls below set changed=1.
  // Window resize/move is detected via raylib's IsWindowResized().
  int held = IsKeyDown(KEY_UP)     || IsKeyDown(KEY_DOWN) ||
             IsKeyDown(KEY_COMMA)  || IsKeyDown(KEY_PERIOD);
  int changed = held || IsWindowResized();
  // 0-4: toggle pairs/tracks
  for (int p = 0; p < SKOPE_NUM_PAIRS; p++) {
    if (IsKeyPressed(KEY_ZERO + p)) {
      s->pairs[p].enabled = !s->pairs[p].enabled;
      changed = 1;
    }
  }

  // [ / ]: cycle selected pair
  if (IsKeyPressed(KEY_LEFT_BRACKET)) {
    s->selected_pair = (s->selected_pair - 1 + SKOPE_NUM_PAIRS) % SKOPE_NUM_PAIRS;
    changed = 1;
  }
  if (IsKeyPressed(KEY_RIGHT_BRACKET)) {
    s->selected_pair = (s->selected_pair + 1) % SKOPE_NUM_PAIRS;
    changed = 1;
  }

  // + / - : amplitude scale for selected pair
  if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)) {
    s->pairs[s->selected_pair].volts_per_div *= 0.5f;
    if (s->pairs[s->selected_pair].volts_per_div < 0.001f)
      s->pairs[s->selected_pair].volts_per_div = 0.001f;
    changed = 1;
  }
  if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) {
    s->pairs[s->selected_pair].volts_per_div *= 2.0f;
    if (s->pairs[s->selected_pair].volts_per_div > 100.f)
      s->pairs[s->selected_pair].volts_per_div = 100.f;
    changed = 1;
  }

  // , / .: vertical offset for selected pair (continuous while held)
  // GetFrameTime() returns 0 when SetTargetFPS(0) and no frame has run yet —
  // use a fixed step rate based on elapsed time instead.
  {
    static double last_pos_t = 0;
    double now_pos = skope_now();
    float dt = (float)(now_pos - last_pos_t);
    if (last_pos_t == 0) dt = 0.0f;
    if (dt > 0.1f) dt = 0.1f;
    last_pos_t = now_pos;
    float pos_step = dt * 4.0f;
    if (IsKeyDown(KEY_COMMA))  { s->pairs[s->selected_pair].offset_div -= pos_step; changed = 1; }
    if (IsKeyDown(KEY_PERIOD)) { s->pairs[s->selected_pair].offset_div += pos_step; changed = 1; }
  }

  // Trigger mode: T
  if (IsKeyPressed(KEY_T)) {
    s->trig_mode = (trig_mode_t)((s->trig_mode + 1) % TRIG_COUNT);
    if (s->trig_mode == TRIG_SINGLE) s->armed = 1;
    changed = 1;
  }

  // Trigger edge: E
  if (IsKeyPressed(KEY_E)) {
    s->trig_edge = (trig_edge_t)((s->trig_edge + 1) % EDGE_COUNT);
    changed = 1;
  }

  // Trigger pair: F1-F5
  for (int p = 0; p < SKOPE_NUM_PAIRS; p++) {
    if (IsKeyPressed(KEY_F1 + p)) { s->trig_pair = p; changed = 1; }
  }

  // Trigger level: UP/DOWN (held keys — already counted in `held` above)
  {
    static double last_trig_t = 0;
    double now_trig = skope_now();
    float dt = (float)(now_trig - last_trig_t);
    if (last_trig_t == 0) dt = 0.0f;
    if (dt > 0.1f) dt = 0.1f;
    last_trig_t = now_trig;

    float lstep = (IsKeyDown(KEY_LEFT_SHIFT)||IsKeyDown(KEY_RIGHT_SHIFT))
      ? 1.0f : 0.2f;
    if (IsKeyDown(KEY_UP))   { s->trig_level += lstep * dt; changed = 1; }
    if (IsKeyDown(KEY_DOWN)) { s->trig_level -= lstep * dt; changed = 1; }
    if (s->trig_level >  2.f) s->trig_level =  2.f;
    if (s->trig_level < -2.f) s->trig_level = -2.f;
  }

  // Re-arm: A
  if (IsKeyPressed(KEY_A)) { s->armed = 1; changed = 1; }

  // Holdoff: H / shift+H
  if (IsKeyPressed(KEY_H)) {
    int shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    s->trig_holdoff_s += shift ? -0.001f : 0.001f;
    if (s->trig_holdoff_s < 0) s->trig_holdoff_s = 0;
    changed = 1;
  }

  // Time/div: LEFT/RIGHT. Shift+LEFT/RIGHT scrolls the visible window
  // through the shared-memory buffer instead.
  int shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
  if (shift && (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_RIGHT))) {
    uint32_t capacity = s->connected && s->reader.header
      ? s->reader.header->capacity_frames : s->sample_capacity;
    uint64_t wf = s->connected && s->reader.header
      ? shm_load64(&s->reader.header->write_frame) : 0;
    uint32_t available = wf < capacity ? (uint32_t)wf : capacity;
    int win_i = frames_for_window(s, available > 1 ? (int)available : 2);
    uint64_t max_offset = available > (uint32_t)win_i
      ? (uint64_t)(available - (uint32_t)win_i) : 0;
    uint64_t step = (uint64_t)(win_i / 4);
    if (step < 1) step = 1;

    if (IsKeyPressed(KEY_LEFT)) {
      uint64_t next = s->view_offset_frames + step;
      s->view_offset_frames = next > max_offset ? max_offset : next;
    }
    if (IsKeyPressed(KEY_RIGHT)) {
      s->view_offset_frames = s->view_offset_frames > step
      ? s->view_offset_frames - step : 0;
    }
    s->view_dirty = 1;
    changed = 1;
  } else if (IsKeyPressed(KEY_RIGHT)) {
    s->time_per_div_s *= 2.0f;
    if (s->time_per_div_s > 1.0f) s->time_per_div_s = 1.0f;
    s->view_offset_frames = 0;
    s->view_dirty = 1;
    changed = 1;
  } else if (IsKeyPressed(KEY_LEFT)) {
    s->time_per_div_s *= 0.5f;
    if (s->time_per_div_s < 0.00005f) s->time_per_div_s = 0.00005f;
    s->view_offset_frames = 0;
    s->view_dirty = 1;
    changed = 1;
  }

  // View mode: V
  if (IsKeyPressed(KEY_V)) {
    if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
        s->view_mode = (view_mode_t)((s->view_mode + 1) % VIEW_COUNT);
    } else {
        s->pairs[s->selected_pair].viz_mode = (pair_viz_mode_t)((s->pairs[s->selected_pair].viz_mode + 1) % VIZ_COUNT);
    }
    changed = 1;
  }

  // Persistence: P
  if (IsKeyPressed(KEY_P)) { s->persistence = !s->persistence; changed = 1; }

  // Pause: SPACE
  if (IsKeyPressed(KEY_SPACE)) { s->paused = !s->paused; changed = 1; }

  // Grid: G
  if (IsKeyPressed(KEY_G)) { s->show_grid = !s->show_grid; changed = 1; }

  // L — toggle light/dark theme
  if (IsKeyPressed(KEY_L)) {
    s->dark_mode = !s->dark_mode;
    s->theme = s->dark_mode ? &kThemeDark : &kThemeLight;
    changed = 1;
  }

  // K — cycle bitmap LCD and Atari vector-style stroke font
  if (IsKeyPressed(KEY_K)) {
    s->text_mode = (text_mode_t)((s->text_mode + 1) % TEXT_COUNT);
    changed = 1;
  }

  // HUD: D
  if (IsKeyPressed(KEY_D)) { s->show_hud = !s->show_hud; changed = 1; }

  // Help: /
  if (IsKeyPressed(KEY_SLASH)) { s->show_help = !s->show_help; changed = 1; }

  // Reset: R
  if (IsKeyPressed(KEY_R)) {
    for (int p = 0; p < SKOPE_NUM_PAIRS; p++) {
      s->pairs[p].volts_per_div = 0.2f;
      s->pairs[p].offset_div    = 0.0f;
    }
    s->time_per_div_s = 0.005f;
    s->trig_level     = 0.0f;
    s->view_offset_frames = 0;
    s->view_dirty = 1;
    changed = 1;
  }

  // Quit: Q
  if (IsKeyPressed(KEY_Q)) SetExitKey(KEY_Q);

  return changed;
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------

static void skope_draw_grid(Rectangle r, int divs_x, int divs_y,
                             float dpi_scale, const skope_theme_t *th) {
  float sx = r.width  / divs_x;
  float sy = r.height / divs_y;

  for (int i = 0; i <= divs_x; i++) {
    float x = r.x + i * sx;
    Color c = (i == divs_x/2) ? th->grid_major : th->grid_minor;
    DrawLine((int)x, (int)r.y, (int)x, (int)(r.y + r.height), c);
  }
  for (int j = 0; j <= divs_y; j++) {
    float y = r.y + j * sy;
    Color c = (j == divs_y/2) ? th->grid_major : th->grid_minor;
    DrawLine((int)r.x, (int)y, (int)(r.x + r.width), (int)y, c);
  }

  float cx = r.x + r.width  / 2.0f;
  float cy = r.y + r.height / 2.0f;
  int   tk = (int)(4 * dpi_scale);
  Color tc = th->grid_tick;

  for (int i = 0; i <= divs_x * 5; i++) {
    float x = r.x + i * (sx / 5.0f);
    int h = (i % 5 == 0) ? tk : (int)(tk * 0.6f);
    DrawLine((int)x, (int)(cy - h), (int)x, (int)(cy + h), tc);
  }
  for (int j = 0; j <= divs_y * 5; j++) {
    float y = r.y + j * (sy / 5.0f);
    int h = (j % 5 == 0) ? tk : (int)(tk * 0.6f);
    DrawLine((int)(cx - h), (int)y, (int)(cx + h), (int)y, tc);
  }
}

// Compute how many frames to display given time/div, sample_rate, and
// available frames in the trace.
static int frames_for_window(skope_t *s, int available) {
  uint32_t rate = s->connected && s->reader.header
    ? s->reader.header->sample_rate : 44100;
  if (!rate) rate = 44100;
  int n = (int)(s->time_per_div_s * 10.0f * (float)rate);
  if (n < 2) n = 2;
  if (n > available) n = available;
  return n;
}

// Compute stereo correlation over count frames for a pair p.
// Returns value in [-1, +1].
static float stereo_correlation(const float *samples, int start,
                                 int count, int p) {
  if (count < 2) return 0.0f;
  double sl = 0, sr = 0, sll = 0, srr = 0, slr = 0;
  int chl = p * 2, chr = p * 2 + 1;
  for (int i = 0; i < count; i++) {
    double l = samples[(size_t)(start + i) * RECORD_CHANNELS + chl];
    double r = samples[(size_t)(start + i) * RECORD_CHANNELS + chr];
    sl  += l;  sr  += r;
    sll += l*l; srr += r*r; slr += l*r;
  }
  double n  = count;
  double ml = sl/n, mr = sr/n;
  double vl = sll/n - ml*ml;
  double vr = srr/n - mr*mr;
  double cv = slr/n - ml*mr;
  double denom = sqrt(vl * vr);
  if (denom < 1e-12) return (vl < 1e-12 && vr < 1e-12) ? 1.0f : 0.0f;
  float rho = (float)(cv / denom);
  if (rho >  1.f) rho =  1.f;
  if (rho < -1.f) rho = -1.f;
  return rho;
}

// Draw a correlation meter bar on the right edge of a band.
// bar_rect: thin vertical strip. rho in [-1,1].
static void draw_corr_meter(Rectangle bar, float rho, Color col) {
  // Recessed background
  DrawRectangleRec(bar, (Color){6, 10, 8, 220});
  // Hair-thin border in pair color at low alpha
  DrawRectangleLinesEx(bar, 1, (Color){col.r, col.g, col.b, 45});

  float cy     = bar.y + bar.height / 2.0f;
  float half   = bar.height / 2.0f;
  float fill_h = fabsf(rho) * half;

  // Positive correlation → P31 green. Negative → amber (danger).
  Color fill_col = rho >= 0
    ? (Color){ 40, 220,  90, 210}   // P31-ish green
    : (Color){230, 140,  20, 210};  // amber warning
  float fill_y = (rho >= 0) ? cy - fill_h : cy;
  DrawRectangle((int)bar.x, (int)fill_y,
                (int)bar.width, (int)fill_h, fill_col);

  // Centre reference tick
  DrawLine((int)bar.x, (int)cy, (int)(bar.x + bar.width), (int)cy,
           (Color){160, 170, 155, 90});
}

// Draw L+R waveforms with stereo-fill into a band rectangle.
//
//   band:       the rectangle for this pair's drawing area
//   samples:    full interleaved trace buffer (start offset already applied)
//   count:      number of frames to draw
//   start:      frame offset into samples[]
//   p:          pair index
//   vpd:        amplitude scale factor (normalised float units per division)
//   offset_div: vertical centre offset in divisions
//   div_h:      pixels per division inside this band
//   alpha:      overall opacity (for persistence layers)
static void draw_pair_waveforms(Rectangle band,
                                 const float *samples, int start, int count, int win,
                                 int p, float vpd, float offset_div,
                                 float div_h, float alpha,
                                 float dpi_scale, float fract_offset) {
  if (win < 2) return;

  int chl = p * 2;
  int chr = p * 2 + 1;
  float mid_y = band.y + band.height / 2.0f;
  float off_px = offset_div * div_h;

  Color col_l    = color_alpha(kPairColor[p], alpha);
  Color col_r    = color_alpha(pair_color_r(p), alpha);
  // Fill between L and R: use pair color at low alpha
  Color col_fill = pair_color_fill(p, (unsigned char)(38 * alpha));
  float lw = fmaxf(1.0f, 1.5f * dpi_scale);

  // We draw filled triangles between successive (x, yL, yR) samples.
  // To avoid per-pixel triangle calls, we use DrawTriangle for each pair
  // of adjacent frames. This gives a solid ribbon where the stereo spread
  // is immediately visible as the ribbon width.
  int n = win;
  for (int i = 0; i < n - 1; i++) {
    int s0 = start + i, s1 = start + i + 1;
    float fl0 = 0.0f, fr0 = 0.0f, fl1 = 0.0f, fr1 = 0.0f;
    if (i < count) {
      fl0 = samples[(size_t)s0 * RECORD_CHANNELS + chl];
      fr0 = samples[(size_t)s0 * RECORD_CHANNELS + chr];
    }
    if (i + 1 < count) {
      fl1 = samples[(size_t)s1 * RECORD_CHANNELS + chl];
      fr1 = samples[(size_t)s1 * RECORD_CHANNELS + chr];
    }

    float x0 = band.x + (band.width * ((float)i - fract_offset))     / (float)(win > 1 ? win - 1 : 1);
    float x1 = band.x + (band.width * ((float)(i+1) - fract_offset)) / (float)(win > 1 ? win - 1 : 1);

    float yl0 = mid_y - off_px - (fl0 / vpd) * div_h;
    float yr0 = mid_y - off_px - (fr0 / vpd) * div_h;
    float yl1 = mid_y - off_px - (fl1 / vpd) * div_h;
    float yr1 = mid_y - off_px - (fr1 / vpd) * div_h;

    // Clip to band
    float clip_top = band.y, clip_bot = band.y + band.height;
    #define CLIP(y) ((y) < clip_top ? clip_top : (y) > clip_bot ? clip_bot : (y))
    yl0 = CLIP(yl0); yr0 = CLIP(yr0);
    yl1 = CLIP(yl1); yr1 = CLIP(yr1);
    #undef CLIP

    // Fill quad as two triangles (L and R define the ribbon)
    // Winding must be counter-clockwise for raylib DrawTriangle
    if (col_fill.a > 0) {
      // Triangle 1: (x0,yL0), (x0,yR0), (x1,yL1)
      DrawTriangle((Vector2){x0, yl0}, (Vector2){x1, yl1},
                   (Vector2){x0, yr0}, col_fill);
      // Triangle 2: (x1,yL1), (x0,yR0), (x1,yR1)
      DrawTriangle((Vector2){x1, yl1}, (Vector2){x1, yr1},
                   (Vector2){x0, yr0}, col_fill);
    }

    // Analog-style glow pass
    DrawLineEx((Vector2){x0, yl0}, (Vector2){x1, yl1}, lw * 3.0f, color_alpha(col_l, alpha * 0.25f));
    DrawLineEx((Vector2){x0, yr0}, (Vector2){x1, yr1}, fmaxf(1.0f, lw * 2.0f), color_alpha(col_r, alpha * 0.25f));
    // Core line pass
    DrawLineEx((Vector2){x0, yl0}, (Vector2){x1, yl1}, lw, col_l);
    DrawLineEx((Vector2){x0, yr0}, (Vector2){x1, yr1}, fmaxf(1.0f, lw * 0.7f), col_r);
  }
}

// tek_text / tek_measure — draw and measure text using the selected scope
// readout font. The wrappers keep layout code independent of the current
// rendering mode.
static void tek_text(const char *txt, int x, int y, int size, Color col) {
  (void)size; // most callers pass g_hd_cell_h; keep the API stable.
  if (g_text_mode == TEXT_ATARI) {
    avf_text(txt, x, y, g_hd_cell_h, col);
  } else {
    hd_draw(g_hd_font, txt, x, y, col);
  }
}

static int tek_measure(const char *txt) {
  if (g_text_mode == TEXT_ATARI) return avf_measure(txt, g_hd_cell_h);
  return hd_measure(g_hd_font, txt);
}

// Small filled right-pointing triangle — Tek-style channel marker on left edge.
// tip_x, mid_y: the rightmost point (tip) of the triangle.
static void tek_channel_marker(float tip_x, float mid_y, float size, Color col) {
  float h = size;
  float w = size * 0.8f;
  DrawTriangle(
    (Vector2){tip_x,     mid_y},
    (Vector2){tip_x - w, mid_y - h/2},
    (Vector2){tip_x - w, mid_y + h/2},
    col);
}

static void skope_draw_stacked(skope_t *s, const trace_t *t,
                                Rectangle plot, float alpha) {
  if (!t->valid || t->frame_count < 2) return;

  int active = 0;
  for (int p = 0; p < SKOPE_NUM_PAIRS; p++) if (s->pairs[p].enabled) active++;
  if (!active) return;

  uint32_t rate = s->connected && s->reader.header
    ? s->reader.header->sample_rate : 44100;

  int win = frames_for_window(s, t->frame_count);
  int start;
  if (s->trig_mode != TRIG_AUTO || t->trig_idx > 0) {
      start = t->trig_idx - (int)(0.2f * win);
  } else {
      start = t->frame_count / 3 - (int)(0.2f * win);
  }
  if (start < 0) start = 0;
  int count = win;
  if (start + count > t->frame_count) count = t->frame_count - start;

  float corr_bar_w = 12 * s->dpi_x;
  float usable_w   = plot.width - corr_bar_w - 2;
  float band_h     = plot.height / (float)active;
  float div_h      = band_h / 8.0f;

  int band_idx = 0;
  for (int p = 0; p < SKOPE_NUM_PAIRS; p++) {
    if (!s->pairs[p].enabled) continue;

    float band_y = plot.y + band_idx * band_h;

    // Band separator — thin, Tek grid-line style (not a glowing green bar)
    if (band_idx > 0)
      DrawLine((int)plot.x, (int)band_y,
               (int)(plot.x + plot.width), (int)band_y,
               TH(s, grid_major));

    Rectangle band = {plot.x, band_y, usable_w, band_h};

    float vpd = s->pairs[p].volts_per_div;
    if (s->pairs[p].viz_mode == VIZ_TIME) {
      if (s->show_grid) skope_draw_grid(band, 10, 8, s->dpi_y, s->theme);
      draw_pair_waveforms(band, t->samples, start, count, win,
                          p, vpd, s->pairs[p].offset_div,
                          div_h, alpha, s->dpi_y, t->trig_fract);
    } else {
      if (s->show_grid) skope_draw_grid(band, 8, 8, s->dpi_y, s->theme);
      float rot = (s->pairs[p].viz_mode == VIZ_XY_ROT) ? (float)GetTime() * 1.5f : 0.0f;
      draw_lissajous_cell(s, t, band, p, alpha, rot);
    }

    // --- Band labels and readout (Tek-style) --------------------------------
    // All spacing uses g_hd_cell_h (actual rendered glyph height) and
    // g_hd_cell_w (actual rendered glyph width). The fsz param to tek_text
    // is ignored; using it for layout was the source of all stacking bugs.

    int fh = g_hd_cell_h;   // actual rendered text height
    int fw = g_hd_cell_w;   // actual rendered character width

    Color lcol = color_alpha(kPairColor[p], alpha);
    Color rcol = color_alpha(pair_color_r(p), alpha);

    float mkr_sz = (float)(fh);
    int row0_y = (int)(band_y + 2);
    const char *pname = skope_pair_name(s, p);
    
    if (s->pairs[p].viz_mode == VIZ_TIME) {
      float dc_y = band_y + band_h / 2.0f - s->pairs[p].offset_div * div_h;
      dc_y = fmaxf(band_y + 2, fminf(band_y + band_h - 2, dc_y));
      
      tek_channel_marker(plot.x + mkr_sz + 2, dc_y, mkr_sz, lcol);
      
      tek_text(pname, (int)(plot.x + mkr_sz + fw), row0_y, fh, lcol);
      int lbl_w = tek_measure(pname);
      int leg_x = (int)(plot.x + mkr_sz + fw) + lbl_w + fw;
      tek_text("L", leg_x,            row0_y, fh, lcol);
      tek_text("R", leg_x + fw * 2,   row0_y, fh, rcol);
    } else {
      tek_text(pname, (int)(plot.x + 2), row0_y, fh, lcol);
    }

    // Scale annotation — right-aligned in the usable band width.
    // We don't know the physical unit (V, dBu, etc.) — skred hasn't
    // told us. Show the raw normalised scale factor plus its dBFS
    // equivalent (20*log10(vpd)). Forward-compatible: when skred
    // sends units, replace the format string here.
    char scale_str[32];
    float dbfs = (vpd > 0.0f) ? 20.0f * log10f(vpd) : -99.0f;
    if (dbfs < -99.0f) dbfs = -99.0f;
    float track_db = skope_pair_db(s, p);
    if (isfinite(track_db))
      snprintf(scale_str, sizeof(scale_str), "VOL%+.0fdB %.2f/D",
               track_db, vpd);
    else
      snprintf(scale_str, sizeof(scale_str), "%.2f/D(%.0fdB)", vpd, dbfs);
    int vw = tek_measure(scale_str);
    tek_text(scale_str, (int)(band.x + usable_w - vw - fw),
             row0_y, fh, color_alpha(TH(s, p31_mid), alpha));

    // Row 1: correlation value — only if band is tall enough for two rows
    if (s->pairs[p].viz_mode == VIZ_TIME && band_h >= (float)(fh * 2 + 6)) {
      int row1_y = row0_y + fh + 2;
      float corr = stereo_correlation(t->samples, start, count, p);
      char corr_str[24];
      snprintf(corr_str, sizeof(corr_str), "RHO %.2f", corr);
      Color corr_col = corr >= 0.7f
        ? color_alpha(TH(s, p31_bright), alpha)
        : corr >= 0.0f
          ? color_alpha(TH(s, amber), alpha)
          : color_alpha((Color){220, 80, 60, 255}, alpha);
      tek_text(corr_str, (int)(plot.x + mkr_sz + fw),
               row1_y, fh, corr_col);
    }

    // Correlation meter bar on the right edge
    float corr2 = stereo_correlation(t->samples, start, count, p);
    Rectangle cbar = {plot.x + usable_w + 2, band_y + 2,
                      corr_bar_w - 2, band_h - 4};
    draw_corr_meter(cbar, corr2, kPairColor[p]);

    // Selected pair highlight:
    //   1. Faint full-width tint across the entire band (like a Tek cursor band)
    //   2. A solid 3px left-edge bar in the pair color
    //   3. A ">SEL" label replacing the normal pair name brightness
    if (p == s->selected_pair) {
      // Tinted band — pair color at very low alpha, covers the full band
      Color hl = color_alpha(kPairColor[p], 0.07f);
      DrawRectangle((int)plot.x, (int)band_y,
                    (int)(plot.width), (int)band_h, hl);
      // Solid left-edge bar: 4px, bright pair color
      DrawRectangle((int)plot.x, (int)band_y,
                    4, (int)band_h,
                    color_alpha(kPairColor[p], 0.85f));
      // Right-edge hairline to frame the selection
      DrawLine((int)(plot.x + plot.width - 1), (int)band_y,
               (int)(plot.x + plot.width - 1), (int)(band_y + band_h),
               color_alpha(kPairColor[p], 0.35f));
    }

    // Trigger indicator on the trigger pair
    if (p == s->trig_pair && s->trig_mode != TRIG_AUTO) {
      float mid_y2 = band_y + band_h / 2.0f;
      float ty = mid_y2 - s->pairs[p].offset_div * div_h
                        - (s->trig_level / vpd) * div_h;
      if (ty >= band_y && ty <= band_y + band_h) {
        for (float x = plot.x; x < plot.x + usable_w; x += 10.f)
          DrawLine((int)x, (int)ty, (int)(x+5), (int)ty, TH(s, amber));
        const char *arrow = s->trig_edge == EDGE_RISING ? "T/" : "T\\";
        // Place label just above the trigger line; use g_hd_cell_h for offset
        tek_text(arrow,
                 (int)(plot.x + mkr_sz + fw),
                 (int)(ty - g_hd_cell_h - 1),
                 g_hd_cell_h, TH(s, amber));
      }
    }

    band_idx++;
    (void)rate;
  }
}

// ---------------------------------------------------------------------------
// OVERLAY view
// ---------------------------------------------------------------------------

static void skope_draw_overlay(skope_t *s, const trace_t *t,
                                Rectangle plot, float alpha) {
  if (!t->valid || t->frame_count < 2) return;

  int win   = frames_for_window(s, t->frame_count);
  int start;
  if (s->trig_mode != TRIG_AUTO || t->trig_idx > 0) {
      start = t->trig_idx - (int)(0.2f * win);
  } else {
      start = t->frame_count / 3 - (int)(0.2f * win);
  }
  if (start < 0) start = 0;
  int count = win;
  if (start + count > t->frame_count) count = t->frame_count - start;

  float div_h = plot.height / 8.0f;

  for (int p = 0; p < SKOPE_NUM_PAIRS; p++) {
    if (!s->pairs[p].enabled) continue;
    float vpd = s->pairs[p].volts_per_div;
    if (vpd < 1e-5f) vpd = 1e-5f;
    if (s->pairs[p].viz_mode == VIZ_TIME) {
      draw_pair_waveforms(plot, t->samples, start, count, win,
                          p, vpd, s->pairs[p].offset_div,
                          div_h, alpha, s->dpi_y, t->trig_fract);
    } else {
      float rot = (s->pairs[p].viz_mode == VIZ_XY_ROT) ? (float)GetTime() * 1.5f : 0.0f;
      draw_lissajous_cell(s, t, plot, p, alpha, rot);
    }
  }

  // Trigger line — amber dashed horizontal + edge marker
  if (s->trig_mode != TRIG_AUTO && s->pairs[s->trig_pair].enabled) {
    int p = s->trig_pair;
    float div_h2 = plot.height / 8.0f;
    float mid_y  = plot.y + plot.height / 2.0f;
    float ty = mid_y
      - s->pairs[p].offset_div * div_h2
      - (s->trig_level / s->pairs[p].volts_per_div) * div_h2;
    for (float x = plot.x; x < plot.x + plot.width; x += 10.f)
      DrawLine((int)x, (int)ty, (int)(x+5), (int)ty, TH(s, amber));
    const char *arr = s->trig_edge == EDGE_RISING ? "T/" : "T\\";
    tek_text(arr, (int)(plot.x + g_hd_cell_w),
             (int)(ty - g_hd_cell_h - 1),
             g_hd_cell_h, TH(s, amber));
  }
}

// ---------------------------------------------------------------------------
// LISSAJOUS view
// ---------------------------------------------------------------------------

// Draw one X-Y phase plot for pair p into cell.
static void draw_lissajous_cell(skope_t *s, const trace_t *t,
                                 Rectangle cell, int p, float alpha, float rot) {
  int win   = frames_for_window(s, t->frame_count);
  int start;
  if (s->trig_mode != TRIG_AUTO || t->trig_idx > 0) {
      start = t->trig_idx - (int)(0.2f * win);
  } else {
      start = t->frame_count / 3 - (int)(0.2f * win);
  }
  if (start < 0) start = 0;
  int count = win;
  if (start + count > t->frame_count) count = t->frame_count - start;
  if (win < 2) return;

  int chl = p * 2, chr = p * 2 + 1;
  float vpd = s->pairs[p].volts_per_div;
  if (vpd < 1e-5f) vpd = 1e-5f;
  float scale = (cell.width < cell.height ? cell.width : cell.height) / 2.0f
                / vpd;
  float cx = cell.x + cell.width  / 2.0f;
  float cy = cell.y + cell.height / 2.0f;

  float rc = cosf(rot), rs = sinf(rot);

  Color col  = color_alpha(kPairColor[p], alpha * 0.85f);
  Color dotc = color_alpha(kPairColor[p], alpha);
  float lw   = fmaxf(1.0f, s->dpi_y);

  Vector2 prev = {0}; int hp = 0;
  for (int i = 0; i < count; i++) {
    int si = start + i;
    float l = t->samples[(size_t)si * RECORD_CHANNELS + chl];
    float r = t->samples[(size_t)si * RECORD_CHANNELS + chr];
    float rl = l * rc - r * rs;
    float rr = l * rs + r * rc;
    Vector2 cur = {cx + rl * scale, cy - rr * scale};
    // Clip to cell
    float cl = cell.x, cr2 = cell.x+cell.width;
    float ct = cell.y, cb  = cell.y+cell.height;
    if (cur.x<cl||cur.x>cr2||cur.y<ct||cur.y>cb) { hp=0; continue; }
    if (hp) DrawLineEx(prev, cur, lw, col);
    prev = cur; hp = 1;
  }

  // Most recent dot
  if (count > 0) {
    int si = start + count - 1;
    float l = t->samples[(size_t)si * RECORD_CHANNELS + chl];
    float r = t->samples[(size_t)si * RECORD_CHANNELS + chr];
    float rl = l * rc - r * rs;
    float rr = l * rs + r * rc;
    Vector2 dot = {cx + rl * scale, cy - rr * scale};
    DrawCircleV(dot, 2.5f * s->dpi_x, dotc);
  }

  // Axes — faint grid-line color, not glowing
  DrawLine((int)cell.x, (int)cy, (int)(cell.x+cell.width), (int)cy,
           TH(s, grid_major));
  DrawLine((int)cx, (int)cell.y, (int)cx, (int)(cell.y+cell.height),
           TH(s, grid_major));

  // 45° reference lines (mono = diagonal, anti-phase = anti-diagonal)
  float half = (cell.width < cell.height ? cell.width : cell.height) / 2.0f;
  DrawLine((int)(cx-half),(int)(cy-half),(int)(cx+half),(int)(cy+half),
           (Color){TH(s, grid_major).r, TH(s, grid_major).g, TH(s, grid_major).b, 35});
  DrawLine((int)(cx-half),(int)(cy+half),(int)(cx+half),(int)(cy-half),
           (Color){TH(s, grid_major).r, TH(s, grid_major).g, TH(s, grid_major).b, 20});

  // Correlation readout — bottom-left of cell
  float corr = stereo_correlation(t->samples, start, count, p);
  char cs[20]; snprintf(cs, sizeof(cs), "RHO %.2f", corr);
  Color ccol = corr >= 0.7f
    ? TH(s, p31_bright)
    : corr >= 0.0f
      ? TH(s, amber)
      : (Color){220, 70, 50, 255};
  tek_text(cs,
           (int)(cell.x + g_hd_cell_w),
           (int)(cell.y + cell.height - g_hd_cell_h - 3),
           g_hd_cell_h, ccol);

}



static void skope_draw_grid_layout(skope_t *s, const trace_t *t, Rectangle plot, float alpha) {
  if (!t->valid || t->frame_count < 2) return;

  int win   = frames_for_window(s, t->frame_count);
  int start;
  if (s->trig_mode != TRIG_AUTO || t->trig_idx > 0) {
      start = t->trig_idx - (int)(0.2f * win);
  } else {
      start = t->frame_count / 3 - (int)(0.2f * win);
  }
  if (start < 0) start = 0;
  int count = win;
  if (start + count > t->frame_count) count = t->frame_count - start;

  int active = 0;
  for (int p = 0; p < SKOPE_NUM_PAIRS; p++) if (s->pairs[p].enabled) active++;
  if (!active) return;

  int cols = active <= 2 ? active : (active <= 4 ? 2 : 3);
  int rows = (active + cols - 1) / cols;

  float cell_w = plot.width  / (float)cols;
  float cell_h = plot.height / (float)rows;
  float cell_sz = cell_w < cell_h ? cell_w : cell_h;

  float grid_w = cell_sz * cols;
  float grid_h = cell_sz * rows;
  float ox = plot.x + (plot.width  - grid_w) / 2.0f;
  float oy = plot.y + (plot.height - grid_h) / 2.0f;

  int col = 0, row = 0;
  for (int p = 0; p < SKOPE_NUM_PAIRS; p++) {
    if (!s->pairs[p].enabled) continue;

    float pad = 4.0f * s->dpi_x;
    Rectangle cell = {
      ox + col * cell_sz + pad,
      oy + row * cell_sz + pad,
      cell_sz - pad * 2,
      cell_sz - pad * 2
    };

    DrawRectangleRec(cell, TH(s, crt_bg));
    Color border_col = (Color){kPairColor[p].r, kPairColor[p].g,
                                kPairColor[p].b, 55};
    DrawRectangleLinesEx(cell, 1, border_col);

    float vpd = s->pairs[p].volts_per_div;
    if (s->pairs[p].viz_mode == VIZ_TIME) {
      if (s->show_grid) skope_draw_grid(cell, 10, 8, s->dpi_y, s->theme);
      float div_h = cell.height / 8.0f;
      draw_pair_waveforms(cell, t->samples, start, count, win,
                          p, vpd, s->pairs[p].offset_div,
                          div_h, alpha, s->dpi_y, t->trig_fract);
    } else {
      if (s->show_grid) skope_draw_grid(cell, 8, 8, s->dpi_y, s->theme);
      float rot = (s->pairs[p].viz_mode == VIZ_XY_ROT) ? (float)GetTime() * 1.5f : 0.0f;
      draw_lissajous_cell(s, t, cell, p, alpha, rot);
    }

    // Name and scale in corner
    int fh = g_hd_cell_h;
    Color lcol = color_alpha(kPairColor[p], alpha);
    int row0_y = (int)(cell.y + 2);
    const char *pname = skope_pair_name(s, p);
    tek_text(pname, (int)(cell.x + 2), row0_y, fh, lcol);
    
    char scale_str[32];
    snprintf(scale_str, sizeof(scale_str), "%.2f/D", vpd);
    int vw = tek_measure(scale_str);
    tek_text(scale_str, (int)(cell.x + cell.width - vw - 2), row0_y, fh, color_alpha(TH(s, p31_mid), alpha));

    col++;
    if (col >= cols) { col = 0; row++; }
  }
}

// ---------------------------------------------------------------------------
// Master draw
// ---------------------------------------------------------------------------

static const char *view_name(view_mode_t v) {
  switch(v) {
    case VIEW_STACKED:       return "STACKED";
    case VIEW_OVERLAY:       return "OVERLAY";
    case VIEW_GRID:          return "GRID";
    default: return "?";
  }
}

static void skope_draw(skope_t *s) {
  int sw = GetScreenWidth(), sh = GetScreenHeight();
  g_text_mode = s->text_mode;   // sync once per frame for tek_text/tek_measure

  // -------------------------------------------------------------------------
  // Layout: bezel border + CRT area + HUD panel at the bottom
  //
  //   ┌─────────────────── bezel ──────────────────────────────┐
  //   │  ┌─────────────────── CRT ───────────────────────────┐ │
  //   │  │                                                    │ │
  //   │  └────────────────────────────────────────────────────┘ │
  //   │  ┌─────────────────── HUD ───────────────────────────┐ │
  //   │  │ softkey row                                        │ │
  //   │  └────────────────────────────────────────────────────┘ │
  //   └────────────────────────────────────────────────────────┘
  //
  // Bezel padding (scales with DPI)
  // -------------------------------------------------------------------------

  float bpad_x = 22.0f * s->dpi_x;   // left/right bezel width
  float bpad_t  = 48.0f * s->dpi_y;  // top bezel (title + buffer overview)
  float bpad_b  =  8.0f * s->dpi_y;  // bottom bezel (below HUD)
  // HUD height: 2 status rows + button row + gaps
  // g_hd_cell_h is available after InitWindow(), so this is safe.
  int gap3 = 3;
  int btn_row_h = g_hd_cell_h * 2 + 6;  // label + key hint with padding
  int hud_h = s->show_hud
    ? (g_hd_cell_h + gap3) * 2 + btn_row_h + gap3 * 3 + 2
    : 0;

  // CRT rectangle (inside bezel, above HUD)
  Rectangle crt = {
    bpad_x,
    bpad_t,
    (float)sw - bpad_x * 2.0f,
    (float)sh - bpad_t - bpad_b - (float)hud_h
  };

  // -------------------------------------------------------------------------
  // Fill the whole window with bezel color first
  // -------------------------------------------------------------------------
  ClearBackground(TH(s, bezel));

  // Brand name in the top-left of the bezel — "octetta · skope"
  // Centre of the bezel title row holds source/rate status.
  // Right side holds mode + armed/hold indicators.
  {
    int bx0 = (int)bpad_x;
    int by0 = (int)((bpad_t - g_hd_cell_h) / 2);  // vertically centred in bezel strip
    if (by0 < 1) by0 = 1;

    // Left: brand
    tek_text("octetta", bx0, by0, g_hd_cell_h, TH(s, p31_mid));
    int tw = tek_measure("octetta");
    DrawCircle(bx0 + tw + g_hd_cell_w, by0 + g_hd_cell_h / 2,
               2.0f * s->dpi_x, TH(s, p31_dim));
    tek_text("skope", bx0 + tw + g_hd_cell_w * 2, by0, g_hd_cell_h,
             (Color){TH(s,p31_dim).r, TH(s,p31_dim).g, TH(s,p31_dim).b, 160});

    // Centre: source name + sample rate (only when connected)
    if (s->connected && s->reader.header) {
      uint32_t rate = s->reader.header->sample_rate;
      char centre[48];
      snprintf(centre, sizeof(centre), "%.20s  %uHz",
               s->reader.name, rate);
      int cw = tek_measure(centre);
      tek_text(centre, sw / 2 - cw / 2, by0, g_hd_cell_h, TH(s, p31_dim));
    }

    // Right: theme indicator, then armed/hold status
    // Build right-side string: "DARK  SGL HELD" or "LITE  HOLD" etc.
    char right[48] = "";
    if (s->trig_mode == TRIG_SINGLE) {
      snprintf(right, sizeof(right), "%s  %s",
               s->armed ? "ARMED" : "HELD",
               s->dark_mode ? "DARK" : "LITE");
    } else if (s->paused) {
      snprintf(right, sizeof(right), "HOLD  %s",
               s->dark_mode ? "DARK" : "LITE");
    } else {
      snprintf(right, sizeof(right), "%s",
               s->dark_mode ? "DARK" : "LITE");
    }
    int rw = tek_measure(right);
    // Colour: amber if HELD or HOLD, else dim
    Color rc = (!s->armed && s->trig_mode == TRIG_SINGLE) || s->paused
      ? TH(s, amber) : TH(s, p31_dim);
    tek_text(right, (int)((float)sw - bpad_x - rw), by0, g_hd_cell_h, rc);
  }

  skope_draw_buffer_overview(s, (Rectangle){
    bpad_x,
    bpad_t - 18.0f * s->dpi_y,
    (float)sw - bpad_x * 2.0f,
    10.0f * s->dpi_y
  });

  // -------------------------------------------------------------------------
  // CRT face
  // The CRT is visually distinct from the bezel through:
  //   1. Different fill color (darker in dark mode, lighter in light mode)
  //   2. A visible 2px inset border that reads as a screen edge
  //   3. An inner bevel for depth
  // -------------------------------------------------------------------------
  DrawRectangleRec(crt, TH(s, crt_bg));

  // CRT border — a proper visible outline so the screen reads as a panel.
  // Two pixels wide; outer pixel is the bezel color blended, inner is brighter.
  DrawRectangleLinesEx(crt, 2.0f,
    (Color){ TH(s,p31_dim).r/2, TH(s,p31_dim).g/2, TH(s,p31_dim).b/2, 200 });
  // Inner highlight line (top + left only) for the CRT-behind-glass look
  DrawLine((int)(crt.x+2), (int)(crt.y+2),
           (int)(crt.x + crt.width - 2), (int)(crt.y+2),
           (Color){100, 110, 95, 60});
  DrawLine((int)(crt.x+2), (int)(crt.y+2),
           (int)(crt.x+2), (int)(crt.y + crt.height - 2),
           (Color){90, 100, 85, 50});

  // -------------------------------------------------------------------------
  // Grid
  // -------------------------------------------------------------------------
  if (s->show_grid && s->view_mode == VIEW_OVERLAY)
    skope_draw_grid(crt, 10, 8, s->dpi_y, s->theme);

  // -------------------------------------------------------------------------
  // Waveforms / Lissajous
  // -------------------------------------------------------------------------
  if (!s->connected) {
    // Waiting message — centered, P31 dim green
    const char *msg = "WAITING FOR SOURCE...";
    int fsz = (int)(16 * s->dpi_y);
    int tw  = tek_measure(msg);
    tek_text(msg,
             (int)(crt.x + crt.width  / 2 - tw / 2),
             (int)(crt.y + crt.height / 2 - fsz / 2),
             fsz, TH(s, p31_dim));
    // Source name below
    int fsz2 = (int)(11 * s->dpi_y);
    char src[160];
    snprintf(src, sizeof(src), "SOURCE: %s", s->name);
    int tw2 = tek_measure(src);
    tek_text(src,
             (int)(crt.x + crt.width / 2 - tw2 / 2),
             (int)(crt.y + crt.height / 2 + fsz + 6),
             fsz2, TH(s, p31_dim));
  } else {
    // Persistence layers
    if (s->persistence && s->history_count > 1) {
      double now = skope_now();
      for (int back = s->history_count - 1; back >= 1; back--) {
        int idx = (s->history_head - back + SKOPE_TRACE_HISTORY * 4)
                  % SKOPE_TRACE_HISTORY;
        trace_t *ht = &s->history[idx];
        if (!ht->valid) continue;
        double age = now - ht->captured_at;
        if (age > s->persist_s) continue;
        float a = (1.0f - (float)(age / s->persist_s)) * 0.4f;
        if (a < 0.02f) continue;
        if      (s->view_mode == VIEW_STACKED)   skope_draw_stacked(s, ht, crt, a);
        else if (s->view_mode == VIEW_OVERLAY)   skope_draw_overlay(s, ht, crt, a);

      }
    }
    // Current trace
    if (s->history_head >= 0) {
      trace_t *ct = &s->history[s->history_head];
      if      (s->view_mode == VIEW_STACKED)   skope_draw_stacked(s, ct, crt, 1.0f);
      else if (s->view_mode == VIEW_OVERLAY)   skope_draw_overlay(s, ct, crt, 1.0f);
      else if (s->view_mode == VIEW_GRID)      skope_draw_grid_layout(s, ct, crt, 1.0f);

    }
  }

  // -------------------------------------------------------------------------
  // CRT vignette — only in dark mode. Light mode has a pale background and
  // black-gradient corners would look wrong. Four gradient rectangles,
  // each ~5% of the CRT dimension wide/tall.
  // -------------------------------------------------------------------------
  if (s->dark_mode) {
    int vw = (int)(crt.width  * 0.05f);
    int vh = (int)(crt.height * 0.05f);
    DrawRectangleGradientH((int)crt.x, (int)crt.y,
                           vw, (int)crt.height,
                           (Color){0,0,0,55}, (Color){0,0,0,0});
    DrawRectangleGradientH((int)(crt.x + crt.width - vw), (int)crt.y,
                           vw, (int)crt.height,
                           (Color){0,0,0,0}, (Color){0,0,0,55});
    DrawRectangleGradientV((int)crt.x, (int)crt.y,
                           (int)crt.width, vh,
                           (Color){0,0,0,40}, (Color){0,0,0,0});
    DrawRectangleGradientV((int)crt.x, (int)(crt.y + crt.height - vh),
                           (int)crt.width, vh,
                           (Color){0,0,0,0}, (Color){0,0,0,40});
  }

  // -------------------------------------------------------------------------
  // On-CRT readout — time/div only, bottom-right corner.
  // Sample rate and armed/hold status moved to the bezel title bar.
  // -------------------------------------------------------------------------
  if (s->connected) {
    int margin = g_hd_cell_w;
    char tdiv[24];
    if (s->time_per_div_s >= 0.001f)
      snprintf(tdiv, sizeof(tdiv), "%.3Gms/DIV", s->time_per_div_s * 1000.f);
    else
      snprintf(tdiv, sizeof(tdiv), "%.0Fus/DIV", s->time_per_div_s * 1e6f);
    int tw = tek_measure(tdiv);
    tek_text(tdiv,
             (int)(crt.x + crt.width  - tw - margin),
             (int)(crt.y + crt.height - g_hd_cell_h - margin),
             g_hd_cell_h, TH(s, p31_mid));
  }

  // -------------------------------------------------------------------------
  // HUD
  // -------------------------------------------------------------------------
  if (s->show_hud)
    skope_draw_hud(s, (float)(sh - hud_h - (int)bpad_b), (float)hud_h);

  // Help overlay
  if (s->show_help) skope_draw_help(s);
}

static void skope_draw_buffer_overview(skope_t *s, Rectangle r) {
  Color frame = TH(s, p31_dim);
  Color dim = (Color){frame.r, frame.g, frame.b, 70};
  Color fill = TH(s, btn_active);

  DrawLineEx((Vector2){r.x, r.y + r.height / 2.0f},
             (Vector2){r.x + r.width, r.y + r.height / 2.0f},
             fmaxf(1.0f, s->dpi_y), dim);
  DrawLineEx((Vector2){r.x, r.y},
             (Vector2){r.x, r.y + r.height},
             fmaxf(1.0f, s->dpi_y), frame);
  DrawLineEx((Vector2){r.x + r.width, r.y},
             (Vector2){r.x + r.width, r.y + r.height},
             fmaxf(1.0f, s->dpi_y), frame);

  if (!s->connected || !s->reader.header || s->history_head < 0) return;

  uint64_t wf = shm_load64(&s->reader.header->write_frame);
  uint32_t capacity = s->reader.header->capacity_frames;
  uint32_t available = wf < capacity ? (uint32_t)wf : capacity;
  if (available < 2) return;

  uint64_t oldest = wf - available;
  trace_t *t = &s->history[s->history_head];
  uint64_t vis0 = t->first_frame;
  uint64_t vis1 = t->first_frame + (uint64_t)t->frame_count;
  if (vis0 < oldest) vis0 = oldest;
  if (vis1 > wf) vis1 = wf;
  if (vis1 <= vis0) return;

  float denom = (float)(wf - oldest);
  if (denom < 1.0f) denom = 1.0f;
  float x0 = r.x + ((float)(vis0 - oldest) / denom) * r.width;
  float x1 = r.x + ((float)(vis1 - oldest) / denom) * r.width;
  if (x1 - x0 < 3.0f * s->dpi_x) x1 = x0 + 3.0f * s->dpi_x;
  if (x1 > r.x + r.width) x1 = r.x + r.width;

  Rectangle win = {x0, r.y, x1 - x0, r.height};
  DrawRectangleRec(win, (Color){fill.r, fill.g, fill.b, 90});
  DrawRectangleLinesEx(win, fmaxf(1.0f, s->dpi_y), fill);
}

// ---------------------------------------------------------------------------
// HUD
// ---------------------------------------------------------------------------

static void skope_draw_hud(skope_t *s, float hud_y, float hud_h) {
  int sw  = GetScreenWidth();
  int fh  = g_hd_cell_h;   // one text row height (fixed, no dpi guessing)
  int fw  = g_hd_cell_w;
  int pad = fw * 2;         // 2-character left/right padding
  int gap = 3;              // pixel gap between rows

  // -------------------------------------------------------------------------
  // Panel background + separator
  // -------------------------------------------------------------------------
  DrawRectangle(0, (int)hud_y, sw, (int)hud_h, TH(s, hud_bg));
  DrawLine(0, (int)hud_y, sw, (int)hud_y, TH(s, hud_sep));

  // Work top-down from hud_y+gap
  int y = (int)hud_y + gap;

  // -------------------------------------------------------------------------
  // Row 1: source status  (left)  +  pair chips  (right)
  // -------------------------------------------------------------------------
  {
    // Source line
    if (s->connected && s->reader.header) {
      uint32_t rate = s->reader.header->sample_rate;
      uint32_t cap  = s->reader.header->capacity_frames;
      uint64_t wf   = shm_load64(&s->reader.header->write_frame);
      char l1[128];
      snprintf(l1, sizeof(l1), "SRC:%.12s %uHz BUF%.1fs FRM%" PRIu64,
               s->reader.name, rate,
               (double)cap / (rate ? rate : 1), wf);
      tek_text(l1, pad, y, fh, TH(s, p31_mid));
    } else {
      char l1[80];
      snprintf(l1, sizeof(l1), "SRC:%.24s  DISCONNECTED", s->name);
      tek_text(l1, pad, y, fh, TH(s, amber_dim));
    }

    // Pair chips — right-aligned on the same row
    int chip_x = sw - pad;
    for (int p = SKOPE_NUM_PAIRS - 1; p >= 0; p--) {
      const char *pname = skope_pair_name(s, p);
      int label_w = tek_measure(pname);
      int cw = label_w + fw * 2;   // 1-char padding each side
      chip_x -= cw + 2;

      Color lc = s->pairs[p].enabled
        ? kPairColor[p]
        : (Color){kPairColor[p].r/4, kPairColor[p].g/4,
                  kPairColor[p].b/4, 100};

      // Chip box
      DrawRectangle(chip_x, y, cw, fh + 2, TH(s, bezel_inner));
      Color border = (p == s->selected_pair)
        ? TH(s, btn_active) : lc;
      float bthick = (p == s->selected_pair) ? 1.5f : 1.0f;
      DrawRectangleLinesEx(
        (Rectangle){(float)chip_x,(float)y,(float)cw,(float)(fh+2)},
        bthick, border);
      // Disabled chips are dim; selected gets a tiny selection dot above
      tek_text(pname, chip_x + fw, y + 1, fh, lc);

      if (!s->pairs[p].enabled) {
        DrawLine(chip_x + 1, y + fh/2, chip_x + cw - 2, y + fh/2,
                 (Color){lc.r, lc.g, lc.b, 80});
      }
    }
  }
  y += fh + gap + 2;

  // -------------------------------------------------------------------------
  // Row 2: trigger + view status
  // -------------------------------------------------------------------------
  {
    const char *tmode = s->trig_mode == TRIG_AUTO   ? "AUTO"
                      : s->trig_mode == TRIG_NORMAL ? "NORM" : "SNGL";
    const char *tedge = s->trig_edge == EDGE_RISING ? "/" : "\\";
    char tdiv[16];
    if (s->time_per_div_s >= 0.001f)
      snprintf(tdiv, sizeof(tdiv), "%.3Gms/D", s->time_per_div_s * 1000.f);
    else
      snprintf(tdiv, sizeof(tdiv), "%.0Fus/D", s->time_per_div_s * 1e6f);

    char l2[160];
    snprintf(l2, sizeof(l2),
             "TRIG:%s%s SRC:%s LVL:%+.2f  "
             "%s T:%s  SEL:[%s]  %s",
             tmode, tedge, skope_pair_name(s, s->trig_pair), s->trig_level,
             view_name(s->view_mode), tdiv,
             skope_pair_name(s, s->selected_pair),
             s->persistence ? "PRST:ON" : "PRST:OFF");
    tek_text(l2, pad, y, fh, TH(s, p31_dim));
  }
  y += fh + gap + 4;

  // -------------------------------------------------------------------------
  // Row 3: softkey buttons — sized to fit remaining HUD space exactly
  // -------------------------------------------------------------------------
  typedef struct { const char *label; const char *key; int active; } sk_t;
  const char *tmode_str = s->trig_mode == TRIG_AUTO   ? "AUTO"
                        : s->trig_mode == TRIG_NORMAL ? "NORM" : "SNGL";
  const char *view_str  = s->pairs[s->selected_pair].viz_mode == VIZ_TIME ? "TIME"
                        : s->pairs[s->selected_pair].viz_mode == VIZ_XY ? "X-Y"
                        : "X-Y*";

  sk_t keys[] = {
    { tmode_str,                              "T",   1               },
    { s->trig_edge==EDGE_RISING ? "RISE":"FALL", "E", 1             },
    { view_str,                               "V",   1               },
    { s->persistence ? "PRST" : "PRST",       "P",   s->persistence  },
    { s->paused ? "HOLD" : "RUN",             "SPC", s->paused       },
    { s->show_grid ? "GRID" : "GRID",         "G",   s->show_grid    },
    { s->dark_mode ? "DARK" : "LITE",         "L",   0               },
    { text_mode_name(s->text_mode),            "K",   s->text_mode != TEXT_LCD },
    { "RESET",                                "R",   0               },
    { "HELP",                                 "/",   s->show_help    },
  };
  int nkeys = (int)(sizeof(keys)/sizeof(keys[0]));

  int btn_area_h = (int)hud_h - (y - (int)hud_y) - gap;
  if (btn_area_h < fh + 6) btn_area_h = fh + 6;
  int btn_h = btn_area_h;
  int btn_y = y;
  int total_w = sw - pad * 2;
  int btn_w = total_w / nkeys;
  int bx = pad;

  for (int i = 0; i < nkeys; i++) {
    Color outline = keys[i].active ? TH(s, btn_active) : TH(s, btn_outline);
    Color text_c  = keys[i].active ? TH(s, p31_bright) : TH(s, p31_mid);

    DrawRectangle(bx, btn_y, btn_w - 2, btn_h, TH(s, bezel_inner));
    DrawRectangleLinesEx(
      (Rectangle){(float)bx,(float)btn_y,(float)(btn_w-2),(float)btn_h},
      keys[i].active ? 1.5f : 1.0f, outline);

    int lw = tek_measure(keys[i].label);
    int label_x = bx + (btn_w - 2) / 2 - lw / 2;

    int have_hint = (btn_h >= fh * 2 + 6);
    if (have_hint) {
      // Two rows: label in upper half, key hint in lower half
      int label_y = btn_y + (btn_h / 2 - fh) / 2;              // centered in top half
      int hint_y  = btn_y + btn_h / 2 + (btn_h / 2 - fh) / 2; // centered in bottom half
      if (label_y < btn_y + 2) label_y = btn_y + 2;
      tek_text(keys[i].label, label_x, label_y, fh, text_c);
      // Separator between label and hint
      DrawLine(bx + 2, btn_y + btn_h / 2,
               bx + btn_w - 4, btn_y + btn_h / 2,
               (Color){outline.r/3, outline.g/3, outline.b/3, 180});
      int hw = tek_measure(keys[i].key);
      Color hint_c = (Color){outline.r/2, outline.g/2, outline.b/2, 220};
      tek_text(keys[i].key, bx + (btn_w-2)/2 - hw/2, hint_y, fh, hint_c);
    } else {
      // Single row: vertically center the label
      int label_y = btn_y + (btn_h - fh) / 2;
      if (label_y < btn_y) label_y = btn_y;
      tek_text(keys[i].label, label_x, label_y, fh, text_c);
    }

    bx += btn_w;
  }
}

// ---------------------------------------------------------------------------
// Help overlay
// ---------------------------------------------------------------------------

static void skope_draw_help(skope_t *s) {
  int sw = GetScreenWidth(), sh = GetScreenHeight();
  int fh = g_hd_cell_h;
  int fw = g_hd_cell_w;
  int lh = fh + 2;   // line height

  static const char *col_a[] = {
    "-- CHANNELS --",
    "0-4    TOGGLE TRACK",
    "[ / ]  SELECT PAIR",
    "+/-    VERT SCALE",
    ",.     VERT POSITION",
    "",
    "-- TRIGGER --",
    "T      MODE AUTO/NORM/SNGL",
    "E      EDGE RISE/FALL",
    "F1-F5  TRIG SOURCE",
    "UP/DN  TRIG LEVEL",
    "A      REARM SINGLE",
    "H      HOLDOFF +1ms",
  };
  static const char *col_b[] = {
    "-- TIMEBASE --",
    "LT/RT  TIME/DIV",
    "S+LT   SCROLL OLDER",
    "S+RT   SCROLL NEWER",
    "",
    "-- DISPLAY --",
    "V      VIEW STKD/OVLY/XY",
    "P      PHOSPHOR PERSIST",
    "SPC    PAUSE / RUN",
    "G      GRID",
    "D      HUD",
    "L      LIGHT/DARK",
    "K      LCD/ATARI",
    "R      RESET",
    "/      THIS HELP",
    "Q      QUIT",
  };

  int na = (int)(sizeof(col_a)/sizeof(col_a[0]));
  int nb = (int)(sizeof(col_b)/sizeof(col_b[0]));
  int nrows = na > nb ? na : nb;

  // Measure the widest line in each column to size it correctly
  int max_a = 0, max_b = 0;
  for (int i = 0; i < na; i++) {
    int w = tek_measure(col_a[i]); if (w > max_a) max_a = w;
  }
  for (int i = 0; i < nb; i++) {
    int w = tek_measure(col_b[i]); if (w > max_b) max_b = w;
  }

  int col_gap = fw * 4;   // gap between columns
  int margin  = fw * 2;   // inner margin
  int title_h = fh + 6;

  int bw = margin + max_a + col_gap + max_b + margin;
  int bh = title_h + lh * nrows + margin;

  // Clamp to window with a small border
  int bord = 8;
  if (bw > sw - bord*2) bw = sw - bord*2;
  if (bh > sh - bord*2) bh = sh - bord*2;

  int bx = sw/2 - bw/2;
  int by = sh/2 - bh/2;
  if (bx < bord) bx = bord;
  if (by < bord) by = bord;

  // Background — use CRT bg color so it looks like a screen overlay
  DrawRectangle(bx, by, bw, bh, TH(s, crt_bg));
  DrawRectangleLinesEx((Rectangle){(float)bx,(float)by,(float)bw,(float)bh},
                       1.5f, TH(s, p31_mid));

  // Title
  const char *title = "SKOPE  KEYS";
  int tw = tek_measure(title);
  tek_text(title, bx + bw/2 - tw/2, by + 4, fh, TH(s, p31_bright));
  DrawLine(bx + margin, by + title_h,
           bx + bw - margin, by + title_h, TH(s, hud_sep));

  // Column divider
  int div_x = bx + margin + max_a + col_gap/2;
  DrawLine(div_x, by + title_h + 2, div_x, by + bh - 4, TH(s, hud_sep));

  // Column content
  int row_y = by + title_h + 3;
  int ax  = bx + margin;
  int bxc = div_x + col_gap/2;

  for (int i = 0; i < na; i++) {
    Color c = (col_a[i][0] == '-')
      ? TH(s, amber) : TH(s, p31_dim);
    tek_text(col_a[i], ax, row_y + i * lh, fh, c);
  }
  for (int i = 0; i < nb; i++) {
    Color c = (col_b[i][0] == '-')
      ? TH(s, amber) : TH(s, p31_dim);
    tek_text(col_b[i], bxc, row_y + i * lh, fh, c);
  }
}
