/* LAST AISLE - QA autopilot: plays the game by itself (--autoplay) */
#include "world.h"
#include "gfx.h"
#include "input.h"
#include "game.h"
#include "hub.h"

bool g_autoplay;
bool g_aim_override;
V2 g_aim_world;

static int path[48][2];
static int path_len, path_i;
static V2 goal;
static float repath_t, stuck_t, think_t, press_cd;
static V2 last_pos;
static int target_cont = -1;
static int blacklist[64];
static int nblack;

static bool black(int ci) {
    for (int i = 0; i < nblack; i++) if (blacklist[i] == ci) return true;
    return false;
}

static void go(V2 g) {
    if (v2_dist(g, goal) > 8 || repath_t <= 0) {
        goal = g;
        path_find(player()->pos, g, path, 48, &path_len);
        path_i = 0;
        repath_t = 0.8f;
    }
}

static V2 steer(void) {
    Actor *p = player();
    V2 t = goal;
    if (path_i < path_len) {
        t = tile_center(path[path_i][0], path[path_i][1]);
        if (path_i == path_len - 1) t = goal;
        if (v2_dist(p->pos, t) < 7) path_i++;
    }
    V2 d = v2_sub(t, p->pos);
    if (v2_len(d) < 3) return v2(0, 0);
    return v2_norm(d);
}

static int nearest_enemy(float range) {
    Actor *p = player();
    int best = -1;
    float bd = range;
    for (int i = 1; i < W.nactors; i++) {
        Actor *a = &W.actors[i];
        if (!a->used || !a->alive) continue;
        bool hostile = actor_hostile(0, i) || (a->br.state == AI_CHASE && a->br.target == 0);
        if (!hostile) continue;
        float d = v2_dist(a->pos, p->pos);
        if (is_plant(a) && a->br.target != 0 && d > 60) continue;   /* weeds only when they're in the way */
        if (d < bd && los_clear(p->pos, a->pos, false)) { bd = d; best = i; }
    }
    return best;
}

static bool wants(ItemId id) {
    for (int i = 0; i < W.nlist; i++) if (W.list[i].id == id && !W.list[i].done) return true;
    for (int i = 0; i < W.nfav; i++) if (W.fav[i].id == id && !W.fav[i].done) return true;   /* the camp's favours too */
    return false;
}

void autoplay_update(float dt) {
    if (!g_autoplay) return;
    if (g_scene == SC_HUB) { hub_bot(dt); return; }
    /* menus: just keep pressing confirm */
    if (g_scene != SC_PLAY) {
        press_cd -= dt;
        memset(IN.pressed, 0, sizeof IN.pressed);
        if (press_cd <= 0) { IN.pressed[ACT_CONFIRM] = true; press_cd = 0.4f; }
        g_aim_override = false;
        return;
    }
    Actor *p = player();
    static float watch;
    watch += dt;
    if (watch > 120) {
        watch = 0;
        char lst[256] = "";
        for (int i = 0; i < W.nlist; i++) {
            char b[48];
            SDL_snprintf(b, sizeof b, "%s %d/%d; ", ITEMS[W.list[i].id].name, W.list[i].have, W.list[i].need);
            SDL_strlcat(lst, b, sizeof lst);
        }
        SDL_Log("BOT t=%.0f pos=%.0f,%.0f bag=%d/%d cart=%d tc=%d list: %s", W.time, p->pos.x, p->pos.y, inv_used(p),
                inv_capacity(p), p->cart, target_cont, lst);
        for (int i = 0; i < W.nlist; i++) {
            if (W.list[i].done) continue;
            int inc = 0, carried = 0, floor = 0, carts = 0;
            for (int c = 0; c < W.nconts; c++)
                for (int k = 0; k < W.conts[c].n; k++) if (W.conts[c].items[k].id == W.list[i].id) inc++;
            for (int a = 1; a < W.nactors; a++)
                if (W.actors[a].alive) carried += inv_count(&W.actors[a], W.list[i].id);
            for (int k = 0; k < MAX_PICKUPS; k++) if (W.pickups[k].alive && W.pickups[k].st.id == W.list[i].id) floor++;
            for (int c = 0; c < MAX_CARTS; c++)
                for (int k = 0; W.carts[c].alive && k < W.carts[c].n; k++)
                    if (W.carts[c].items[k].id == W.list[i].id && !cart_counts(&W.carts[c])) carts += W.carts[c].items[k].count;
            SDL_Log("   missing %s: containers %d, carried %d, floor %d, carts left behind %d", ITEMS[W.list[i].id].name, inc, carried,
                    floor, carts);
        }
    }
    g_aim_override = true;
    IN.move = v2(0, 0);
    memset(IN.pressed, 0, sizeof IN.pressed);
    IN.down[ACT_ATTACK] = false;
    if (W.player_dead) { IN.pressed[ACT_RELOAD] = true; return; }
    if (!p->alive || W.exiting) return;
    /* long stall: wander somewhere random for a moment */
    static float stall_t, wander_t;
    static V2 stall_pos, wander_goal;
    stall_t += dt;
    if (stall_t > 15) {
        if (v2_dist(p->pos, stall_pos) < 20) {
            wander_t = 3;
            wander_goal = random_floor_spot(&g_rng, cell_flag(tile_of(p->pos.x), tile_of(p->pos.y), CF_INDOOR), 0, p->pos);
            if (p->cart >= 0) IN.pressed[ACT_INTERACT] = true;
        }
        stall_t = 0;
        stall_pos = p->pos;
    }
    if (wander_t > 0) {
        wander_t -= dt;
        go(wander_goal);
        IN.move = steer();
        g_aim_world = wander_goal;
        repath_t -= dt;
        return;
    }
    repath_t -= dt;
    think_t -= dt;
    press_cd -= dt;
    stuck_t += dt;
    if (stuck_t > 2.0f) {
        if (v2_dist(p->pos, last_pos) < 8) {
            repath_t = 0;
            if (target_cont >= 0 && nblack < 64) blacklist[nblack++] = target_cont;
            target_cont = -1;
        }
        stuck_t = 0;
        last_pos = p->pos;
    }
    /* finish downed enemies */
    for (int i = 1; i < W.nactors; i++) {
        Actor *a = &W.actors[i];
        if (a->used && a->alive && a->down_t > 0 && v2_dist(a->pos, p->pos) < 60) {
            go(a->pos);
            IN.move = steer();
            g_aim_world = a->pos;
            if (v2_dist(a->pos, p->pos) < 14 && press_cd <= 0) { IN.pressed[ACT_EXECUTE] = true; press_cd = 0.3f; }
            return;
        }
    }
    /* bare hands, or a gun with nothing left to shoot it with: take something off your back */
    const WeaponDef *hw = item_weapon(p->weapon.id);
    if ((!p->weapon.id || (hw->kind == WK_GUN && p->weapon.cond <= 0 && inv_count(p, hw->ammo) <= 0)) && press_cd <= 0)
        for (int k = 0; k < WSLOTS; k++)
            if (k != p->wslot && p->slots[k].id) { IN.pressed[ACT_SLOT1 + k] = true; press_cd = 0.3f; break; }
    /* fight */
    int e = p->cart >= 0 ? -1 : nearest_enemy(150);
    if (e >= 0) {
        Actor *a = &W.actors[e];
        g_aim_world = a->pos;
        const WeaponDef *w = item_weapon(p->weapon.id);
        float d = v2_dist(a->pos, p->pos);
        if (w->kind == WK_GUN && p->weapon.cond > 0) {
            IN.down[ACT_ATTACK] = true;
            if (p->atk_cd <= 0) IN.pressed[ACT_ATTACK] = true;
            if (d < 50) IN.move = v2_norm(v2_sub(p->pos, a->pos));
        } else {
            go(a->pos);
            IN.move = steer();
            if (d < w->range + 10 && p->atk_cd <= 0) IN.pressed[ACT_ATTACK] = true;
            if (w->kind == WK_THROWN && d < 120 && p->atk_cd <= 0) IN.pressed[ACT_ATTACK] = true;
        }
        return;
    }
    /* grab a weapon if unarmed */
    if (!p->weapon.id) {
        for (int i = 0; i < MAX_PICKUPS; i++) {
            Pickup *pk = &W.pickups[i];
            if (!pk->alive || pk->flying || ITEMS[pk->st.id].cat != CAT_WEAPON) continue;
            if (v2_dist(pk->pos, p->pos) > 160) continue;
            go(pk->pos);
            IN.move = steer();
            g_aim_world = pk->pos;
            if (v2_dist(pk->pos, p->pos) < 14 && press_cd <= 0) { IN.pressed[ACT_INTERACT] = true; press_cd = 0.3f; }
            return;
        }
    }
    if (p->hp < p->maxhp - 2) IN.pressed[ACT_HEAL] = true;
    /* bag nearly full: grab a shopping cart - or, if the van's nearer and the bag holds list items, unload there */
    if (p->cart < 0 && inv_used(p) >= inv_capacity(p) - 1 && !W.list_done) {
        int best = -1;
        float bd = 1e9f;   /* any cart beats standing around with a full bag */
        for (int i = 0; i < MAX_CARTS; i++) {
            Cart *c = &W.carts[i];
            if (!c->alive || c->holder >= 0) continue;
            float d = v2_dist(c->pos, p->pos);
            if (d < bd) { bd = d; best = i; }
        }
        V2 ex = v2(W.exit_rect.x + W.exit_rect.w / 2, W.exit_rect.y + W.exit_rect.h / 2);
        if (van_loadable() && v2_dist(ex, p->pos) < bd) {
            go(ex);
            IN.move = steer();
            g_aim_world = ex;
            if (v2_dist(ex, p->pos) < 14 && press_cd <= 0) { IN.pressed[ACT_INTERACT] = true; press_cd = 0.5f; }
            return;
        }
        if (best >= 0) {
            Cart *c = &W.carts[best];
            go(c->pos);
            IN.move = steer();
            g_aim_world = c->pos;
            if (bd < 18 && press_cd <= 0) { IN.pressed[ACT_INTERACT] = true; press_cd = 0.5f; }
            return;
        }
    }
    /* list done: drive home */
    if (W.list_done) {
        V2 ex = v2(W.exit_rect.x + W.exit_rect.w / 2, W.exit_rect.y + W.exit_rect.h / 2);
        go(ex);
        IN.move = steer();
        g_aim_world = ex;
        if (v2_dist(ex, p->pos) < 14 && press_cd <= 0) { IN.pressed[ACT_INTERACT] = true; press_cd = 0.5f; }
        return;
    }
    /* loose list items on the floor */
    for (int i = 0; i < MAX_PICKUPS; i++) {
        Pickup *pk = &W.pickups[i];
        if (!pk->alive || pk->flying || !wants((ItemId)pk->st.id)) continue;
        go(pk->pos);
        IN.move = steer();
        g_aim_world = pk->pos;
        if (v2_dist(pk->pos, p->pos) < 14 && press_cd <= 0) {
            /* full bag: dump some junk, as a player would in the bag screen */
            if (interact_pickup(p) != i) make_room(p, (ItemId)pk->st.id, pk->st.count);
            if (p->cart < 0) IN.pressed[ACT_INTERACT] = true;   /* behind a cart, walking over it picks it up */
            press_cd = 0.6f;
        }
        return;
    }
    /* someone carries what we need: take it */
    for (int i = 1; i < W.nactors; i++) {
        Actor *a = &W.actors[i];
        if (!a->used || !a->alive) continue;
        for (int k = 0; k < a->ninv; k++)
            if (wants((ItemId)a->inv[k].id)) {
                go(a->pos);
                IN.move = steer();
                g_aim_world = a->pos;
                if (v2_dist(a->pos, p->pos) < 40 && p->cart >= 0 && press_cd <= 0) { IN.pressed[ACT_INTERACT] = true; press_cd = 0.4f; }
                else if (v2_dist(a->pos, p->pos) < 22 && p->atk_cd <= 0 && p->cart < 0) IN.pressed[ACT_ATTACK] = true;
                return;
            }
    }
    /* shop: the nearest container with a list item, else any loot */
    if (target_cont < 0 || W.conts[target_cont].n == 0 || think_t <= 0) {
        think_t = 3;
        int best = -1;
        float bd = 1e9f;
        for (int i = 0; i < W.nconts; i++) {
            Container *c = &W.conts[i];
            if (c->n == 0 || black(i)) continue;
            bool want = false;
            for (int k = 0; k < c->n; k++) if (wants((ItemId)c->items[k].id)) want = true;
            float d = v2_dist(c->pos, p->pos) * (want ? 0.2f : 1.0f);
            if (d < bd) { bd = d; best = i; }
        }
        if (best < 0 && nblack) nblack = 0;   /* ran out of targets: give the blacklisted ones another go */
        target_cont = best;
    }
    if (target_cont >= 0) {
        Container *c = &W.conts[target_cont];
        go(c->pos);
        V2 mv = steer();
        g_aim_world = c->pos;
        extern int g_search_cont;
        if (v2_dist(c->pos, p->pos) < (p->cart >= 0 ? 34 : 22)) {
            if (g_search_cont < 0 && press_cd <= 0) { IN.pressed[ACT_INTERACT] = true; press_cd = 0.8f; }
        } else IN.move = mv;
    }
}
