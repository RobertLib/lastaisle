/* LAST AISLE - entry point, main loop, persistence */
#include "world.h"
#include "gfx.h"
#include "input.h"
#include "audio.h"
#include "game.h"
#include "cutscene.h"
#include "hub.h"

Rng g_rng;
Settings SET = {1.0f, 0.8f, 1.0f, 1.0f, true, false, true, false};
Stats STATS;
Scene g_scene = SC_TITLE;
bool g_quit;
float g_real_dt;
bool g_paused;
bool g_inventory_open;

static SDL_Window *win;
static SDL_Renderer *ren;

/* debug / automation */
static int dbg_level = -1;
static int dbg_frames = 0;
static const char *dbg_shot;
static uint64_t dbg_seed;
static int dbg_scene = -1;
static bool dbg_god;
static const char *dbg_mapshot;
static bool dbg_fast;
static bool dbg_inv;
static bool dbg_complete;
static bool dbg_perf;
static int dbg_cut = -1, dbg_cut_level;
static int dbg_level_hub = -1;   /* --hub-level N: which evening --scene 12 (the Greenhouse) shows */
static bool dbg_favours;   /* --favours: the favours of the stores before were brought home, this store's were asked */
static int dbg_crew = -1;  /* --crew N: the first N living of the crew are asked along (up to the store's limit) */
static int dbg_fallen;     /* --fallen N: the first N of the crew died in the store before */
static bool dbg_armed;     /* --armed: every weapon slot full */
static bool dbg_pile;      /* --pile: a heap of things dropped right at your feet, all in one spot */
static float dbg_cut_t;
extern bool g_autoplay;
void autoplay_update(float dt);
void world_mapshot(const char *path);

/* ----------------------------------------------------------- persistence */
bool g_nosave;   /* debug sessions never touch the player's files */
static const char *dbg_prefdir;   /* --prefdir DIR: keep all files in DIR instead (for testing saves) */

static const char *pref_file(const char *name, char *buf, size_t n) {
    if (dbg_prefdir) {
        SDL_snprintf(buf, n, "%s/%s", dbg_prefdir, name);
        return buf;
    }
    char *base = SDL_GetPrefPath("lastaisle", "LastAisle");
    SDL_snprintf(buf, n, "%s%s", base ? base : "", name);
    SDL_free(base);
    return buf;
}

/* files are written to name.tmp and then swapped in, so a crash mid-write never leaves a torn file */
static FILE *save_begin(const char *name, const char *mode) {
    char nm[64], path[1100];
    SDL_snprintf(nm, sizeof nm, "%s.tmp", name);
    return fopen(pref_file(nm, path, sizeof path), mode);
}

static bool save_commit(FILE *f, const char *name) {
    char nm[64], tmp[1100], dst[1100];
    bool ok = !ferror(f);
    ok = fclose(f) == 0 && ok;
    SDL_snprintf(nm, sizeof nm, "%s.tmp", name);
    pref_file(nm, tmp, sizeof tmp);
    if (ok && SDL_RenamePath(tmp, pref_file(name, dst, sizeof dst))) return true;
    remove(tmp);
    return false;
}

void settings_apply(void) {
    audio_set_volumes(SET.master, SET.music, SET.sfx);
    G.grain = SET.grain;
    SDL_SetWindowFullscreen(win, SET.fullscreen);
}

void settings_save(void) {
    if (g_nosave) return;
    FILE *f = save_begin("settings.cfg", "w");
    if (!f) return;
    fprintf(f, "master=%.2f\nmusic=%.2f\nsfx=%.2f\nshake=%.2f\ngrain=%d\nfullscreen=%d\nhints=%d\n",
            SET.master, SET.music, SET.sfx, SET.shake, SET.grain, SET.fullscreen, SET.hints);
    save_commit(f, "settings.cfg");
}

/* 0..1, anything unreadable (nan) keeps the old value */
static float unit_or(float v, float def) { return v != v ? def : CLAMP(v, 0.0f, 1.0f); }

void settings_load(void) {
    char path[1100];
    FILE *f = fopen(pref_file("settings.cfg", path, sizeof path), "r");
    if (f) {
        char line[128];
        while (fgets(line, sizeof line, f)) {
            char key[64];
            float v;
            if (sscanf(line, "%63[^=]=%f", key, &v) != 2) continue;
            if (!strcmp(key, "master")) SET.master = unit_or(v, SET.master);
            else if (!strcmp(key, "music")) SET.music = unit_or(v, SET.music);
            else if (!strcmp(key, "sfx")) SET.sfx = unit_or(v, SET.sfx);
            else if (!strcmp(key, "shake")) SET.shake = unit_or(v, SET.shake);
            else if (!strcmp(key, "grain")) SET.grain = v != 0;
            else if (!strcmp(key, "fullscreen")) SET.fullscreen = v != 0;
            else if (!strcmp(key, "hints")) SET.hints = v != 0;
        }
        fclose(f);
    }
    f = fopen(pref_file("stats.cfg", path, sizeof path), "r");
    if (f) {
        if (fscanf(f, "%d %d %d %d %d %d", &STATS.runs, &STATS.wins, &STATS.deaths, &STATS.best_score, &STATS.total_kills,
                   &STATS.best_level) != 6 ||
            STATS.runs < 0 || STATS.wins < 0 || STATS.deaths < 0 || STATS.best_score < 0 || STATS.total_kills < 0 ||
            STATS.best_level < 0 || STATS.best_level >= NUM_LEVELS)
            memset(&STATS, 0, sizeof STATS);
        fclose(f);
    }
}

void stats_save(void) {
    if (g_nosave) return;
    FILE *f = save_begin("stats.cfg", "w");
    if (!f) return;
    fprintf(f, "%d %d %d %d %d %d\n", STATS.runs, STATS.wins, STATS.deaths, STATS.best_score, STATS.total_kills, STATS.best_level);
    save_commit(f, "stats.cfg");
}

#define RUN_MAGIC 0x4C415336u /* "LAS6" */

static int save_state = -1;      /* cached: -1 unknown, 0 no usable save, 1 valid save on disk */
static int rogue_saved_hp;       /* hp written by the last save of the current store */

static bool stack_ok(const Stack *s) {
    return s->id > IT_NONE && s->id < IT_COUNT && s->count > 0 && s->mods >= 0 && s->mods < (1 << MOD_COUNT);
}
static bool weapon_ok(const Stack *s) { return s->id == IT_NONE || (stack_ok(s) && item_is_weapon((ItemId)s->id)); }

static bool run_valid(const Run *r) {
    if (r->mode != MODE_STORY && r->mode != MODE_ROGUE) return false;
    if (r->level < 0 || r->level >= NUM_LEVELS) return false;
    if (r->maxhp < 1 || r->maxhp > 999 || r->hp < 1 || r->hp > r->maxhp) return false;
    if (r->ninv < 0 || r->ninv > INV_MAX) return false;
    for (int i = 0; i < r->ninv; i++) if (!stack_ok(&r->inv[i])) return false;
    if (!weapon_ok(&r->weapon)) return false;
    if (r->wslot < 0 || r->wslot >= WSLOTS || r->snap_wslot < 0 || r->snap_wslot >= WSLOTS) return false;
    for (int k = 0; k < WSLOTS; k++)
        if (!weapon_ok(&r->slots[k]) || (k == r->wslot && r->slots[k].id)) return false;
    if (r->bag <= IT_NONE || r->bag >= IT_COUNT || ITEMS[r->bag].cat != CAT_BAG) return false;
    for (int i = 0; i < PK_COUNT; i++) if (r->perks[i] < 0 || r->perks[i] > 99) return false;
    for (int i = 0; i < STAT_COUNT; i++) if (r->xp[i] < 0 || r->xp[i] > 99999) return false;
    if (r->nlocker < 0 || r->nlocker > ARRAY_LEN(r->locker)) return false;
    for (int i = 0; i < r->nlocker; i++) if (!stack_ok(&r->locker[i])) return false;
    for (int i = 0; i < MAX_FAVOURS; i++) if (r->favour[i] >= FS_COUNT || (i >= NUM_FAVOURS && r->favour[i])) return false;
    for (int i = 0; i < MAX_CREW; i++)
        if (r->crew[i] >= CR_COUNT || r->snap_crew[i] >= CR_COUNT || r->crew_fell[i] >= NUM_LEVELS || r->snap_crew_fell[i] >= NUM_LEVELS)
            return false;
    if (r->stage == RS_HOME) {
        if (r->level >= NUM_LEVELS - 1) return false;   /* the last store ends the run */
        for (int i = 0; i < 3; i++) if (r->offer[i] < 0 || r->offer[i] >= PK_COUNT) return false;
        if (r->nhome < 0 || r->nhome > ARRAY_LEN(r->home)) return false;
        for (int i = 0; i < r->nhome; i++)
            if (r->home[i].id <= IT_NONE || r->home[i].id >= IT_COUNT || r->home[i].n < 1 || r->home[i].n > 99) return false;
    } else if (r->stage != RS_LEVEL) return false;
    return true;
}

/* 1 valid, 0 no file or another version's save (left alone), -1 unreadable or corrupt */
static int run_read(Run *out) {
    char path[1100];
    FILE *f = fopen(pref_file("run.sav", path, sizeof path), "rb");
    if (!f) return 0;
    uint32_t magic = 0, size = 0;
    bool ok = fread(&magic, 4, 1, f) == 1 && fread(&size, 4, 1, f) == 1;
    if (ok && magic != RUN_MAGIC && (magic >> 8) == (RUN_MAGIC >> 8)) { fclose(f); return 0; }
    ok = ok && magic == RUN_MAGIC && size == sizeof(Run) && fread(out, sizeof *out, 1, f) == 1 && fgetc(f) == EOF;
    fclose(f);
    return ok && run_valid(out) ? 1 : -1;
}

void run_save(void) {
    if (!RUN.active) return;
    Run r = RUN;
    if (r.stage == RS_LEVEL && scene_in_level()) {
        /* mid-store: continuing restarts the store with what you walked in with */
        r.hp = RUN.snap_hp ? RUN.snap_hp : RUN.hp;
        r.weapon = RUN.snap_weapon;
        memcpy(r.slots, RUN.snap_slots, sizeof r.slots);
        r.wslot = RUN.snap_wslot;
        r.ninv = RUN.snap_ninv;
        memcpy(r.inv, RUN.snap_inv, sizeof r.inv);
        r.bag = RUN.snap_bag;
        r.kills = RUN.snap_kills;
        r.execs = RUN.snap_execs;
        if (RUN.mode == MODE_STORY) {   /* ...and whoever of the crew died in there is back */
            memcpy(r.crew, RUN.snap_crew, sizeof r.crew);
            memcpy(r.crew_fell, RUN.snap_crew_fell, sizeof r.crew_fell);
        }
        /* ...but in roguelike your wounds come along (the meds are handed back, so healing doesn't count) - and your dead stay
           dead (crew_killed saves at once) */
        if (RUN.mode == MODE_ROGUE && player()->alive) {
            int cap = rogue_saved_hp > 0 ? rogue_saved_hp : r.hp;
            r.hp = MAXF(1, MINF(MINF(r.hp, player()->hp), cap));
        }
    }
    if (r.stage == RS_LEVEL) rogue_saved_hp = r.hp;
    if (g_nosave) return;
    FILE *f = save_begin("run.sav", "wb");
    if (!f) return;
    uint32_t magic = RUN_MAGIC, size = sizeof(Run);
    fwrite(&magic, 4, 1, f);
    fwrite(&size, 4, 1, f);
    fwrite(&r, sizeof r, 1, f);
    if (save_commit(f, "run.sav")) save_state = 1;
}

bool run_load(void) {
    Run r;
    int st = run_read(&r);
    if (st <= 0) {
        if (st < 0) run_save_delete();
        save_state = 0;
        return false;
    }
    RUN = r;
    RUN.active = true;
    for (int i = 0; i < NUM_LEVELS; i++) RUN.level_grades[i][2] = 0;
    return true;
}

bool run_save_exists(void) {
    if (save_state < 0) {
        Run tmp;
        int st = run_read(&tmp);
        if (st < 0) run_save_delete();   /* broken: don't offer a CONTINUE that can't work */
        save_state = st > 0;
    }
    return save_state > 0;
}

void run_save_delete(void) {
    if (g_nosave) return;
    char path[1100];
    remove(pref_file("run.sav", path, sizeof path));
    save_state = 0;
}

/* ---------------------------------------------------------- play scene */
static void play_update(float dt) {
    if (g_paused) {
        pause_update(dt);
        audio_set_muffle(0.7f);
        return;
    }
    if (g_inventory_open) {
        inventory_update(dt);
        audio_set_muffle(0.55f);
        return;
    }
    audio_set_muffle(W.player_dead ? 0.6f : 0.0f);
    if (IN.pressed[ACT_PAUSE] && !W.player_dead && !W.exiting) {
        pause_open();
        audio_play(SFX_UI_SELECT, 0.85f, 0, 0.8f);
        return;
    }
    if (IN.pressed[ACT_INVENTORY] && !W.player_dead && !W.exiting) {
        g_inventory_open = true;
        audio_play(SFX_UI_SELECT, 0.85f, 0, 1.1f);
        return;
    }
    if (dbg_god) player()->hp = player()->maxhp;
    world_update(dt);
    /* roguelike: every wound is written down at once, so quitting (or a crash) can't undo it */
    if (RUN.mode == MODE_ROGUE && !W.player_dead && player()->alive && MINF(player()->hp, RUN.snap_hp) < rogue_saved_hp)
        run_save();
    /* the last of the crew is dead: a moment to take it in, then nobody's holding the gate */
    if (W.crew_wiped && !W.player_dead && !W.exiting && (W.crew_wipe_t += dt) > 3.0f) {
        audio_loop(LOOP_CHAINSAW_IDLE, 0, 1);
        audio_loop(LOOP_CHAINSAW_CUT, 0, 1);
        audio_loop(LOOP_FIRE, 0, 1);
        run_lost(LOST_GATE);
        return;
    }
    if (W.exiting && W.exit_t > 1.6f && !W.player_dead) {
        audio_loop(LOOP_CHAINSAW_IDLE, 0, 1);
        audio_loop(LOOP_CHAINSAW_CUT, 0, 1);
        audio_loop(LOOP_FIRE, 0, 1);
        audio_play(SFX_ENGINE, 0.8f, 0, 1);
        level_complete();
        return;
    }
    if (W.player_dead && W.dead_t > 1.6f) {
        if (IN.pressed[ACT_CONFIRM] || IN.pressed[ACT_RELOAD] || IN.pressed[ACT_INTERACT] || IN.click || W.dead_t > 12) {
            audio_loop(LOOP_CHAINSAW_IDLE, 0, 1);
            audio_loop(LOOP_CHAINSAW_CUT, 0, 1);
            audio_loop(LOOP_FIRE, 0, 1);
            if (RUN.mode == MODE_STORY && (IN.pressed[ACT_RELOAD])) {
                RUN.retries++;
                RUN.deaths++;
                STATS.deaths++;
                stats_save();
                run_restore_snapshot();
                RUN.hp = RUN.maxhp;
                start_level();
            } else {
                player_died();
            }
        }
    }
}

static void play_draw(void) {
    world_draw();
    gfx_begin_hud();
    if (g_inventory_open) {
        inventory_draw();
    } else if (g_paused) {
        hud_draw();
        pause_draw();
    } else {
        hud_draw();
    }
}

/* --favours: every favour for an earlier store came home (it's in your bag), this store's have been asked for */
static void debug_favours(void) {
    for (int f = 0; f < NUM_FAVOURS; f++) {
        if (FAVOURS[f].level == RUN.level) RUN.favour[f] = FS_OPEN;
        if (FAVOURS[f].level >= RUN.level || RUN.ninv >= INV_MAX) continue;
        RUN.favour[f] = FS_FOUND;
        RUN.inv[RUN.ninv++] = (Stack){(int16_t)FAVOURS[f].item, (int16_t)FAVOURS[f].n, 0, 0};
    }
    RUN.bag = IT_HIKINGPACK;   /* room for it all */
}

/* --fallen / --crew: who of the crew is dead, and who's coming along */
static void debug_crew(void) {
    for (int k = 0; k < MAX_CREW; k++) {
        RUN.crew[k] = k < dbg_fallen ? CR_DEAD : CR_HOME;
        RUN.crew_fell[k] = (uint8_t)MAXF(0, RUN.level - 1);
    }
    int n = dbg_crew < 0 ? 0 : MINF(dbg_crew, LEVELS[RUN.level].crew_max);
    for (int k = 0; k < MAX_CREW && n > 0; k++) if (RUN.crew[k] == CR_HOME) { RUN.crew[k] = CR_SQUAD; n--; }
}

/* --armed: the bat in hand, a pistol (rounds in the bag) and three molotovs on your back */
static void debug_armed(void) {
    RUN.wslot = 0;
    RUN.slots[1] = (Stack){IT_PISTOL, 1, (int16_t)WEAPONS[W_PISTOL].mag, 0};
    RUN.slots[2] = (Stack){IT_MOLOTOV, 3, 0, 0};
    if (RUN.ninv < INV_MAX) RUN.inv[RUN.ninv++] = (Stack){IT_AMMO9, 24, 0, 0};
}

/* --pile: weapons, junk and something off the list, all on one spot at your feet (dropped: E or the bag screen takes them) */
static void debug_pile(void) {
    Actor *p = player();
    Stack heap[] = {
        {IT_BAT, 1, (int16_t)WEAPONS[W_BAT].durability, 0}, {IT_PISTOL, 1, 5, 0}, {IT_KNIFE, 1, (int16_t)WEAPONS[W_KNIFE].durability, 0},
        {IT_DUCTTAPE, 1, 0, 0}, {IT_RAG, 2, 0, 0}, {IT_BANDAGE, 1, 0, 0}, {IT_BOTTLE, 1, 0, 0},
        {W.nlist ? W.list[0].id : IT_AMMO9, 1, 0, 0},
    };
    for (int k = 0; k < (int)ARRAY_LEN(heap); k++) {
        int pi = pickup_spawn(heap[k], p->pos, v2(0, 0));
        if (pi >= 0) W.pickups[pi].dropped = true;
    }
    SDL_Log("PILE: %d things at %.0f,%.0f", (int)ARRAY_LEN(heap), p->pos.x, p->pos.y);
}

/* ----------------------------------------------------------------- main */
static void parse_args(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--level") && i + 1 < argc) dbg_level = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) dbg_frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) dbg_shot = argv[++i];
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) dbg_seed = strtoull(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--scene") && i + 1 < argc) dbg_scene = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--god")) dbg_god = true;
        else if (!strcmp(argv[i], "--autoplay")) g_autoplay = true;
        else if (!strcmp(argv[i], "--fast")) dbg_fast = true;
        else if (!strcmp(argv[i], "--inv")) dbg_inv = true;
        else if (!strcmp(argv[i], "--complete")) dbg_complete = true;
        else if (!strcmp(argv[i], "--perf")) dbg_perf = true;
        else if (!strcmp(argv[i], "--cut") && i + 1 < argc) dbg_cut = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--cut-level") && i + 1 < argc) dbg_cut_level = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--cut-t") && i + 1 < argc) dbg_cut_t = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--mapshot") && i + 1 < argc) dbg_mapshot = argv[++i];
        else if (!strcmp(argv[i], "--prefdir") && i + 1 < argc) dbg_prefdir = argv[++i];
        else if (!strcmp(argv[i], "--hub-level") && i + 1 < argc) dbg_level_hub = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--favours")) dbg_favours = true;
        else if (!strcmp(argv[i], "--armed")) dbg_armed = true;
        else if (!strcmp(argv[i], "--pile")) dbg_pile = true;
        else if (!strcmp(argv[i], "--crew") && i + 1 < argc) dbg_crew = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--fallen") && i + 1 < argc) { int n = atoi(argv[++i]); dbg_fallen = CLAMP(n, 0, MAX_CREW); }
    }
}

int main(int argc, char **argv) {
    parse_args(argc, argv);
    /* debug sessions only save when pointed away from the player's files */
    g_nosave = (dbg_level >= 0 || dbg_scene >= 0 || dbg_cut >= 0 || dbg_frames > 0 || g_autoplay) && !dbg_prefdir;
    SDL_SetAppMetadata(GAME_TITLE, GAME_VERSION, "com.lastaisle.game");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    int ww = 1440, wh = 810;
    const SDL_DisplayMode *dm = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
    if (dm) {
        int s = 4;
        while (s > 1 && (VIEW_W * s > dm->w * 0.92f || VIEW_H * s > dm->h * 0.88f)) s--;
        ww = VIEW_W * s;
        wh = VIEW_H * s;
    }
    win = SDL_CreateWindow(GAME_TITLE, ww, wh, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!win) { SDL_Log("window: %s", SDL_GetError()); return 1; }
    ren = SDL_CreateRenderer(win, NULL);
    if (!ren) { SDL_Log("renderer: %s", SDL_GetError()); return 1; }
    SDL_SetRenderVSync(ren, dbg_fast ? 0 : 1);
    if (!gfx_init(win, ren)) return 1;
    rng_seed(&g_rng, SDL_GetTicksNS());
    if (!audio_init()) SDL_Log("audio unavailable - continuing silently");
    input_init();
    settings_load();
    if (dbg_shot) SET.fullscreen = false;
    settings_apply();
    SDL_HideCursor();

    if (dbg_level >= 0) {
        new_run(MODE_STORY);
        if (dbg_seed) RUN.seed = dbg_seed;
        RUN.level = dbg_level;
        if (dbg_favours) debug_favours();
        if (dbg_armed) debug_armed();
        debug_crew();
        start_level();
        if (dbg_pile) debug_pile();
        if (dbg_mapshot) { world_mapshot(dbg_mapshot); return 0; }
        if (dbg_complete) {
            W.kills = 9; W.score = 4350; W.max_combo = 4; W.time = 94; W.weapons_used = 0x2D;
            level_complete();
        }
    } else if (dbg_cut >= 0) {
        new_run(MODE_STORY);
        RUN.level = CLAMP(dbg_cut_level, 0, NUM_LEVELS - 1);
        cutscene_start((CutsceneId)CLAMP(dbg_cut, 0, CUT_COUNT - 1), RUN.level);
        cutscene_seek(dbg_cut_t);
        G.flash = 0;
    } else if (dbg_scene >= 0) {
        new_run(MODE_STORY);
        if (dbg_seed) RUN.seed = dbg_seed;
        if (dbg_level_hub >= 0) RUN.level = CLAMP(dbg_level_hub, 0, NUM_LEVELS - 1);
        if (dbg_favours) debug_favours();
        if (dbg_armed) debug_armed();
        debug_crew();
        scene_set((Scene)dbg_scene);
        if (dbg_pile && g_scene == SC_HUB) debug_pile();
        if (dbg_mapshot && g_scene == SC_HUB) { world_mapshot(dbg_mapshot); return 0; }
    } else {
        scene_set(SC_TITLE);
    }

    uint64_t last = SDL_GetTicksNS();
    int frame = 0;
    while (!g_quit && !IN.quit) {
        input_begin_frame();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            input_event(&e);
            if (e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || e.type == SDL_EVENT_WINDOW_RESIZED) gfx_resize();
            if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST && g_scene == SC_PLAY && !W.player_dead && !W.exiting && !g_autoplay) {
                g_inventory_open = false;
                pause_open();
            }
            if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST && g_scene == SC_HUB && !g_autoplay) pause_open();
        }
        uint64_t now = SDL_GetTicksNS();
        float dt = (float)((now - last) / 1e9);
        last = now;
        if (dt > 1.0f / 20) dt = 1.0f / 20;
        if (dbg_frames) dt = 1.0f / 60;
        g_real_dt = dt;
        G.time += dt;
        input_update(dt);
        autoplay_update(dt);
        if (IN.fullscreen_toggle) { SET.fullscreen = !SET.fullscreen; settings_apply(); settings_save(); }

        /* post-fx decay */
        G.flash = MAXF(0, G.flash - dt * 3);
        G.impact = MAXF(0, G.impact - dt * 5);

        int steps = dbg_fast ? 4 : 1;
        for (int st = 0; st < steps; st++) {
            if (st > 0) { input_begin_frame(); input_update(dt); autoplay_update(dt); }
            if (g_scene == SC_PLAY) play_update(dt);
            else screens_update(dt);
        }
        bool world_scene = g_scene == SC_PLAY || g_scene == SC_HUB;
        if ((!world_scene && g_scene != SC_CUTSCENE) || g_paused || g_inventory_open) {
            for (int l = 0; l < LOOP_COUNT; l++) {
                bool ambience = l == LOOP_RAIN || l == LOOP_WIND || l == LOOP_HUM;
                if (!world_scene || !ambience) audio_loop((LoopId)l, 0, 1);
            }
        }

        if (g_scene == SC_PLAY) play_draw();
        else screens_draw();
        if (dbg_perf) {
            static uint64_t acc_u, acc_d;
            static int nper;
            uint64_t t1 = SDL_GetTicksNS();
            (void)t1;
            acc_u += t1 - now;
            nper++;
            if (nper == 300) {
                SDL_Log("PERF: avg update+draw %.2f ms over 300 frames (%d actors, %d particles alive)", acc_u / 300.0 / 1e6, W.nactors, 0);
                acc_u = acc_d = 0;
                nper = 0;
            }
        }
        frame++;
        if (dbg_inv && dbg_frames && frame == dbg_frames - 3 && (g_scene == SC_PLAY || g_scene == SC_HUB)) { g_autoplay = false; g_inventory_open = true; }
        bool last_frame = dbg_frames && frame >= dbg_frames;
        if (last_frame && dbg_shot) G.capture_path = dbg_shot;
        gfx_present();
        if (last_frame) break;
    }
    if (RUN.active && scene_in_level() && !W.player_dead) {
        if (W.exiting) level_complete();   /* already driving off: the store counts as cleared */
        else run_save();
    }
    if (RUN.active && scene_in_hub()) {
        hub_sync_run();
        run_save();
    }
    world_free();
    audio_shutdown();
    gfx_shutdown();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
