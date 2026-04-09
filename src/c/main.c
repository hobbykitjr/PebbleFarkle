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

// Player icon types
enum { ICN_STAR=0, ICN_HEART, ICN_DIAMOND, ICN_CIRCLE, ICN_SQUARE, ICN_BOLT };
static const char *s_icon_names[] = {"Star","Heart","Diamond","Circle","Square","Bolt"};

// Icon colors
#ifdef PBL_COLOR
static GColor icon_color(int icon) {
  switch(icon) {
    case ICN_STAR:    return GColorYellow;
    case ICN_HEART:   return GColorRed;
    case ICN_DIAMOND: return GColorCyan;
    case ICN_CIRCLE:  return GColorGreen;
    case ICN_SQUARE:  return GColorOrange;
    case ICN_BOLT:    return GColorPurple;
    default:          return GColorWhite;
  }
}
#endif

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
static int s_turns = 0;
static int s_dice_remaining = 6;
static bool s_show_help = false;    // Hold DOWN: scoring reference
static bool s_show_scores = false;  // Hold UP: scoreboard

// ============================================================================
// ICON DRAWING (10x10 pixel icons)
// ============================================================================
static void draw_icon(GContext *ctx, int cx, int cy, int icon, int sz) {
  #ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx, icon_color(icon));
  #else
  graphics_context_set_fill_color(ctx, GColorWhite);
  #endif
  int r = sz/2;
  switch(icon) {
    case ICN_STAR:
      // 5-point star approximation
      graphics_fill_rect(ctx, GRect(cx-r,cy-1,sz,3), 0, GCornerNone);
      graphics_fill_rect(ctx, GRect(cx-1,cy-r,3,sz), 0, GCornerNone);
      graphics_fill_rect(ctx, GRect(cx-r+1,cy-r+1,2,2), 0, GCornerNone);
      graphics_fill_rect(ctx, GRect(cx+r-2,cy-r+1,2,2), 0, GCornerNone);
      graphics_fill_rect(ctx, GRect(cx-r+1,cy+r-2,2,2), 0, GCornerNone);
      graphics_fill_rect(ctx, GRect(cx+r-2,cy+r-2,2,2), 0, GCornerNone);
      break;
    case ICN_HEART:
      graphics_fill_circle(ctx, GPoint(cx-r/2,cy-r/3), r/2+1);
      graphics_fill_circle(ctx, GPoint(cx+r/2,cy-r/3), r/2+1);
      graphics_fill_rect(ctx, GRect(cx-r,cy-1,sz,r), 0, GCornerNone);
      // Point at bottom
      for(int y=0;y<r;y++) {
        int w2=r-y;
        graphics_fill_rect(ctx, GRect(cx-w2,cy+y,w2*2+1,1), 0, GCornerNone);
      }
      break;
    case ICN_DIAMOND:
      for(int y=-r;y<=r;y++) {
        int w2=r-abs(y);
        graphics_fill_rect(ctx, GRect(cx-w2,cy+y,w2*2+1,1), 0, GCornerNone);
      }
      break;
    case ICN_CIRCLE:
      graphics_fill_circle(ctx, GPoint(cx,cy), r);
      break;
    case ICN_SQUARE:
      graphics_fill_rect(ctx, GRect(cx-r,cy-r,sz,sz), 2, GCornersAll);
      break;
    case ICN_BOLT:
      graphics_fill_rect(ctx, GRect(cx-1,cy-r,4,r), 0, GCornerNone);
      graphics_fill_rect(ctx, GRect(cx-3,cy-1,6,3), 0, GCornerNone);
      graphics_fill_rect(ctx, GRect(cx-2,cy+1,4,r), 0, GCornerNone);
      break;
  }
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
  if(s_rolls==0) s_turns++;
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
  new_turn();
}

static void bank_score(void) {
  Player *p=cur_player();
  int total=s_turn_score+s_select_score;
  if(p->score==0 && total<MIN_OPEN) return;
  p->score+=total;
  s_state=ST_BANKED;
  if(p->score>=WIN_SCORE) s_state=ST_WIN;
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
  s_turns=0;
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
      GRect(0,h*10/100,w,34),GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);
    graphics_draw_text(ctx,"How many players?",f_sm,
      GRect(0,h*25/100,w,18),GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);

    // Player count options
    const char *opts[]={"Solo","2 Players","3 Players","4 Players","5 Players","6 Players"};
    int oy=h*35/100;
    int row_h=big?26:22;
    for(int i=0;i<6;i++){
      bool sel=(s_setup_cursor==i);
      if(sel){
        #ifdef PBL_COLOR
        graphics_context_set_fill_color(ctx,GColorFromHEX(0x006600));
        #else
        graphics_context_set_fill_color(ctx,GColorWhite);
        #endif
        int mx=big?40:20;
        graphics_fill_rect(ctx,GRect(mx,oy,w-mx*2,row_h),4,GCornersAll);
      }
      #ifdef PBL_COLOR
      graphics_context_set_text_color(ctx,sel?GColorWhite:GColorLightGray);
      #else
      graphics_context_set_text_color(ctx,sel?GColorBlack:GColorWhite);
      #endif
      graphics_draw_text(ctx,opts[i],sel?f_md:f_sm,
        GRect(0,oy+2,w,row_h),GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);
      oy+=row_h;
    }
    graphics_context_set_text_color(ctx,GColorWhite);
    graphics_draw_text(ctx,"Hold UP: Scores  Hold DOWN: Rules",f_sm,
      GRect(0,h-20,w,16),GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);
    // Fall through to overlays below
  }

  // ======== ORDER SCREEN ========
  else if(s_state==ST_ORDER) {
    graphics_context_set_text_color(ctx,GColorWhite);
    graphics_draw_text(ctx,"Turn Order",f_lg,
      GRect(0,h*8/100,w,34),GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);
    graphics_draw_text(ctx,"Pick your token!",f_sm,
      GRect(0,h*22/100,w,16),GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);

    int oy=h*32/100;
    int row_h=big?28:24;
    for(int i=0;i<s_num_players;i++){
      int pi=s_order[i];
      int ix=w/2-40;
      draw_icon(ctx,ix,oy+row_h/2,s_players[pi].icon,big?12:10);
      char lbl[20];
      snprintf(lbl,sizeof(lbl),"%d. %s",i+1,s_icon_names[s_players[pi].icon]);
      graphics_context_set_text_color(ctx,GColorWhite);
      graphics_draw_text(ctx,lbl,f_sm,
        GRect(ix+12,oy+4,w/2+20,18),GTextOverflowModeTrailingEllipsis,GTextAlignmentLeft,NULL);
      oy+=row_h;
    }
    graphics_context_set_text_color(ctx,GColorWhite);
    graphics_draw_text(ctx,"SELECT to start!",f_md,
      GRect(0,h*82/100,w,24),GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);
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
      draw_icon(ctx,w/2,top_y+7,p->icon,big?12:10);
      top_y+=big?16:14;
    }
    graphics_context_set_text_color(ctx,GColorWhite);
    char sbuf[32];
    snprintf(sbuf,sizeof(sbuf),"Turn %d",s_turns);
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
        char bbuf[20]; snprintf(bbuf,sizeof(bbuf),"Bank %d",total);
        graphics_context_set_text_color(ctx,bank_hl?GColorBlack:GColorWhite);
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
        snprintf(wbuf,sizeof(wbuf),"%s WINS!",s_icon_names[p->icon]);
      } else {
        snprintf(wbuf,sizeof(wbuf),"YOU WIN!");
      }
      graphics_draw_text(ctx,wbuf,f_lg,
        GRect(0,bot_y-4,w,32),GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);
      graphics_context_set_text_color(ctx,GColorWhite);
      char tbuf2[24]; snprintf(tbuf2,sizeof(tbuf2),"%d pts in %d turns",p->score,s_turns);
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

  // Scoreboard (hold UP)
  if(s_show_scores && s_num_players>1) {
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
      draw_icon(ctx,pad+16,ly+lh/2,s_players[pi].icon,big?10:8);
      char lbl[24];
      snprintf(lbl,sizeof(lbl),"%s: %d",s_icon_names[s_players[pi].icon],s_players[pi].score);
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
  if(pos<NUM_DICE) return s_active[pos]&&(s_kept[pos]||die_can_score(pos));
  if(pos==POS_ROLL||pos==POS_BANK) return s_select_score>0;
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
        int v=s_dice[i]; bool is_single=(v==1||v==5);
        if(s_kept[i]){
          if(is_single) s_kept[i]=false;
          else { int rm=0; for(int j=NUM_DICE-1;j>=0;j--)
            if(s_active[j]&&!s_locked[j]&&s_dice[j]==v&&s_kept[j]&&rm<3){s_kept[j]=false;rm++;}}
        } else if(die_can_score(i)){
          if(is_single) s_kept[i]=true;
          else { int pk=0; for(int j=0;j<NUM_DICE;j++)
            if(s_active[j]&&!s_locked[j]&&s_dice[j]==v&&pk<3){s_kept[j]=true;pk++;}}
        }
        s_select_score=calc_selected_score();
        if(s_select_score>0) s_cursor=POS_ROLL;
      }
    } else if(s_cursor==POS_ROLL&&s_select_score>0){
      lock_selected(); roll_dice();
    } else if(s_cursor==POS_BANK&&s_select_score>0){
      s_turn_score+=s_select_score; bank_score();
    }
  }
  else if(s_state==ST_FARKLE||s_state==ST_BANKED){
    if(s_num_players>1) next_player(); else new_turn();
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
  s_win=window_create();
  window_set_background_color(s_win,GColorBlack);
  window_set_window_handlers(s_win,(WindowHandlers){.load=win_load,.unload=win_unload});
  window_stack_push(s_win,true);
}

static void deinit(void) { window_destroy(s_win); }

int main(void) { init(); app_event_loop(); deinit(); return 0; }
