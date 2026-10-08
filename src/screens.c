/* LAST AISLE - menus, story screens and game flow */
#include "world.h"
#include "gfx.h"
#include "input.h"
#include "audio.h"
#include "game.h"
#include "cutscene.h"
#include "hub.h"

static const Color INK = {30, 45, 110, 255};
static const Color INK_FADE = {120, 110, 120, 255};

static float st;            /* time in current scene */
static int sel;             /* menu selection */
static float type_t;        /* typewriter */
static Scene opts_return = SC_TITLE;
static bool bg_ready;
static float bg_pan;
static int results_line;
static float results_t;
static const char *death_line;
static const char *death_tip;
static char grade[3];
static int lost_why;        /* LOST_*: what the game over screen says */
static int res_vals[8];
static int res_total;
static bool title_level_loaded;
Scene g_scene_prev;

static void pick_perks(void);

enum { OPT_COUNT = 8 };  /* 4 sliders, 3 toggles, back */

static const int STORE_SIGNS[NUM_LEVELS] = {SPR_P_SIGN_QUICKSTOP, SPR_P_SIGN_MARKET, SPR_P_SIGN_HARDWARE,
                                            SPR_P_SIGN_PHARMACY, SPR_P_SIGN_MEGAMART, SPR_P_SIGN_MALL};

/* ------------------------------------------------------------ helpers */
static bool menu_nav(int n) {
    if (IN.repeat[ACT_MENU_UP]) { sel = (sel + n - 1) % n; audio_play(SFX_UI_MOVE, 0.8f, 0, 1); }
    if (IN.repeat[ACT_MENU_DOWN]) { sel = (sel + 1) % n; audio_play(SFX_UI_MOVE, 0.8f, 0, 1); }
    bool ok = IN.pressed[ACT_CONFIRM] || IN.pressed[ACT_INTERACT];
    if (ok) audio_play(SFX_UI_SELECT, 0.9f, 0, 1);
    return ok;
}

static bool mouse_items(float x, float y0, float dy, int n, float halfw, bool centered) {
    V2 m = IN.mouse_view;
    for (int i = 0; i < n; i++) {
        float y = y0 + i * dy;
        bool inx = centered ? fabsf(m.x - x) < halfw : (m.x >= x - 12 && m.x < x + halfw * 2);
        if (inx && m.y >= y - 4 && m.y < y + dy - 4) {
            if (IN.mouse_moved && sel != i) { sel = i; audio_play(SFX_UI_MOVE, 0.75f, 0, 1); }
            if (IN.click) { sel = i; audio_play(SFX_UI_SELECT, 0.9f, 0, 1); return true; }
        }
    }
    return false;
}

/* the crew coming along tonight (or all still living), as names: "Ozzie", "Ozzie and Wes", "Bex, Ozzie and Wes" */
static void crew_names(char *buf, int n, bool squad) {
    buf[0] = 0;
    int total = 0, k = 0;
    for (int i = 0; i < MAX_CREW; i++) total += squad ? RUN.crew[i] == CR_SQUAD : RUN.crew[i] != CR_DEAD;
    for (int i = 0; i < MAX_CREW; i++) {
        if (squad ? RUN.crew[i] != CR_SQUAD : RUN.crew[i] == CR_DEAD) continue;
        if (k) SDL_strlcat(buf, k == total - 1 ? " and " : ", ", n);
        SDL_strlcat(buf, ARCH[CREW[i].arch].name, n);
        k++;
    }
}

/* the selected entry gets a swipe of yellow highlighter, like a ticked item on a list */
static void draw_menu(const char **items, int n, float x, float y, float dy, bool centered) {
    for (int i = 0; i < n; i++) {
        bool s = i == sel;
        float w = gfx_text_w(FONT_BIG, items[i]);
        float lx = centered ? x - w / 2 : x + (s ? 4 : 0);
        if (s) gfx_marker(lx - 4, y + i * dy - 2, w + 8, 15, COL_YELLOW);
        gfx_text(FONT_BIG, items[i], lx, y + i * dy, s ? COL_BLACK : COL_WHITE, s ? 0 : TXT_OUTLINE);
    }
}

/* the title backdrop: a real generated level, slowly panning, colour graded */
static void bg_prepare(void) {
    if (bg_ready) return;
    uint64_t save_seed = RUN.seed;
    int save_level = RUN.level;
    bool active = RUN.active;
    Run backup = RUN;
    RUN.seed = SDL_GetTicks() ^ 0xA5A5;
    RUN.hp = RUN.maxhp = 8;
    RUN.ninv = 0;
    RUN.weapon.id = IT_NONE;
    memset(RUN.slots, 0, sizeof RUN.slots);
    RUN.bag = IT_BACKPACK;
    memset(RUN.perks, 0, sizeof RUN.perks);
    int lvl = (int)(SDL_GetTicks() % 3) + 1;
    world_start_level(lvl);
    W.actors[0].alive = false;
    W.actors[0].used = false;
    RUN = backup;
    RUN.seed = save_seed;
    RUN.level = save_level;
    RUN.active = active;
    bg_ready = true;
    title_level_loaded = true;
    bg_pan = 0;
    audio_music(MUS_MENU);
}

static void bg_draw(Color tint, float dim) {
    if (!bg_ready) return;
    bg_pan += g_real_dt;
    float cx = W.building.x + W.building.w * 0.5f + sinf(bg_pan * 0.05f) * W.building.w * 0.42f;
    float cy = W.building.y + W.building.h * 0.5f + cosf(bg_pan * 0.037f) * W.building.h * 0.35f;
    G.cam_x = cx;
    G.cam_y = cy;
    G.cam_angle = 0;
    G.cam_zoom = 1.1f + sinf(bg_pan * 0.13f) * 0.05f;
    W.nlights = 0;
    W.time += g_real_dt;
    /* a few ambient lights */
    for (int i = 0; i < W.nprops; i++)
        if (W.props[i].glow) add_light(v2(W.props[i].x + 32, W.props[i].y + 8), 80, COL_AMBER, 0.5f);
    world_draw();
    gfx_begin_hud();
    Color t = tint;
    t.a = (Uint8)(255 * dim);
    gfx_fill(0, 0, VIEW_W, VIEW_H, t);
}

static void bg_release(void) {
    if (!title_level_loaded) return;
    world_free();
    bg_ready = false;
    title_level_loaded = false;
}

/* ------------------------------------------------------------- scenes */
static void on_enter(Scene s) {
    st = 0;
    sel = 0;
    type_t = 0;
    input_clear();
    switch (s) {
    case SC_TITLE:
    case SC_MODE:
    case SC_HOWTO:
    case SC_CREDITS:
        audio_music(MUS_MENU);
        audio_set_muffle(0);
        bg_prepare();
        break;
    case SC_OPTIONS:
        audio_set_muffle(0.3f);
        if (opts_return != SC_PLAY && opts_return != SC_HUB) bg_prepare();
        break;
    case SC_HUB:
        audio_set_muffle(0);
        hub_enter();
        break;
    case SC_CUTSCENE:
        audio_set_muffle(0);
        break;
    case SC_BRIEFING:
        audio_music(MUS_SAFEHOUSE);
        audio_set_muffle(0);
        audio_play(SFX_RADIO, 0.9f, 0, 1);
        break;
    case SC_SAFEHOUSE:
        audio_music(MUS_SAFEHOUSE);
        audio_set_muffle(0);
        if (RUN.stage != RS_HOME) pick_perks();   /* normally rolled (and saved) when the store was cleared */
        if (g_scene_prev == SC_CUTSCENE) type_t = 9999;   /* the homecoming already told it */
        break;
    case SC_RESULTS:
        audio_music(MUS_MENU);
        audio_set_muffle(0);
        results_line = 0;
        results_t = 0;
        break;
    case SC_GAMEOVER:
        audio_music(MUS_NONE);
        audio_play(SFX_GAME_OVER, 0.9f, 0, 1);
        audio_set_muffle(0);
        death_line = DEATH_LINES[irange(0, NUM_DEATH_LINES - 1)];
        death_tip = LOADING_TIPS[irange(0, NUM_LOADING_TIPS - 1)];
        break;
    case SC_ENDING:
        audio_music(MUS_ENDING);
        audio_set_muffle(0);
        break;
    case SC_PLAY:
        audio_set_muffle(0);
        break;
    }
}

bool scene_in_level(void) { return g_scene == SC_PLAY || (g_scene == SC_OPTIONS && opts_return == SC_PLAY); }
bool scene_in_hub(void) { return g_scene == SC_HUB || (g_scene == SC_OPTIONS && opts_return == SC_HUB); }

void scene_set(Scene s) {
    if (s == SC_OPTIONS) opts_return = g_scene;
    if ((s == SC_CUTSCENE || s == SC_BRIEFING || s == SC_HUB) && title_level_loaded) bg_release();
    g_scene_prev = g_scene;
    g_scene = s;
    G.flash = 1.0f;
    G.flash_col = rgba(11, 10, 16, 255);
    on_enter(s);
}

/* --------------------------------------------------------------- flow */
void new_run(int mode) {
    memset(&RUN, 0, sizeof RUN);
    RUN.active = true;
    RUN.mode = (GameMode)mode;
    RUN.seed = ((uint64_t)SDL_GetTicksNS() * 6364136223846793005ull) ^ 0x1234567ull;
    RUN.level = 0;
    RUN.hp = RUN.maxhp = ARCH[AR_PLAYER].hp;
    RUN.bag = IT_BACKPACK;
    Stack bat = {IT_BAT, 1, 22};
    RUN.weapon = bat;
    Stack band = {IT_BANDAGE, 1, 0};
    RUN.inv[RUN.ninv++] = band;
}

static void snapshot(void) {
    RUN.snap_hp = RUN.hp;
    RUN.snap_weapon = RUN.weapon;
    memcpy(RUN.snap_slots, RUN.slots, sizeof RUN.slots);
    RUN.snap_wslot = RUN.wslot;
    RUN.snap_ninv = RUN.ninv;
    memcpy(RUN.snap_inv, RUN.inv, sizeof RUN.inv);
    RUN.snap_bag = RUN.bag;
    RUN.snap_kills = RUN.kills;
    RUN.snap_execs = RUN.execs;
    memcpy(RUN.snap_crew, RUN.crew, sizeof RUN.crew);
    memcpy(RUN.snap_crew_fell, RUN.crew_fell, sizeof RUN.crew_fell);
}

/* back to how the store was entered (story retry / restart) */
void run_restore_snapshot(void) {
    RUN.hp = RUN.snap_hp;
    RUN.weapon = RUN.snap_weapon;
    memcpy(RUN.slots, RUN.snap_slots, sizeof RUN.slots);
    RUN.wslot = RUN.snap_wslot;
    RUN.ninv = RUN.snap_ninv;
    memcpy(RUN.inv, RUN.snap_inv, sizeof RUN.inv);
    RUN.bag = RUN.snap_bag;
    RUN.kills = RUN.snap_kills;
    RUN.execs = RUN.snap_execs;
    memcpy(RUN.crew, RUN.snap_crew, sizeof RUN.crew);   /* back at the store's door: whoever died in there is alive again */
    memcpy(RUN.crew_fell, RUN.snap_crew_fell, sizeof RUN.crew_fell);
}

/* the crew can't carry the run to the store `level` (NUM_LEVELS: there's none after this): nobody left to hold the gate,
   or fewer left than it takes to go there - LOST_*, or 0 */
static int crew_shortfall(int level) {
    int n = crew_alive();
    if (n == 0) return LOST_GATE;
    if (level < NUM_LEVELS && n < LEVELS[level].crew_min) return LOST_SHORT;
    return 0;
}

void start_level(void) {
    bg_release();
    snapshot();
    run_save();
    world_start_level(RUN.level);
    if (RUN.level > STATS.best_level) { STATS.best_level = RUN.level; stats_save(); }
    g_paused = g_inventory_open = false;
    scene_set(SC_PLAY);
}

/* supplies you use yourself: kept unless the list asked for them */
static bool is_tool(ItemId id) { return id == IT_FLASHLIGHT || id == IT_GASOLINE; }

/* hand one stack over: list items first, then everything edible or useful goes to the camp - except what somebody
   asked a favour for, which you hand them yourself. Returns the value delivered; what's left in *s stays with you. */
static int hand_in(Stack *s, int owe[], ItemCount keep[], int nkeep) {
    int value = 0;
    for (int i = 0; i < W.nlist && s->count > 0; i++) {
        if (W.list[i].id != s->id || owe[i] <= 0) continue;
        int n = MINF(owe[i], s->count);
        owe[i] -= n;
        s->count -= n;
        value += ITEMS[s->id].value * n;
    }
    int kept = 0;
    for (int i = 0; i < nkeep && s->count > 0; i++) {
        if (keep[i].id != s->id || keep[i].n <= 0) continue;
        int n = MINF(keep[i].n, s->count);
        keep[i].n -= n;
        s->count -= n;
        kept += n;
    }
    ItemCat c = ITEMS[s->id].cat;
    if (s->count > 0 && (c == CAT_FOOD || c == CAT_SUPPLY) && !is_tool((ItemId)s->id)) {
        value += ITEMS[s->id].value * s->count;
        s->count = 0;
    }
    s->count += kept;
    return value;
}

/* into the locker at the Greenhouse (what came home in the van and won't fit in the bag) */
static bool locker_put(Stack s) {
    int used = 0;
    for (int k = 0; k < RUN.nlocker; k++) used += ITEMS[RUN.locker[k].id].size * RUN.locker[k].count;
    if (used + ITEMS[s.id].size * s.count > 48) return false;
    if (ITEMS[s.id].stack > 1)
        for (int k = 0; k < RUN.nlocker; k++)
            if (RUN.locker[k].id == s.id) { RUN.locker[k].count += s.count; return true; }
    if (RUN.nlocker >= ARRAY_LEN(RUN.locker)) return false;
    RUN.locker[RUN.nlocker++] = s;
    return true;
}

static int deliver(void) {
    Actor *p = player();
    int owe[ARRAY_LEN(W.list)], value = 0;
    for (int i = 0; i < W.nlist; i++) owe[i] = W.list[i].need;
    /* kept back for the camp's favours: this store's, and any brought home earlier and not handed over yet */
    ItemCount keep[MAX_FAVOURS + ARRAY_LEN(W.fav)];
    int nkeep = 0;
    for (int i = 0; i < W.nfav; i++) keep[nkeep++] = (ItemCount){W.fav[i].id, W.fav[i].need};
    for (int f = 0; f < NUM_FAVOURS; f++)
        if (RUN.favour[f] == FS_FOUND) keep[nkeep++] = (ItemCount){FAVOURS[f].item, FAVOURS[f].n};
    ItemCount kept0[ARRAY_LEN(keep)];
    memcpy(kept0, keep, sizeof keep);
    /* what was loaded into the van, and the carts the list counted (pushed, parked in sight, or at the van): the list
       comes out of them first; gear moves into the bag if it fits */
    Stack gear[ARRAY_LEN(W.van_load) + MAX_CARTS * 16];
    int ngear = 0;
    for (int k = 0; k < W.nvan; k++) {
        Stack s = W.van_load[k];
        value += hand_in(&s, owe, keep, nkeep);
        if (s.count > 0) gear[ngear++] = s;
    }
    W.nvan = 0;
    for (int i = 0; i < MAX_CARTS; i++) {
        Cart *c = &W.carts[i];
        if (!cart_counts(c) && !(c->alive && v2_dist(c->pos, W.van) <= 90)) continue;
        for (int k = 0; k < c->n; k++) {
            Stack s = c->items[k];
            value += hand_in(&s, owe, keep, nkeep);
            if (s.count > 0) gear[ngear++] = s;
        }
        c->n = 0;
    }
    Stack bag[INV_MAX];
    int nbag = p->ninv;
    memcpy(bag, p->inv, sizeof(Stack) * nbag);
    p->ninv = 0;
    for (int i = 0; i < nbag; i++) {
        value += hand_in(&bag[i], owe, keep, nkeep);
        if (bag[i].count > 0) p->inv[p->ninv++] = bag[i];
    }
    /* a favour that came home in the van or a cart and won't fit in the bag waits in your locker */
    for (int i = 0; i < ngear; i++) {
        if (inv_add(p, gear[i])) continue;
        bool fav = false;
        for (int k = 0; k < nkeep; k++) if (kept0[k].id == gear[i].id) fav = true;
        if (fav) locker_put(gear[i]);
    }
    RUN.hp = p->hp;
    RUN.weapon = p->weapon;
    memcpy(RUN.slots, p->slots, sizeof RUN.slots);
    RUN.wslot = p->wslot;
    RUN.ninv = p->ninv;
    memcpy(RUN.inv, p->inv, sizeof(Stack) * p->ninv);
    /* the greenhouse shows what the list brought home */
    RUN.nhome = 0;
    for (int i = 0; i < W.nlist && RUN.nhome < ARRAY_LEN(RUN.home); i++) {
        RUN.home[RUN.nhome].id = W.list[i].id;
        RUN.home[RUN.nhome++].n = W.list[i].need;
    }
    return value;
}

void level_complete(void) {
    /* home without enough of the crew left to go on: that's where it ends (story: try the store again, they're back) */
    int lost = crew_shortfall(W.level + 1);
    if (lost) { run_lost(lost); return; }
    /* score breakdown */
    int extra = 0;
    for (int i = 0; i < W.nbonus; i++)
        if (W.bonus[i].done) extra += ITEMS[W.bonus[i].id].value * 3;
    extra += deliver();
    /* the favours for this store: brought home (hand them over at the Greenhouse) or not; older misses are forgotten */
    for (int f = 0; f < NUM_FAVOURS; f++) {
        if (RUN.favour[f] == FS_MISSED || RUN.favour[f] == FS_DECLINED) RUN.favour[f] = FS_DONE;
        if (RUN.favour[f] != FS_OPEN || FAVOURS[f].level != W.level) continue;
        int have = inv_count(player(), FAVOURS[f].item);
        for (int k = 0; k < RUN.nlocker; k++) if (RUN.locker[k].id == FAVOURS[f].item) have += RUN.locker[k].count;
        RUN.favour[f] = have >= FAVOURS[f].n ? FS_FOUND : FS_MISSED;
        SDL_Log("FAVOUR: %s for %s - %s", ITEMS[FAVOURS[f].item].name, ARCH[FAVOURS[f].arch].name,
                RUN.favour[f] == FS_FOUND ? "brought home" : "missed");
    }
    int nweap = 0;
    for (int i = 0; i < W_COUNT; i++) if (W.weapons_used & (1ull << i)) nweap++;
    res_vals[0] = W.kills;
    res_vals[1] = W.score;
    res_vals[2] = W.max_combo >= 2 ? W.max_combo * 150 : 0;
    res_vals[3] = nweap * 120;
    res_vals[4] = extra;
    int secs = (int)W.time;
    res_vals[5] = MAXF(0, 2500 - secs * 6);
    res_vals[6] = W.took_damage ? 0 : 1500;
    res_vals[7] = secs;
    res_total = res_vals[1] + res_vals[2] + res_vals[3] + res_vals[4] + res_vals[5] + res_vals[6];
    int par = 2400 + W.level * 1000;
    float k = (float)res_total / par;
    const char *g = k > 2.2f ? "S" : k > 1.7f ? "A+" : k > 1.3f ? "A" : k > 1.0f ? "B" : k > 0.7f ? "C" : "D";
    SDL_strlcpy(grade, g, sizeof grade);
    SDL_Log("LEVEL %d COMPLETE: kills %d, time %.0fs, score %d, grade %s, hp %d, crew %d of %d alive", W.level + 1, W.kills, W.time,
            res_total, grade, player()->hp, crew_alive(), MAX_CREW);
    /* everybody who came along and back is at the Greenhouse again: ask them anew tomorrow evening */
    for (int k = 0; k < MAX_CREW; k++) if (RUN.crew[k] == CR_SQUAD) RUN.crew[k] = CR_HOME;
    RUN.score += res_total;
    RUN.level_scores[W.level] = res_total;
    SDL_strlcpy(RUN.level_grades[W.level], g, 3);
    RUN.play_time += W.time;
    if (RUN.score > STATS.best_score) STATS.best_score = RUN.score;
    STATS.total_kills += W.kills;
    if (RUN.level >= NUM_LEVELS - 1) {
        /* the run is won the moment the van pulls out */
        STATS.wins++;
        run_save_delete();
        RUN.active = false;
    } else {
        /* saved right away: continuing resumes at the greenhouse with these perks on offer */
        RUN.stage = RS_HOME;
        pick_perks();
        run_save();
    }
    stats_save();
    scene_set(SC_RESULTS);
}

void run_lost(int why) {
    lost_why = why;
    if (why != LOST_DIED) SDL_Log("RUN LOST on level %d: %s (%d of the crew alive)", RUN.level + 1,
                                  why == LOST_GATE ? "nobody left on the gate" : "too few of the crew left", crew_alive());
    if (RUN.mode == MODE_ROGUE) {
        run_save_delete();
        RUN.active = false;
    }
    scene_set(SC_GAMEOVER);
}

void player_died(void) {
    SDL_Log("PLAYER DIED on level %d after %.0fs", W.level + 1, W.time);
    RUN.deaths++;
    STATS.deaths++;
    stats_save();
    run_lost(LOST_DIED);
}

static void pick_perks(void) {
    int pool[PK_COUNT];
    int n = 0;
    for (int i = 0; i < PK_COUNT; i++)
        if (PERKS[i].stacks || RUN.perks[i] == 0) pool[n++] = i;
    for (int i = n - 1; i > 0; i--) { int j = irange(0, i); int t = pool[i]; pool[i] = pool[j]; pool[j] = t; }
    for (int i = 0; i < 3; i++) RUN.offer[i] = i < n ? pool[i] : PK_THICKSKIN;
}

/* ============================================================== update */
static void title_update(void) {
    bool cont = run_save_exists();
    const int n = cont ? 6 : 5;
    int choice = -1;
    if (menu_nav(n)) choice = sel;
    if (mouse_items(330, 120, 20, n, 80, false)) choice = sel;
    if (choice < 0) return;
    int idx = cont ? choice : (choice == 0 ? 0 : choice + 1);
    switch (idx) {
    case 0: scene_set(SC_MODE); break;
    case 1:
        if (run_load()) {
            bg_release();
            /* a roguelike quit while the last of the crew died: the gate's still empty */
            int lost = RUN.mode == MODE_ROGUE ? crew_shortfall(RUN.level + (RUN.stage == RS_HOME)) : 0;
            if (lost) run_lost(lost);
            else scene_set(RUN.stage == RS_HOME ? SC_SAFEHOUSE : SC_HUB);
        }
        break;
    case 2: scene_set(SC_HOWTO); break;
    case 3: scene_set(SC_OPTIONS); break;
    case 4: scene_set(SC_CREDITS); break;
    case 5: g_quit = true; break;
    }
}

static void mode_update(void) {
    if (IN.repeat[ACT_MENU_LEFT] || IN.repeat[ACT_MENU_UP]) { sel = 0; audio_play(SFX_UI_MOVE, 0.8f, 0, 1); }
    if (IN.repeat[ACT_MENU_RIGHT] || IN.repeat[ACT_MENU_DOWN]) { sel = 1; audio_play(SFX_UI_MOVE, 0.8f, 0, 1); }
    V2 m = IN.mouse_view;
    for (int i = 0; i < 2; i++) {
        float x = 50 + i * 200;
        if (m.x >= x && m.x < x + 180 && m.y >= 80 && m.y < 210) {
            if (IN.mouse_moved) sel = i;
            if (IN.click) { IN.pressed[ACT_CONFIRM] = true; sel = i; }
        }
    }
    if (IN.pressed[ACT_BACK]) { scene_set(SC_TITLE); audio_play(SFX_UI_BACK, 0.85f, 0, 1); return; }
    if (IN.pressed[ACT_CONFIRM] || IN.pressed[ACT_INTERACT]) {
        audio_play(SFX_UI_SELECT, 0.8f, 0, 1);
        new_run(sel == 0 ? MODE_STORY : MODE_ROGUE);
        STATS.runs++;
        stats_save();
        bg_release();
        cutscene_start(CUT_INTRO, 0);
    }
}

static int text_len(const char *s) {
    int n = 0;
    for (; *s; s++) {
        if (*s == '^' && s[1]) { s++; continue; }
        if (*s != '\n') n++;
    }
    return n;
}

static void typewriter(const char *text, float speed) {
    int before = (int)type_t;
    type_t += g_real_dt * speed;
    int after = (int)type_t;
    if (after != before && after <= text_len(text) && (after % 2) == 0) audio_play(SFX_TYPE, 0.85f, 0, frange(0.9f, 1.15f));
}

static void briefing_update(void) {
    const LevelDef *d = &LEVELS[RUN.level];
    typewriter(d->radio, 55);
    if (IN.pressed[ACT_BACK]) { audio_play(SFX_UI_BACK, 0.85f, 0, 1); scene_set(SC_HUB); return; }   /* not yet: back to the camp */
    if (IN.pressed[ACT_CONFIRM] || IN.pressed[ACT_INTERACT] || IN.click) {
        if (type_t < text_len(d->radio)) { type_t = 9999; return; }
        cutscene_start(CUT_DRIVE, RUN.level);
    }
}

static void results_update(void) {
    results_t += g_real_dt;
    if (results_line < 9 && results_t > 0.35f) {
        results_t = 0;
        results_line++;
        audio_play(results_line == 9 ? SFX_LEVEL_CLEAR : SFX_UI_SELECT, results_line == 9 ? 0.9f : 0.5f, 0, 1.0f + results_line * 0.04f);
    }
    if (IN.pressed[ACT_CONFIRM] || IN.pressed[ACT_INTERACT] || IN.click) {
        if (results_line < 9) { results_line = 9; return; }
        /* the win was counted in level_complete */
        cutscene_start(RUN.level >= NUM_LEVELS - 1 ? CUT_ENDING : CUT_HOME, RUN.level);
    }
}

static void safehouse_update(void) {
    const LevelDef *d = &LEVELS[RUN.level];
    typewriter(d->after, 45);
    if (IN.repeat[ACT_MENU_LEFT]) { sel = (sel + 2) % 3; audio_play(SFX_UI_MOVE, 0.8f, 0, 1); }
    if (IN.repeat[ACT_MENU_RIGHT]) { sel = (sel + 1) % 3; audio_play(SFX_UI_MOVE, 0.8f, 0, 1); }
    V2 m = IN.mouse_view;
    bool click = false;
    for (int i = 0; i < 3; i++) {
        float x = 60 + i * 124;
        if (m.x >= x && m.x < x + 116 && m.y >= 130 && m.y < 238) {
            if (IN.mouse_moved && sel != i) { sel = i; audio_play(SFX_UI_MOVE, 0.75f, 0, 1); }
            if (IN.click) { sel = i; click = true; }   /* a click takes the card it lands on */
        }
    }
    if (IN.pressed[ACT_CONFIRM] || IN.pressed[ACT_INTERACT] || click) {
        if (type_t < text_len(d->after)) { type_t = 9999; return; }
        int pk = RUN.offer[sel];
        RUN.perks[pk]++;
        if (pk == PK_THICKSKIN) RUN.maxhp += 2;
        RUN.hp = RUN.maxhp;  /* a night's rest */
        audio_play(SFX_PERK, 0.9f, 0, 1);
        RUN.level++;
        RUN.stage = RS_LEVEL;
        RUN.hub_done = 0;   /* a new evening at the Greenhouse */
        run_save();
        scene_set(SC_HUB);
    }
}

static void gameover_update(void) {
    const int n = 2;
    int choice = -1;
    if (st > 1.0f && menu_nav(n)) choice = sel;
    if (st > 1.0f && mouse_items(VIEW_W / 2, 200, 20, n, 90, true)) choice = sel;
    if (choice < 0) return;
    if (choice == 0) {
        if (RUN.mode == MODE_STORY) {
            RUN.retries++;
            run_restore_snapshot();
            RUN.hp = RUN.maxhp;
            start_level();
        } else {
            new_run(MODE_ROGUE);
            STATS.runs++;
            stats_save();
            scene_set(SC_HUB);
        }
    } else {
        world_free();
        scene_set(SC_TITLE);
    }
}

/* the credits roll after the ending cutscene */
static void ending_update(void) {
    if (IN.pressed[ACT_CONFIRM] || IN.pressed[ACT_BACK] || IN.click || st > 40) {
        world_free();
        scene_set(SC_TITLE);
    }
}

static void credits_update(void) {
    if (IN.pressed[ACT_CONFIRM] || IN.pressed[ACT_BACK] || IN.click || st > 30) scene_set(SC_TITLE);
}

static void howto_update(void) {
    if (IN.pressed[ACT_CONFIRM] || IN.pressed[ACT_BACK] || IN.click) {
        audio_play(SFX_UI_BACK, 0.85f, 0, 1);
        scene_set(SC_TITLE);
    }
}

static void options_update(void) {
    const int n = OPT_COUNT;
    if (IN.repeat[ACT_MENU_UP]) { sel = (sel + n - 1) % n; audio_play(SFX_UI_MOVE, 0.8f, 0, 1); }
    if (IN.repeat[ACT_MENU_DOWN]) { sel = (sel + 1) % n; audio_play(SFX_UI_MOVE, 0.8f, 0, 1); }
    V2 m = IN.mouse_view;
    int hit = -1;
    for (int i = 0; i < n; i++) {
        float y = 64 + i * 18;
        if (m.y >= y - 3 && m.y < y + 15 && m.x > 100 && m.x < 380) hit = i;
    }
    if (IN.mouse_moved && hit >= 0) sel = hit;
    int dir = 0;
    if (IN.repeat[ACT_MENU_LEFT]) dir = -1;
    if (IN.repeat[ACT_MENU_RIGHT]) dir = 1;
    bool act = IN.pressed[ACT_CONFIRM] || IN.pressed[ACT_INTERACT];
    float *vol[4] = {&SET.master, &SET.music, &SET.sfx, &SET.shake};
    /* clicks only count on a row: a slider takes the value under the pointer, the rest toggle */
    if (IN.click && hit >= 0) {
        sel = hit;
        if (hit >= 4) act = true;
        else if (m.x >= 246 && m.x < 356) {
            *vol[hit] = roundf(CLAMP((m.x - 250) / 100, 0.0f, 1.0f) * 10) / 10;
            settings_apply();
            audio_play(SFX_UI_MOVE, 0.6f, 0, 1);
        }
    }
    if (sel < 4 && dir) {
        *vol[sel] = CLAMP(*vol[sel] + dir * 0.1f, 0.0f, 1.0f);
        *vol[sel] = roundf(*vol[sel] * 10) / 10;
        settings_apply();
        audio_play(SFX_UI_MOVE, 0.6f, 0, 1);
    }
    if (sel >= 4 && sel < n - 1 && (act || dir)) {
        bool *b[3] = {&SET.grain, &SET.fullscreen, &SET.hints};
        *b[sel - 4] = !*b[sel - 4];
        settings_apply();
        audio_play(SFX_UI_SELECT, 0.85f, 0, 1);
    }
    if ((sel == n - 1 && act) || IN.pressed[ACT_BACK]) {
        settings_save();
        audio_play(SFX_UI_BACK, 0.85f, 0, 1);
        if (opts_return == SC_PLAY || opts_return == SC_HUB) {
            g_scene = opts_return;   /* back to the pause menu over the world, which stays as it was */
            audio_set_muffle(0.6f);
            input_clear();
        } else scene_set(opts_return);
    }
}

void screens_update(float dt) {
    st += dt;
    switch (g_scene) {
    case SC_TITLE: title_update(); break;
    case SC_MODE: mode_update(); break;
    case SC_CUTSCENE: cutscene_update(dt); break;
    case SC_BRIEFING: briefing_update(); break;
    case SC_RESULTS: results_update(); break;
    case SC_SAFEHOUSE: safehouse_update(); break;
    case SC_GAMEOVER: gameover_update(); break;
    case SC_ENDING: ending_update(); break;
    case SC_CREDITS: credits_update(); break;
    case SC_OPTIONS: options_update(); break;
    case SC_HOWTO: howto_update(); break;
    case SC_HUB: hub_update(dt); break;
    default: break;
    }
}

/* =============================================================== draw */
extern void hud_cursor(void);

/* light through holes in the roof, dust hanging in it */
static void sunbeams(void) {
    static const float base[3] = {70, 230, 390}, width[3] = {46, 26, 60};
    for (int b = 0; b < 3; b++) {
        float sway = sinf(G.time * 0.11f + b * 2.1f) * 6;
        for (int y = 0; y < VIEW_H; y += 2) {
            float x = base[b] + sway + y * 0.42f;
            Uint8 a = (Uint8)(14 * (1.0f - (float)y / VIEW_H * 0.7f));
            gfx_fill(x, y, width[b], 2, rgba(255, 226, 170, a));
            gfx_fill(x + 6, y, width[b] - 12, 2, rgba(255, 226, 170, a / 2));
        }
    }
    for (int i = 0; i < 46; i++) {
        float sp = 2.0f + (i % 5) * 0.9f;
        float x = fmodf(i * 97.31f + sinf(G.time * 0.4f + i) * 9 + G.time * 1.5f, VIEW_W);
        float y = fmodf(i * 53.17f + G.time * sp, VIEW_H);
        gfx_fill(x, y, 1, 1, rgba(255, 236, 200, (Uint8)(40 + (i * 37) % 80)));
    }
}

static void dark_bg(Color top, Color bot) {
    gfx_begin_world();
    gfx_begin_hud();
    for (int y = 0; y < VIEW_H; y += 6) {
        float k = (float)y / VIEW_H;
        Color c = color_lerp(top, bot, k);
        gfx_fill(0, y, VIEW_W, 6, c);
    }
    sunbeams();
}

static void draw_title(void) {
    bg_draw(rgb(30, 20, 10), 0.5f);
    sunbeams();
    /* logo: an old store sign, rust running down from the letters */
    gfx_spr_ex(SPR_UI_LOGO, VIEW_W / 2 + 3, 52 + 3, 0, 1.6f, 1.6f, rgba(0, 0, 0, 150));
    gfx_spr_ex(SPR_UI_LOGO, VIEW_W / 2, 52, 0, 1.6f, 1.6f, TINT_NONE);
    gfx_text(FONT_SMALL, "a game about going shopping", VIEW_W / 2, 90, COL_KRAFT, TXT_CENTER | TXT_OUTLINE);
    /* portrait: a polaroid taped to the wall */
    float px = 46, py = 110;
    gfx_fill(px - 3, py - 3, 140, 154, rgba(0, 0, 0, 120));
    gfx_fill(px - 6, py - 6, 140, 154, COL_RECEIPT);
    gfx_fill(px - 6, py + 145, 140, 3, rgb(198, 188, 166));
    gfx_spr_stretch(SPR_UI_PORTRAIT, px, py, 128, 128, TINT_NONE);
    gfx_spr_ex(SPR_UI_TAPE, px + 64, py - 6, 0, 2, 2, TINT_NONE);
    bool cont = run_save_exists();
    const char *a[6] = {"NEW RUN", "CONTINUE", "HOW TO PLAY", "OPTIONS", "CREDITS", "QUIT"};
    const char *b[5] = {"NEW RUN", "HOW TO PLAY", "OPTIONS", "CREDITS", "QUIT"};
    draw_menu(cont ? a : b, cont ? 6 : 5, 330, 120, 20, false);
    char buf[96];
    if (STATS.runs > 0) {
        SDL_snprintf(buf, sizeof buf, "BEST %d   RUNS %d   WINS %d", STATS.best_score, STATS.runs, STATS.wins);
        gfx_text(FONT_SMALL, buf, VIEW_W - 8, VIEW_H - 12, COL_GREY, TXT_RIGHT);
    }
    gfx_text(FONT_SMALL, "v" GAME_VERSION, 8, VIEW_H - 12, COL_GREY, 0);
    hud_cursor();
}

static void draw_mode(void) {
    bg_draw(rgb(24, 18, 12), 0.72f);
    gfx_text(FONT_BIG, "HOW DO YOU WANT TO LIVE?", VIEW_W / 2, 40, COL_WHITE, TXT_CENTER | TXT_SHADOW | TXT_OUTLINE);
    const char *title[2] = {"STORY", "ROGUELIKE"};
    const char *desc[2] = {
        "Die, and try the level again.\nKeep your gear. See the story through.\n\nRecommended for a first run.",
        "Die once and it's over.\nEvery run a new city, new stores.\n\nThe way it was meant to be.",
    };
    for (int i = 0; i < 2; i++) {
        float x = 50 + i * 200, y = 80;
        bool s = sel == i;
        gfx_nine(SPR_UI_PANEL, x, y, 180, 130, s ? TINT_NONE : rgba(150, 150, 160, 255));
        if (s) gfx_rect(x - 2, y - 2, 184, 134, COL_YELLOW);
        gfx_text(FONT_BIG, title[i], x + 90, y + 12, s ? COL_YELLOW : COL_GREY, TXT_CENTER | TXT_SHADOW);
        gfx_text_wrap(FONT_SMALL, desc[i], x + 12, y + 40, 156, s ? COL_WHITE : COL_GREY, 0, 10);
    }
    gfx_text(FONT_SMALL, ctl("^yENTER^0 choose   ^yESC^0 back", "^yA^0 choose   ^yB^0 back"), VIEW_W / 2, 238, COL_WHITE, TXT_CENTER);
    hud_cursor();
}

static void draw_skulls(float x, float y, int n) {
    for (int i = 0; i < 5; i++) gfx_spr_c(SPR_UI_SKULL, x + i * 13, y, i < n ? COL_RED : rgba(80, 70, 90, 255));
}

static void draw_briefing(void) {
    const LevelDef *d = &LEVELS[RUN.level];
    dark_bg(rgb(30, 24, 17), rgb(12, 10, 8));
    char buf[96];
    SDL_snprintf(buf, sizeof buf, "LEVEL %d OF %d", RUN.level + 1, NUM_LEVELS);
    gfx_text(FONT_SMALL, buf, 24, 12, COL_GREY, 0);
    int sign = STORE_SIGNS[CLAMP(RUN.level, 0, NUM_LEVELS - 1)];
    gfx_spr_ex(sign, 24, 24, 0, 2, 2, TINT_NONE);
    gfx_text(FONT_SMALL, d->place, 24, 60, COL_KRAFT, 0);
    gfx_text(FONT_SMALL, "THREAT", 24, 73, COL_GREY, 0);
    draw_skulls(60, 71, d->threat);
    /* the crew in the back of the van */
    {
        char who[64];
        crew_names(who, sizeof who, true);
        gfx_text(FONT_SMALL, "WITH YOU", 136, 73, COL_GREY, 0);
        gfx_text(FONT_SMALL, who[0] ? who : "nobody", 141 + gfx_text_w(FONT_SMALL, "WITH YOU"), 73, who[0] ? COL_GREEN : COL_GREY, 0);
    }
    /* radio message */
    gfx_spr(SPR_UI_RADIO, 24, 92);
    gfx_text(FONT_SMALL, "ROSA, ON THE RADIO:", 62, 94, COL_GREEN, 0);
    gfx_text_wrap_n(FONT_SMALL, d->radio, 62, 108, 236, COL_WHITE, 0, 10, (int)type_t);
    /* the list (preview: what kind of things) */
    float lx = 318, ly = 34;
    int nm = 0, nf = 0;
    for (int i = 0; i < 3; i++) if (d->must[i].id) nm++;
    for (int f = 0; f < NUM_FAVOURS; f++) if (RUN.favour[f] == FS_OPEN && FAVOURS[f].level == RUN.level) nf++;
    gfx_nine(SPR_UI_PAPER, lx, ly, 140, 64 + nm * 16 + (nf ? 6 + nf * 21 : 0), TINT_NONE);
    gfx_spr(SPR_UI_TAPE, lx + 70, ly + 1);
    gfx_text(FONT_SMALL, "WE NEED:", lx + 10, ly + 10, INK, 0);
    float y = ly + 26;
    for (int i = 0; i < 3; i++) {
        if (!d->must[i].id) continue;
        gfx_spr(ITEMS[d->must[i].id].spr, lx + 18, y + 4);
        SDL_snprintf(buf, sizeof buf, "%s x%d", ITEMS[d->must[i].id].name, d->must[i].n);
        gfx_text(FONT_SMALL, buf, lx + 30, y + 1, INK, 0);
        y += 16;
    }
    gfx_text(FONT_SMALL, "+ whatever else", lx + 12, y + 2, INK_FADE, 0);
    gfx_text(FONT_SMALL, "  you can find", lx + 12, y + 12, INK_FADE, 0);
    /* favours the camp asked for: off the list, in another pen */
    if (nf) {
        static const Color INK_FAV = {40, 104, 58, 255};
        y += 28;
        gfx_text(FONT_SMALL, "FAVOURS:", lx + 10, y, INK_FAV, 0);
        y += 12;
        for (int f = 0; f < NUM_FAVOURS; f++) {
            if (RUN.favour[f] != FS_OPEN || FAVOURS[f].level != RUN.level) continue;
            gfx_spr(ITEMS[FAVOURS[f].item].spr, lx + 18, y + 6);
            if (FAVOURS[f].n > 1) SDL_snprintf(buf, sizeof buf, "%s x%d", ITEMS[FAVOURS[f].item].name, FAVOURS[f].n);
            else SDL_strlcpy(buf, ITEMS[FAVOURS[f].item].name, sizeof buf);
            gfx_text(FONT_SMALL, buf, lx + 30, y, INK_FAV, 0);
            SDL_snprintf(buf, sizeof buf, "for %s", ARCH[FAVOURS[f].arch].name);
            gfx_text(FONT_SMALL, buf, lx + 30, y + 9, INK_FADE, 0);
            y += 21;
        }
    }
    /* tip */
    gfx_nine(SPR_UI_PANEL, 18, 206, 444, 30, TINT_NONE);
    gfx_text(FONT_SMALL, "TIP", 26, 212, COL_TAG, 0);
    gfx_text_wrap(FONT_SMALL, d->tip, 50, 212, 400, COL_WHITE, 0, 10);
    /* status */
    SDL_snprintf(buf, sizeof buf, "HEALTH %d/%d   BAG %s   SCORE %d", RUN.hp, RUN.maxhp, ITEMS[RUN.bag].name, RUN.score);
    gfx_text(FONT_SMALL, buf, 24, 242, COL_GREY, 0);
    gfx_text(FONT_SMALL, ctl("^yESC^0 not yet - back to the camp", "^yB^0 not yet - back to the camp"), 24, 254, COL_GREY, 0);
    gfx_text(FONT_BIG, ctl("ENTER: DRIVE", "A: DRIVE"), VIEW_W - 20, 244, ((int)(G.time * 2) & 1) ? COL_YELLOW : COL_ORANGE,
             TXT_RIGHT | TXT_SHADOW);
    hud_cursor();
}

/* ------------------------------------------------------------- receipt */
static const Color RC_INK = {44, 38, 46, 255};
static const Color RC_FADE = {128, 118, 112, 255};

static void receipt_dashes(float x, float y, float w) {
    for (float i = 0; i < w; i += 4) gfx_fill(x + i, y, 2, 1, RC_FADE);
}

/* torn edge: a row of 6px saw teeth hanging off (down) or rising from (up) the paper */
static void receipt_teeth(float x, float y, float w, bool down) {
    for (int i = 0; i < (int)w; i++) {
        int t = 3 - abs(i % 6 - 3);
        if (down) gfx_fill(x + i, y, 1, t, COL_RECEIPT);
        else gfx_fill(x + i, y - t, 1, t, COL_RECEIPT);
    }
}

static void receipt_barcode(float x, float y, float w, float h) {
    uint32_t v = 0x9E3779B9u ^ (uint32_t)(RUN.seed + RUN.level * 7919u);
    for (float i = 0; i < w;) {
        v = v * 1664525u + 1013904223u;
        int bw = 1 + (int)((v >> 28) & 1), gap = 1 + (int)((v >> 25) & 1);
        gfx_fill(x + i, y, bw, h, RC_INK);
        i += bw + gap;
    }
}

static void draw_results(void) {
    static float paper_h;
    dark_bg(rgb(40, 29, 18), rgb(12, 10, 8));
    const float px = 120, pw = 196, top = 10;
    /* the receipt feeds out line by line as the printer works through it */
    float target = results_line < 8 ? 76 + results_line * 12 : (results_line == 8 ? 196 : 238);
    if (results_line == 0) paper_h = 70;
    paper_h = lerpf(paper_h, target, smooth_k(14, g_real_dt));
    gfx_fill(px + 4, top + 4, pw, paper_h, rgba(0, 0, 0, 110));
    gfx_fill(px, top, pw, paper_h, COL_RECEIPT);
    receipt_teeth(px, top, pw, false);
    receipt_teeth(px, top + paper_h, pw, true);
    gfx_fill(px + pw - 2, top, 2, paper_h, rgb(214, 205, 184));

    int sp = STORE_SIGNS[CLAMP(RUN.level, 0, NUM_LEVELS - 1)];
    gfx_spr(sp, px + pw / 2 - g_atlas[sp].w / 2, top + 6);
    gfx_text(FONT_BIG, "STORE CLEARED", px + pw / 2, top + 28, RC_INK, TXT_CENTER);
    char buf[48];
    SDL_snprintf(buf, sizeof buf, "LEVEL %d/%d", RUN.level + 1, NUM_LEVELS);
    gfx_text(FONT_SMALL, buf, px + 10, top + 44, RC_FADE, 0);
    SDL_snprintf(buf, sizeof buf, "NO.%04u", (unsigned)((RUN.seed * 2654435761u + RUN.level * 977u) >> 9) % 10000);
    gfx_text(FONT_SMALL, buf, px + pw - 10, top + 44, RC_FADE, TXT_RIGHT);
    receipt_dashes(px + 8, top + 56, pw - 16);

    const char *labels[7] = {"KILLS", "KILL POINTS", "MAX COMBO", "FLEXIBILITY", "BROUGHT HOME", "SPEED", "UNTOUCHED"};
    char val[32], pts[32];
    for (int i = 0; i < 7 && i < results_line; i++) {
        float y = top + 62 + i * 12;
        gfx_text(FONT_SMALL, labels[i], px + 10, y, RC_INK, 0);
        for (float x = px + 10 + gfx_text_w(FONT_SMALL, labels[i]) + 3; x < px + 122; x += 3) gfx_fill(x, y + 6, 1, 1, RC_FADE);
        val[0] = pts[0] = 0;
        switch (i) {
        case 0: SDL_snprintf(val, sizeof val, "%d", res_vals[0]); break;
        case 1: SDL_snprintf(pts, sizeof pts, "%d", res_vals[1]); break;
        case 2: SDL_snprintf(val, sizeof val, "x%d", W.max_combo); SDL_snprintf(pts, sizeof pts, "+%d", res_vals[2]); break;
        case 3: SDL_snprintf(pts, sizeof pts, "+%d", res_vals[3]); break;
        case 4: SDL_snprintf(pts, sizeof pts, "+%d", res_vals[4]); break;
        case 5: SDL_snprintf(val, sizeof val, "%d:%02d", res_vals[7] / 60, res_vals[7] % 60); SDL_snprintf(pts, sizeof pts, "+%d", res_vals[5]); break;
        case 6: SDL_snprintf(val, sizeof val, "%s", W.took_damage ? "NO" : "YES"); SDL_snprintf(pts, sizeof pts, "+%d", res_vals[6]); break;
        }
        gfx_text(FONT_SMALL, val, px + 140, y, i == 6 && W.took_damage ? COL_TAG : RC_INK, TXT_RIGHT);
        gfx_text(FONT_SMALL, pts, px + pw - 10, y, RC_INK, TXT_RIGHT);
    }
    if (results_line >= 8) {
        receipt_dashes(px + 8, top + 150, pw - 16);
        gfx_text(FONT_BIG, "TOTAL", px + 10, top + 162, RC_INK, 0);
        SDL_snprintf(buf, sizeof buf, "%d", res_total);
        gfx_text_big(FONT_BIG, buf, px + pw - 10, top + 156, 2, RC_INK, TXT_RIGHT);
        receipt_dashes(px + 8, top + 186, pw - 16);
    }
    if (results_line >= 9) {
        gfx_text(FONT_SMALL, "THANK YOU FOR SHOPPING", px + pw / 2, top + 194, RC_INK, TXT_CENTER);
        receipt_barcode(px + pw / 2 - 50, top + 208, 100, 16);
        /* the grade gets rubber-stamped next to the slip: one heavy thump, then it sits */
        float cx = 384, cy = 128, ang = -0.22f;
        bool thump = results_t < 0.1f;
        Color ink = COL_TAG;
        ink.a = 235;
        float r = thump ? 44 : 38;
        gfx_ring(cx, cy, r, 3, ink);
        gfx_ring(cx, cy, r - 6, 1, ink);
        gfx_text_rot(FONT_SMALL, "GRADE", cx + sinf(ang) * (r - 14), cy - cosf(ang) * (r - 14), 1, ang, ink);
        gfx_text_rot(FONT_BIG, grade, cx, cy + 4, thump ? 5 : 4, ang, ink);
        gfx_text(FONT_SMALL, ctl("^yENTER^0 go home", "^yA^0 go home"), VIEW_W - 16, VIEW_H - 18, COL_WHITE, TXT_RIGHT);
    }
    hud_cursor();
}

static void draw_safehouse(void) {
    const LevelDef *d = &LEVELS[RUN.level];
    dark_bg(rgb(18, 38, 20), rgb(10, 12, 8));
    gfx_text_big(FONT_BIG, "THE GREENHOUSE", VIEW_W / 2, 8, 2, COL_GREEN, TXT_CENTER | TXT_SHADOW);
    gfx_text_wrap_n(FONT_SMALL, d->after, VIEW_W / 2, 40, 360, COL_WHITE, TXT_CENTER, 10, (int)type_t);
    if (RUN.nhome > 0) {
        int n = 0;
        for (int i = 0; i < RUN.nhome; i++) n += RUN.home[i].n;
        float lw = gfx_text_w(FONT_SMALL, "BROUGHT HOME") + 6;
        float x = VIEW_W / 2 - (lw + n * 18) / 2;
        gfx_text(FONT_SMALL, "BROUGHT HOME", x, 90, COL_GREEN, 0);
        x += lw;
        for (int i = 0; i < RUN.nhome; i++)
            for (int k = 0; k < RUN.home[i].n; k++) {
                float bob = sinf(G.time * 3 + x * 0.1f) * 1.0f;
                gfx_spr(ITEMS[RUN.home[i].id].spr, x + 8, 94 + bob);
                x += 18;
            }
    }
    {
        char who[64] = "", buf[112];
        int n = 0;
        for (int k = 0; k < MAX_CREW; k++) {
            if (RUN.crew[k] != CR_DEAD || RUN.crew_fell[k] != RUN.level) continue;
            if (n++) SDL_strlcat(who, ", ", sizeof who);
            SDL_strlcat(who, ARCH[CREW[k].arch].name, sizeof who);
        }
        if (n) {
            SDL_snprintf(buf, sizeof buf, "DIDN'T COME HOME: %s", who);
            gfx_text(FONT_SMALL, buf, VIEW_W / 2, 103, COL_RED, TXT_CENTER);
        }
    }
    gfx_text(FONT_SMALL, "Before the next run, you...", VIEW_W / 2, 114, COL_YELLOW, TXT_CENTER);
    for (int i = 0; i < 3; i++) {
        float x = 60 + i * 124, y = 130;
        bool s = sel == i;
        const PerkDef *pk = &PERKS[RUN.offer[i]];
        float lift = s ? -4 + sinf(G.time * 5) : 0;
        gfx_nine(SPR_UI_PANEL, x, y + lift, 116, 104, s ? TINT_NONE : rgba(140, 140, 150, 255));
        if (s) gfx_rect(x - 2, y + lift - 2, 120, 108, COL_YELLOW);
        gfx_spr_stretch(SPR_UI_PERK + RUN.offer[i], x + 58 - 16, y + lift + 10, 32, 32, TINT_NONE);
        gfx_text(FONT_SMALL, pk->name, x + 58, y + lift + 48, s ? COL_YELLOW : COL_WHITE, TXT_CENTER | TXT_OUTLINE);
        gfx_text_wrap(FONT_SMALL, pk->desc, x + 58, y + lift + 62, 100, s ? COL_WHITE : COL_GREY, TXT_CENTER, 10);
        if (RUN.perks[RUN.offer[i]] > 0) gfx_text(FONT_SMALL, "OWNED", x + 58, y + lift + 92, COL_GREEN, TXT_CENTER);
    }
    gfx_text(FONT_SMALL, ctl("^yENTER^0 choose", "^yA^0 choose"), VIEW_W / 2, 250, COL_WHITE, TXT_CENTER);
    hud_cursor();
}

static void draw_gameover(void) {
    dark_bg(rgb(54, 14, 8), rgb(12, 9, 7));
    char buf[128];
    if (lost_why == LOST_DIED) {
        gfx_text_big(FONT_BIG, "YOU DIED", VIEW_W / 2, 30, 3, COL_RED, TXT_CENTER | TXT_SHADOW | TXT_SHAKE);
        if (death_line) gfx_text(FONT_SMALL, death_line, VIEW_W / 2, 82, COL_WHITE, TXT_CENTER);
    } else {
        /* the crew: nobody left to hold the gate, or not enough of them to go on */
        gfx_text_big(FONT_BIG, "THE GREENHOUSE FELL", VIEW_W / 2, 34, 2, COL_RED, TXT_CENTER | TXT_SHADOW | TXT_SHAKE);
        if (lost_why == LOST_GATE) SDL_strlcpy(buf, "The crew are dead. Nobody was left to hold the gate.", sizeof buf);
        else {
            char who[64];
            int next = MINF(RUN.level + 1, NUM_LEVELS - 1);
            crew_names(who, sizeof who, false);
            SDL_snprintf(buf, sizeof buf, "Only %s left. %s takes %s of you - and the gate needs somebody.", who,
                         STORE_SAID[next], NUM_WORD[LEVELS[next].crew_min]);
        }
        gfx_text_wrap(FONT_SMALL, buf, VIEW_W / 2, 72, 340, COL_WHITE, TXT_CENTER, 10);
    }
    SDL_snprintf(buf, sizeof buf, "Reached level %d - %s", RUN.level + 1, LEVELS[RUN.level].name);
    gfx_text(FONT_SMALL, buf, VIEW_W / 2, 120, COL_GREY, TXT_CENTER);
    SDL_snprintf(buf, sizeof buf, "Kills %d   Executions %d   Score %d", RUN.kills, RUN.execs, RUN.score);
    gfx_text(FONT_SMALL, buf, VIEW_W / 2, 134, COL_GREY, TXT_CENTER);
    if (death_tip) {
        int th = gfx_text_wrap_h(FONT_SMALL, death_tip, 300, 10);
        gfx_nine(SPR_UI_PANEL, 70, 152, 340, th + 14, TINT_NONE);
        gfx_text(FONT_SMALL, "TIP", 78, 159, COL_TAG, 0);
        gfx_text_wrap(FONT_SMALL, death_tip, 100, 159, 300, COL_WHITE, 0, 10);
    }
    if (st > 1.0f) {
        const char *a[2] = {RUN.mode == MODE_STORY ? "TRY AGAIN" : "NEW RUN", "TITLE"};
        draw_menu(a, 2, VIEW_W / 2, 200, 20, true);
    }
    hud_cursor();
}

static void draw_ending(void) {
    dark_bg(rgb(22, 48, 24), rgb(10, 12, 8));
    float y = VIEW_H - st * 18;
    gfx_spr_ex(SPR_UI_LOGO, VIEW_W / 2, y - 30, 0, 1.2f, 1.2f, TINT_NONE);
    for (int i = 0; i < NUM_CREDITS; i++)
        gfx_text(FONT_SMALL, CREDITS[i], VIEW_W / 2, y + i * 13, COL_WHITE, TXT_CENTER | TXT_SHADOW);
    char buf[96];
    SDL_snprintf(buf, sizeof buf, "FINAL SCORE %d", RUN.score);
    gfx_text(FONT_BIG, buf, VIEW_W / 2, y + NUM_CREDITS * 13 + 20, COL_YELLOW, TXT_CENTER | TXT_SHADOW);
}

static void draw_credits(void) {
    bg_draw(rgb(14, 12, 8), 0.78f);
    float y = VIEW_H - st * 22 + 20;
    gfx_spr_ex(SPR_UI_LOGO, VIEW_W / 2, y - 30, 0, 1.2f, 1.2f, TINT_NONE);
    for (int i = 0; i < NUM_CREDITS; i++)
        gfx_text(FONT_SMALL, CREDITS[i], VIEW_W / 2, y + i * 13, COL_WHITE, TXT_CENTER | TXT_SHADOW);
}

static void draw_howto(void) {
    bg_draw(rgb(14, 12, 8), 0.84f);
    gfx_text(FONT_BIG, "HOW TO SHOP", VIEW_W / 2, 10, COL_YELLOW, TXT_CENTER | TXT_SHADOW | TXT_OUTLINE);
    /* the table follows the device in use; the other one is summed up underneath */
    const char *keys[] = {"WASD", "MOUSE", "LMB", "RMB", "E", "SPACE", "SHIFT", "TAB", "F", "Q / 1-3", "R", "ESC"};
    const char *pads[] = {"L STICK", "R STICK", "RT", "LT", "A", "X", "L3", "BACK", "LB / B", "Y / D-PAD", "RB", "START"};
    const char *acts[] = {"Move", "Aim", "Attack / shoot", "Throw your weapon", "Search / take / cart / exit",
                          "Execute a downed enemy", "Look further", "Bag + crafting", "Use bandage / meds",
                          "Switch weapon (you carry three)", "Reload / try again", "Pause"};
    for (int i = 0; i < 12; i++) {
        float y = 36 + i * 13;
        gfx_text(FONT_SMALL, ctl(keys[i], pads[i]), 70, y, COL_YELLOW, TXT_RIGHT);
        gfx_text(FONT_SMALL, acts[i], 78, y, COL_WHITE, 0);
    }
    char rules[512];
    SDL_snprintf(rules, sizeof rules,
                 "Every store comes with a ^yshopping list^0. Find the items on shelves, in fridges, crates and lockers "
                 "- or take them from the people who got there first.\n\n"
                 "Fill the list, then back to the ^yvan^0: ^y%s^0.\n\n"
                 "Your bag is small. ^yShopping carts^0 hold a lot more.\n\n"
                 "Throw things to ^yknock people down^0, then finish them.\n\n"
                 "Combine junk into better weapons in the ^ycraft^0 menu.\n\n"
                 "Between stores, walk the ^yGreenhouse^0: train, tinker, and ask the ^ycrew^0 along.",
                 ctl("E", "A"));
    gfx_text_wrap(FONT_SMALL, rules, 252, 36, 212, COL_WHITE, 0, 10);
    gfx_text(FONT_SMALL,
             ctl("Gamepad: sticks move/aim, RT attack, LT throw, A use, X execute, L3 look, BACK bag,\n"
                 "LB or B heal, Y or D-PAD switch weapon, RB reload, START pause. Menus: A choose, B back.",
                 "Keyboard: WASD move, mouse aim, LMB attack, RMB throw, E use, SPACE execute, SHIFT look,\n"
                 "TAB bag, F heal, Q / 1-3 / wheel switch weapon, R reload, ESC pause. Menus: ENTER choose, ESC back."),
             VIEW_W / 2, 220, COL_GREY, TXT_CENTER);
    gfx_text(FONT_SMALL, ctl("^yENTER^0 back", "^yA^0 back"), VIEW_W / 2, 250, COL_WHITE, TXT_CENTER);
    hud_cursor();
}

static void draw_options(void) {
    if (opts_return == SC_PLAY || opts_return == SC_HUB) {
        world_draw();
        gfx_begin_hud();
        gfx_fill(0, 0, VIEW_W, VIEW_H, rgba(11, 10, 16, 200));
    } else bg_draw(rgb(14, 12, 8), 0.82f);
    gfx_text(FONT_BIG, "OPTIONS", VIEW_W / 2, 26, COL_YELLOW, TXT_CENTER | TXT_SHADOW | TXT_OUTLINE);
    const char *labels[OPT_COUNT] = {"MASTER VOLUME", "MUSIC", "SOUND FX", "SCREEN SHAKE", "FILM GRAIN", "FULLSCREEN", "HINTS", "BACK"};
    float vals[4] = {SET.master, SET.music, SET.sfx, SET.shake};
    bool bools[3] = {SET.grain, SET.fullscreen, SET.hints};
    for (int i = 0; i < OPT_COUNT; i++) {
        float y = 64 + i * 18;
        bool s = sel == i;
        float tw = gfx_text_w(FONT_SMALL, labels[i]);
        if (s) gfx_marker(116, y - 2, tw + 8, 11, COL_YELLOW);
        gfx_text(FONT_SMALL, labels[i], 120, y, s ? COL_BLACK : COL_WHITE, s ? 0 : TXT_OUTLINE);
        if (i < 4) {
            float w = 100;
            gfx_fill(250, y + 2, w, 5, rgba(46, 40, 34, 255));
            gfx_fill(250, y + 2, w * vals[i], 5, s ? COL_YELLOW : COL_GREY);
            char buf[16];
            SDL_snprintf(buf, sizeof buf, "%d%%", (int)roundf(vals[i] * 100));
            gfx_text(FONT_SMALL, buf, 360, y, s ? COL_YELLOW : COL_WHITE, 0);
        } else if (i < OPT_COUNT - 1) {
            gfx_text(FONT_SMALL, bools[i - 4] ? "ON" : "OFF", 250, y, bools[i - 4] ? COL_GREEN : COL_GREY, 0);
        }
    }
    gfx_text(FONT_SMALL, ctl("^yLEFT/RIGHT^0 change   ^yESC^0 back", "^yD-PAD^0 change   ^yB^0 back"), VIEW_W / 2, 236, COL_GREY,
             TXT_CENTER);
    hud_cursor();
}

void screens_draw(void) {
    switch (g_scene) {
    case SC_TITLE: draw_title(); break;
    case SC_MODE: draw_mode(); break;
    case SC_CUTSCENE: cutscene_draw(); break;
    case SC_BRIEFING: draw_briefing(); break;
    case SC_RESULTS: draw_results(); break;
    case SC_SAFEHOUSE: draw_safehouse(); break;
    case SC_GAMEOVER: draw_gameover(); break;
    case SC_ENDING: draw_ending(); break;
    case SC_CREDITS: draw_credits(); break;
    case SC_OPTIONS: draw_options(); break;
    case SC_HOWTO: draw_howto(); break;
    case SC_HUB: hub_draw(); break;
    default: break;
    }
}
