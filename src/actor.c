/* LAST AISLE - actors: spawning, physics, player control, inventory, drawing */
#include "world.h"
#include "gfx.h"
#include "input.h"
#include "audio.h"
#include "game.h"
#include "hub.h"

Actor *player(void) { return &W.actors[0]; }

static int search_ci = -1;
static float search_t = 0;
static float saw_noise_t = 0;   /* next chainsaw noise event */
float g_search_progress = 0;   /* read by the HUD */
int g_search_cont = -1;

void actor_reset_level_state(void) {
    search_ci = -1;
    search_t = 0;
    saw_noise_t = 0;
    g_search_progress = 0;
    g_search_cont = -1;
}

/* player-only interaction state */
static const float SEARCH_TIME = 0.55f;

int actor_spawn(int arch, V2 pos) {
    int idx = -1;
    if (arch == AR_PLAYER) idx = 0;
    else
        for (int i = 1; i < MAX_ACTORS; i++)
            if (!W.actors[i].used) { idx = i; break; }
    if (idx < 0) return -1;
    Actor *a = &W.actors[idx];
    memset(a, 0, sizeof *a);
    const ArchDef *d = &ARCH[arch];
    a->used = a->alive = true;
    a->arch = arch;
    a->faction = d->faction;
    a->temper = d->temper;
    a->pos = pos;
    a->radius = d->radius;
    a->hp = a->maxhp = d->hp;
    a->face = frange(-PI_F, PI_F);
    a->cart = -1;
    a->exec_target = -1;
    a->last_hit_by = -1;
    a->speed_mul = frange(0.92f, 1.08f);
    a->br.target = -1;
    a->br.leader = -1;
    a->br.container = -1;
    for (int k = 0; k < 4; k++) a->br.grudge[k] = -1;
    a->br.state = AI_WANDER;
    a->br.think = frange(0, 0.5f);
    a->br.strafe_dir = chance(0.5f) ? 1.0f : -1.0f;
    a->spawn_grace = 1.0f;
    if (arch == AR_SCAV && chance(0.35f)) a->temper = TEMP_DEFENSIVE;
    if (arch == AR_LOOTER && chance(0.3f)) a->temper = TEMP_TIMID;
    if (idx >= W.nactors) W.nactors = idx + 1;
    return idx;
}

/* ------------------------------------------------------------ inventory */
int inv_capacity(Actor *a) {
    if (a != player()) return 999;
    return bag_capacity(RUN.bag) + RUN.perks[PK_PACKMULE] * 4;
}

int inv_used(Actor *a) {
    int u = 0;
    for (int i = 0; i < a->ninv; i++) u += ITEMS[a->inv[i].id].size * a->inv[i].count;
    return u;
}

int inv_count(Actor *a, ItemId id) {
    int n = 0;
    for (int i = 0; i < a->ninv; i++)
        if (a->inv[i].id == id) n += a->inv[i].count;
    return n;
}

/* the whole stack fits in the bag: space and slots (exactly what inv_add needs) */
static bool inv_fits(Actor *a, Stack st) {
    if (st.id <= IT_NONE || st.count <= 0) return true;
    if (inv_used(a) + ITEMS[st.id].size * st.count > inv_capacity(a)) return false;
    int maxs = MAXF(1, ITEMS[st.id].stack);
    int room = 0;
    if (maxs > 1)
        for (int i = 0; i < a->ninv; i++)
            if (a->inv[i].id == st.id) room += MAXF(0, maxs - a->inv[i].count);
    int rest = st.count - room;
    int slots_needed = rest > 0 ? (rest + maxs - 1) / maxs : 0;
    return a->ninv + slots_needed <= INV_MAX;
}

bool inv_can_fit(Actor *a, ItemId id, int n) { return inv_fits(a, (Stack){(int16_t)id, (int16_t)n, 0}); }

bool inv_add(Actor *a, Stack st) {
    if (st.id <= IT_NONE || st.count <= 0) return true;
    /* all-or-nothing: make sure the whole stack fits before touching anything */
    if (!inv_fits(a, st)) return false;
    int maxs = MAXF(1, ITEMS[st.id].stack);
    if (maxs > 1) {
        for (int i = 0; i < a->ninv && st.count > 0; i++) {
            Stack *s = &a->inv[i];
            if (s->id != st.id) continue;
            int mv = MINF(maxs - s->count, st.count);
            if (mv <= 0) continue;
            s->count += mv;
            st.count -= mv;
        }
    }
    while (st.count > 0) {
        Stack part = st;
        part.count = (int16_t)MINF(st.count, maxs);
        a->inv[a->ninv++] = part;
        st.count -= part.count;
    }
    return true;
}

bool inv_remove(Actor *a, ItemId id, int n) {
    if (inv_count(a, id) < n) return false;
    for (int i = a->ninv - 1; i >= 0 && n > 0; i--) {
        Stack *s = &a->inv[i];
        if (s->id != id) continue;
        int mv = MINF(n, s->count);
        s->count -= mv;
        n -= mv;
        if (s->count <= 0) {
            memmove(&a->inv[i], &a->inv[i + 1], sizeof(Stack) * (a->ninv - i - 1));
            a->ninv--;
        }
    }
    return true;
}

/* still short of it for the list, or for one of the camp's favours */
bool item_needed(ItemId id) {
    for (int i = 0; i < W.nlist; i++)
        if (W.list[i].id == id && W.list[i].have < W.list[i].need) return true;
    for (int i = 0; i < W.nfav; i++)
        if (W.fav[i].id == id && W.fav[i].have < W.fav[i].need) return true;
    return false;
}

/* units of `it` that may be thrown out: everything for junk, only the surplus for shopping-list and favour items */
static int spare_units(ItemId it) {
    int want = list_want(it);
    return want ? MAXF(0, total_have(it) - want) : 1 << 20;
}

static int drop_score(ItemId it) {
    int sc = ITEMS[it].value;
    if (ITEMS[it].cat == CAT_MED) sc += 400;
    if (ITEMS[it].cat == CAT_WEAPON) sc += 200;
    return sc;
}

/* can enough junk go to fit `want`? (bag: space only; the slot check is done while dropping) */
static bool room_possible(Actor *a, Stack want) {
    int left[IT_COUNT], freeable = 0;
    for (int i = 0; i < IT_COUNT; i++) left[i] = -1;
    for (int i = 0; i < a->ninv; i++) {
        ItemId it = (ItemId)a->inv[i].id;
        if (it == want.id || ITEMS[it].size == 0) continue;
        if (left[it] < 0) left[it] = spare_units(it);
        int u = MINF(a->inv[i].count, left[it]);
        left[it] -= u;
        freeable += u * ITEMS[it].size;
    }
    return inv_used(a) - freeable + ITEMS[want.id].size * want.count <= inv_capacity(a);
}

/* drop the least useful non-list items until `n` of `id` fit - or drop nothing if they can't be made to fit */
bool make_room(Actor *a, ItemId id, int n) {
    Stack want = {(int16_t)id, (int16_t)n, 0};
    if (!room_possible(a, want)) return false;
    while (!inv_fits(a, want)) {
        /* short of space: one unit at a time; only short of slots: a whole junk stack */
        bool space = inv_used(a) + ITEMS[id].size * n > inv_capacity(a);
        int best = -1, bscore = 1 << 30;
        for (int i = 0; i < a->ninv; i++) {
            ItemId it = (ItemId)a->inv[i].id;
            if (it == id || ITEMS[it].size == 0) continue;
            int spare = spare_units(it);
            if (spare <= 0 || (!space && spare < a->inv[i].count)) continue;
            int sc = drop_score(it);
            if (sc < bscore) { bscore = sc; best = i; }
        }
        if (best < 0) return false;
        Stack s = a->inv[best];
        int drop = space ? 1 : s.count;
        a->inv[best].count -= drop;
        if (a->inv[best].count <= 0) {
            memmove(&a->inv[best], &a->inv[best + 1], sizeof(Stack) * (a->ninv - best - 1));
            a->ninv--;
        }
        s.count = (int16_t)drop;
        int pi = pickup_spawn(s, a->pos, v2(frange(-40, 40), frange(-40, 40)));
        if (pi >= 0) W.pickups[pi].dropped = true;
        char buf[48];
        SDL_snprintf(buf, sizeof buf, "Dropped %s", ITEMS[s.id].name);
        floater(v2(a->pos.x, a->pos.y + 10), buf, COL_GREY, false);
    }
    return true;
}

/* same for a shopping cart: dump junk out of the basket */
static int cart_used_units(Cart *c) {
    int u = 0;
    for (int k = 0; k < c->n; k++) u += ITEMS[c->items[k].id].size * c->items[k].count;
    return u;
}

static bool cart_can_merge(Cart *c, ItemId id) {
    if (ITEMS[id].stack <= 1) return false;
    for (int k = 0; k < c->n; k++) if (c->items[k].id == id) return true;
    return false;
}

static bool cart_fits(Cart *c, Stack st) {
    return cart_used_units(c) + ITEMS[st.id].size * st.count <= CART_CAP && (c->n < 16 || cart_can_merge(c, (ItemId)st.id));
}

bool cart_make_room(Cart *c, ItemId id, int n) {
    int need = ITEMS[id].size * n;
    bool merge = cart_can_merge(c, id);
    /* dry run: if it can't be done, don't dump anything */
    int left[IT_COUNT], freeable = 0, slots = 16 - c->n;
    for (int i = 0; i < IT_COUNT; i++) left[i] = -1;
    for (int k = 0; k < c->n; k++) {
        ItemId it = (ItemId)c->items[k].id;
        if (it == id) continue;
        if (left[it] < 0) left[it] = spare_units(it);
        int u = MINF(c->items[k].count, left[it]);
        left[it] -= u;
        freeable += u * ITEMS[it].size;
        if (u == c->items[k].count) slots++;
    }
    if (cart_used_units(c) - freeable + need > CART_CAP || (!merge && slots < 1)) return false;
    while (cart_used_units(c) + need > CART_CAP || (!merge && c->n >= 16)) {
        bool space = cart_used_units(c) + need > CART_CAP;
        int best = -1, bscore = 1 << 30;
        for (int k = 0; k < c->n; k++) {
            ItemId it = (ItemId)c->items[k].id;
            if (it == id) continue;
            int spare = spare_units(it);
            if (spare <= 0 || (!space && spare < c->items[k].count)) continue;
            int sc = ITEMS[it].value + (ITEMS[it].cat == CAT_MED ? 400 : 0);
            if (sc < bscore) { bscore = sc; best = k; }
        }
        if (best < 0) return false;
        /* junk goes whole; a list item only loses what the list doesn't need */
        Stack s = c->items[best];
        int drop = MINF(s.count, spare_units((ItemId)s.id));
        c->items[best].count -= drop;
        if (c->items[best].count <= 0) {
            memmove(&c->items[best], &c->items[best + 1], sizeof(Stack) * (c->n - best - 1));
            c->n--;
        }
        s.count = (int16_t)drop;
        int pi = pickup_spawn(s, c->pos, v2(frange(-40, 40), frange(-40, 40)));
        if (pi >= 0) W.pickups[pi].dropped = true;
    }
    return true;
}

static Stack make_weapon_stack(ItemId id) {
    Stack s = {id, 1, 0};
    const WeaponDef *w = item_weapon(id);
    if (w->kind == WK_GUN) s.cond = (int16_t)w->mag;
    else if (w->kind == WK_MELEE || w->kind == WK_CHAINSAW || w->kind == WK_FLAME) s.cond = (int16_t)w->durability;
    return s;
}

void drop_weapon(Actor *a, bool thrown) {
    if (!a->weapon.id) return;
    V2 v = thrown ? v2(0, 0) : v2(frange(-30, 30), frange(-30, 30));
    pickup_spawn(a->weapon, a->pos, v);
    a->weapon.id = IT_NONE;
    a->weapon.count = 0;
    a->weapon.cond = 0;
    a->hit_pending = false;
}

/* ------------------------------------------------------- weapon slots */
/* you carry up to WSLOTS weapons: the one in your hands (slot wslot) and the rest on your back, none of them in the bag */
int weapon_free_slot(Actor *a) {
    if (!a->weapon.id) return a->wslot;
    for (int k = 0; k < WSLOTS; k++) if (!weapon_slot(a, k)->id) return k;
    return -1;
}

static void hands_reset(Actor *a, float cd) {
    a->atk_cd = MAXF(a->atk_cd, cd);
    a->atk_t = -1;
    a->hit_pending = false;   /* a swing started with the old weapon never lands with the new one */
    a->reload_t = 0;
}

static void hands_to(Actor *a, int k) {
    a->slots[a->wslot] = a->weapon;
    a->weapon = a->slots[k];
    a->slots[k] = (Stack){IT_NONE, 0, 0, 0};
    a->wslot = k;
    hands_reset(a, 0.2f);
}

bool weapon_select(Actor *a, int k) {
    if (k < 0 || k >= WSLOTS || k == a->wslot) return false;
    hands_to(a, k);
    audio_play(SFX_PICKUP_WEAPON, a->weapon.id ? 0.6f : 0.4f, 0, a->weapon.id ? 1.0f : 0.9f);
    SDL_Log("WEAPON: slot %d in hand (%s)", k + 1, a->weapon.id ? ITEMS[a->weapon.id].name : "bare hands");
    return true;
}

/* steps over the empty slots - unless the one in hand is all you carry: then it goes on your back and back again */
void weapon_cycle(Actor *a, int dir) {
    for (int i = 1; i < WSLOTS; i++) {
        int k = ((a->wslot + dir * i) % WSLOTS + WSLOTS) % WSLOTS;
        if (weapon_slot(a, k)->id) { weapon_select(a, k); return; }
    }
    if (a->weapon.id) {
        for (int i = 1; i < WSLOTS; i++) {
            int k = ((a->wslot + dir * i) % WSLOTS + WSLOTS) % WSLOTS;
            if (!a->slots[k].id) { weapon_select(a, k); return; }
        }
    }
}

/* onto a stack of the same throwables that has room, else into a free slot and in hand, else in place of the one in
   your hands. *st keeps what didn't fit on the stack (count 0: all of it went in); returns the weapon it took the place of */
Stack weapon_take(Actor *a, Stack *st) {
    Stack old = {IT_NONE, 0, 0, 0};
    int maxs = ITEMS[st->id].stack;
    if (maxs > 1) {
        bool merged = false;
        for (int i = 0; i < WSLOTS && st->count > 0; i++) {
            Stack *s = weapon_slot(a, (a->wslot + i) % WSLOTS);
            if (s->id != st->id || s->count >= maxs) continue;
            int mv = MINF(maxs - s->count, st->count);
            s->count += mv;
            st->count -= mv;
            merged = true;
        }
        if (merged) return old;   /* topped up: what didn't fit stays where it was */
    }
    int k = weapon_free_slot(a);
    if (k < 0) {
        old = a->weapon;
        k = a->wslot;
    }
    if (k == a->wslot) {
        a->weapon = *st;
        hands_reset(a, 0.15f);
    } else {
        a->slots[k] = *st;
        hands_to(a, k);
    }
    st->count = 0;
    return old;
}

Stack weapon_slot_remove(Actor *a, int k) {
    Stack *s = weapon_slot(a, k);
    Stack out = *s;
    *s = (Stack){IT_NONE, 0, 0, 0};
    if (k == a->wslot) a->hit_pending = false;
    return out;
}

void use_heal(Actor *a) {
    if (a->hp >= a->maxhp) { world_hint("Already at full health."); return; }
    ItemId order[3] = {IT_BANDAGE, IT_PAINKILLERS, IT_MEDKIT};
    int missing = a->maxhp - a->hp;
    /* pick the smallest med that covers the damage, otherwise the biggest */
    ItemId pick = IT_NONE;
    for (int i = 0; i < 3; i++)
        if (inv_count(a, order[i]) > 0 && ITEMS[order[i]].heal >= missing) { pick = order[i]; break; }
    if (!pick)
        for (int i = 2; i >= 0; i--)
            if (inv_count(a, order[i]) > 0) { pick = order[i]; break; }
    if (!pick) { world_hint("No bandages or meds."); audio_play(SFX_UI_ERROR, 0.5f, 0, 1); return; }
    inv_remove(a, pick, 1);
    a->hp = MINF(a->maxhp, a->hp + ITEMS[pick].heal);
    audio_play(SFX_HEAL, 0.8f, 0, 1);
    floater(v2(a->pos.x, a->pos.y - 14), "+HEALTH", COL_GREEN, false);
}

/* -------------------------------------------------------------- crafting */
static int have_for_craft(Actor *a, ItemId id) {
    int n = inv_count(a, id);
    for (int k = 0; k < WSLOTS; k++) if (weapon_slot(a, k)->id == id) n += weapon_slot(a, k)->count;
    return n;
}

/* a chainsaw you carry - hands, back or bag - that could take more fuel */
static Stack *thirsty_saw(Actor *a) {
    for (int i = 0; i < WSLOTS; i++) {
        Stack *s = weapon_slot(a, (a->wslot + i) % WSLOTS);
        if (s->id == IT_CHAINSAW && s->cond < 100) return s;
    }
    for (int i = 0; i < a->ninv; i++) if (a->inv[i].id == IT_CHAINSAW && a->inv[i].cond < 100) return &a->inv[i];
    return NULL;
}

/* units crafting may use: bag + hands, minus what the shopping list and the favours still need (bag and carts counted) */
int craft_have(Actor *a, ItemId id) {
    int n = have_for_craft(a, id);
    int want = a == player() ? list_want(id) : 0;
    if (want) n = MINF(n, MAXF(0, total_have(id) - want));
    return n;
}

static bool can_craft_ex(Actor *a, const Recipe *r, bool keep_list) {
    if (r->flag == RF_REPAIR) {
        const WeaponDef *w = item_weapon(a->weapon.id);
        if (!a->weapon.id || w->kind != WK_MELEE) return false;
        if (a->weapon.cond >= weapon_max_cond(&a->weapon)) return false;
    }
    if (r->flag == RF_REFUEL && !thirsty_saw(a)) return false;
    for (int k = 0; k < 3; k++) {
        if (!r->in[k].id) continue;
        int have = keep_list ? craft_have(a, r->in[k].id) : have_for_craft(a, r->in[k].id);
        if (have < r->in[k].n) return false;
    }
    return true;
}

/* shopping-list items the list still needs are never crafted away */
bool can_craft(Actor *a, const Recipe *r) { return can_craft_ex(a, r, true); }

bool craft_list_blocked(Actor *a, const Recipe *r) { return !can_craft_ex(a, r, true) && can_craft_ex(a, r, false); }

/* from the bag first, then the weapons on your back, the one in hand last */
static void consume_for_craft(Actor *a, ItemId id, int n) {
    int from_bag = MINF(n, inv_count(a, id));
    inv_remove(a, id, from_bag);
    n -= from_bag;
    for (int i = 1; i <= WSLOTS && n > 0; i++) {
        int k = (a->wslot + i) % WSLOTS;
        Stack *s = weapon_slot(a, k);
        if (s->id != id) continue;
        int mv = MINF(n, s->count);
        s->count -= mv;
        n -= mv;
        if (s->count <= 0) weapon_slot_remove(a, k);
    }
}

bool craft(Actor *a, const Recipe *r) {
    if (!can_craft(a, r)) return false;
    a->hit_pending = false;
    if (r->flag == RF_REPAIR) {
        WeaponDef w = weapon_stats(&a->weapon);
        consume_for_craft(a, IT_DUCTTAPE, 1);
        int maxd = weapon_max_cond(&a->weapon);
        a->weapon.cond = (int16_t)MINF(maxd, a->weapon.cond + w.durability * (RUN.perks[PK_TINKERER] ? 1.0f : 0.6f));
        audio_play(SFX_CRAFT, 0.8f, 0, 1);
        return true;
    }
    if (r->flag == RF_REFUEL) {
        consume_for_craft(a, IT_GASOLINE, 1);
        Stack *saw = thirsty_saw(a);
        if (saw) saw->cond = 100;
        audio_play(SFX_CRAFT, 0.8f, 0, 0.8f);
        return true;
    }
    for (int k = 0; k < 3; k++)
        if (r->in[k].id) consume_for_craft(a, r->in[k].id, r->in[k].n);
    Stack out = {IT_NONE, 0, 0, 0};
    if (item_is_weapon(r->out) && ITEMS[r->out].stack == 1) {
        out = make_weapon_stack(r->out);
        if (RUN.perks[PK_TINKERER] && item_weapon(r->out)->kind == WK_MELEE) out.cond *= 2;
    } else {
        out.id = r->out;
        out.count = (int16_t)r->out_n;
        out.cond = 0;
    }
    /* crafted weapons: onto the stack of the same you carry, else the hands if they're free, else a free slot on your
       back, else the bag */
    if (item_is_weapon(r->out)) {
        int maxs = ITEMS[r->out].stack;
        for (int k = 0; k < WSLOTS && maxs > 1 && out.count > 0; k++) {
            Stack *s = weapon_slot(a, (a->wslot + k) % WSLOTS);
            if (s->id != r->out || s->count >= maxs) continue;
            int mv = MINF(maxs - s->count, out.count);
            s->count += mv;
            out.count -= mv;
        }
        int k = weapon_free_slot(a);
        if (out.count > 0 && k >= 0) {
            *weapon_slot(a, k) = out;
            out.count = 0;
        }
    }
    if (out.count > 0 && !inv_add(a, out)) pickup_spawn(out, a->pos, v2(0, 0));
    audio_play(SFX_CRAFT, 0.9f, 0, 1);
    return true;
}

/* ----------------------------------------------------------- shopping */
/* the cart you push, one parked where you can see it (list_recount keeps near_until fresh), or one at the van */
bool cart_counts(const Cart *c) {
    if (!c->alive) return false;
    return c->holder == 0 || (W.time < c->near_until && v2_dist(c->pos, player()->pos) < 96) ||
           (c->pos.x > W.exit_rect.x - 16 && c->pos.x < W.exit_rect.x + W.exit_rect.w + 16 &&
            c->pos.y > W.exit_rect.y - 16 && c->pos.y < W.exit_rect.y + W.exit_rect.h + 16);
}

int total_have(ItemId id) {
    int n = inv_count(player(), id) + van_count(id);
    for (int i = 0; i < MAX_CARTS; i++) {
        Cart *c = &W.carts[i];
        if (!cart_counts(c)) continue;
        for (int k = 0; k < c->n; k++)
            if (c->items[k].id == id) n += c->items[k].count;
    }
    return n;
}

/* ------------------------------------------------------------- physics */
static void actor_physics(Actor *a, int idx, float dt) {
    V2 v = v2_add(a->vel, a->push);
    a->pos = v2_add(a->pos, v2_scale(v, dt));
    float k = a->down_t > 0 ? 5.0f : 9.0f;
    a->push = v2_scale(a->push, MAXF(0.0f, 1.0f - k * dt));
    V2 before = a->pos;
    collide_circle(&a->pos, a->radius, &a->vel);
    /* slammed into a wall hard */
    if (v2_len(a->push) > 220 && v2_dist(before, a->pos) > 0.5f) {
        a->push = v2_scale(a->push, -0.25f);
        if (a->alive && idx != 0 && a->down_t <= 0) {
            add_shake(2);
        }
    }
    /* keep inside map */
    a->pos.x = CLAMP(a->pos.x, TILE, (W.w - 1) * TILE);
    a->pos.y = CLAMP(a->pos.y, TILE, (W.h - 1) * TILE);
}

/* ---------------------------------------------------------- common update */
void actor_update(Actor *a, int idx, float dt) {
    if (!a->used) return;
    if (!a->alive) return;
    a->hurt_t = MAXF(0, a->hurt_t - dt);
    a->invuln = MAXF(0, a->invuln - dt);
    a->spawn_grace = MAXF(0, a->spawn_grace - dt);
    a->alert_icon_t = MAXF(0, a->alert_icon_t - dt);
    a->atk_cd -= dt;
    if (a->stun_t > 0) a->stun_t -= dt;
    /* burning */
    if (a->burn_t > 0) {
        a->burn_t -= dt;
        if (!(idx == 0 && RUN.perks[PK_FIREBUG])) {
            a->flame_t += dt;
            if (a->flame_t > 0.5f) {
                a->flame_t = 0;
                damage_actor(idx, a->last_hit_by, 1, v2(0, 0), 0, 0, -1, DMG_FIRE);
                if (!a->alive) return;
            }
        } else a->burn_t = 0;
        if (chance(dt * 30)) {
            Particle *p = particle_add(PT_FIRE, v2(a->pos.x + frange(-4, 4), a->pos.y + frange(-4, 4)), v2(frange(-10, 10), frange(-30, -10)), frange(0.3f, 0.6f));
            if (p) { p->spr = SPR_FX_FIRE; p->frames = 4; p->scale = 0.6f; }
        }
        add_light(a->pos, 40, COL_ORANGE, 0.6f);
    }
    /* knocked down */
    if (a->down_t > 0) {
        a->down_t -= dt;
        a->vel = v2(0, 0);
        actor_physics(a, idx, dt);
        if (a->down_t <= 0) {
            a->down_t = 0;
            a->invuln = 0.25f;
        }
        return;
    }
    actor_physics(a, idx, dt);
    /* walking animation */
    float sp = v2_len(a->vel);
    if (sp > 5) {
        a->move_angle = lerp_angle(a->move_angle, v2_to_angle(a->vel), smooth_k(14, dt));
        a->leg_anim += sp * dt;
        a->step_t -= sp * dt;
        if (a->step_t <= 0) {
            a->step_t = 22;
            if (a->blood_steps > 0) {
                a->blood_steps--;
                V2 side = v2_scale(v2_angle(a->move_angle + PI_F * 0.5f), (a->blood_steps & 1) ? 2.5f : -2.5f);
                decal(SPR_FX_FOOTPRINT + (a->blood_steps & 1), v2_add(a->pos, side), a->move_angle, 1,
                      rgba(255, 255, 255, (Uint8)(110 + a->blood_steps * 12)));
            }
            if (sp > 70 && chance(0.35f)) {
                Particle *d = particle_add(PT_DUST, v2(a->pos.x + frange(-3, 3), a->pos.y + frange(-3, 3)),
                                           v2_scale(a->vel, -0.15f), frange(0.25f, 0.4f));
                if (d) { d->spr = SPR_FX_DUST; d->frames = 3; d->col = rgba(255, 255, 255, 150); }
            }
            if (idx == 0) {
                audio_play(SFX_FOOTSTEP, 0.6f, 0, frange(0.85f, 1.15f));
                make_noise(a->pos, RUN.perks[PK_LIGHTFEET] ? 22 : 44, 0);
            }
        }
    } else {
        a->leg_anim = 0;
    }
    /* stepping through blood (paws leave no shoe prints) */
    if (a->blood_steps < 4 && !is_animal(a) && !is_plant(a)) {
        for (int i = 0; i < W.ncorpses; i++)
            if (v2_dist2(W.corpses[i].pos, a->pos) < 12 * 12) { a->blood_steps = 10; break; }
    }
}

/* ------------------------------------------------------------- player */
/* E on this floor item would actually take it (a bag that's bigger, room in the cart or the bag) */
static bool can_collect(Actor *p, Pickup *pk) {
    ItemId id = (ItemId)pk->st.id;
    if (ITEMS[id].cat == CAT_WEAPON) return true;
    if (ITEMS[id].cat == CAT_BAG) return bag_capacity(id) > bag_capacity(RUN.bag);
    if (p->cart >= 0 && cart_fits(&W.carts[p->cart], pk->st)) return true;
    if (inv_fits(p, pk->st)) return true;
    return item_needed(id) && room_possible(p, pk->st);
}

/* lying within arm's reach: not in the air, not a lit fuse, nothing in between */
static bool in_reach(Actor *p, Pickup *pk) {
    return pk->alive && !pk->flying && pk->fuse <= 0 && v2_dist2(pk->pos, p->pos) < PICK_REACH * PICK_REACH &&
           reach_clear(p->pos, pk->pos);
}

/* where your hand goes: the cursor (pulled in to arm's length), else the way you aim or face */
static V2 reach_point(Actor *p) {
    extern bool g_aim_override;
    extern V2 g_aim_world;
    V2 d = v2_scale(v2_angle(p->aim), PICK_REACH * 0.6f);
    if (!IN.pad_active || g_aim_override) {
        d = v2_sub(g_aim_override ? g_aim_world : gfx_screen_to_world(IN.mouse_screen.x, IN.mouse_screen.y), p->pos);
        float l = v2_len(d);
        if (l > PICK_REACH) d = v2_scale(d, PICK_REACH / l);
    }
    return v2_add(p->pos, d);
}

/* of what's within reach and would actually fit, the one nearest your hand - point at the one you want.
   Junk you can't carry never eats [E] */
int interact_pickup(Actor *p) {
    V2 at = reach_point(p);
    int best = -1;
    float bd = 1e18f;
    for (int i = 0; i < MAX_PICKUPS; i++) {
        Pickup *pk = &W.pickups[i];
        if (!in_reach(p, pk) || !can_collect(p, pk)) continue;
        float d = v2_dist2(pk->pos, at);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

/* everything lying within reach, oldest first (what you drop joins the end) - the bag screen's floor panel.
   Fills up to max, returns how many there are */
int floor_items(Actor *p, int *out, int max) {
    int all[MAX_PICKUPS], n = 0;
    for (int i = 0; i < MAX_PICKUPS; i++) {
        Pickup *pk = &W.pickups[i];
        if (!in_reach(p, pk)) continue;
        int k = n++;
        while (k > 0 && W.pickups[all[k - 1]].age < pk->age) { all[k] = all[k - 1]; k--; }
        all[k] = i;
    }
    for (int k = 0; k < n && k < max; k++) out[k] = all[k];
    return n;
}

/* a weapon off the floor: a free slot takes it (in hand); with every slot full it takes the place of the one in your hands */
void pickup_take_weapon(Actor *p, int pi) {
    Pickup *pp = &W.pickups[pi];
    Stack old = weapon_take(p, &pp->st);
    if (pp->st.count <= 0) pp->alive = false;
    if (old.id) pickup_spawn(old, p->pos, v2(frange(-30, 30), frange(-30, 30)));
    audio_play(SFX_PICKUP_WEAPON, 0.8f, 0, 1);
}

/* the downed enemy [SPACE] finishes: close, and not through a door or a wall */
int execute_target(Actor *p) {
    int best = -1;
    float bd = 18 * 18;
    for (int i = 1; i < W.nactors; i++) {
        Actor *a = &W.actors[i];
        if (!a->used || !a->alive || a->down_t <= 0 || is_crew(a)) continue;   /* your own get back up */
        float d = v2_dist2(a->pos, p->pos);
        if (d < bd && reach_clear(p->pos, a->pos)) { bd = d; best = i; }
    }
    return best;
}

static const char *bag_full_hint(void) {
    if (W.hub) return ctl("Bag is full. Drop something [TAB] or grab a cart.", "Bag is full. Drop something [BACK] or grab a cart.");
    return ctl("Bag is full. Drop something [TAB], grab a cart or unload at the van.",
               "Bag is full. Drop something [BACK], grab a cart or unload at the van.");
}

void pickup_collect(Actor *p, int pi) {
    Pickup *pk = &W.pickups[pi];
    ItemId id = (ItemId)pk->st.id;
    const ItemDef *it = &ITEMS[id];
    if (it->cat == CAT_BAG) {
        if (bag_capacity(id) > bag_capacity(RUN.bag)) {
            Stack old = {RUN.bag, 1, 0};
            RUN.bag = id;
            pk->alive = false;
            pickup_spawn(old, p->pos, v2(frange(-20, 20), frange(-20, 20)));
            audio_play(SFX_PICKUP, 0.8f, 0, 0.8f);
            char buf[64];
            SDL_snprintf(buf, sizeof buf, "%s! (%d SPACE)", it->name, bag_capacity(id));
            floater(v2(p->pos.x, p->pos.y - 16), buf, COL_YELLOW, false);
        }
        return;
    }
    /* pushing a cart: loot goes into the cart */
    if (p->cart >= 0) {
        Cart *c = &W.carts[p->cart];
        bool fits = cart_fits(c, pk->st);
        if (!fits && item_needed(id) && !inv_fits(p, pk->st)) fits = cart_make_room(c, id, pk->st.count);
        if (fits) {
            bool merged = false;
            if (it->stack > 1)
                for (int k = 0; k < c->n; k++)
                    if (c->items[k].id == id) { c->items[k].count += pk->st.count; merged = true; break; }
            if (!merged) c->items[c->n++] = pk->st;
            pk->alive = false;
            audio_play(SFX_PICKUP, 0.9f, 0, frange(0.95f, 1.1f));
            floater(v2(pk->pos.x, pk->pos.y - 10), it->name, COL_WHITE, false);
            list_recount();
            return;
        }
    }
    if (!inv_add(p, pk->st)) {
        if (!(item_needed(id) && make_room(p, id, pk->st.count) && inv_add(p, pk->st))) {
            if (W.loot_cd <= 0) { world_hint(bag_full_hint()); W.loot_cd = 2.5f; }
            return;
        }
    }
    pk->alive = false;
    audio_play(SFX_PICKUP, 0.9f, 0, frange(0.95f, 1.1f));
    char buf[48];
    if (pk->st.count > 1) SDL_snprintf(buf, sizeof buf, "%s x%d", it->name, pk->st.count);
    else SDL_snprintf(buf, sizeof buf, "%s", it->name);
    floater(v2(pk->pos.x, pk->pos.y - 10), buf, COL_WHITE, false);
    list_recount();
}

static void try_reload(Actor *p) {
    const WeaponDef *w = item_weapon(p->weapon.id);
    if (w->kind != WK_GUN || p->reload_t > 0) return;
    int mag = weapon_max_cond(&p->weapon);
    if (p->weapon.cond >= mag) return;
    int per = w->ammo == IT_NAILS ? 15 : 1;  /* a box of nails holds 15 */
    if (inv_count(p, w->ammo) <= 0) {
        if (p == player()) { world_hint(ctl("Out of ammo. Throw it [RMB]!", "Out of ammo. Throw it [LT]!")); audio_play(SFX_EMPTY, 0.6f, 0, 1); }
        return;
    }
    int need = mag - p->weapon.cond;
    int take = MINF((need + per - 1) / per, inv_count(p, w->ammo));
    inv_remove(p, w->ammo, take);
    p->weapon.cond = (int16_t)MINF(mag, p->weapon.cond + take * per);
    p->reload_t = (w->mag <= 2 ? 0.5f : 0.85f) * (1.0f - 0.07f * train_level(STAT_AIM));
    audio_play(SFX_RELOAD, 0.7f, 0, 1);
}

void player_update(Actor *p, float dt) {
    if (!p->alive) return;
    float speed = ARCH[AR_PLAYER].run * (RUN.perks[PK_LIGHTFEET] ? 1.12f : 1.0f) * (1.0f + 0.03f * train_level(STAT_FIT));
    bool locked = p->exec_t > 0 || p->down_t > 0 || W.exiting || W.hub_lock == HUB_HELD;
    if (p->cart >= 0) speed *= 0.8f;
    if (search_ci >= 0) speed *= 0.0f;
    /* aim */
    if (IN.pad_active && v2_len2(IN.aim_stick) > 0.05f) {
        p->aim = v2_to_angle(IN.aim_stick);
        p->face = lerp_angle(p->face, p->aim, smooth_k(25, dt));
    } else if (IN.pad_active && v2_len2(IN.move) > 0.05f) {
        p->aim = v2_to_angle(IN.move);
        p->face = lerp_angle(p->face, p->aim, smooth_k(12, dt));
    } else {
        extern bool g_aim_override;
        extern V2 g_aim_world;
        V2 m = g_aim_override ? g_aim_world : gfx_screen_to_world(IN.mouse_screen.x, IN.mouse_screen.y);
        p->aim = v2_to_angle(v2_sub(m, p->pos));
        if (g_aim_override) p->face = lerp_angle(p->face, p->aim, smooth_k(20, dt));
        else p->face = p->aim;
    }
    if (p->down_t > 0) return;
    if (p->exec_t > 0) {
        execute_update(p, 0, dt);
        p->vel = v2(0, 0);
        return;
    }
    /* movement */
    V2 target = locked || W.hub_lock == HUB_AIM ? v2(0, 0) : v2_scale(IN.move, speed);
    float acc = v2_len2(IN.move) > 0 ? 22.0f : 16.0f;
    p->vel = v2_add(p->vel, v2_scale(v2_sub(target, p->vel), smooth_k(acc, dt)));
    if (locked) return;

    /* searching a container */
    if (search_ci >= 0) {
        Container *c = &W.conts[search_ci];
        if (v2_len2(IN.move) > 0.1f || v2_dist(p->pos, c->pos) > (p->cart >= 0 ? 46 : 34) || IN.pressed[ACT_ATTACK]) {
            search_ci = -1;
            search_t = 0;
        } else {
            search_t += dt;
            p->face = v2_to_angle(v2_sub(c->pos, p->pos));
            if (search_t >= SEARCH_TIME) {
                search_container(p, search_ci, true);
                search_ci = -1;
                search_t = 0;
            }
        }
    }
    g_search_progress = search_ci >= 0 ? search_t / SEARCH_TIME : 0;
    g_search_cont = search_ci;

    if (p->reload_t > 0) {
        p->reload_t -= dt;
    }

    /* the weapons you carry: 1-3 take that slot in hand, Q / Y / the wheel / the d-pad step through them
       (at the Greenhouse they're packed: the bag screen picks the one in hand) */
    if (!W.hub) {
        for (int k = 0; k < WSLOTS; k++) if (IN.pressed[ACT_SLOT1 + k]) weapon_select(p, k);
        if (IN.pressed[ACT_SWAP] || IN.pressed[ACT_WEAPON_NEXT]) weapon_cycle(p, 1);
        if (IN.pressed[ACT_WEAPON_PREV]) weapon_cycle(p, -1);
    }

    const WeaponDef *w = item_weapon(p->weapon.id);
    /* attack */
    if (p->cart >= 0) {
        if (IN.pressed[ACT_ATTACK]) {
            Cart *c = &W.carts[p->cart];
            c->holder = -1;
            c->pusher = 0;
            c->vel = v2_add(v2_scale(v2_angle(p->face), 330), v2_scale(p->vel, 0.5f));
            c->av = frange(-2, 2);
            p->cart = -1;
            audio_play(SFX_THROW, 0.8f, 0, 0.7f);
        }
    } else if (search_ci < 0 && (!W.hub || W.hub_lock == HUB_AIM)) {   /* at the Greenhouse only on the range */
        bool want = (w->kind == WK_GUN && p->weapon.id == IT_NAILGUN) || w->kind == WK_CHAINSAW || w->kind == WK_FLAME ||
                    (w->kind == WK_GUN && (p->weapon.id == IT_PISTOL))
                        ? IN.down[ACT_ATTACK]
                        : IN.pressed[ACT_ATTACK];
        if (w->kind == WK_GUN && p->weapon.id != IT_PISTOL && p->weapon.id != IT_NAILGUN) want = IN.pressed[ACT_ATTACK] || (IN.down[ACT_ATTACK] && p->atk_cd < -0.12f);
        if (w->kind == WK_MELEE || w->kind == WK_FIST) want = IN.pressed[ACT_ATTACK] || (IN.down[ACT_ATTACK] && p->atk_cd < -0.05f);
        if (want && p->atk_cd <= 0 && p->reload_t <= 0) {
            if (w->kind == WK_GUN && p->weapon.cond <= 0) {
                if (IN.pressed[ACT_ATTACK]) {
                    if (inv_count(p, w->ammo) > 0) try_reload(p);
                    else { audio_play(SFX_EMPTY, 0.7f, 0, 1); world_hint(ctl("Out of ammo. Throw it [RMB]!", "Out of ammo. Throw it [LT]!")); }
                }
            } else {
                attack_begin(p, 0);
            }
        }
    }
    if (w->kind == WK_CHAINSAW && p->weapon.id) {
        bool cutting = IN.down[ACT_ATTACK] && p->weapon.cond > 0;
        audio_loop(LOOP_CHAINSAW_IDLE, cutting ? 0 : 0.35f, 1);
        audio_loop(LOOP_CHAINSAW_CUT, cutting ? 0.55f : 0, 1);
        /* a running saw is heard a few times a second, not every frame (same at any frame rate, and cheap) */
        saw_noise_t -= dt;
        if (p->weapon.cond > 0 && saw_noise_t <= 0) {
            make_noise(p->pos, cutting ? 260 : 120, 0);
            saw_noise_t = 0.25f;
        }
    } else {
        audio_loop(LOOP_CHAINSAW_IDLE, 0, 1);
        audio_loop(LOOP_CHAINSAW_CUT, 0, 1);
    }
    audio_loop(LOOP_FIRE, 0, 1);
    /* throw */
    if (IN.pressed[ACT_THROW] && !W.hub) {
        if (p->cart >= 0) {
            W.carts[p->cart].holder = -1;
            p->cart = -1;
        } else if (p->weapon.id) {
            Stack one = p->weapon;
            if (ITEMS[p->weapon.id].stack > 1) {
                one.count = 1;
                p->weapon.count--;
                if (p->weapon.count <= 0) p->weapon.id = IT_NONE;
            } else {
                p->weapon.id = IT_NONE;
            }
            p->hit_pending = false;   /* the weapon left the hand mid-swing: that swing is gone */
            throw_item(p, 0, one, p->face, 430);
            p->atk_cd = 0.25f;
        }
    }
    /* the range pistol is the camp's: hub.c loads it, and it never takes rounds from your bag */
    bool range_gun = W.hub_lock == HUB_AIM;
    if (IN.pressed[ACT_RELOAD] && !range_gun) try_reload(p);
    if (w->kind == WK_GUN && p->weapon.cond <= 0 && p->atk_cd < -0.3f && inv_count(p, w->ammo) > 0 && p->reload_t <= 0 && !range_gun)
        try_reload(p);
    if (IN.pressed[ACT_HEAL]) use_heal(p);

    /* execute */
    if (IN.pressed[ACT_EXECUTE]) {
        int t = execute_target(p);
        if (t >= 0) execute_begin(p, 0, t);
    }

    /* interact (update_prompt in world.c mirrors this order) */
    if (IN.pressed[ACT_INTERACT] && W.hub_lock == HUB_FREE) {
        bool done = W.hub && hub_interact(p);   /* people, the stations and the van at the Greenhouse */
        if (p->cart >= 0) {
            /* behind a cart: search what's in front (loot goes in the cart), otherwise let go -
             * at the van with the list done, E drives home with the cart */
            int ci = container_seen(p->pos, v2_add(p->pos, v2_scale(v2_angle(p->face), 14)), 30, NULL);
            bool at_van = in_exit(p->pos);
            if (!at_van && ci >= 0 && !(W.conts[ci].searched && W.conts[ci].n == 0)) {
                search_ci = ci;
                search_t = 0;
                audio_play(SFX_LOOT_RUMMAGE, 1.0f, 0, frange(0.9f, 1.1f));
                make_noise(p->pos, 60, 0);
                done = true;
            } else if (!at_van || !W.list_done) {
                W.carts[p->cart].holder = -1;
                p->cart = -1;
                done = true;
            }
        }
        if (!done && !W.hub) {
            /* exit - or, the list not done yet, load what you have of it into the van and go back for the rest
               (nothing for the van yet: [E] is for the thing on the ground you're reaching for) */
            if (in_exit(p->pos) && (W.list_done || van_loadable() || interact_pickup(p) < 0)) {
                list_recount();   /* judge what's here now, not a quarter-second-old tally */
                int loaded = W.list_done ? 0 : van_load_bag();
                if (W.list_done) {
                    W.exiting = true;
                    W.exit_t = 0;
                    audio_play(SFX_VAN_DOOR, 1, 0, 1);
                } else if (loaded) {
                    char buf[32];
                    SDL_snprintf(buf, sizeof buf, "INTO THE VAN x%d", loaded);
                    floater(v2(p->pos.x, p->pos.y - 16), buf, COL_WHITE, false);
                    audio_play(SFX_VAN_DOOR, 0.8f, 0, 1.1f);
                    if (!W.van_hinted) { world_hint("Don't leave the van alone too long - people steal."); W.van_hinted = true; }
                } else {
                    world_message("THE LIST ISN'T DONE", COL_RED);
                    audio_play(SFX_UI_ERROR, 0.6f, 0, 1);
                }
                done = true;
            }
        }
        int pk = done ? -1 : interact_pickup(p);
        if (pk >= 0 && ITEMS[W.pickups[pk].st.id].cat != CAT_WEAPON) {
            /* deliberately pick up an item lying here (e.g. one you dropped) - only offered if it fits */
            W.pickups[pk].dropped = false;
            W.pickups[pk].age = 1;
            pickup_collect(p, pk);
            done = true;
        } else if (pk >= 0) {
            pickup_take_weapon(p, pk);
            done = true;
        }
        if (!done) {
            int dist;
            int ci = container_seen(p->pos, v2_add(p->pos, v2_scale(v2_angle(p->face), 6)), 30, &dist);
            if (ci >= 0) {
                Container *c = &W.conts[ci];
                if (c->searched && c->n == 0) {
                    world_hint("Nothing left here.");
                } else {
                    search_ci = ci;
                    search_t = 0;
                    audio_play(SFX_LOOT_RUMMAGE, 0.7f, 0, frange(0.9f, 1.1f));
                    make_noise(p->pos, 60, 0);
                }
                done = true;
            }
        }
        if (!done) {
            int c = cart_near(p->pos, 22);
            if (c >= 0) {
                W.carts[c].holder = 0;
                p->cart = c;
                audio_play(SFX_CART_ROLL, 0.8f, 0, 1);
                done = true;
            }
        }
        if (!done) {
            /* nothing else to do here, but something on the floor that won't fit: say why */
            for (int i = 0; i < MAX_PICKUPS; i++) {
                Pickup *pk = &W.pickups[i];
                if (!in_reach(p, pk) || ITEMS[pk->st.id].cat == CAT_WEAPON) continue;
                audio_play(SFX_UI_ERROR, 0.5f, 0, 1);
                world_hint(ITEMS[pk->st.id].cat == CAT_BAG ? "Your bag is already bigger." : bag_full_hint());
                break;
            }
        }
    }
    /* walk-over pickups */
    for (int i = 0; i < MAX_PICKUPS; i++) {
        Pickup *pk = &W.pickups[i];
        if (!pk->alive || pk->flying || pk->age < 0.35f || pk->dropped) continue;
        if (ITEMS[pk->st.id].cat == CAT_WEAPON) continue;
        if (v2_dist2(pk->pos, p->pos) < 12 * 12 && reach_clear(p->pos, pk->pos)) pickup_collect(p, i);
    }
    W.loot_cd -= dt;
}

/* ------------------------------------------------------------- drawing */
int arch_pose_sprite(Actor *a, int *gx, int *gy) {
    const ArchDef *d = &ARCH[a->arch];
    const WeaponDef *w = item_weapon(a->weapon.id);
    bool boss = a->arch == AR_BOSS;
    if (a->cart >= 0) { *gx = boss ? 7 : 5; *gy = 1; return d->spr_2h; }
    if (!a->weapon.id) {
        *gx = *gy = 0;
        if (a->atk_t >= 0 && a->atk_t < 0.16f) return d->spr_punch + (a->atk_dir & 1);
        if (a->exec_t > 0) return d->spr_punch + ((int)(a->exec_t * 10) & 1);
        return d->spr_idle;
    }
    if (w->kind == WK_GUN || w->kind == WK_CHAINSAW || w->kind == WK_FLAME) {
        *gx = boss ? 7 : 5;
        *gy = 1;
        return d->spr_2h;
    }
    *gx = boss ? 8 : 6;
    *gy = boss ? 6 : 4;
    return d->spr_1h;
}

void actor_draw_shadow(Actor *a) {
    if (!a->used) return;
    if (is_animal(a)) { animal_draw_shadow(a); return; }
    if (is_plant(a)) { plant_draw_shadow(a); return; }
    float s = a->arch == AR_BOSS ? 1.6f : (a->arch == AR_BRUTE ? 1.2f : 1.0f);
    gfx_spr_ex(SPR_FX_SHADOW, a->pos.x + 2, a->pos.y + 3, 0, s, s, TINT_NONE);
}

/* weapon swing pose: returns weapon angle relative to body and body twist */
static void swing_pose(Actor *a, const WeaponDef *w, float *wang, float *twist, float *thrust) {
    *wang = 0.55f;
    *twist = 0;
    *thrust = 0;
    if (w->kind == WK_GUN || w->kind == WK_CHAINSAW || w->kind == WK_FLAME) {
        *wang = 0;
        if (a->atk_t >= 0 && a->atk_t < 0.12f) *thrust = -2.0f * (1.0f - a->atk_t / 0.12f);
        if (w->kind == WK_CHAINSAW) *thrust += sinf(W.time * 60) * 0.6f;
        return;
    }
    if (a->windup > 0) {
        /* NPC telegraph: pull back */
        float k = 1.0f - CLAMP(a->windup / MAXF(0.05f, w->windup), 0.0f, 1.0f);
        float dir = (a->atk_dir & 1) ? -1.0f : 1.0f;
        *wang = lerpf(0.55f, 1.9f * dir, k);
        *twist = 0.35f * dir * k;
        return;
    }
    if (a->atk_t < 0) return;
    float dur = w->style == ST_HEAVY ? 0.2f : 0.13f;
    float t = CLAMP(a->atk_t / dur, 0.0f, 1.0f);
    float e = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
    if (w->style == ST_STAB) {
        *thrust = sinf(t * PI_F) * 7.0f;
        *wang = 0.1f;
        return;
    }
    float dir = (a->atk_dir & 1) ? -1.0f : 1.0f;
    float from = 1.8f * dir, to = -1.5f * dir;
    *wang = lerpf(from, to, e);
    *twist = lerpf(0.45f * dir, -0.45f * dir, e);
    if (a->atk_t > dur) {
        /* recover back to rest */
        float r = CLAMP((a->atk_t - dur) / 0.18f, 0.0f, 1.0f);
        *wang = lerpf(to, 0.55f, r);
        *twist = lerpf(-0.45f * dir, 0, r);
    }
}

void actor_draw(Actor *a) {
    if (!a->used || !a->alive) return;
    if (is_animal(a)) { animal_draw(a); return; }
    if (is_plant(a)) { plant_draw(a); return; }
    const ArchDef *d = &ARCH[a->arch];
    Color tint = TINT_NONE;
    if (a->hurt_t > 0) tint = rgb(255, 90, 90);
    if (a->burn_t > 0 && ((int)(W.time * 20) & 1)) tint = rgb(255, 170, 90);
    if (a->down_t > 0) {
        float shake = a->down_t < 0.6f ? sinf(W.time * 50) * 0.8f : 0;
        gfx_spr_ex(d->spr_down, a->pos.x + shake, a->pos.y, a->face, 1, 1, tint);
        int fr = (int)(W.time * 8) % 3;
        gfx_spr(SPR_UI_STARS + fr, a->pos.x, a->pos.y - 13);
        return;
    }
    /* legs */
    float la = v2_len(a->vel) > 5 ? a->move_angle : a->face;
    int lf = v2_len(a->vel) > 5 ? ((int)(a->leg_anim / 7.0f) % 4) : 1;
    gfx_spr_ex(d->spr_legs + lf, a->pos.x, a->pos.y, la, 1, 1, tint);
    /* torso + weapon */
    int gx, gy;
    int body = arch_pose_sprite(a, &gx, &gy);
    const WeaponDef *w = item_weapon(a->weapon.id);
    float wang = 0, twist = 0, thrust = 0;
    bool show_weapon = a->weapon.id && a->cart < 0;
    if (show_weapon) swing_pose(a, w, &wang, &twist, &thrust);
    float bang = a->face + twist;
    if (a->stun_t > 0) bang += sinf(W.time * 18) * 0.25f;
    V2 fwd = v2_angle(bang);
    V2 bpos = v2_add(a->pos, v2_scale(fwd, thrust * 0.3f));
    gfx_spr_ex(body, bpos.x, bpos.y, bang, 1, 1, tint);
    if (show_weapon) {
        V2 grip = v2_add(bpos, v2_rot(v2((float)gx + thrust, (float)gy), bang));
        int spr = w->spr;
        gfx_spr_ex(spr, grip.x, grip.y, bang + wang, 1, 1, TINT_NONE);
        if (w->kind == WK_THROWN && a->weapon.id == IT_MOLOTOV) {
            V2 tip = v2_add(grip, v2_rot(v2(8, 0), bang + wang));
            int fr = (int)(W.time * 12) % 4;
            gfx_spr_ex(SPR_FX_FIRE + fr, tip.x, tip.y, 0, 0.4f, 0.4f, TINT_NONE);
        }
    }
    /* melee slash trail */
    if (a->weapon.id && w->kind == WK_MELEE && a->atk_t >= 0 && a->atk_t < 0.14f && w->style != ST_STAB) {
        int fr = CLAMP((int)(a->atk_t / 0.14f * 3), 0, 2);
        float dir = (a->atk_dir & 1) ? -1.0f : 1.0f;
        float sc = w->range / 16.0f;
        gfx_spr_ex(SPR_FX_SLASH + fr, a->pos.x, a->pos.y, a->face, sc, sc * dir, rgba(255, 255, 255, 200));
    }
    if (a->weapon.id && w->kind == WK_MELEE && w->style == ST_STAB && a->atk_t >= 0 && a->atk_t < 0.1f) {
        V2 tip = v2_add(a->pos, v2_scale(fwd, 6 + w->range * 0.5f));
        gfx_spr_ex(SPR_FX_STAB + ((int)(a->atk_t * 20) & 1), tip.x, tip.y, bang, w->range / 16.0f, 1, rgba(255, 255, 255, 190));
    }
}
