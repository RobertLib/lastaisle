/* LAST AISLE - entry point, main loop, persistence */
#include "world.h"
#include "gfx.h"
#include "input.h"
#include "audio.h"
#include "game.h"

Rng g_rng;
Settings SET = {1.0f, 0.8f, 1.0f, 1.0f, true, true, false, true, false};
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
extern bool g_autoplay;
void autoplay_update(float dt);
void world_mapshot(const char *path);

/* ----------------------------------------------------------- persistence */
bool g_nosave;   /* debug sessions never touch the player's files */

static char *pref_file(const char *name) {
    static char buf[1024];
    char *base = SDL_GetPrefPath("lastaisle", "LastAisle");
    SDL_snprintf(buf, sizeof buf, "%s%s", base ? base : "", name);
    SDL_free(base);
    return buf;
}

void settings_apply(void) {
    audio_set_volumes(SET.master, SET.music, SET.sfx);
    G.scanlines = SET.scanlines;
    SDL_SetWindowFullscreen(win, SET.fullscreen);
}

void settings_save(void) {
    if (g_nosave) return;
    FILE *f = fopen(pref_file("settings.cfg"), "w");
    if (!f) return;
    fprintf(f, "master=%.2f\nmusic=%.2f\nsfx=%.2f\nshake=%.2f\nsway=%d\nscanlines=%d\nfullscreen=%d\nhints=%d\n",
            SET.master, SET.music, SET.sfx, SET.shake, SET.sway, SET.scanlines, SET.fullscreen, SET.hints);
    fclose(f);
}

void settings_load(void) {
    FILE *f = fopen(pref_file("settings.cfg"), "r");
    if (f) {
        char line[128];
        while (fgets(line, sizeof line, f)) {
            char key[64];
            float v;
            if (sscanf(line, "%63[^=]=%f", key, &v) != 2) continue;
            if (!strcmp(key, "master")) SET.master = v;
            else if (!strcmp(key, "music")) SET.music = v;
            else if (!strcmp(key, "sfx")) SET.sfx = v;
            else if (!strcmp(key, "shake")) SET.shake = v;
            else if (!strcmp(key, "sway")) SET.sway = v != 0;
            else if (!strcmp(key, "scanlines")) SET.scanlines = v != 0;
            else if (!strcmp(key, "fullscreen")) SET.fullscreen = v != 0;
            else if (!strcmp(key, "hints")) SET.hints = v != 0;
        }
        fclose(f);
    }
    f = fopen(pref_file("stats.cfg"), "r");
    if (f) {
        if (fscanf(f, "%d %d %d %d %d %d", &STATS.runs, &STATS.wins, &STATS.deaths, &STATS.best_score, &STATS.total_kills,
                   &STATS.best_level) != 6)
            memset(&STATS, 0, sizeof STATS);
        fclose(f);
    }
}

void stats_save(void) {
    if (g_nosave) return;
    FILE *f = fopen(pref_file("stats.cfg"), "w");
    if (!f) return;
    fprintf(f, "%d %d %d %d %d %d\n", STATS.runs, STATS.wins, STATS.deaths, STATS.best_score, STATS.total_kills, STATS.best_level);
    fclose(f);
}

#define RUN_MAGIC 0x4C415331u /* "LAS1" */

void run_save(void) {
    if (g_nosave) return;
    if (!RUN.active) return;
    FILE *f = fopen(pref_file("run.sav"), "wb");
    if (!f) return;
    uint32_t magic = RUN_MAGIC, size = sizeof(Run);
    fwrite(&magic, 4, 1, f);
    fwrite(&size, 4, 1, f);
    /* save the state as it was when the level started */
    Run r = RUN;
    if (g_scene == SC_PLAY) {
        r.hp = RUN.snap_hp ? RUN.snap_hp : RUN.hp;
        r.weapon = RUN.snap_weapon;
        r.ninv = RUN.snap_ninv;
        memcpy(r.inv, RUN.snap_inv, sizeof r.inv);
        r.bag = RUN.snap_bag;
    }
    fwrite(&r, sizeof r, 1, f);
    fclose(f);
}

bool run_load(void) {
    FILE *f = fopen(pref_file("run.sav"), "rb");
    if (!f) return false;
    uint32_t magic = 0, size = 0;
    bool ok = fread(&magic, 4, 1, f) == 1 && fread(&size, 4, 1, f) == 1 && magic == RUN_MAGIC && size == sizeof(Run) &&
              fread(&RUN, sizeof RUN, 1, f) == 1;
    fclose(f);
    if (!ok) { memset(&RUN, 0, sizeof RUN); return false; }
    RUN.active = true;
    return true;
}

bool run_save_exists(void) {
    FILE *f = fopen(pref_file("run.sav"), "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

void run_save_delete(void) {
    if (g_nosave) return;
    remove(pref_file("run.sav"));
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
    if (IN.pressed[ACT_PAUSE] && !W.player_dead) {
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
                RUN.hp = RUN.maxhp;
                RUN.weapon = RUN.snap_weapon;
                RUN.ninv = RUN.snap_ninv;
                memcpy(RUN.inv, RUN.snap_inv, sizeof RUN.inv);
                RUN.bag = RUN.snap_bag;
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
        else if (!strcmp(argv[i], "--mapshot") && i + 1 < argc) dbg_mapshot = argv[++i];
    }
}

int main(int argc, char **argv) {
    parse_args(argc, argv);
    g_nosave = dbg_level >= 0 || dbg_scene >= 0 || dbg_frames > 0 || g_autoplay;
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
        start_level();
        if (dbg_mapshot) { world_mapshot(dbg_mapshot); return 0; }
        if (dbg_complete) {
            W.kills = 9; W.score = 4350; W.max_combo = 4; W.time = 94; W.weapons_used = 0x2D;
            level_complete();
        }
    } else if (dbg_scene >= 0) {
        new_run(MODE_STORY);
        if (dbg_seed) RUN.seed = dbg_seed;
        scene_set((Scene)dbg_scene);
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
            if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST && g_scene == SC_PLAY && !W.player_dead && !g_autoplay) {
                g_inventory_open = false;
                pause_open();
            }
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
        G.chroma = MAXF(0, G.chroma - dt * 4);

        int steps = dbg_fast ? 4 : 1;
        for (int st = 0; st < steps; st++) {
            if (st > 0) { input_begin_frame(); input_update(dt); autoplay_update(dt); }
            if (g_scene == SC_PLAY) play_update(dt);
            else screens_update(dt);
        }
        if (g_scene != SC_PLAY || g_paused || g_inventory_open) {
            for (int l = 0; l < LOOP_COUNT; l++) {
                bool ambience = l == LOOP_RAIN || l == LOOP_WIND || l == LOOP_HUM;
                if (g_scene != SC_PLAY || !ambience) audio_loop((LoopId)l, 0, 1);
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
        if (dbg_inv && dbg_frames && frame == dbg_frames - 3 && g_scene == SC_PLAY) { g_autoplay = false; g_inventory_open = true; }
        bool last_frame = dbg_frames && frame >= dbg_frames;
        if (last_frame && dbg_shot) G.capture_path = dbg_shot;
        gfx_present();
        if (last_frame) break;
    }
    if (RUN.active && g_scene == SC_PLAY && !W.player_dead) run_save();
    world_free();
    audio_shutdown();
    gfx_shutdown();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
