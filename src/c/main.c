/**
 * Pixel Farkle — Dice game for Pebble
 * Targets: chalk, emery, gabbro
 *
 * Roll 6 dice, select scoring dice to keep, roll again or bank.
 * First to 10,000 wins. Farkle = no scoring dice = lose turn.
 */

#include <pebble.h>
#include <stdlib.h>

// ============================================================================
// GAME CONSTANTS
// ============================================================================
#define NUM_DICE    6
#define WIN_SCORE   10000
#define MIN_OPEN    500   // Minimum score to "open" (get on the board)

// Game states
enum { ST_ROLL, ST_SELECT, ST_FARKLE, ST_BANKED, ST_WIN };

// Cursor positions: 0-5 = dice, 6 = ROLL button, 7 = BANK button
#define POS_ROLL 6
#define POS_BANK 7
#define NUM_POS  8

// ============================================================================
// GAME STATE
// ============================================================================
static Window *s_win;
static Layer *s_canvas;

static int s_dice[NUM_DICE];       // Die values 1-6
static bool s_kept[NUM_DICE];      // Selected to keep this roll
static bool s_locked[NUM_DICE];    // Locked from previous rolls (can't unkeep)
static bool s_active[NUM_DICE];    // Die was rolled (not locked and not kept)

static int s_state = ST_ROLL;
static int s_cursor = 0;           // 0-5=dice, 6=roll, 7=bank
static int s_turn_score = 0;       // Accumulated this turn (from locked dice)
static int s_select_score = 0;     // Score of currently selected (not yet locked)
static int s_total_score = 0;      // Banked total
static int s_rolls = 0;            // Number of rolls this turn
static int s_dice_remaining = 6;   // Dice available to roll

// ============================================================================
// SCORING
// ============================================================================
static int calc_score(int vals[], int n) {
  // Count occurrences
  int cnt[7] = {0};
  for(int i=0; i<n; i++) cnt[vals[i]]++;

  int score = 0;
  bool used[7] = {false};

  // Check straight 1-6
  if(n==6) {
    bool straight = true;
    for(int i=1;i<=6;i++) if(cnt[i]!=1) { straight=false; break; }
    if(straight) return 1500;
  }

  // Check three pairs
  if(n==6) {
    int pairs=0;
    for(int i=1;i<=6;i++) if(cnt[i]==2) pairs++;
    if(pairs==3) return 1500;
  }

  // Check two triplets
  if(n==6) {
    int trips=0;
    for(int i=1;i<=6;i++) if(cnt[i]==3) trips++;
    if(trips==2) return 2500;
  }

  // Score multiples (3+ of a kind)
  for(int v=1;v<=6;v++) {
    if(cnt[v]>=3) {
      int base = (v==1) ? 1000 : v*100;
      int mult = 1;
      for(int e=3; e<cnt[v]; e++) mult *= 2;  // 4oak=2x, 5oak=4x, 6oak=8x
      score += base * mult;
      used[v] = true;
    }
  }

  // Individual 1s and 5s (not already counted in multiples)
  if(!used[1]) score += cnt[1] * 100;
  if(!used[5]) score += cnt[5] * 50;

  return score;
}

// Calculate score of currently selected (kept but not locked) dice
static int calc_selected_score(void) {
  int vals[NUM_DICE], n=0;
  for(int i=0;i<NUM_DICE;i++) {
    if(s_kept[i] && !s_locked[i]) vals[n++] = s_dice[i];
  }
  if(n==0) return 0;
  return calc_score(vals, n);
}

// Check if any scoring is possible in the rolled dice
static bool has_scoring_dice(void) {
  int vals[NUM_DICE], n=0;
  for(int i=0;i<NUM_DICE;i++) {
    if(s_active[i]) vals[n++] = s_dice[i];
  }
  if(n==0) return false;
  return calc_score(vals, n) > 0;
}

// Check if individual die contributes to scoring
static bool die_can_score(int idx) {
  if(!s_active[idx]) return false;
  int v = s_dice[idx];
  if(v==1 || v==5) return true;
  // Check if part of 3+ of a kind among active dice
  int cnt=0;
  for(int i=0;i<NUM_DICE;i++) {
    if(s_active[i] && s_dice[i]==v) cnt++;
  }
  return cnt >= 3;
}

// ============================================================================
// GAME LOGIC
// ============================================================================
static void roll_dice(void) {
  s_dice_remaining = 0;
  for(int i=0;i<NUM_DICE;i++) {
    s_active[i] = false;
    if(!s_locked[i]) {
      s_dice[i] = (rand() % 6) + 1;
      s_kept[i] = false;
      s_active[i] = true;
      s_dice_remaining++;
    }
  }
  s_rolls++;

  if(!has_scoring_dice()) {
    s_state = ST_FARKLE;
    s_turn_score = 0;
    vibes_long_pulse();
  } else {
    s_state = ST_SELECT;
    s_cursor = 0;
    // Move cursor to first active die
    while(s_cursor < NUM_DICE && !s_active[s_cursor]) s_cursor++;
  }
  s_select_score = 0;
}

static void new_turn(void) {
  for(int i=0;i<NUM_DICE;i++) {
    s_kept[i] = false;
    s_locked[i] = false;
    s_active[i] = false;
  }
  s_turn_score = 0;
  s_select_score = 0;
  s_rolls = 0;
  s_dice_remaining = 6;
  s_state = ST_ROLL;
  s_cursor = 0;
}

static void bank_score(void) {
  int total = s_turn_score + s_select_score;
  if(s_total_score == 0 && total < MIN_OPEN) return;  // Can't open
  s_total_score += total;
  s_state = ST_BANKED;
  if(s_total_score >= WIN_SCORE) s_state = ST_WIN;
  vibes_short_pulse();
}

static void lock_selected(void) {
  // Lock the currently selected dice and add their score
  s_select_score = calc_selected_score();
  if(s_select_score <= 0) return;
  s_turn_score += s_select_score;
  for(int i=0;i<NUM_DICE;i++) {
    if(s_kept[i] && !s_locked[i]) {
      s_locked[i] = true;
    }
  }
  // Check for hot dice (all 6 locked)
  int locked_cnt = 0;
  for(int i=0;i<NUM_DICE;i++) if(s_locked[i]) locked_cnt++;
  if(locked_cnt == NUM_DICE) {
    // Hot dice! Reset all locks, roll all 6 again
    for(int i=0;i<NUM_DICE;i++) s_locked[i] = false;
    s_dice_remaining = 6;
  } else {
    s_dice_remaining = NUM_DICE - locked_cnt;
  }
  s_select_score = 0;
}

// ============================================================================
// DRAWING
// ============================================================================

// Draw a single die face
static void draw_die(GContext *ctx, int cx, int cy, int sz, int val,
                     bool selected, bool kept, bool locked, bool active) {
  // Background
  GColor bg;
  if(locked) bg = GColorDarkGray;
  else if(kept) bg = GColorYellow;
  else if(!active) bg = GColorDarkGray;
  else bg = GColorWhite;

  graphics_context_set_fill_color(ctx, bg);
  graphics_fill_rect(ctx, GRect(cx-sz/2, cy-sz/2, sz, sz), 4, GCornersAll);

  // Border
  if(selected) {
    #ifdef PBL_COLOR
    graphics_context_set_stroke_color(ctx, GColorRed);
    #else
    graphics_context_set_stroke_color(ctx, GColorBlack);
    #endif
    graphics_context_set_stroke_width(ctx, 3);
  } else {
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_context_set_stroke_width(ctx, 1);
  }
  graphics_draw_round_rect(ctx, GRect(cx-sz/2, cy-sz/2, sz, sz), 4);
  graphics_context_set_stroke_width(ctx, 1);

  // Dots
  graphics_context_set_fill_color(ctx, GColorBlack);
  int d = sz/4;  // Dot offset from center
  int dr = sz/10; // Dot radius
  if(dr < 2) dr = 2;

  // Center dot: 1,3,5
  if(val==1||val==3||val==5)
    graphics_fill_circle(ctx, GPoint(cx, cy), dr);
  // Top-left, bottom-right: 2,3,4,5,6
  if(val>=2) {
    graphics_fill_circle(ctx, GPoint(cx-d, cy-d), dr);
    graphics_fill_circle(ctx, GPoint(cx+d, cy+d), dr);
  }
  // Top-right, bottom-left: 4,5,6
  if(val>=4) {
    graphics_fill_circle(ctx, GPoint(cx+d, cy-d), dr);
    graphics_fill_circle(ctx, GPoint(cx-d, cy+d), dr);
  }
  // Mid-left, mid-right: 6
  if(val==6) {
    graphics_fill_circle(ctx, GPoint(cx-d, cy), dr);
    graphics_fill_circle(ctx, GPoint(cx+d, cy), dr);
  }
}

static void canvas_proc(Layer *l, GContext *ctx) {
  GRect b = layer_get_bounds(l);
  int w=b.size.w, h=b.size.h;
  bool big = w >= 240;

  // Background
  #ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx, GColorFromHEX(0x004400));
  #else
  graphics_context_set_fill_color(ctx, GColorBlack);
  #endif
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  GFont f_lg = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  GFont f_md = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GFont f_sm = fonts_get_system_font(FONT_KEY_GOTHIC_14);

  // Dice layout: 2 rows of 3
  int die_sz = big ? 38 : 28;
  int gap = big ? 8 : 6;
  int row1_y = h * 38 / 100;
  int row2_y = row1_y + die_sz + gap;
  int start_x = w/2 - (die_sz*3 + gap*2)/2 + die_sz/2;

  for(int i=0; i<NUM_DICE; i++) {
    int row = i / 3;
    int col = i % 3;
    int dx = start_x + col * (die_sz + gap);
    int dy = (row == 0) ? row1_y : row2_y;
    draw_die(ctx, dx, dy, die_sz, s_dice[i],
             s_cursor == i && s_state == ST_SELECT,
             s_kept[i], s_locked[i], s_active[i]);
  }

  // Top: total score
  graphics_context_set_text_color(ctx, GColorWhite);
  char sbuf[24];
  snprintf(sbuf, sizeof(sbuf), "Score: %d", s_total_score);
  graphics_draw_text(ctx, sbuf, f_md,
    GRect(0, big?8:4, w, 22), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  // Turn score
  if(s_turn_score > 0 || s_select_score > 0) {
    int ts = s_turn_score + s_select_score;
    snprintf(sbuf, sizeof(sbuf), "Turn: +%d", ts);
    #ifdef PBL_COLOR
    graphics_context_set_text_color(ctx, GColorYellow);
    #endif
    graphics_draw_text(ctx, sbuf, f_sm,
      GRect(0, big?30:22, w, 16), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    graphics_context_set_text_color(ctx, GColorWhite);
  }

  // Bottom area: state-dependent
  int bot_y = row2_y + die_sz/2 + gap + 4;

  if(s_state == ST_ROLL) {
    graphics_draw_text(ctx, "SELECT to Roll", f_md,
      GRect(0, bot_y, w, 24), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    if(s_rolls == 0 && s_total_score == 0) {
      graphics_draw_text(ctx, "Need 500 to open", f_sm,
        GRect(0, bot_y+22, w, 16), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    }
  }
  else if(s_state == ST_SELECT) {
    // Show action buttons
    bool has_sel = s_select_score > 0;
    char rbuf[20], bbuf[20];
    snprintf(rbuf, sizeof(rbuf), "UP: Roll (%d)", s_dice_remaining - (has_sel?1:0));

    // Roll button
    bool roll_sel = (s_cursor == POS_ROLL);
    if(roll_sel) {
      #ifdef PBL_COLOR
      graphics_context_set_text_color(ctx, GColorGreen);
      #endif
    }
    if(!has_sel) {
      graphics_draw_text(ctx, "SELECT to keep dice", f_sm,
        GRect(0, bot_y, w, 16), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    } else {
      int total = s_turn_score + s_select_score;
      bool can_bank = (s_total_score > 0 || total >= MIN_OPEN);
      // Show actions
      #ifdef PBL_COLOR
      graphics_context_set_text_color(ctx, GColorGreen);
      #endif
      graphics_draw_text(ctx, "Hold SEL: Roll", f_sm,
        GRect(0, bot_y, w, 16), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
      char bbuf[24];
      snprintf(bbuf, sizeof(bbuf), "BACK: Bank %d", total);
      #ifdef PBL_COLOR
      graphics_context_set_text_color(ctx, can_bank ? GColorCyan : GColorDarkGray);
      #endif
      graphics_draw_text(ctx, bbuf, f_sm,
        GRect(0, bot_y+16, w, 16), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
      graphics_context_set_text_color(ctx, GColorWhite);
    }

    // Highlight which dice can score
    #ifdef PBL_COLOR
    for(int i=0; i<NUM_DICE; i++) {
      if(s_active[i] && !s_kept[i] && die_can_score(i)) {
        int row = i/3, col = i%3;
        int dx = start_x + col*(die_sz+gap);
        int dy = (row==0)?row1_y:row2_y;
        graphics_context_set_fill_color(ctx, GColorGreen);
        graphics_fill_rect(ctx, GRect(dx-die_sz/2-1, dy+die_sz/2+1, die_sz+2, 2), 0, GCornerNone);
      }
    }
    #endif
  }
  else if(s_state == ST_FARKLE) {
    #ifdef PBL_COLOR
    graphics_context_set_text_color(ctx, GColorRed);
    #endif
    graphics_draw_text(ctx, "FARKLE!", f_lg,
      GRect(0, bot_y-4, w, 32), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "No scoring dice! SELECT to continue", f_sm,
      GRect(0, bot_y+24, w, 16), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }
  else if(s_state == ST_BANKED) {
    #ifdef PBL_COLOR
    graphics_context_set_text_color(ctx, GColorGreen);
    #endif
    graphics_draw_text(ctx, "BANKED!", f_lg,
      GRect(0, bot_y-4, w, 32), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "SELECT to continue", f_sm,
      GRect(0, bot_y+24, w, 16), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }
  else if(s_state == ST_WIN) {
    #ifdef PBL_COLOR
    graphics_context_set_text_color(ctx, GColorYellow);
    #endif
    char wbuf[24];
    snprintf(wbuf, sizeof(wbuf), "YOU WIN! %d", s_total_score);
    graphics_draw_text(ctx, wbuf, f_lg,
      GRect(0, bot_y-4, w, 32), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "SELECT for new game", f_sm,
      GRect(0, bot_y+24, w, 16), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }
}

// ============================================================================
// BUTTON HANDLERS
// ============================================================================
static void select_click(ClickRecognizerRef ref, void *ctx) {
  if(s_state == ST_ROLL) {
    roll_dice();
  }
  else if(s_state == ST_SELECT) {
    if(s_cursor < NUM_DICE) {
      // Toggle die selection
      int i = s_cursor;
      if(s_active[i] && !s_locked[i]) {
        if(s_kept[i]) {
          s_kept[i] = false;
        } else if(die_can_score(i)) {
          s_kept[i] = true;
        }
        s_select_score = calc_selected_score();
      }
    }
  }
  else if(s_state == ST_FARKLE || s_state == ST_BANKED) {
    new_turn();
  }
  else if(s_state == ST_WIN) {
    s_total_score = 0;
    new_turn();
  }
  if(s_canvas) layer_mark_dirty(s_canvas);
}

// Long press SELECT = Roll remaining dice
static void select_long(ClickRecognizerRef ref, void *ctx) {
  if(s_state == ST_SELECT && s_select_score > 0) {
    lock_selected();
    roll_dice();
  }
  if(s_canvas) layer_mark_dirty(s_canvas);
}

static void up_click(ClickRecognizerRef ref, void *ctx) {
  if(s_state == ST_SELECT) {
    // Always move cursor up/left
    int start = s_cursor;
    do {
      s_cursor = (s_cursor + NUM_DICE - 1) % NUM_DICE;
    } while(!s_active[s_cursor] && s_cursor != start);
  }
  if(s_canvas) layer_mark_dirty(s_canvas);
}

static void down_click(ClickRecognizerRef ref, void *ctx) {
  if(s_state == ST_SELECT) {
    // Always move cursor down/right
    int start = s_cursor;
    do {
      s_cursor = (s_cursor + 1) % NUM_DICE;
    } while(!s_active[s_cursor] && s_cursor != start);
  }
  if(s_canvas) layer_mark_dirty(s_canvas);
}

// BACK = Bank score (when dice selected), or exit game
static void back_click(ClickRecognizerRef ref, void *ctx) {
  if(s_state == ST_SELECT && s_select_score > 0) {
    s_turn_score += s_select_score;
    bank_score();
    if(s_canvas) layer_mark_dirty(s_canvas);
  } else {
    window_stack_pop(true);
  }
}

static void click_config(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
  window_long_click_subscribe(BUTTON_ID_SELECT, 500, select_long, NULL);
  window_single_click_subscribe(BUTTON_ID_UP, up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click);
  window_single_click_subscribe(BUTTON_ID_BACK, back_click);
}

// ============================================================================
// WINDOW
// ============================================================================
static void win_load(Window *w) {
  Layer *wl = window_get_root_layer(w);
  GRect b = layer_get_bounds(wl);
  s_canvas = layer_create(b);
  layer_set_update_proc(s_canvas, canvas_proc);
  layer_add_child(wl, s_canvas);
  window_set_click_config_provider(w, click_config);
  new_turn();
}

static void win_unload(Window *w) {
  if(s_canvas) { layer_destroy(s_canvas); s_canvas = NULL; }
}

// ============================================================================
// LIFECYCLE
// ============================================================================
static void init(void) {
  srand(time(NULL));
  s_win = window_create();
  window_set_background_color(s_win, GColorBlack);
  window_set_window_handlers(s_win, (WindowHandlers){.load=win_load, .unload=win_unload});
  window_stack_push(s_win, true);
}

static void deinit(void) {
  window_destroy(s_win);
}

int main(void) { init(); app_event_loop(); deinit(); return 0; }
