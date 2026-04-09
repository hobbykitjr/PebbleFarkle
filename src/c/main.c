/**
 * Pixel Farkle — Dice game for Pebble
 * Targets: emery, gabbro
 *
 * Solo or 2-6 players. Roll dice, keep scoring dice, bank or push luck.
 * First to 10,000 wins. Farkle = no scoring dice = lose turn.
 */

#include <pebble.h>
#include <stdlib.h>

// ============================================================================
// CONSTANTS
// ============================================================================
#define NUM_DICE    6
#define WIN_SCORE   10000
#define MIN_OPEN    500
#define MAX_PLAYERS 6

// Game states
enum { ST_SETUP, ST_ORDER, ST_ROLL, ST_SELECT, ST_FARKLE, ST_BANKED, ST_WIN };

// Cursor positions: 0-5=dice, 6=ROLL, 7=BANK
#define POS_ROLL 6
#define POS_BANK 7
#define NUM_POS  8

// Player tokens: Font Awesome icons
#define NUM_TOKENS 6
static const char *s_tok_name[] = {"Star","Heart","Diamond","Circle","Square","Bolt"};
// Font Awesome Unicode codepoints encoded as UTF-8
static const char *s_tok_char[] = {
  "\xEF\x80\x85",  // U+F005 Star
  "\xEF\x80\x84",  // U+F004 Heart
  "\xEF\x88\x99",  // U+F219 Diamond
  "\xEF\x84\x91",  // U+F111 Circle
  "\xEF\x83\x88",  // U+F0C8 Square
  "\xEF\x83\xA7",  // U+F0E7 Bolt
};

#ifdef PBL_COLOR
static GColor tok_color(int t) {
  switch(t) {
    case 0: return GColorYellow;       // Star
    case 1: return GColorRed;          // Heart
    case 2: return GColorCyan;         // Diamond
    case 3: return GColorGreen;        // Circle
    case 4: return GColorOrange;       // Square
    case 5: return GColorPurple;       // Bolt
    default: return GColorWhite;
  }
}
#endif

// Custom icon fonts (loaded from resources)
static GFont s_icon_font_20;
static GFont s_icon_font_14;

// ============================================================================
// PLAYER DATA
// ============================================================================
typedef struct {
  int score;
  int icon;
} Player;

static int s_num_players = 1;
static Player s_players[MAX_PLAYERS];
static int s_order[MAX_PLAYERS];   // Play order (indices into s_players)
static int s_cur_idx = 0;         // Index into s_order for current player
static int s_setup_cursor = 0;    // 0=Solo, 1=2p, 2=3p...

// ============================================================================
// GAME STATE
// ============================================================================
static Window *s_win;
static Layer *s_canvas;

static int s_dice[NUM_DICE];
static bool s_kept[NUM_DICE];
static bool s_locked[NUM_DICE];
static bool s_active[NUM_DICE];

static int s_state = ST_SETUP;
static int s_cursor = 0;
static int s_turn_score = 0;
static int s_select_score = 0;
static int s_rolls = 0;
static int s_rounds = 0;            // Rounds (all players played = 1 round)
static int s_dice_remaining = 6;
static bool s_show_help = false;    // Hold DOWN: scoring reference
static bool s_show_scores = false;  // Hold UP: scoreboard

// High score persistence
#define P_HI_SCORE  0
#define P_HI_ROUNDS 1
static int s_hi_score = 0;
static int s_hi_rounds = 0;

// Draw player token (Font Awesome icon, colored)
static void draw_token(GContext *ctx, int cx, int cy, int icon, bool large) {
  #ifdef PBL_COLOR
  graphics_context_set_text_color(ctx, tok_color(icon));
  #else
  graphics_context_set_text_color(ctx, GColorWhite);
  #endif
  GFont f = large ? s_icon_font_20 : s_icon_font_14;
  int sz = large ? 30 : 22;
  if(!f) {
    // Font failed to load — fallback to first letter of name
    f = fonts_get_system_font(large ? FONT_KEY_GOTHIC_24_BOLD : FONT_KEY_GOTHIC_18_BOLD);
    graphics_draw_text(ctx, s_tok_name[icon], f,
      GRect(cx-sz, cy-sz/2, sz*2, sz), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    return;
  }
  graphics_draw_text(ctx, s_tok_char[icon], f,
    GRect(cx-sz, cy-sz/2, sz*2, sz), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

// ============================================================================
// SCORING (unchanged)
// ============================================================================
static int calc_score(int vals[], int n) {
  int cnt[7]={0};
  for(int i=0;i<n;i++) cnt[vals[i]]++;
  int score=0; bool used[7]={false};
  if(n==6){
    bool str=true; for(int i=1;i<=6;i++) if(cnt[i]!=1){str=false;break;} if(str) return 1500;
    int pr=0; for(int i=1;i<=6;i++) if(cnt[i]==2) pr++; if(pr==3) return 1500;
    int tr=0; for(int i=1;i<=6;i++) if(cnt[i]==3) tr++; if(tr==2) return 2500;
  }
  for(int v=1;v<=6;v++){
    if(cnt[v]>=3){ int b=(v==1)?1000:v*100; int m=1;
      for(int e=3;e<cnt[v];e++) m*=2;
      score+=b*m; used[v]=true;
    }
  }
  if(!used[1]) score+=cnt[1]*100;
  if(!used[5]) score+=cnt[5]*50;
  return score;
}
static int calc_selected_score(void) {
  int vals[NUM_DICE],n=0;
  for(int i=0;i<NUM_DICE;i++) if(s_kept[i]&&!s_locked[i]) vals[n++]=s_dice[i];
  return n>0?calc_score(vals,n):0;
}
static bool has_scoring_dice(void) {
  int vals[NUM_DICE],n=0;
  for(int i=0;i<NUM_DICE;i++) if(s_active[i]) vals[n++]=s_dice[i];
  return n>0&&calc_score(vals,n)>0;
}
static bool die_can_score(int idx) {
  if(!s_active[idx]) return false;
  if(s_kept[idx]) return true;
  int v=s_dice[idx];
  if(v==1||v==5) return true;
  int cnt=0;
  for(int i=0;i<NUM_DICE;i++)
    if(s_active[i]&&!s_locked[i]&&!s_kept[i]&&s_dice[i]==v) cnt++;
  return cnt>=3;
}

// ============================================================================
// GAME LOGIC
// ============================================================================
static Player* cur_player(void) { return &s_players[s_order[s_cur_idx]]; }

static void roll_dice(void) {
  s_dice_remaining=0;
  for(int i=0;i<NUM_DICE;i++){
    s_active[i]=false;
    if(!s_locked[i]){ s_dice[i]=(rand()%6)+1; s_kept[i]=false; s_active[i]=true; s_dice_remaining++; }
  }
  s_rolls++;
  if(!has_scoring_dice()){
    s_state=ST_FARKLE; s_turn_score=0; vibes_long_pulse();
  } else {
    s_state=ST_SELECT;
    bool all=true;
    for(int i=0;i<NUM_DICE;i++) if(s_active[i]&&!die_can_score(i)){all=false;break;}
    if(all){ for(int i=0;i<NUM_DICE;i++) if(s_active[i]) s_kept[i]=true;
      s_select_score=calc_selected_score(); s_cursor=POS_ROLL;
    } else { s_cursor=0; while(s_cursor<NUM_DICE&&!die_can_score(s_cursor)) s_cursor++;
      s_select_score=0; }
  }
}

static void new_turn(void) {
  for(int i=0;i<NUM_DICE;i++){ s_kept[i]=false; s_locked[i]=false; s_active[i]=false; }
  s_turn_score=0; s_select_score=0; s_rolls=0; s_dice_remaining=6;
  s_state=ST_ROLL; s_cursor=0;
}

static void next_player(void) {
  s_cur_idx = (s_cur_idx+1) % s_num_players;
  if(s_cur_idx == 0) s_rounds++;  // All players went = new round
  new_turn();
}

static void bank_score(void) {
  Player *p=cur_player();
  int total=s_turn_score;
  if(p->score==0 && total<MIN_OPEN) return;
  p->score+=total;
  s_state=ST_BANKED;
  if(p->score>=WIN_SCORE) {
    s_state=ST_WIN;
    // Save high score: better score in same/fewer rounds, or same score in fewer rounds
    if(s_hi_score==0 || s_rounds<s_hi_rounds ||
       (s_rounds==s_hi_rounds && p->score>s_hi_score)) {
      s_hi_score=p->score;
      s_hi_rounds=s_rounds;
      persist_write_int(P_HI_SCORE,s_hi_score);
      persist_write_int(P_HI_ROUNDS,s_hi_rounds);
    }
  }
  vibes_short_pulse();
}

static void lock_selected(void) {
  s_select_score=calc_selected_score();
  if(s_select_score<=0) return;
  s_turn_score+=s_select_score;
  for(int i=0;i<NUM_DICE;i++) if(s_kept[i]&&!s_locked[i]) s_locked[i]=true;
  int lc=0; for(int i=0;i<NUM_DICE;i++) if(s_locked[i]) lc++;
  if(lc==NUM_DICE){ for(int i=0;i<NUM_DICE;i++) s_locked[i]=false; s_dice_remaining=6; }
  else s_dice_remaining=NUM_DICE-lc;
  s_select_score=0;
}

// Shuffle player order
static void shuffle_order(void) {
  for(int i=0;i<s_num_players;i++) s_order[i]=i;
  for(int i=s_num_players-1;i>0;i--){
    int j=rand()%(i+1);
    int tmp=s_order[i]; s_order[i]=s_order[j]; s_order[j]=tmp;
  }
}

static void init_game(void) {
  // Assign unique random icons
  int avail[]={0,1,2,3,4,5};
  for(int i=5;i>0;i--){ int j=rand()%(i+1); int t=avail[i]; avail[i]=avail[j]; avail[j]=t; }
  for(int i=0;i<s_num_players;i++){
    s_players[i].score=0;
    s_players[i].icon=avail[i];
  }
  shuffle_order();
  s_cur_idx=0;
  s_rounds=1;
}

// ============================================================================
// DRAWING: DIE
// ============================================================================
static void draw_die(GContext *ctx, int cx, int cy, int sz, int val,
                     bool selected, bool kept, bool locked, bool active) {
  GColor bg;
  if(locked) bg=GColorDarkGray;
  else if(kept) bg=GColorYellow;
  else if(!active) bg=GColorDarkGray;
  else bg=GColorWhite;
  graphics_context_set_fill_color(ctx,bg);
  graphics_fill_rect(ctx,GRect(cx-sz/2,cy-sz/2,sz,sz),4,GCornersAll);
  if(selected){
    #ifdef PBL_COLOR
    graphics_context_set_stroke_color(ctx,GColorRed);
    #else
    graphics_context_set_stroke_color(ctx,GColorBlack);
    #endif
    graphics_context_set_stroke_width(ctx,3);
  } else {
    graphics_context_set_stroke_color(ctx,GColorBlack);
    graphics_context_set_stroke_width(ctx,1);
  }
  graphics_draw_round_rect(ctx,GRect(cx-sz/2,cy-sz/2,sz,sz),4);
  graphics_context_set_stroke_width(ctx,1);
  graphics_context_set_fill_color(ctx,GColorBlack);
  int d=sz/4, dr=sz/10; if(dr<2)dr=2;
  if(val==1||val==3||val==5) graphics_fill_circle(ctx,GPoint(cx,cy),dr);
  if(val>=2){ graphics_fill_circle(ctx,GPoint(cx-d,cy-d),dr);
    graphics_fill_circle(ctx,GPoint(cx+d,cy+d),dr); }
  if(val>=4){ graphics_fill_circle(ctx,GPoint(cx+d,cy-d),dr);
    graphics_fill_circle(ctx,GPoint(cx-d,cy+d),dr); }
  if(val==6){ graphics_fill_circle(ctx,GPoint(cx-d,cy),dr);
    graphics_fill_circle(ctx,GPoint(cx+d,cy),dr); }
}

// ============================================================================
// DRAWING: MAIN CANVAS
// ============================================================================
static void canvas_proc(Layer *l, GContext *ctx) {
  GRect b=layer_get_bounds(l);
  int w=b.size.w, h=b.size.h;
  bool big=w>=240;

  // Background
  #ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx,GColorFromHEX(0x004400));
  #else
  graphics_context_set_fill_color(ctx,GColorBlack);
  #endif
  graphics_fill_rect(ctx,b,0,GCornerNone);

  GFont f_lg=fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  GFont f_md=fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GFont f_sm=fonts_get_system_font(FONT_KEY_GOTHIC_14);

  // ======== SETUP SCREEN ========
  if(s_state==ST_SETUP) {
    graphics_context_set_text_color(ctx,GColorWhite);
    graphics_draw_text(ctx,"FARKLE",f_lg,
      GRect(0,h*12/100,w,34),GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);

    // Centered picker: UP/DOWN cycles through options
    const char *opts[]={"Solo","2 Players","3 Players","4 Players","5 Players","6 Players"};
    int cy=h*42/100;

    // Down indicator
    graphics_draw_text(ctx,"v",f_sm,
      GRect(0,cy+28,w,16),GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);

    // Current selection (big, highlighted)
    #ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx,GColorFromHEX(0x006600));
    #else
    graphics_context_set_fill_color(ctx,GColorWhite);
    #endif
    int mx=big?50:30;
    graphics_fill_rect(ctx,GRect(mx,cy-2,w-mx*2,30),6,GCornersAll);
    #ifdef PBL_COLOR
    graphics_context_set_text_color(ctx,GColorWhite);
    #else
    graphics_context_set_text_color(ctx,GColorBlack);
    #endif
    graphics_draw_text(ctx,opts[s_setup_cursor],f_lg,
      GRect(0,cy-2,w,30),GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);

    // High score under Solo option
    graphics_context_set_text_color(ctx,GColorWhite);
    if(s_hi_score>0) {
      char hbuf[32];
      snprintf(hbuf,sizeof(hbuf),"Best: %d in %d rounds",s_hi_score,s_hi_rounds);
      #ifdef PBL_COLOR
      graphics_context_set_text_color(ctx,GColorYellow);
      #endif
      graphics_draw_text(ctx,hbuf,f_sm,
        GRect(0,cy+32,w,16),GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);
      graphics_context_set_text_color(ctx,GColorWhite);
    }

    graphics_draw_text(ctx,"SELECT to start",f_sm,
      GRect(0,h*72/100,w,16),GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);
    graphics_draw_text(ctx,"Hold DOWN: Rules",f_sm,
      GRect(0,h*80/100,w,16),GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);
  }

  // ======== TOKEN SCREEN (multiplayer) ========
  else if(s_state==ST_ORDER) {
    graphics_context_set_text_color(ctx,GColorWhite);
    graphics_draw_text(ctx,"Choose Tokens!",f_lg,
      GRect(0,h*10/100,w,34),GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);

    // Show tokens in a list
    int gy=h*30/100;
    int row_h=big?28:24;
    int cols=(s_num_players<=3)?1:2;
    int per_col=(s_num_players+cols-1)/cols;
    for(int i=0;i<s_num_players;i++){
      int c=i/per_col, r=i%per_col;
      int col_w=w/cols;
      int tx=c*col_w+col_w/2;
      int ty=gy+r*row_h;
      draw_token(ctx,tx-20,ty+row_h/2,s_players[i].icon,true);
      graphics_context_set_text_color(ctx,GColorWhite);
      graphics_draw_text(ctx,s_tok_name[s_players[i].icon],f_md,
        GRect(tx-8,ty+2,col_w/2+8,row_h),GTextOverflowModeTrailingEllipsis,GTextAlignmentLeft,NULL);
    }

    graphics_context_set_text_color(ctx,GColorWhite);
    graphics_draw_text(ctx,"Pick tokens, then",f_sm,
      GRect(0,h*70/100,w,16),GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);
    graphics_draw_text(ctx,"SELECT to randomize & go!",f_md,
      GRect(0,h*78/100,w,24),GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);
  }

  // ======== GAME SCREENS ========
  else {
    // Dice layout
    int die_sz=big?38:28;
    int gap=big?8:6;
    int row1_y=h*38/100;
    int row2_y=row1_y+die_sz+gap;
    int start_x=w/2-(die_sz*3+gap*2)/2+die_sz/2;

    for(int i=0;i<NUM_DICE;i++){
      int row=i/3,col=i%3;
      int dx=start_x+col*(die_sz+gap);
      int dy=(row==0)?row1_y:row2_y;
      draw_die(ctx,dx,dy,die_sz,s_dice[i],
        s_cursor==i&&s_state==ST_SELECT,s_kept[i],s_locked[i],s_active[i]);
    }

    // Top: player icon + turn
    Player *p=cur_player();
    int top_y=big?4:2;
    if(s_num_players>1) {
      draw_token(ctx,w/2,top_y+12,p->icon,true);
      top_y+=big?22:18;
    }
    graphics_context_set_text_color(ctx,GColorWhite);
    char sbuf[32];
    snprintf(sbuf,sizeof(sbuf),"Round %d",s_rounds);
    graphics_draw_text(ctx,sbuf,f_sm,
      GRect(0,top_y,w,16),GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);

    // Turn score
    int ts=s_turn_score+s_select_score;
    if(ts>0){
      char tbuf[24]; snprintf(tbuf,sizeof(tbuf),"+%d this turn",ts);
      #ifdef PBL_COLOR
      graphics_context_set_text_color(ctx,GColorYellow);
      #endif
      graphics_draw_text(ctx,tbuf,f_sm,
        GRect(0,top_y+14,w,16),GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);
      graphics_context_set_text_color(ctx,GColorWhite);
    }

    // Bottom area
    int bot_y=row2_y+die_sz/2+gap+4;

    if(s_state==ST_ROLL){
      graphics_draw_text(ctx,"SELECT to Roll",f_md,
        GRect(0,bot_y,w,24),GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);
      if(p->score==0)
        graphics_draw_text(ctx,"Need 500 to open",f_sm,
          GRect(0,bot_y+22,w,16),GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);
    }
    else if(s_state==ST_SELECT){
      bool has_sel=s_select_score>0;
      if(!has_sel){
        graphics_draw_text(ctx,"SELECT to keep dice",f_sm,
          GRect(0,bot_y,w,16),GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);
      } else {
        int total=s_turn_score+s_select_score;
        bool can_bank=(p->score>0||total>=MIN_OPEN);
        int margin=big?30:10;
        int btn_w=(w-margin*2-10)/2;
        bool roll_hl=(s_cursor==POS_ROLL), bank_hl=(s_cursor==POS_BANK);

        #ifdef PBL_COLOR
        graphics_context_set_fill_color(ctx,roll_hl?GColorGreen:GColorFromHEX(0x003300));
        #else
        graphics_context_set_fill_color(ctx,roll_hl?GColorWhite:GColorDarkGray);
        #endif
        graphics_fill_rect(ctx,GRect(margin,bot_y,btn_w,18),4,GCornersAll);
        graphics_context_set_text_color(ctx,roll_hl?GColorBlack:GColorWhite);
        graphics_draw_text(ctx,"Roll",f_sm,
          GRect(margin,bot_y,btn_w,18),GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);

        #ifdef PBL_COLOR
        graphics_context_set_fill_color(ctx,bank_hl?GColorCyan:(can_bank?GColorFromHEX(0x003333):GColorDarkGray));
        #else
        graphics_context_set_fill_color(ctx,bank_hl?GColorWhite:GColorDarkGray);
        #endif
        graphics_fill_rect(ctx,GRect(margin+btn_w+10,bot_y,btn_w,18),4,GCornersAll);
        char bbuf[20];
        if(can_bank) snprintf(bbuf,sizeof(bbuf),"Bank %d",total);
        else snprintf(bbuf,sizeof(bbuf),"Need 500+");
        graphics_context_set_text_color(ctx,bank_hl?GColorBlack:(can_bank?GColorWhite:GColorLightGray));
        graphics_draw_text(ctx,bbuf,f_sm,
          GRect(margin+btn_w+10,bot_y,btn_w,18),GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);
        graphics_context_set_text_color(ctx,GColorWhite);
      }
      // Green underline on scoreable dice
      #ifdef PBL_COLOR
      for(int i=0;i<NUM_DICE;i++){
        if(s_active[i]&&!s_kept[i]&&die_can_score(i)){
          int row=i/3,col=i%3;
          int dx=start_x+col*(die_sz+gap);
          int dy=(row==0)?row1_y:row2_y;
          graphics_context_set_fill_color(ctx,GColorGreen);
          graphics_fill_rect(ctx,GRect(dx-die_sz/2-1,dy+die_sz/2+1,die_sz+2,2),0,GCornerNone);
        }
      }
      #endif
    }
    else if(s_state==ST_FARKLE){
      #ifdef PBL_COLOR
      graphics_context_set_text_color(ctx,GColorRed);
      #endif
      graphics_draw_text(ctx,"FARKLE!",f_lg,
        GRect(0,bot_y-4,w,32),GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);
      graphics_context_set_text_color(ctx,GColorWhite);
      const char *msg=(s_num_players>1)?"SELECT: next player":"SELECT to continue";
      graphics_draw_text(ctx,msg,f_sm,
        GRect(0,bot_y+24,w,16),GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);
    }
    else if(s_state==ST_BANKED){
      #ifdef PBL_COLOR
      graphics_context_set_text_color(ctx,GColorGreen);
      #endif
      graphics_draw_text(ctx,"BANKED!",f_lg,
        GRect(0,bot_y-4,w,32),GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);
      graphics_context_set_text_color(ctx,GColorWhite);
      const char *msg=(s_num_players>1)?"SELECT: next player":"SELECT to continue";
      graphics_draw_text(ctx,msg,f_sm,
        GRect(0,bot_y+24,w,16),GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);
    }
    else if(s_state==ST_WIN){
      #ifdef PBL_COLOR
      graphics_context_set_text_color(ctx,GColorYellow);
      #endif
      char wbuf[32];
      if(s_num_players>1) {
        snprintf(wbuf,sizeof(wbuf),"%s WINS!",s_tok_name[p->icon]);
      } else {
        snprintf(wbuf,sizeof(wbuf),"YOU WIN!");
      }
      graphics_draw_text(ctx,wbuf,f_lg,
        GRect(0,bot_y-4,w,32),GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);
      graphics_context_set_text_color(ctx,GColorWhite);
      char tbuf2[24]; snprintf(tbuf2,sizeof(tbuf2),"%d pts in %d rounds",p->score,s_rounds);
      graphics_draw_text(ctx,tbuf2,f_sm,
        GRect(0,bot_y+24,w,16),GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);
      graphics_draw_text(ctx,"SELECT for new game",f_sm,
        GRect(0,bot_y+38,w,16),GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);
    }

    // Bottom: total score
    if(s_state!=ST_WIN){
      snprintf(sbuf,sizeof(sbuf),"%d / 10K",p->score);
      graphics_context_set_text_color(ctx,GColorWhite);
      graphics_draw_text(ctx,sbuf,f_md,
        GRect(0,h-(big?32:26),w,24),GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);
    }
  }

  // ======== OVERLAYS ========

  // Scoreboard (hold UP — only during gameplay, multiplayer only)
  if(s_show_scores && s_num_players>1 && s_state>=ST_ROLL) {
    graphics_context_set_fill_color(ctx,GColorBlack);
    int pad=big?25:15;
    graphics_fill_rect(ctx,GRect(pad,pad,w-pad*2,h-pad*2),8,GCornersAll);
    graphics_context_set_text_color(ctx,GColorWhite);
    graphics_draw_text(ctx,"SCORES",f_md,
      GRect(pad,pad+4,w-pad*2,22),GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);
    int ly=pad+28;
    int lh=big?24:20;
    for(int i=0;i<s_num_players;i++){
      int pi=s_order[i];
      bool cur=(i==s_cur_idx);
      draw_token(ctx,pad+14,ly+lh/2,s_players[pi].icon,false);
      char lbl[24];
      snprintf(lbl,sizeof(lbl),"%s: %d",s_tok_name[s_players[pi].icon],s_players[pi].score);
      #ifdef PBL_COLOR
      graphics_context_set_text_color(ctx,cur?GColorYellow:GColorWhite);
      #endif
      graphics_draw_text(ctx,lbl,cur?f_md:f_sm,
        GRect(pad+28,ly+2,w-pad*2-36,lh),GTextOverflowModeTrailingEllipsis,GTextAlignmentLeft,NULL);
      ly+=lh;
    }
    graphics_context_set_text_color(ctx,GColorWhite);
  }

  // Scoring reference (hold DOWN)
  if(s_show_help) {
    graphics_context_set_fill_color(ctx,GColorBlack);
    int pad=big?20:10;
    graphics_fill_rect(ctx,GRect(pad,pad,w-pad*2,h-pad*2),8,GCornersAll);
    graphics_context_set_text_color(ctx,GColorWhite);
    int ly=pad+4; int lh=big?18:15;
    graphics_draw_text(ctx,"SCORING",f_md,
      GRect(pad,ly,w-pad*2,22),GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);
    ly+=lh+6;
    const char *lines[]={"Single 1 = 100","Single 5 = 50","Three 1s = 1000",
      "Three Xs = X*100","4-of-kind = 2x","5-of-kind = 4x","6-of-kind = 8x",
      "Straight = 1500","3 Pairs = 1500","2 Trips = 2500"};
    #ifdef PBL_COLOR
    graphics_context_set_text_color(ctx,GColorYellow);
    #endif
    for(int i=0;i<10&&ly<h-pad-lh;i++){
      graphics_draw_text(ctx,lines[i],f_sm,
        GRect(pad+8,ly,w-pad*2-16,lh),GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);
      ly+=lh;
    }
  }
}

// ============================================================================
// BUTTON HANDLERS
// ============================================================================
static bool pos_valid(int pos) {
  if(pos<NUM_DICE) {
    if(!s_active[pos]) return false;
    if(s_locked[pos]) return false;
    // Unkept dice that can score — always navigable
    if(!s_kept[pos]) return die_can_score(pos);
    // Kept 1s/5s — can individually deselect
    int v=s_dice[pos];
    if(v==1||v==5) return true;
    // Kept non-1/5 (triplet) — stop on the FIRST one only so player can deselect the group
    for(int j=0;j<pos;j++)
      if(s_active[j]&&s_kept[j]&&s_dice[j]==v) return false;  // Not the first — skip
    return true;  // First of the group — navigable
  }
  if(pos==POS_ROLL) return s_select_score>0;
  if(pos==POS_BANK) {
    if(s_select_score<=0) return false;
    Player *p=&s_players[s_order[s_cur_idx]];
    int total=s_turn_score+s_select_score;
    return (p->score>0 || total>=MIN_OPEN);
  }
  return false;
}
static void move_cursor(int dir) {
  int start=s_cursor;
  int limit=(s_select_score>0)?NUM_POS:NUM_DICE;
  do { s_cursor=(s_cursor+dir+limit)%limit; } while(!pos_valid(s_cursor)&&s_cursor!=start);
}

static void select_click(ClickRecognizerRef ref, void *ctx) {
  if(s_state==ST_SETUP){
    s_num_players=s_setup_cursor+1;
    init_game();
    s_state=(s_num_players>1)?ST_ORDER:ST_ROLL;
    if(s_num_players==1) new_turn();
  }
  else if(s_state==ST_ORDER){
    new_turn();
    s_state=ST_ROLL;
  }
  else if(s_state==ST_ROLL){
    roll_dice();
  }
  else if(s_state==ST_SELECT){
    if(s_cursor<NUM_DICE){
      int i=s_cursor;
      if(s_active[i]&&!s_locked[i]){
        int v=s_dice[i];
        // Count unkept matching dice to decide single vs triplet
        int unkept=0;
        for(int j=0;j<NUM_DICE;j++)
          if(s_active[j]&&!s_locked[j]&&!s_kept[j]&&s_dice[j]==v) unkept++;
        bool as_triple=(unkept>=3);

        if(s_kept[i]){
          // Deselect: count how many of this value are kept
          int kept_cnt=0;
          for(int j=0;j<NUM_DICE;j++)
            if(s_active[j]&&!s_locked[j]&&s_kept[j]&&s_dice[j]==v) kept_cnt++;
          if(kept_cnt>=3) {
            // Deselect the triplet (3 at a time)
            int rm=0; for(int j=NUM_DICE-1;j>=0;j--)
              if(s_active[j]&&!s_locked[j]&&s_dice[j]==v&&s_kept[j]&&rm<3){s_kept[j]=false;rm++;}
          } else {
            // Individual deselect (single 1 or 5)
            s_kept[i]=false;
          }
        } else if(die_can_score(i)){
          // Select: triplet if 3+ available, else individual (1s/5s only)
          if(as_triple) {
            int pk=0; for(int j=0;j<NUM_DICE;j++)
              if(s_active[j]&&!s_locked[j]&&s_dice[j]==v&&!s_kept[j]&&pk<3){s_kept[j]=true;pk++;}
          } else {
            s_kept[i]=true;
          }
        }
        s_select_score=calc_selected_score();
        // Move to next selectable die forward, or Roll if none left
        if(s_select_score>0) {
          bool found=false;
          for(int j=s_cursor+1;j<NUM_DICE;j++) {
            if(pos_valid(j)&&!s_kept[j]) { s_cursor=j; found=true; break; }
          }
          if(!found) s_cursor=POS_ROLL;
        }
      }
    } else if(s_cursor==POS_ROLL&&s_select_score>0){
      lock_selected(); roll_dice();
    } else if(s_cursor==POS_BANK&&s_select_score>0){
      s_turn_score+=s_select_score;
      int saved_sel=s_select_score;
      s_select_score=0;
      bank_score();
      // If bank failed (under MIN_OPEN), restore
      if(s_state==ST_SELECT) {
        s_turn_score-=saved_sel;
        s_select_score=saved_sel;
      }
    }
  }
  else if(s_state==ST_FARKLE||s_state==ST_BANKED){
    if(s_num_players>1) next_player();
    else { s_rounds++; new_turn(); }
  }
  else if(s_state==ST_WIN){
    s_state=ST_SETUP; s_setup_cursor=0;
  }
  if(s_canvas) layer_mark_dirty(s_canvas);
}

static void up_click(ClickRecognizerRef ref, void *ctx) {
  if(s_state==ST_SETUP){
    s_setup_cursor=(s_setup_cursor+5)%6;
  } else if(s_state==ST_SELECT){
    move_cursor(-1);
  }
  if(s_canvas) layer_mark_dirty(s_canvas);
}

static void down_click(ClickRecognizerRef ref, void *ctx) {
  if(s_state==ST_SETUP){
    s_setup_cursor=(s_setup_cursor+1)%6;
  } else if(s_state==ST_SELECT){
    move_cursor(1);
  }
  if(s_canvas) layer_mark_dirty(s_canvas);
}

static void back_click(ClickRecognizerRef ref, void *ctx) {
  if(s_state!=ST_SETUP && s_state!=ST_WIN) {
    // Go back to setup
    s_state=ST_SETUP; s_setup_cursor=0;
    if(s_canvas) layer_mark_dirty(s_canvas);
  } else {
    window_stack_pop(true);
  }
}

// Hold UP = scoreboard
static void up_long_down(ClickRecognizerRef ref, void *ctx) {
  s_show_scores=true; if(s_canvas) layer_mark_dirty(s_canvas);
}
static void up_long_up(ClickRecognizerRef ref, void *ctx) {
  s_show_scores=false; if(s_canvas) layer_mark_dirty(s_canvas);
}

// Hold DOWN = scoring reference
static void down_long_down(ClickRecognizerRef ref, void *ctx) {
  s_show_help=true; if(s_canvas) layer_mark_dirty(s_canvas);
}
static void down_long_up(ClickRecognizerRef ref, void *ctx) {
  s_show_help=false; if(s_canvas) layer_mark_dirty(s_canvas);
}

static void click_config(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
  window_single_click_subscribe(BUTTON_ID_UP, up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click);
  window_single_click_subscribe(BUTTON_ID_BACK, back_click);
  window_long_click_subscribe(BUTTON_ID_UP, 500, up_long_down, up_long_up);
  window_long_click_subscribe(BUTTON_ID_DOWN, 500, down_long_down, down_long_up);
}

// ============================================================================
// WINDOW
// ============================================================================
static void win_load(Window *w) {
  Layer *wl=window_get_root_layer(w);
  GRect b=layer_get_bounds(wl);
  s_canvas=layer_create(b);
  layer_set_update_proc(s_canvas,canvas_proc);
  layer_add_child(wl,s_canvas);
  window_set_click_config_provider(w,click_config);
  s_state=ST_SETUP;
  s_setup_cursor=0;
}

static void win_unload(Window *w) {
  if(s_canvas){layer_destroy(s_canvas);s_canvas=NULL;}
}

// ============================================================================
// LIFECYCLE
// ============================================================================
static void init(void) {
  srand(time(NULL));
  if(persist_exists(P_HI_SCORE)) s_hi_score=persist_read_int(P_HI_SCORE);
  if(persist_exists(P_HI_ROUNDS)) s_hi_rounds=persist_read_int(P_HI_ROUNDS);
  s_icon_font_20=fonts_load_custom_font(resource_get_handle(RESOURCE_ID_ICON_FONT_20));
  s_icon_font_14=fonts_load_custom_font(resource_get_handle(RESOURCE_ID_ICON_FONT_14));
  s_win=window_create();
  window_set_background_color(s_win,GColorBlack);
  window_set_window_handlers(s_win,(WindowHandlers){.load=win_load,.unload=win_unload});
  window_stack_push(s_win,true);
}

static void deinit(void) {
  window_destroy(s_win);
  fonts_unload_custom_font(s_icon_font_20);
  fonts_unload_custom_font(s_icon_font_14);
}

int main(void) { init(); app_event_loop(); deinit(); return 0; }
