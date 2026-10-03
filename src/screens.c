/* LAST AISLE - menus, story screens and game flow */
#include "world.h"
#include "gfx.h"
#include "input.h"
#include "audio.h"
#include "game.h"

static const Color INK = {30, 45, 110, 255};
static const Color INK_FADE = {120, 110, 120, 255};

static float st;            /* time in current scene */
static int sel;             /* menu selection */
static int page;            /* story page */
static float type_t;        /* typewriter */
static Scene opts_return = SC_TITLE;
static bool bg_ready;
static float bg_pan;
static int perk_choice[3];
static int results_line;
static float results_t;
static const char *death_line;
static const char *death_tip;
static char grade[3];
static int res_vals[8];
static int res_total;
static bool title_level_loaded;

static void pick_perks(void);

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
            if (IN.click && sel == i) { audio_play(SFX_UI_SELECT, 0.9f, 0, 1); return true; }
        }
    }
    return false;
}

static void draw_menu(const char **items, int n, float x, float y, float dy, bool centered) {
    for (int i = 0; i < n; i++) {
        bool s = i == sel;
        int flags = TXT_OUTLINE | (centered ? TXT_CENTER : 0) | (s ? TXT_WAVE | TXT_SHADOW : 0);
        Color c = s ? gfx_rainbow(G.time * 0.4f) : COL_WHITE;
        float ox = s ? sinf(G.time * 8) * 1.5f + 4 : 0;
        gfx_text(FONT_BIG, items[i], x + (centered ? 0 : ox), y + i * dy, c, flags);
        if (s) {
            float w = gfx_text_w(FONT_BIG, items[i]);
            float ax = centered ? x - w / 2 - 12 : x - 10 + ox;
            gfx_spr(SPR_UI_ARROW, ax, y + i * dy + 6);
        }
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
    G.cam_angle = sinf(bg_pan * 0.2f) * 0.03f;
    G.cam_zoom = 1.1f + sinf(bg_pan * 0.13f) * 0.05f;
    W.nlights = 0;
    W.time += g_real_dt;
    /* a few ambient lights */
    for (int i = 0; i < W.nprops; i++)
        if (W.props[i].glow) add_light(v2(W.props[i].x + 32, W.props[i].y + 8), 80, COL_PINK, 0.5f);
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
    page = 0;
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
        if (opts_return != SC_PLAY) bg_prepare();
        break;
    case SC_INTRO:
        audio_music(MUS_SAFEHOUSE);
        break;
    case SC_BRIEFING:
        audio_music(MUS_SAFEHOUSE);
        audio_set_muffle(0);
        audio_play(SFX_RADIO, 0.9f, 0, 1);
        break;
    case SC_SAFEHOUSE:
        audio_music(MUS_SAFEHOUSE);
        audio_set_muffle(0);
        pick_perks();
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

Scene g_scene_prev;

void scene_set(Scene s) {
    if (s == SC_OPTIONS) opts_return = g_scene;
    if ((s == SC_INTRO || s == SC_BRIEFING || s == SC_PLAY) && title_level_loaded && s != SC_PLAY) bg_release();
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
    RUN.snap_ninv = RUN.ninv;
    memcpy(RUN.snap_inv, RUN.inv, sizeof RUN.inv);
    RUN.snap_bag = RUN.bag;
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

static void deliver(void) {
    /* everything edible or useful goes to the camp; tools stay with you */
    Actor *p = player();
    for (int i = 0; i < W.nlist; i++) {
        int need = W.list[i].need;
        int from_bag = MINF(need, inv_count(p, W.list[i].id));
        inv_remove(p, W.list[i].id, from_bag);
    }
    RUN.hp = p->hp;
    RUN.weapon = p->weapon;
    RUN.ninv = 0;
    for (int i = 0; i < p->ninv; i++) {
        ItemCat c = ITEMS[p->inv[i].id].cat;
        if (c == CAT_FOOD || c == CAT_SUPPLY) continue;
        RUN.inv[RUN.ninv++] = p->inv[i];
    }
}

void level_complete(void) {
    Actor *p = player();
    /* score breakdown */
    int extra = 0;
    for (int i = 0; i < W.nbonus; i++)
        if (W.bonus[i].done) extra += ITEMS[W.bonus[i].id].value * 3;
    for (int i = 0; i < p->ninv; i++) {
        ItemCat c = ITEMS[p->inv[i].id].cat;
        if (c == CAT_FOOD || c == CAT_SUPPLY) extra += ITEMS[p->inv[i].id].value * p->inv[i].count;
    }
    for (int i = 0; i < MAX_CARTS; i++) {
        Cart *c = &W.carts[i];
        if (!c->alive || v2_dist(c->pos, W.van) > 90) continue;
        for (int k = 0; k < c->n; k++)
            if (ITEMS[c->items[k].id].cat == CAT_FOOD || ITEMS[c->items[k].id].cat == CAT_SUPPLY)
                extra += ITEMS[c->items[k].id].value * c->items[k].count;
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
    SDL_Log("LEVEL %d COMPLETE: kills %d, time %.0fs, score %d, grade %s, hp %d", W.level + 1, W.kills, W.time, res_total, grade, player()->hp);
    RUN.score += res_total;
    RUN.level_scores[W.level] = res_total;
    SDL_strlcpy(RUN.level_grades[W.level], g, 3);
    RUN.play_time += W.time;
    deliver();
    if (RUN.score > STATS.best_score) STATS.best_score = RUN.score;
    STATS.total_kills += W.kills;
    stats_save();
    scene_set(SC_RESULTS);
}

void player_died(void) {
    SDL_Log("PLAYER DIED on level %d after %.0fs", W.level + 1, W.time);
    RUN.deaths++;
    STATS.deaths++;
    stats_save();
    if (RUN.mode == MODE_ROGUE) {
        run_save_delete();
        RUN.active = false;
    }
    scene_set(SC_GAMEOVER);
}

static void pick_perks(void) {
    int pool[PK_COUNT];
    int n = 0;
    for (int i = 0; i < PK_COUNT; i++)
        if (PERKS[i].stacks || RUN.perks[i] == 0) pool[n++] = i;
    for (int i = n - 1; i > 0; i--) { int j = irange(0, i); int t = pool[i]; pool[i] = pool[j]; pool[j] = t; }
    for (int i = 0; i < 3; i++) perk_choice[i] = i < n ? pool[i] : PK_THICKSKIN;
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
            scene_set(SC_BRIEFING);
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
        scene_set(SC_INTRO);
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

static void intro_update(void) {
    const char *txt = INTRO_PAGES[page];
    typewriter(txt, 38);
    bool next = IN.pressed[ACT_CONFIRM] || IN.pressed[ACT_INTERACT] || IN.click;
    if (IN.pressed[ACT_BACK]) { scene_set(SC_BRIEFING); return; }
    if (next) {
        if (type_t < text_len(txt)) type_t = 9999;
        else {
            page++;
            type_t = 0;
            audio_play(SFX_UI_SELECT, 0.4f, 0, 0.8f);
            if (page >= NUM_INTRO_PAGES) scene_set(SC_BRIEFING);
        }
    }
}

static void briefing_update(void) {
    const LevelDef *d = &LEVELS[RUN.level];
    typewriter(d->radio, 55);
    if (IN.pressed[ACT_BACK]) { run_save(); scene_set(SC_TITLE); return; }
    if (IN.pressed[ACT_CONFIRM] || IN.pressed[ACT_INTERACT] || IN.click) {
        if (type_t < text_len(d->radio)) { type_t = 9999; return; }
        audio_play(SFX_ENGINE, 0.8f, 0, 1);
        start_level();
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
        if (RUN.level >= NUM_LEVELS - 1) {
            STATS.wins++;
            stats_save();
            run_save_delete();
            RUN.active = false;
            scene_set(SC_ENDING);
        } else {
            scene_set(SC_SAFEHOUSE);
        }
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
            if (IN.click) click = true;
        }
    }
    if (IN.pressed[ACT_CONFIRM] || IN.pressed[ACT_INTERACT] || click) {
        if (type_t < text_len(d->after)) { type_t = 9999; return; }
        int pk = perk_choice[sel];
        RUN.perks[pk]++;
        if (pk == PK_THICKSKIN) RUN.maxhp += 2;
        RUN.hp = RUN.maxhp;  /* a night's rest */
        audio_play(SFX_PERK, 0.9f, 0, 1);
        RUN.level++;
        run_save();
        scene_set(SC_BRIEFING);
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
            RUN.hp = RUN.maxhp;
            RUN.weapon = RUN.snap_weapon;
            RUN.ninv = RUN.snap_ninv;
            memcpy(RUN.inv, RUN.snap_inv, sizeof RUN.inv);
            RUN.bag = RUN.snap_bag;
            start_level();
        } else {
            new_run(MODE_ROGUE);
            STATS.runs++;
            stats_save();
            scene_set(SC_BRIEFING);
        }
    } else {
        world_free();
        scene_set(SC_TITLE);
    }
}

static void ending_update(void) {
    if (page < NUM_ENDING_PAGES) {
        const char *txt = ENDING_PAGES[page];
        typewriter(txt, 30);
        if (IN.pressed[ACT_CONFIRM] || IN.pressed[ACT_INTERACT] || IN.click) {
            if (type_t < text_len(txt)) type_t = 9999;
            else { page++; type_t = 0; st = 0; }
        }
    } else {
        if (IN.pressed[ACT_CONFIRM] || IN.pressed[ACT_BACK] || IN.click || st > 40) {
            world_free();
            scene_set(SC_TITLE);
        }
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
    const int n = 9;
    if (IN.repeat[ACT_MENU_UP]) { sel = (sel + n - 1) % n; audio_play(SFX_UI_MOVE, 0.8f, 0, 1); }
    if (IN.repeat[ACT_MENU_DOWN]) { sel = (sel + 1) % n; audio_play(SFX_UI_MOVE, 0.8f, 0, 1); }
    V2 m = IN.mouse_view;
    for (int i = 0; i < n; i++) {
        float y = 64 + i * 18;
        if (IN.mouse_moved && m.y >= y - 3 && m.y < y + 15 && m.x > 100 && m.x < 380) sel = i;
    }
    int dir = 0;
    if (IN.repeat[ACT_MENU_LEFT]) dir = -1;
    if (IN.repeat[ACT_MENU_RIGHT]) dir = 1;
    bool act = IN.pressed[ACT_CONFIRM] || IN.pressed[ACT_INTERACT];
    if (IN.click) {
        if (m.x > 240) dir = m.x > 330 ? 1 : -1;
        act = true;
    }
    float *vol[4] = {&SET.master, &SET.music, &SET.sfx, &SET.shake};
    if (sel < 4 && dir) {
        *vol[sel] = CLAMP(*vol[sel] + dir * 0.1f, 0.0f, 1.0f);
        *vol[sel] = roundf(*vol[sel] * 10) / 10;
        settings_apply();
        audio_play(SFX_UI_MOVE, 0.6f, 0, 1);
    }
    if (sel >= 4 && sel < 8 && (act || dir)) {
        bool *b[4] = {&SET.sway, &SET.scanlines, &SET.fullscreen, &SET.hints};
        *b[sel - 4] = !*b[sel - 4];
        settings_apply();
        audio_play(SFX_UI_SELECT, 0.85f, 0, 1);
    }
    if ((sel == 8 && act) || IN.pressed[ACT_BACK]) {
        settings_save();
        audio_play(SFX_UI_BACK, 0.85f, 0, 1);
        if (opts_return == SC_PLAY) {
            g_scene = SC_PLAY;
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
    case SC_INTRO: intro_update(); break;
    case SC_BRIEFING: briefing_update(); break;
    case SC_RESULTS: results_update(); break;
    case SC_SAFEHOUSE: safehouse_update(); break;
    case SC_GAMEOVER: gameover_update(); break;
    case SC_ENDING: ending_update(); break;
    case SC_CREDITS: credits_update(); break;
    case SC_OPTIONS: options_update(); break;
    case SC_HOWTO: howto_update(); break;
    default: break;
    }
}

/* =============================================================== draw */
extern void hud_cursor(void);

static void dark_bg(Color top, Color bot) {
    gfx_begin_world();
    gfx_begin_hud();
    for (int y = 0; y < VIEW_H; y += 6) {
        float k = (float)y / VIEW_H;
        Color c = color_lerp(top, bot, k);
        gfx_fill(0, y, VIEW_W, 6, c);
    }
    /* drifting scan bands */
    for (int i = 0; i < 4; i++) {
        float y = fmodf(G.time * (12 + i * 7) + i * 80, VIEW_H + 40) - 20;
        gfx_fill(0, y, VIEW_W, 2 + i, rgba(255, 255, 255, 6));
    }
}

static void draw_title(void) {
    bg_draw(rgb(40, 10, 50), 0.45f);
    /* logo */
    float wob = sinf(G.time * 1.3f) * 2;
    gfx_spr_ex(SPR_UI_LOGO, VIEW_W / 2 + 2, 52 + wob + 3, sinf(G.time * 0.8f) * 0.03f, 1.6f, 1.6f, rgba(0, 0, 0, 160));
    gfx_spr_ex(SPR_UI_LOGO, VIEW_W / 2, 52 + wob, sinf(G.time * 0.8f) * 0.03f, 1.6f, 1.6f, TINT_NONE);
    gfx_text(FONT_SMALL, "a game about going shopping", VIEW_W / 2, 90, COL_YELLOW, TXT_CENTER | TXT_OUTLINE);
    /* portrait */
    float px = 46, py = 112;
    gfx_fill(px - 3, py - 3, 134, 134, rgba(11, 10, 16, 200));
    gfx_spr_stretch(SPR_UI_PORTRAIT, px, py, 128, 128, TINT_NONE);
    gfx_rect(px - 3, py - 3, 134, 134, gfx_rainbow(G.time * 0.2f));
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
    bg_draw(rgb(20, 10, 40), 0.7f);
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
        if (s) gfx_rect(x - 2, y - 2, 184, 134, gfx_rainbow(G.time * 0.4f));
        gfx_text(FONT_BIG, title[i], x + 90, y + 12, s ? COL_PINK : COL_GREY, TXT_CENTER | TXT_SHADOW | (s ? TXT_WAVE : 0));
        gfx_text_wrap(FONT_SMALL, desc[i], x + 12, y + 40, 156, s ? COL_WHITE : COL_GREY, 0, 10);
    }
    gfx_text(FONT_SMALL, "^yENTER^0 choose   ^yESC^0 back", VIEW_W / 2, 238, COL_WHITE, TXT_CENTER);
    hud_cursor();
}

static void draw_story_page(const char *txt, Color bg_top) {
    dark_bg(bg_top, rgb(11, 10, 16));
    int n = (int)type_t;
    int h = gfx_text_wrap_h(FONT_SMALL, txt, 340, 12);
    float y = VIEW_H / 2 - h / 2 - 10;
    gfx_text_wrap_n(FONT_SMALL, txt, VIEW_W / 2, y, 340, COL_WHITE, TXT_CENTER | TXT_SHADOW, 12, n);
    if (type_t >= text_len(txt) && ((int)(G.time * 2) & 1))
        gfx_text(FONT_SMALL, "^y>^0", VIEW_W / 2, VIEW_H - 40, COL_WHITE, TXT_CENTER);
}

static void draw_intro(void) {
    draw_story_page(INTRO_PAGES[MINF(page, NUM_INTRO_PAGES - 1)], rgb(30, 14, 40));
    gfx_text(FONT_SMALL, "^kESC skip", VIEW_W - 8, VIEW_H - 12, COL_GREY, TXT_RIGHT);
}

static void draw_skulls(float x, float y, int n) {
    for (int i = 0; i < 5; i++) gfx_spr_c(SPR_UI_SKULL, x + i * 13, y, i < n ? COL_RED : rgba(80, 70, 90, 255));
}

static void draw_briefing(void) {
    const LevelDef *d = &LEVELS[RUN.level];
    dark_bg(rgb(24, 16, 44), rgb(11, 10, 16));
    char buf[96];
    SDL_snprintf(buf, sizeof buf, "LEVEL %d OF %d", RUN.level + 1, NUM_LEVELS);
    gfx_text(FONT_SMALL, buf, 24, 14, COL_GREY, 0);
    gfx_text(FONT_BIG, d->name, 24, 26, gfx_rainbow(G.time * 0.25f), TXT_SHADOW | TXT_OUTLINE | TXT_WAVE);
    gfx_text(FONT_SMALL, d->place, 24, 44, COL_YELLOW, 0);
    gfx_text(FONT_SMALL, "THREAT", 24, 58, COL_GREY, 0);
    draw_skulls(60, 56, d->threat);
    /* radio message */
    gfx_spr(SPR_UI_RADIO, 24, 78);
    gfx_text(FONT_SMALL, "ROSA, ON THE RADIO:", 62, 80, COL_CYAN, 0);
    gfx_text_wrap_n(FONT_SMALL, d->radio, 62, 94, 236, COL_WHITE, 0, 10, (int)type_t);
    /* the list (preview: what kind of things) */
    float lx = 318, ly = 34;
    int nm = 0;
    for (int i = 0; i < 3; i++) if (d->must[i].id) nm++;
    gfx_nine(SPR_UI_PAPER, lx, ly, 140, 64 + nm * 16, TINT_NONE);
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
    /* tip */
    gfx_nine(SPR_UI_PANEL, 18, 206, 444, 30, TINT_NONE);
    gfx_text(FONT_SMALL, "TIP", 26, 212, COL_PINK, 0);
    gfx_text_wrap(FONT_SMALL, d->tip, 50, 212, 400, COL_WHITE, 0, 10);
    /* status */
    SDL_snprintf(buf, sizeof buf, "HEALTH %d/%d   BAG %s   SCORE %d", RUN.hp, RUN.maxhp, ITEMS[RUN.bag].name, RUN.score);
    gfx_text(FONT_SMALL, buf, 24, 246, COL_GREY, 0);
    if ((int)(G.time * 2) & 1) gfx_text(FONT_BIG, "ENTER: DRIVE", VIEW_W - 20, 244, COL_YELLOW, TXT_RIGHT | TXT_SHADOW);
    else gfx_text(FONT_BIG, "ENTER: DRIVE", VIEW_W - 20, 244, COL_ORANGE, TXT_RIGHT | TXT_SHADOW);
    hud_cursor();
}

static void draw_results(void) {
    dark_bg(rgb(50, 10, 40), rgb(11, 10, 16));
    gfx_text_big(FONT_BIG, "STORE CLEARED", VIEW_W / 2, 8, 2, COL_GREEN, TXT_CENTER | TXT_SHADOW | TXT_WAVE);
    {
        static const int signs[NUM_LEVELS] = {SPR_P_SIGN_QUICKSTOP, SPR_P_SIGN_MARKET, SPR_P_SIGN_HARDWARE,
                                              SPR_P_SIGN_PHARMACY, SPR_P_SIGN_MEGAMART, SPR_P_SIGN_MALL};
        int sp = signs[CLAMP(RUN.level, 0, NUM_LEVELS - 1)];
        gfx_glow(VIEW_W / 2, 44, 50, COL_PINK, 0.25f);
        gfx_spr(sp, VIEW_W / 2 - g_atlas[sp].w / 2, 37);
    }
    const char *labels[7] = {"KILLS", "KILL POINTS", "MAX COMBO", "FLEXIBILITY", "BROUGHT HOME", "SPEED", "UNTOUCHED"};
    char val[32], pts[32];
    for (int i = 0; i < 7 && i < results_line; i++) {
        float y = 58 + i * 17;
        gfx_text(FONT_SMALL, labels[i], 70, y, COL_WHITE, TXT_OUTLINE);
        for (float x = 70 + gfx_text_w(FONT_SMALL, labels[i]) + 4; x < 262; x += 4) gfx_fill(x, y + 6, 1, 1, COL_GREY);
        val[0] = 0;
        switch (i) {
        case 0: SDL_snprintf(val, sizeof val, "%d", res_vals[0]); SDL_snprintf(pts, sizeof pts, "%d", res_vals[1]); break;
        case 1: SDL_snprintf(pts, sizeof pts, "%d", res_vals[1]); break;
        case 2: SDL_snprintf(val, sizeof val, "x%d", W.max_combo); SDL_snprintf(pts, sizeof pts, "+%d", res_vals[2]); break;
        case 3: SDL_snprintf(pts, sizeof pts, "+%d", res_vals[3]); break;
        case 4: SDL_snprintf(pts, sizeof pts, "+%d", res_vals[4]); break;
        case 5: SDL_snprintf(val, sizeof val, "%d:%02d", res_vals[7] / 60, res_vals[7] % 60); SDL_snprintf(pts, sizeof pts, "+%d", res_vals[5]); break;
        case 6: SDL_snprintf(val, sizeof val, "%s", W.took_damage ? "NO" : "YES"); SDL_snprintf(pts, sizeof pts, "+%d", res_vals[6]); break;
        }
        if (i == 0) pts[0] = 0;
        gfx_text(FONT_SMALL, val, 262, y, COL_CYAN, TXT_RIGHT);
        gfx_text(FONT_SMALL, pts, 330, y, COL_YELLOW, TXT_RIGHT);
    }
    if (results_line >= 8) {
        char buf[48];
        SDL_snprintf(buf, sizeof buf, "TOTAL %d", res_total);
        gfx_text_big(FONT_BIG, buf, 70, 184, 2, COL_WHITE, TXT_SHADOW);
    }
    if (results_line >= 9) {
        float s = 2.0f + sinf(G.time * 4) * 0.1f;
        (void)s;
        gfx_text(FONT_SMALL, "GRADE", 428, 100, COL_GREY, TXT_CENTER);
        gfx_text_big(FONT_BIG, grade, 428, 112, 5, COL_WHITE, TXT_CENTER | TXT_SHADOW | TXT_RAINBOW | TXT_SHAKE);
        gfx_text(FONT_SMALL, "^yENTER^0 go home", VIEW_W / 2, 244, COL_WHITE, TXT_CENTER);
    }
    hud_cursor();
}

static void draw_safehouse(void) {
    const LevelDef *d = &LEVELS[RUN.level];
    dark_bg(rgb(14, 34, 30), rgb(11, 10, 16));
    gfx_text_big(FONT_BIG, "THE GREENHOUSE", VIEW_W / 2, 8, 2, COL_GREEN, TXT_CENTER | TXT_SHADOW);
    gfx_text_wrap_n(FONT_SMALL, d->after, VIEW_W / 2, 40, 360, COL_WHITE, TXT_CENTER, 10, (int)type_t);
    if (W.nlist > 0) {
        int n = 0;
        for (int i = 0; i < W.nlist; i++) n += W.list[i].need;
        float total_w = n * 18 + 50;
        float x = VIEW_W / 2 - total_w / 2;
        gfx_text(FONT_SMALL, "BROUGHT HOME", x, 90, COL_GREEN, 0);
        x += 64;
        for (int i = 0; i < W.nlist; i++)
            for (int k = 0; k < W.list[i].need; k++) {
                float bob = sinf(G.time * 3 + x * 0.1f) * 1.0f;
                gfx_spr(ITEMS[W.list[i].id].spr, x + 8, 94 + bob);
                x += 18;
            }
    }
    gfx_text(FONT_SMALL, "Before the next run, you...", VIEW_W / 2, 114, COL_YELLOW, TXT_CENTER);
    for (int i = 0; i < 3; i++) {
        float x = 60 + i * 124, y = 130;
        bool s = sel == i;
        const PerkDef *pk = &PERKS[perk_choice[i]];
        float lift = s ? -4 + sinf(G.time * 5) : 0;
        gfx_nine(SPR_UI_PANEL, x, y + lift, 116, 104, s ? TINT_NONE : rgba(140, 140, 150, 255));
        if (s) gfx_rect(x - 2, y + lift - 2, 120, 108, gfx_rainbow(G.time * 0.4f));
        gfx_spr_stretch(SPR_UI_PERK + perk_choice[i], x + 58 - 16, y + lift + 10, 32, 32, TINT_NONE);
        gfx_text(FONT_SMALL, pk->name, x + 58, y + lift + 48, s ? COL_PINK : COL_WHITE, TXT_CENTER | TXT_OUTLINE);
        gfx_text_wrap(FONT_SMALL, pk->desc, x + 58, y + lift + 62, 100, s ? COL_WHITE : COL_GREY, TXT_CENTER, 10);
        if (RUN.perks[perk_choice[i]] > 0) gfx_text(FONT_SMALL, "^cOWNED", x + 58, y + lift + 92, COL_CYAN, TXT_CENTER);
    }
    gfx_text(FONT_SMALL, "^yENTER^0 choose", VIEW_W / 2, 250, COL_WHITE, TXT_CENTER);
    hud_cursor();
}

static void draw_gameover(void) {
    dark_bg(rgb(60, 6, 18), rgb(11, 10, 16));
    gfx_text_big(FONT_BIG, "YOU DIED", VIEW_W / 2, 30, 3, COL_RED, TXT_CENTER | TXT_SHADOW | TXT_SHAKE);
    if (death_line) gfx_text(FONT_SMALL, death_line, VIEW_W / 2, 82, COL_WHITE, TXT_CENTER);
    char buf[128];
    SDL_snprintf(buf, sizeof buf, "Reached level %d - %s", RUN.level + 1, LEVELS[RUN.level].name);
    gfx_text(FONT_SMALL, buf, VIEW_W / 2, 120, COL_GREY, TXT_CENTER);
    SDL_snprintf(buf, sizeof buf, "Kills %d   Executions %d   Score %d", RUN.kills, RUN.execs, RUN.score);
    gfx_text(FONT_SMALL, buf, VIEW_W / 2, 134, COL_GREY, TXT_CENTER);
    if (death_tip) {
        int th = gfx_text_wrap_h(FONT_SMALL, death_tip, 300, 10);
        gfx_nine(SPR_UI_PANEL, 70, 152, 340, th + 14, TINT_NONE);
        gfx_text(FONT_SMALL, "TIP", 78, 159, COL_PINK, 0);
        gfx_text_wrap(FONT_SMALL, death_tip, 100, 159, 300, COL_WHITE, 0, 10);
    }
    if (st > 1.0f) {
        const char *a[2] = {RUN.mode == MODE_STORY ? "TRY AGAIN" : "NEW RUN", "TITLE"};
        draw_menu(a, 2, VIEW_W / 2, 200, 20, true);
    }
    hud_cursor();
}

static void draw_ending(void) {
    if (page < NUM_ENDING_PAGES) {
        Color top = color_lerp(rgb(40, 14, 30), rgb(30, 60, 30), (float)page / NUM_ENDING_PAGES);
        draw_story_page(ENDING_PAGES[page], top);
        return;
    }
    dark_bg(rgb(20, 50, 30), rgb(11, 10, 16));
    float y = VIEW_H - st * 18;
    gfx_spr_ex(SPR_UI_LOGO, VIEW_W / 2, y - 30, 0, 1.2f, 1.2f, TINT_NONE);
    for (int i = 0; i < NUM_CREDITS; i++)
        gfx_text(FONT_SMALL, CREDITS[i], VIEW_W / 2, y + i * 13, COL_WHITE, TXT_CENTER | TXT_SHADOW);
    char buf[96];
    SDL_snprintf(buf, sizeof buf, "FINAL SCORE %d", RUN.score);
    gfx_text(FONT_BIG, buf, VIEW_W / 2, y + NUM_CREDITS * 13 + 20, COL_YELLOW, TXT_CENTER | TXT_SHADOW | TXT_WAVE);
}

static void draw_credits(void) {
    bg_draw(rgb(10, 10, 30), 0.75f);
    float y = VIEW_H - st * 22 + 20;
    gfx_spr_ex(SPR_UI_LOGO, VIEW_W / 2, y - 30, 0, 1.2f, 1.2f, TINT_NONE);
    for (int i = 0; i < NUM_CREDITS; i++)
        gfx_text(FONT_SMALL, CREDITS[i], VIEW_W / 2, y + i * 13, COL_WHITE, TXT_CENTER | TXT_SHADOW);
}

static void draw_howto(void) {
    bg_draw(rgb(10, 10, 30), 0.82f);
    gfx_text(FONT_BIG, "HOW TO SHOP", VIEW_W / 2, 10, COL_PINK, TXT_CENTER | TXT_SHADOW | TXT_OUTLINE);
    const char *keys[] = {"WASD", "MOUSE", "LMB", "RMB", "E", "SPACE", "SHIFT", "TAB", "F", "Q", "R", "ESC"};
    const char *acts[] = {"Move", "Aim", "Attack / shoot", "Throw your weapon", "Search / take / cart / exit",
                          "Execute a downed enemy", "Look further", "Bag + crafting", "Use bandage / meds",
                          "Swap weapon with bag", "Reload", "Pause"};
    for (int i = 0; i < 12; i++) {
        float y = 36 + i * 13;
        gfx_text(FONT_SMALL, keys[i], 70, y, COL_YELLOW, TXT_RIGHT);
        gfx_text(FONT_SMALL, acts[i], 78, y, COL_WHITE, 0);
    }
    const char *rules =
        "Every store comes with a ^yshopping list^0. Find the items on shelves, in fridges, crates and lockers "
        "- or take them from the people who got there first.\n\n"
        "Fill the list, get back to the ^yvan^0 and press ^yE^0.\n\n"
        "Your bag is small. ^yShopping carts^0 hold a lot more.\n\n"
        "Throw things to ^yknock people down^0, then finish them.\n\n"
        "Combine junk into better weapons in the ^ycraft^0 menu.";
    gfx_text_wrap(FONT_SMALL, rules, 252, 36, 212, COL_WHITE, 0, 10);
    gfx_text(FONT_SMALL, "Gamepad: sticks move/aim, RT attack, LT throw, A use, X execute, Y swap, LB heal, BACK bag",
             VIEW_W / 2, 230, COL_GREY, TXT_CENTER);
    gfx_text(FONT_SMALL, "^yENTER^0 back", VIEW_W / 2, 250, COL_WHITE, TXT_CENTER);
    hud_cursor();
}

static void draw_options(void) {
    if (opts_return == SC_PLAY) {
        world_draw();
        gfx_begin_hud();
        gfx_fill(0, 0, VIEW_W, VIEW_H, rgba(11, 10, 16, 200));
    } else bg_draw(rgb(10, 10, 30), 0.8f);
    gfx_text(FONT_BIG, "OPTIONS", VIEW_W / 2, 26, COL_PINK, TXT_CENTER | TXT_SHADOW | TXT_OUTLINE);
    const char *labels[9] = {"MASTER VOLUME", "MUSIC", "SOUND FX", "SCREEN SHAKE", "CAMERA SWAY", "SCANLINES", "FULLSCREEN", "HINTS", "BACK"};
    float vals[4] = {SET.master, SET.music, SET.sfx, SET.shake};
    bool bools[4] = {SET.sway, SET.scanlines, SET.fullscreen, SET.hints};
    for (int i = 0; i < 9; i++) {
        float y = 64 + i * 18;
        bool s = sel == i;
        Color c = s ? COL_YELLOW : COL_WHITE;
        gfx_text(FONT_SMALL, labels[i], 120, y, c, TXT_OUTLINE | (s ? TXT_WAVE : 0));
        if (s) gfx_spr(SPR_UI_ARROW, 110, y + 4);
        if (i < 4) {
            float w = 100;
            gfx_fill(250, y + 2, w, 5, rgba(40, 36, 52, 255));
            gfx_fill(250, y + 2, w * vals[i], 5, s ? COL_PINK : COL_GREY);
            char buf[16];
            SDL_snprintf(buf, sizeof buf, "%d%%", (int)roundf(vals[i] * 100));
            gfx_text(FONT_SMALL, buf, 360, y, c, 0);
        } else if (i < 8) {
            gfx_text(FONT_SMALL, bools[i - 4] ? "ON" : "OFF", 250, y, bools[i - 4] ? COL_GREEN : COL_GREY, 0);
        }
    }
    gfx_text(FONT_SMALL, "^yLEFT/RIGHT^0 change   ^yESC^0 back", VIEW_W / 2, 236, COL_GREY, TXT_CENTER);
    hud_cursor();
}

void screens_draw(void) {
    switch (g_scene) {
    case SC_TITLE: draw_title(); break;
    case SC_MODE: draw_mode(); break;
    case SC_INTRO: draw_intro(); break;
    case SC_BRIEFING: draw_briefing(); break;
    case SC_RESULTS: draw_results(); break;
    case SC_SAFEHOUSE: draw_safehouse(); break;
    case SC_GAMEOVER: draw_gameover(); break;
    case SC_ENDING: draw_ending(); break;
    case SC_CREDITS: draw_credits(); break;
    case SC_OPTIONS: draw_options(); break;
    case SC_HOWTO: draw_howto(); break;
    default: break;
    }
}
