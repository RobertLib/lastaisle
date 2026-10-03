/* LAST AISLE - scenes, settings, persistence */
#ifndef GAME_H
#define GAME_H

#include "common.h"

typedef enum {
    SC_TITLE, SC_MODE, SC_CUTSCENE, SC_BRIEFING, SC_PLAY, SC_RESULTS, SC_SAFEHOUSE, SC_GAMEOVER,
    SC_ENDING, SC_CREDITS, SC_OPTIONS, SC_HOWTO, SC_HUB
} Scene;

typedef struct {
    float master, music, sfx;
    float shake;          /* 0..1 */
    bool grain;
    bool fullscreen;
    bool hints;
    bool gore_extra;
} Settings;

typedef struct {
    int runs, wins, deaths;
    int best_score;
    int total_kills;
    int best_level;       /* furthest level reached (0-based) */
} Stats;

extern Settings SET;
extern Stats STATS;
extern Scene g_scene;
extern bool g_quit;
extern float g_real_dt;
extern bool g_paused;
extern bool g_inventory_open;

void scene_set(Scene s);
void settings_apply(void);
void settings_save(void);
void settings_load(void);
void stats_save(void);
void run_save(void);
bool run_load(void);
bool run_save_exists(void);
void run_save_delete(void);
enum { RS_LEVEL, RS_HOME };   /* Run.stage */

/* flow (screens.c) */
void screens_update(float dt);
void screens_draw(void);
void new_run(int mode);
void start_level(void);
void level_complete(void);
void player_died(void);
enum { LOST_DIED, LOST_GATE, LOST_SHORT };   /* how a run ends badly: you died, the crew are all dead, too few of them left */
void run_lost(int why);
void run_restore_snapshot(void);
bool scene_in_level(void);    /* a store is loaded and being played (incl. options opened from pause) */
bool scene_in_hub(void);      /* walking round the Greenhouse (incl. options opened from its pause menu) */

/* in-game overlays (hud.c) */
void hud_draw(void);
void hud_draw_gear(void);     /* hearts, hands and bag panels, messages (the Greenhouse uses just these) */
void inventory_update(float dt);
void inventory_draw(void);
void pause_update(float dt);
void pause_draw(void);
void pause_open(void);
void hud_reset_level_state(void);
void level_intro_draw(void);
const char *ctl(const char *kb, const char *pad);   /* control name for the active input device */

/* world.c */
const char *world_prompt(V2 *pos);

#endif
