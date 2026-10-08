/* LAST AISLE - HUD, inventory + crafting, pause */
#include "world.h"
#include "gfx.h"
#include "input.h"
#include "audio.h"
#include "game.h"
#include "hub.h"

static const Color INK = {30, 45, 110, 255};
static const Color INK_FADE = {120, 110, 120, 255};

static void upper(char *s) { for (; *s; s++) if (*s >= 'a' && *s <= 'z') *s -= 32; }

const char *ctl(const char *kb, const char *pad) { return IN.pad_active ? pad : kb; }

void hud_cursor(void) {
    if (IN.pad_active) return;
    V2 m = IN.mouse_view;
    gfx_spr(SPR_UI_CURSOR, floorf(m.x), floorf(m.y));
}

/* ------------------------------------------------------------ the list */
static const Color INK_FAV = {40, 104, 58, 255};   /* favours: written on the list in somebody else's pen */

/* a line of the list: icon, name (cut to fit), have/need or ticked off */
static void list_row(ListEntry *e, float x, float ry, float w, Color ink, bool marked) {
    float fl = e->flash > 0 ? e->flash : 0;
    float ix = x + 5 + (fl > 0 ? sinf(fl * 40) * 1.5f : 0);
    gfx_spr(ITEMS[e->id].spr, ix + 8, ry + 4);
    Color c = e->done ? INK_FADE : ink;
    if (fl > 0) c = color_lerp(c, rgb(200, 30, 60), fl);
    char nm[40];
    SDL_strlcpy(nm, ITEMS[e->id].name, sizeof nm);
    char cnt[16];
    SDL_snprintf(cnt, sizeof cnt, "%d/%d", MINF(e->have, e->need), e->need);
    int maxw = (int)w - 23 - 7 - (e->done ? 14 : gfx_text_w(FONT_SMALL, cnt)) - 4;
    while (gfx_text_w(FONT_SMALL, nm) > maxw && strlen(nm) > 3) {
        size_t l = strlen(nm);
        nm[l - 1] = 0;
        if (nm[l - 2] == ' ') nm[l - 2] = 0;
        l = strlen(nm);
        nm[l - 1] = '.';
    }
    if (marked) gfx_marker(x + 22, ry - 1, gfx_text_w(FONT_SMALL, nm) + 2, 8, rgba(255, 212, 71, 170));
    gfx_text(FONT_SMALL, nm, x + 23, ry, c, 0);
    if (e->done) {
        int tw = gfx_text_w(FONT_SMALL, nm);
        gfx_fill(x + 22, ry + 3, tw + 2, 1, rgba(200, 30, 60, 220));
        gfx_spr(SPR_UI_CHECK, x + w - 17, ry - 1);
    } else {
        gfx_text(FONT_SMALL, cnt, x + w - 7, ry, c, TXT_RIGHT);
    }
}

static void draw_list(float x, float y) {
    float w = 116, rh = 11;
    int haul = van_haul();
    float h = 18 + W.nlist * rh + (W.nfav ? 12 + W.nfav * rh : 0) + (W.nbonus ? 13 : 0) + (haul ? 12 : 0) + 3;
    gfx_nine(SPR_UI_PAPER, x, y, w, h, TINT_NONE);
    gfx_spr(SPR_UI_TAPE, x + w / 2, y + 1);
    gfx_text(FONT_SMALL, "SHOPPING LIST", x + 9, y + 6, INK, 0);
    float ry = y + 17;
    for (int i = 0; i < W.nlist; i++) {
        ListEntry *e = &W.list[i];
        /* the one the radio talked about: highlighted on the list */
        list_row(e, x, ry, w, INK, W.tip_stage > 0 && W.trail != TR_NONE && W.trail_id == e->id && !e->done);
        ry += rh;
    }
    /* what the camp asked for on top: nice to have, hard to get */
    if (W.nfav) {
        char who[48] = "for ";
        for (int i = 0; i < W.nfav; i++) {
            if (i) SDL_strlcat(who, i == W.nfav - 1 ? " & " : ", ", sizeof who);
            SDL_strlcat(who, ARCH[FAVOURS[W.fav_of[i]].arch].name, sizeof who);
        }
        gfx_text(FONT_SMALL, who, x + 9, ry + 1, INK_FAV, 0);
        ry += 12;
        for (int i = 0; i < W.nfav; i++) {
            list_row(&W.fav[i], x, ry, w, INK_FAV, false);
            ry += rh;
        }
    }
    if (W.nbonus) {
        gfx_text(FONT_SMALL, "bonus", x + 9, ry + 3, INK_FADE, 0);
        float bx = x + 42;
        for (int i = 0; i < W.nbonus; i++) {
            ListEntry *e = &W.bonus[i];
            gfx_spr_c(ITEMS[e->id].spr, bx + 8, ry + 6, e->done ? TINT_NONE : rgba(255, 255, 255, 140));
            if (e->done) gfx_spr(SPR_UI_CHECK, bx + 10, ry + 3);
            bx += 18;
        }
        ry += 13;
    }
    /* left at the van: counts, but nobody's watching it (red while somebody's on the way) */
    if (haul) {
        char buf[32];
        SDL_snprintf(buf, sizeof buf, "%d at the van", haul);
        bool thief = W.thief >= 0 && sinf(G.time * 9) > 0;
        gfx_spr_c(SPR_UI_VAN, x + 7, ry - 3, thief ? rgba(255, 90, 100, 255) : TINT_NONE);
        gfx_text(FONT_SMALL, buf, x + 25, ry + 2, thief ? rgba(200, 30, 60, 255) : INK_FADE, 0);
    }
}

static void draw_hearts(float x, float y) {
    Actor *p = player();
    int hp = MAXF(0, p->hp);
    int hearts = (p->maxhp + 1) / 2;
    for (int i = 0; i < hearts; i++) {
        int v = hp - i * 2;
        int fr = v >= 2 ? 0 : (v == 1 ? 1 : 2);
        float bob = (p->hp <= 2 && v > 0) ? sinf(G.time * 9) * 1.0f : 0;
        gfx_spr(SPR_UI_HEART + fr, x + i * 10, y + bob);
    }
}

static void draw_weapon_panel(void) {
    Actor *p = player();
    float x = 4, y = VIEW_H - 32, w = 136, h = 28;
    gfx_nine(SPR_UI_PANEL, x, y, w, h, TINT_NONE);
    if (p->cart >= 0) {
        gfx_spr(SPR_UI_CART, x + 8, y + 8);
        Cart *c = &W.carts[p->cart];
        int used = 0;
        for (int k = 0; k < c->n; k++) used += ITEMS[c->items[k].id].size * c->items[k].count;
        char buf[48];
        SDL_snprintf(buf, sizeof buf, "SHOPPING CART  %d/%d", used, CART_CAP);
        gfx_text(FONT_SMALL, buf, x + 24, y + 6, COL_WHITE, 0);
        gfx_text(FONT_SMALL, ctl("^kLMB shove - E let go", "^kRT shove - A let go"), x + 24, y + 16, COL_GREY, 0);
        return;
    }
    if (W.hub && !p->weapon.id) {   /* at the Greenhouse it stays packed: it's in your hands at the store */
        const Stack *s = hub_packed();
        if (!s->id) {
            gfx_text(FONT_SMALL, "BARE HANDS", x + 8, y + 6, COL_WHITE, 0);
            gfx_text(FONT_SMALL, "^kno weapon packed", x + 8, y + 16, COL_GREY, 0);
            return;
        }
        gfx_spr_c(ITEMS[s->id].spr, x + 14, y + 14, rgba(255, 255, 255, 170));
        char name[48];
        SDL_strlcpy(name, ITEMS[s->id].name, sizeof name);
        upper(name);
        gfx_text(FONT_SMALL, name, x + 26, y + 5, COL_GREY, 0);
        gfx_text(FONT_SMALL, "^kpacked for the run", x + 26, y + 16, COL_GREY, 0);
        return;
    }
    if (!p->weapon.id) {
        gfx_text(FONT_SMALL, "BARE HANDS", x + 8, y + 6, COL_WHITE, 0);
        gfx_text(FONT_SMALL, ctl("^kLMB punch - E pick up", "^kRT punch - A pick up"), x + 8, y + 16, COL_GREY, 0);
        return;
    }
    const ItemDef *it = &ITEMS[p->weapon.id];
    const WeaponDef *wd = item_weapon(p->weapon.id);
    gfx_spr(it->spr, x + 14, y + 14);
    char name[48];
    SDL_strlcpy(name, it->name, sizeof name);
    upper(name);
    int nw = gfx_text(FONT_SMALL, name, x + 26, y + 5, COL_WHITE, 0);
    int nmods = 0;
    for (int m = 0; m < MOD_COUNT; m++) nmods += (p->weapon.mods >> m) & 1;
    if (nmods) {   /* workbench mods on it */
        char mb[8];
        SDL_snprintf(mb, sizeof mb, "+%d", nmods);
        gfx_text(FONT_SMALL, mb, x + 28 + nw, y + 5, COL_YELLOW, 0);
    }
    char buf[48];
    float bx = x + 26, by = y + 17;
    if (wd->kind == WK_GUN) {
        SDL_snprintf(buf, sizeof buf, "%d", p->weapon.cond);
        gfx_text(FONT_BIG, buf, bx, by - 4, p->weapon.cond > 0 ? COL_YELLOW : COL_RED, TXT_SHADOW);
        int reserve = inv_count(p, wd->ammo) * (wd->ammo == IT_NAILS ? 15 : 1);
        SDL_snprintf(buf, sizeof buf, "^k+%d", reserve);
        gfx_text(FONT_SMALL, buf, bx + 22, by, COL_GREY, 0);
        if (p->reload_t > 0) gfx_text(FONT_SMALL, "RELOADING", bx + 50, by, COL_ORANGE, TXT_SHAKE);
    } else if (wd->kind == WK_THROWN) {
        SDL_snprintf(buf, sizeof buf, "x%d  ^k%s throw", p->weapon.count, ctl("LMB", "RT"));
        gfx_text(FONT_SMALL, buf, bx, by, COL_YELLOW, 0);
    } else {
        int maxd = weapon_max_cond(&p->weapon);
        float k = maxd > 0 ? CLAMP((float)p->weapon.cond / maxd, 0.0f, 1.0f) : 1;
        gfx_fill(bx, by + 1, 52, 5, rgba(11, 10, 16, 255));
        Color c = k > 0.5f ? COL_GREEN : (k > 0.2f ? COL_YELLOW : COL_RED);
        if (wd->kind == WK_CHAINSAW || wd->kind == WK_FLAME) c = COL_ORANGE;
        gfx_fill(bx + 1, by + 2, 50 * k, 3, c);
        gfx_text(FONT_SMALL, wd->kind == WK_MELEE ? ctl("^kRMB throw", "^kLT throw") : "^kfuel", bx + 57, by, COL_GREY, 0);
    }
}

/* the weapons you carry, a box a slot over the hands panel: the one in hand lit up, the keys that take each in hand */
static void draw_weapon_slots(void) {
    Actor *p = player();
    float x = 4, y = VIEW_H - 54;
    char buf[8];
    for (int k = 0; k < WSLOTS; k++) {
        bool held = k == p->wslot;
        const Stack *s = held && W.hub ? hub_packed() : weapon_slot(p, k);   /* at the Greenhouse it's packed */
        float bx = x + k * 22;
        gfx_fill(bx, y, 20, 20, held ? rgba(255, 212, 71, 46) : rgba(11, 10, 16, 150));
        gfx_rect(bx, y, 20, 20, held ? COL_YELLOW : rgba(255, 255, 255, 40));
        if (s->id) gfx_spr_c(ITEMS[s->id].spr, bx + 10, y + 10, held && !W.hub ? TINT_NONE : rgba(255, 255, 255, 140));
        if (s->count > 1) {
            SDL_snprintf(buf, sizeof buf, "%d", s->count);
            gfx_text(FONT_SMALL, buf, bx + 19, y + 12, COL_WHITE, TXT_RIGHT | TXT_OUTLINE);
        }
        if (!IN.pad_active) {
            SDL_snprintf(buf, sizeof buf, "%d", k + 1);
            gfx_text(FONT_SMALL, buf, bx + 2, y + 1, held ? COL_YELLOW : COL_GREY, TXT_OUTLINE);
        }
    }
    if (!W.hub) gfx_text(FONT_SMALL, ctl("^kQ / wheel", "^kY / D-PAD"), x + WSLOTS * 22 + 2, y + 7, COL_GREY, TXT_OUTLINE);
}

static void draw_bag_panel(void) {
    Actor *p = player();
    float w = 92, h = 28, x = VIEW_W - w - 4, y = VIEW_H - 32;
    gfx_nine(SPR_UI_PANEL, x, y, w, h, TINT_NONE);
    gfx_spr(SPR_UI_BAG, x + 6, y + 5);
    char buf[32];
    int used = inv_used(p), cap = inv_capacity(p);
    SDL_snprintf(buf, sizeof buf, "%d/%d", used, cap);
    gfx_text(FONT_SMALL, buf, x + 21, y + 7, used >= cap ? COL_RED : COL_WHITE, 0);
    int meds = inv_count(p, IT_BANDAGE) + inv_count(p, IT_PAINKILLERS) + inv_count(p, IT_MEDKIT);
    SDL_snprintf(buf, sizeof buf, "^y%s^0 +%d", ctl("F", "LB"), meds);
    gfx_text(FONT_SMALL, buf, x + 52, y + 7, meds ? COL_GREEN : COL_GREY, 0);
    gfx_text(FONT_SMALL, ctl("^kTAB bag/craft", "^kBACK bag/craft"), x + 6, y + 17, COL_GREY, 0);
}

/* the score is a shelf-edge price label; a running combo is a clearance sticker slapped under it */
static void draw_score(void) {
    char buf[32];
    SDL_snprintf(buf, sizeof buf, "%d", W.score);
    float w = gfx_text_w(FONT_BIG, buf) + 9, x = VIEW_W - 6 - w;
    gfx_sticker(x, 5, w, 15, COL_YELLOW);
    gfx_text(FONT_BIG, buf, x + 5, 7, COL_BLACK, 0);
    gfx_fill(x + 2, 7, 1, 11, rgba(11, 10, 16, 90));
    if (W.kills > 0) {
        char kb[16];
        SDL_snprintf(kb, sizeof kb, "%d", W.kills);
        int kw = gfx_text_w(FONT_SMALL, kb);
        gfx_text(FONT_SMALL, kb, VIEW_W - 7, 23, COL_WHITE, TXT_RIGHT | TXT_OUTLINE);
        gfx_spr(SPR_UI_SKULL, VIEW_W - 21 - kw, 21);
    }
    if (W.combo >= 2 && W.combo_t > 0) {
        SDL_snprintf(buf, sizeof buf, "%dX", W.combo);
        float nw = gfx_text_w(FONT_BIG, buf), lw = gfx_text_w(FONT_SMALL, "COMBO");
        float cw = nw + lw + 13, cx = VIEW_W - 6 - cw;
        float y = 35 - CLAMP((W.combo_t - 3.05f) / 0.15f, 0.0f, 1.0f) * 4;
        gfx_sticker(cx, y, cw, 15, COL_TAG);
        gfx_text(FONT_BIG, buf, cx + 5, y + 2, COL_WHITE, 0);
        gfx_text(FONT_SMALL, "COMBO", cx + nw + 9, y + 4, COL_WHITE, 0);
        gfx_fill(cx + 2, 52, (cw - 4) * CLAMP(W.combo_t / 3.2f, 0.0f, 1.0f), 2, COL_TAG);
    }
}

static void draw_exit_arrow(void) {
    if (!W.list_done) return;
    V2 v = gfx_world_to_view(W.van);
    float m = 22;
    bool off = v.x < m || v.y < m || v.x > VIEW_W - m || v.y > VIEW_H - m;
    float k = 0.5f + 0.5f * sinf(G.time * 6);
    if (!off) {
        gfx_spr(SPR_UI_VAN, v.x, v.y - 30 - k * 3);
        return;
    }
    V2 c = v2(VIEW_W / 2, VIEW_H / 2);
    V2 d = v2_sub(v, c);
    float sx = (VIEW_W / 2 - m) / MAXF(fabsf(d.x), 0.01f), sy = (VIEW_H / 2 - m) / MAXF(fabsf(d.y), 0.01f);
    float s = MINF(sx, sy);
    V2 p = v2_add(c, v2_scale(d, s));
    float ang = v2_to_angle(d);
    gfx_spr_ex(SPR_UI_ARROW, p.x + cosf(ang) * k * 3, p.y + sinf(ang) * k * 3, ang, 1, 1, COL_GREEN);
    gfx_spr(SPR_UI_VAN, p.x - cosf(ang) * 14 - 8, p.y - sinf(ang) * 14 - 8);
}

/* stuck on the list for a long while: point at the missing item */
static void draw_trail_arrow(void) {
    V2 at;
    if (W.list_done || W.tip_stage < 2 || !list_trail(&at)) return;
    V2 v = gfx_world_to_view(at);
    float m = 22;
    bool off = v.x < m || v.y < m || v.x > VIEW_W - m || v.y > VIEW_H - m;
    if (off && W.trail == TR_CARRIED && W.actors[W.trail_i].br.state == AI_GETAWAY) return;   /* draw_thief_arrows has them */
    float k = 0.5f + 0.5f * sinf(G.time * 6);
    if (!off) {
        float lift = W.trail == TR_CARRIED ? 34 : 14;
        gfx_spr_ex(SPR_UI_ARROW, v.x, v.y - lift - k * 3, PI_F / 2, 1, 1, COL_YELLOW);
        return;
    }
    V2 c = v2(VIEW_W / 2, VIEW_H / 2);
    V2 d = v2_sub(v, c);
    float sx = (VIEW_W / 2 - m) / MAXF(fabsf(d.x), 0.01f), sy = (VIEW_H / 2 - m) / MAXF(fabsf(d.y), 0.01f);
    float s = MINF(sx, sy);
    V2 p = v2_add(c, v2_scale(d, s));
    float ang = v2_to_angle(d);
    gfx_spr_ex(SPR_UI_ARROW, p.x + cosf(ang) * k * 3, p.y + sinf(ang) * k * 3, ang, 1, 1, COL_YELLOW);
    gfx_spr(ITEMS[W.trail_id].spr, p.x - cosf(ang) * 14, p.y - sinf(ang) * 14);
}

/* a thief running round with your shopping: a red arrow at the edge of the screen while they're out of sight */
static void draw_thief_arrows(void) {
    for (int i = 1; i < W.nactors; i++) {
        Actor *a = &W.actors[i];
        if (!a->used || !a->alive || a->br.state != AI_GETAWAY) continue;
        ItemId what = IT_NONE;
        for (int k = 0; k < a->ninv && !what; k++) if (item_needed((ItemId)a->inv[k].id)) what = (ItemId)a->inv[k].id;
        if (!what) continue;
        V2 v = gfx_world_to_view(a->pos);
        float m = 22;
        if (!(v.x < m || v.y < m || v.x > VIEW_W - m || v.y > VIEW_H - m)) continue;   /* on screen: the item over their head */
        float k = 0.5f + 0.5f * sinf(G.time * 6);
        V2 c = v2(VIEW_W / 2, VIEW_H / 2);
        V2 d = v2_sub(v, c);
        float sx = (VIEW_W / 2 - m) / MAXF(fabsf(d.x), 0.01f), sy = (VIEW_H / 2 - m) / MAXF(fabsf(d.y), 0.01f);
        V2 p = v2_add(c, v2_scale(d, MINF(sx, sy)));
        float ang = v2_to_angle(d);
        gfx_spr_ex(SPR_UI_ARROW, p.x + cosf(ang) * k * 3, p.y + sinf(ang) * k * 3, ang, 1, 1, COL_RED);
        gfx_spr(ITEMS[what].spr, p.x - cosf(ang) * 14, p.y - sinf(ang) * 14);
    }
}

/* the crew you brought: a name and their health each, under the score - struck through when they're gone */
static void draw_crew(void) {
    float y = 58;
    for (int k = 0; k < MAX_CREW; k++) {
        if (RUN.snap_crew[k] != CR_SQUAD) continue;   /* came along to this store */
        int i = W.crew_actor[k];
        Actor *a = i >= 0 ? &W.actors[i] : NULL;
        bool alive = a && a->used && a->alive && is_crew(a);
        float x = VIEW_W - 6;
        if (alive) {
            Color on = a->hurt_t > 0 ? COL_RED : (a->down_t > 0 ? COL_ORANGE : COL_GREEN);
            for (int h = a->maxhp - 1; h >= 0; h--) {
                x -= 4;
                gfx_fill(x - 1, y + 1, 5, 6, rgba(11, 10, 16, 160));
                gfx_fill(x, y + 2, 3, 4, h < a->hp ? on : rgba(70, 62, 76, 255));
            }
        } else {
            x -= gfx_text_w(FONT_SMALL, "DEAD");
            gfx_text(FONT_SMALL, "DEAD", x, y, COL_RED, TXT_OUTLINE);
        }
        x -= 5;
        const char *name = ARCH[CREW[k].arch].name;
        gfx_text(FONT_SMALL, name, x, y, alive ? COL_RECEIPT : COL_GREY, TXT_RIGHT | TXT_OUTLINE);
        if (!alive) gfx_fill(x - gfx_text_w(FONT_SMALL, name) - 1, y + 3, gfx_text_w(FONT_SMALL, name) + 2, 1, rgba(200, 30, 60, 230));
        y += 10;
    }
}

/* your crew out of sight, off on their own: a small arrow at the edge of the screen and their name (red, blinking, when
   they're badly hurt and on their way back) */
static void draw_crew_arrows(void) {
    V2 used[MAX_CREW];   /* names already written: the next one the same way goes further in */
    int nused = 0;
    for (int k = 0; k < MAX_CREW; k++) {
        int i = W.crew_actor[k];
        if (i < 0) continue;
        Actor *a = &W.actors[i];
        if (!a->used || !a->alive || !is_crew(a)) continue;
        V2 v = gfx_world_to_view(a->pos);
        float m = 16;
        if (!(v.x < m || v.y < m || v.x > VIEW_W - m || v.y > VIEW_H - m)) continue;   /* on screen: the tick over their head */
        bool hurt = crew_badly_hurt(a);
        if (hurt && sinf(G.time * 9) < -0.3f) continue;
        V2 c = v2(VIEW_W / 2, VIEW_H / 2);
        V2 d = v2_sub(v, c);
        float sx = (VIEW_W / 2 - m) / MAXF(fabsf(d.x), 0.01f), sy = (VIEW_H / 2 - m) / MAXF(fabsf(d.y), 0.01f);
        V2 p = v2_add(c, v2_scale(d, MINF(sx, sy)));
        float ang = v2_to_angle(d);
        Color col = hurt ? COL_RED : COL_GREEN;
        col.a = 200;
        gfx_spr_ex(SPR_UI_ARROW, p.x, p.y, ang, 0.7f, 0.7f, col);
        V2 t = v2(p.x - cosf(ang) * 12, p.y - sinf(ang) * 12 - 4);
        for (int tries = 0; tries < MAX_CREW; tries++) {
            bool clash = false;
            for (int u = 0; u < nused; u++) if (fabsf(used[u].x - t.x) < 34 && fabsf(used[u].y - t.y) < 10) clash = true;
            if (!clash) break;
            t = v2_add(t, v2_scale(v2_norm(v2_sub(c, t)), 11));
        }
        used[nused++] = t;
        gfx_text(FONT_SMALL, ARCH[a->arch].name, t.x, t.y, hurt ? COL_RED : COL_RECEIPT, TXT_CENTER | TXT_OUTLINE);
    }
}

static void draw_boss_bar(void) {
    if (W.boss < 0) return;
    Actor *b = &W.actors[W.boss];
    if (!b->used || (b->br.state == AI_GUARD && b->alive)) return;
    W.boss_bar = lerpf(W.boss_bar, b->alive ? (float)b->hp / b->maxhp : 0, 0.1f);
    if (!b->alive && W.boss_bar < 0.01f) return;
    float w = 200, x = VIEW_W / 2 - w / 2, y = 20;
    gfx_text(FONT_SMALL, "THE MALL KING", VIEW_W / 2, y - 11, COL_YELLOW, TXT_CENTER | TXT_OUTLINE);
    gfx_fill(x - 1, y - 1, w + 2, 7, rgba(11, 10, 16, 230));
    gfx_fill(x, y, w * W.boss_bar, 5, COL_RED);
    gfx_fill(x, y, w * W.boss_bar, 1, rgba(255, 120, 140, 255));
}

void level_intro_draw(void) {
    float t = W.intro_t;
    if (t > 3.6f) return;
    float in = CLAMP(t / 0.35f, 0.0f, 1.0f), out = CLAMP((3.6f - t) / 0.5f, 0.0f, 1.0f);
    float a = MINF(in, out);
    float slide = (1.0f - in) * -200 + (1.0f - out) * 200;
    char buf[64];
    SDL_snprintf(buf, sizeof buf, "LEVEL %d", W.level + 1);
    Color c = COL_WHITE;
    c.a = (Uint8)(255 * a);
    /* a shelf edge sliding across the screen: dark strip, yellow label rail top and bottom */
    Color rail = COL_YELLOW;
    rail.a = c.a;
    gfx_fill(0, VIEW_H / 2 - 30, VIEW_W, 58, rgba(11, 10, 16, (Uint8)(190 * a)));
    gfx_fill(0, VIEW_H / 2 - 32, VIEW_W, 2, rail);
    gfx_fill(0, VIEW_H / 2 + 28, VIEW_W, 2, rail);
    float lw = gfx_text_w(FONT_SMALL, buf) + 8;
    Color tag = COL_YELLOW, ink = COL_BLACK;
    tag.a = ink.a = c.a;
    gfx_sticker(VIEW_W / 2 + slide * 0.5f - lw / 2, VIEW_H / 2 - 27, lw, 11, tag);
    gfx_text(FONT_SMALL, buf, VIEW_W / 2 + slide * 0.5f - lw / 2 + 4, VIEW_H / 2 - 25, ink, 0);
    gfx_text_big(FONT_BIG, W.def->name, VIEW_W / 2 + slide, VIEW_H / 2 - 13, 2, c, TXT_CENTER | TXT_SHADOW);
    Color tc = COL_KRAFT;
    tc.a = c.a;
    gfx_text(FONT_SMALL, W.def->tagline, VIEW_W / 2 - slide * 0.5f, VIEW_H / 2 + 14, tc, TXT_CENTER);
}

/* death: the shop-door sign drops on its string and swings to a stop */
static void draw_closed_sign(float t) {
    float a = CLAMP(t * 2, 0.0f, 1.0f);
    gfx_fill(0, 0, VIEW_W, VIEW_H, rgba(34, 8, 4, (Uint8)(130 * a)));
    float drop = -70 * expf(-t * 6) * cosf(t * 13);
    float w = 156, h = 58, x = VIEW_W / 2 - w / 2, y = floorf(VIEW_H / 2 - 52 + drop);
    Color cord = rgb(196, 170, 130);
    gfx_line(VIEW_W / 2, y - 22, x + 20, y + 2, cord);
    gfx_line(VIEW_W / 2, y - 22, x + w - 20, y + 2, cord);
    gfx_fill(VIEW_W / 2 - 1, y - 24, 3, 3, rgb(150, 140, 130));
    gfx_sticker(x, y, w, h, COL_TAG);
    gfx_rect(x + 3, y + 3, w - 6, h - 6, COL_WHITE);
    gfx_fill(x + 19, y + 1, 3, 3, COL_BLACK);
    gfx_fill(x + w - 22, y + 1, 3, 3, COL_BLACK);
    gfx_text(FONT_SMALL, "SORRY, WE'RE", VIEW_W / 2, y + 9, COL_WHITE, TXT_CENTER);
    gfx_text_big(FONT_BIG, "CLOSED", VIEW_W / 2, y + 22, 2, COL_WHITE, TXT_CENTER);
    if (t > 0.8f)
        gfx_text(FONT_SMALL,
                 RUN.mode == MODE_STORY ? ctl("^yR^0 try again   ^yENTER^0 menu", "^yRB^0 try again   ^yA^0 menu")
                                        : ctl("^yENTER^0 to face it", "^yA^0 to face it"),
                 VIEW_W / 2, y + h + 12, COL_WHITE, TXT_CENTER | TXT_OUTLINE);
}

void hud_draw_gear(void) {
    draw_hearts(6, 5);
    draw_weapon_slots();
    draw_weapon_panel();
    draw_bag_panel();
    if (W.msg_t > 0) {
        Color c = W.msg_col;
        c.a = (Uint8)(255 * CLAMP(W.msg_t * 2, 0.0f, 1.0f));
        gfx_text(FONT_BIG, W.msg, VIEW_W / 2, 58, c, TXT_CENTER | TXT_SHADOW | TXT_OUTLINE);
    }
    if (W.hint_t > 0 && SET.hints) {
        Color c = COL_WHITE;
        c.a = (Uint8)(255 * CLAMP(W.hint_t * 2, 0.0f, 1.0f));
        gfx_text(FONT_SMALL, W.hint, VIEW_W / 2, VIEW_H - 46, c, TXT_CENTER | TXT_OUTLINE);
    }
}

void hud_draw(void) {
    Actor *p = player();
    draw_hearts(6, 5);
    draw_list(3, 15);
    draw_score();
    draw_crew();
    draw_weapon_slots();
    draw_weapon_panel();
    draw_bag_panel();
    draw_exit_arrow();
    draw_trail_arrow();
    draw_thief_arrows();
    draw_crew_arrows();
    draw_boss_bar();
    if (W.msg_t > 0) {
        Color c = W.msg_col;
        c.a = (Uint8)(255 * CLAMP(W.msg_t * 2, 0.0f, 1.0f));
        gfx_text(FONT_BIG, W.msg, VIEW_W / 2, 58, c, TXT_CENTER | TXT_SHADOW | TXT_OUTLINE);
    }
    if (W.hint_t > 0 && SET.hints) {
        Color c = COL_WHITE;
        c.a = (Uint8)(255 * CLAMP(W.hint_t * 2, 0.0f, 1.0f));
        gfx_text(FONT_SMALL, W.hint, VIEW_W / 2, VIEW_H - 46, c, TXT_CENTER | TXT_OUTLINE);
    }
    if (W.radio_t > 0) {
        /* top of the screen, clear of the prompts round the player (under the boss bar when it's up) */
        Color c = COL_RECEIPT;
        c.a = (Uint8)(255 * CLAMP(W.radio_t * 2, 0.0f, 1.0f));
        char buf[112];
        SDL_snprintf(buf, sizeof buf, "^yRADIO^0  %s", W.radio);
        gfx_text(FONT_SMALL, buf, VIEW_W / 2, W.boss >= 0 && W.boss_bar > 0.01f ? 30 : 5, c, TXT_CENTER | TXT_OUTLINE);
    }
    /* first-level help */
    if (W.level == 0 && W.time < 22 && SET.hints && W.intro_t > 3.6f) {
        Color c = COL_WHITE;
        c.a = (Uint8)(255 * CLAMP((22 - W.time) / 2, 0.0f, 1.0f));
        gfx_text(FONT_SMALL,
                 ctl("^yWASD^0 move  ^yMOUSE^0 aim  ^yLMB^0 attack  ^yRMB^0 throw  ^yE^0 search/take  ^ySHIFT^0 look",
                     "^yL STICK^0 move  ^yR STICK^0 aim  ^yRT^0 attack  ^yLT^0 throw  ^yA^0 search/take  ^yL3^0 look"),
                 VIEW_W / 2, VIEW_H - 58, c, TXT_CENTER | TXT_OUTLINE);
    }
    if (W.intro_t < 3.6f) level_intro_draw();
    if (W.player_dead && W.dead_t > 0.8f) draw_closed_sign(W.dead_t - 0.8f);
    if (W.exiting) {
        float a = CLAMP(W.exit_t / 1.4f, 0.0f, 1.0f);
        gfx_fill(0, 0, VIEW_W, VIEW_H, rgba(11, 10, 16, (Uint8)(255 * a)));
    }
    (void)p;
    hud_cursor();
}

/* ======================================================= inventory screen */
typedef enum { FOC_HANDS, FOC_BAG, FOC_STORE, FOC_CRAFT } Focus;
static Focus foc = FOC_BAG;
static int sel_bag, sel_store, sel_craft, sel_hand;   /* sel_hand: a weapon slot */
static char inv_msg[64];
static float inv_msg_t;

/* the second panel beside the bag: a cart standing by, the van when you're at it, or your locker at the Greenhouse */
typedef struct {
    Stack *items;     /* NULL: nothing beside the bag */
    int *n;
    int cap, slots;   /* space, stacks */
    const char *name;
    V2 pos;           /* where things dropped out of it land */
} Store;
static Store st;

static void store_find(void) {
    Actor *p = player();
    memset(&st, 0, sizeof st);
    if (W.hub && hub_near_locker()) {
        st = (Store){RUN.locker, &RUN.nlocker, 48, ARRAY_LEN(RUN.locker), "LOCKER", p->pos};
        return;
    }
    if (!W.hub && p->cart < 0 && in_exit(p->pos)) {
        st = (Store){W.van_load, &W.nvan, VAN_CAP, ARRAY_LEN(W.van_load), "VAN", p->pos};
        return;
    }
    int c = p->cart >= 0 ? p->cart : cart_near(p->pos, 40);
    if (c >= 0) st = (Store){W.carts[c].items, &W.carts[c].n, CART_CAP, ARRAY_LEN(W.carts[c].items), "CART", W.carts[c].pos};
}

static int store_used(void) {
    int u = 0;
    for (int k = 0; k < *st.n; k++) u += ITEMS[st.items[k].id].size * st.items[k].count;
    return u;
}

static bool store_put(Stack s) {
    if (store_used() + ITEMS[s.id].size * s.count > st.cap) return false;
    if (ITEMS[s.id].stack > 1)
        for (int k = 0; k < *st.n; k++)
            if (st.items[k].id == s.id) { st.items[k].count += s.count; return true; }
    if (*st.n >= st.slots) return false;
    st.items[(*st.n)++] = s;
    return true;
}

static void store_remove(int k) {
    memmove(&st.items[k], &st.items[k + 1], sizeof(Stack) * (*st.n - k - 1));
    (*st.n)--;
}

static void inv_note(const char *s) { SDL_strlcpy(inv_msg, s, sizeof inv_msg); inv_msg_t = 2; }

static void store_full(void) {
    char buf[48];
    SDL_snprintf(buf, sizeof buf, "The %s is full.", !strcmp(st.name, "CART") ? "cart" : !strcmp(st.name, "VAN") ? "van" : "locker");
    inv_note(buf);
    audio_play(SFX_UI_ERROR, 0.5f, 0, 1);
}

static void bag_take(int k) {
    Actor *p = player();
    memmove(&p->inv[k], &p->inv[k + 1], sizeof(Stack) * (p->ninv - k - 1));
    p->ninv--;
}

static void bag_primary(int k) {
    Actor *p = player();
    if (k < 0 || k >= p->ninv) return;
    Stack s = p->inv[k];
    const ItemDef *it = &ITEMS[s.id];
    if (it->cat == CAT_WEAPON) {
        /* in hand: a free slot takes it, with every slot full the one in hand goes in the bag instead */
        bag_take(k);
        Stack old = weapon_take(p, &s);
        if (s.count > 0 && !inv_add(p, s)) pickup_spawn(s, p->pos, v2(0, 0));   /* the rest of a stack that topped yours up */
        if (old.id && !inv_add(p, old)) pickup_spawn(old, p->pos, v2(0, 0));
        audio_play(SFX_PICKUP_WEAPON, 0.7f, 0, 1);
        return;
    }
    if (it->cat == CAT_MED) {
        if (p->hp >= p->maxhp) { inv_note("Already at full health."); return; }
        inv_remove(p, (ItemId)s.id, 1);
        p->hp = MINF(p->maxhp, p->hp + it->heal);
        audio_play(SFX_HEAL, 0.8f, 0, 1);
        return;
    }
    if (st.items) {
        if (!store_put(s)) { store_full(); return; }
        bag_take(k);
        audio_play(SFX_CART_ROLL, 0.6f, 0, 1.2f);
        return;
    }
    inv_note(it->desc);
}

/* [Q]: anything - weapons and meds too - over to the cart / van / locker */
static void bag_to_store(int k) {
    Actor *p = player();
    if (!st.items || k < 0 || k >= p->ninv) return;
    if (!store_put(p->inv[k])) { store_full(); return; }
    bag_take(k);
    audio_play(SFX_CART_ROLL, 0.6f, 0, 1.2f);
}

static void bag_drop(int k) {
    Actor *p = player();
    if (k < 0 || k >= p->ninv) return;
    Stack s = p->inv[k];
    bag_take(k);
    int pi = pickup_spawn(s, p->pos, v2(frange(-30, 30), frange(10, 40)));
    if (pi >= 0) W.pickups[pi].dropped = true;
    audio_play(SFX_UI_BACK, 0.85f, 0, 1);
}

static void store_primary(int k) {
    if (!st.items || k < 0 || k >= *st.n) return;
    Actor *p = player();
    if (ITEMS[st.items[k].id].cat == CAT_WEAPON && weapon_free_slot(p) >= 0) {   /* a free slot: in hand; otherwise the bag */
        Stack s = st.items[k];
        store_remove(k);
        weapon_take(p, &s);
        if (s.count > 0 && !store_put(s) && !inv_add(p, s)) pickup_spawn(s, st.pos, v2(0, 0));
        audio_play(SFX_PICKUP_WEAPON, 0.7f, 0, 1);
        return;
    }
    if (!inv_add(p, st.items[k])) { inv_note("No room in the bag."); audio_play(SFX_UI_ERROR, 0.5f, 0, 1); return; }
    store_remove(k);
    audio_play(SFX_PICKUP, 0.9f, 0, 1);
}

void inventory_update(float dt) {
    Actor *p = player();
    inv_msg_t -= dt;
    store_find();
    if (!st.items && foc == FOC_STORE) foc = FOC_BAG;
    if (IN.pressed[ACT_INVENTORY] || IN.pressed[ACT_BACK]) {
        g_inventory_open = false;
        audio_play(SFX_UI_BACK, 0.85f, 0, 1);
        list_recount();
        return;
    }
    /* keyboard navigation */
    if (IN.repeat[ACT_MENU_LEFT] || IN.repeat[ACT_MENU_RIGHT]) {
        int dir = IN.repeat[ACT_MENU_RIGHT] ? 1 : -1;
        if (foc == FOC_BAG) {
            if ((sel_bag % 4 == 3 && dir > 0)) foc = FOC_CRAFT;
            else sel_bag = CLAMP(sel_bag + dir, 0, 23);
        } else if (foc == FOC_CRAFT && dir < 0) foc = FOC_BAG;
        else if (foc == FOC_STORE) sel_store = CLAMP(sel_store + dir, 0, st.slots - 1);
        else if (foc == FOC_HANDS) sel_hand = CLAMP(sel_hand + dir, 0, WSLOTS - 1);
        audio_play(SFX_UI_MOVE, 0.75f, 0, 1);
    }
    if (IN.repeat[ACT_MENU_UP] || IN.repeat[ACT_MENU_DOWN]) {
        int dir = IN.repeat[ACT_MENU_DOWN] ? 1 : -1;
        if (foc == FOC_CRAFT) sel_craft = (sel_craft + dir + NUM_RECIPES) % NUM_RECIPES;
        else if (foc == FOC_BAG) {
            if (dir < 0 && sel_bag < 4) { foc = FOC_HANDS; sel_hand = MINF(sel_bag, WSLOTS - 1); }
            else if (dir > 0 && sel_bag + 4 > 23 && st.items) foc = FOC_STORE;
            else sel_bag = CLAMP(sel_bag + dir * 4, 0, 23);
        } else if (foc == FOC_HANDS && dir > 0) { foc = FOC_BAG; sel_bag = sel_hand; }
        else if (foc == FOC_STORE && dir < 0) foc = FOC_BAG;
        audio_play(SFX_UI_MOVE, 0.75f, 0, 1);
    }
    bool act = IN.pressed[ACT_CONFIRM] || IN.pressed[ACT_INTERACT];
    bool drop = IN.pressed[ACT_RELOAD];
    bool move = IN.pressed[ACT_SWAP] && st.items;
    /* mouse */
    V2 m = IN.mouse_view;
    if (IN.mouse_moved || IN.click || IN.rclick) {
        float bx = 24, by = 66;
        bool hit = false;   /* clicks only count on a slot or a recipe */
        for (int k = 0; k < 24; k++) {
            float x = bx + (k % 4) * 26, y = by + (k / 4) * 26;
            if (m.x >= x && m.x < x + 24 && m.y >= y && m.y < y + 24) { foc = FOC_BAG; sel_bag = k; hit = true; }
        }
        for (int k = 0; k < WSLOTS; k++)
            if (m.x >= 24 + k * 26 && m.x < 48 + k * 26 && m.y >= 30 && m.y < 54) { foc = FOC_HANDS; sel_hand = k; hit = true; }
        if (st.items)
            for (int k = 0; k < st.slots; k++) {
                float x = 136 + (k % 4) * 26, y = by + (k / 4) * 26;
                if (m.x >= x && m.x < x + 24 && m.y >= y && m.y < y + 24) { foc = FOC_STORE; sel_store = k; hit = true; }
            }
        for (int r = 0; r < NUM_RECIPES; r++) {
            float y = 34 + r * 14;
            if (m.x >= 252 && m.x < 470 && m.y >= y - 1 && m.y < y + 13) { foc = FOC_CRAFT; sel_craft = r; hit = true; }
        }
        if (IN.click && hit) act = true;
        if (IN.rclick && hit) drop = true;
    }
    /* 1-3: that slot in hand (at the Greenhouse this is where you choose the one you'll have in hand at the store) */
    for (int k = 0; k < WSLOTS; k++) if (IN.pressed[ACT_SLOT1 + k]) weapon_select(p, k);
    Stack *hand = weapon_slot(p, sel_hand);
    if (move) {
        if (foc == FOC_BAG) bag_to_store(sel_bag);
        else if (foc == FOC_STORE) store_primary(sel_store);
        else if (foc == FOC_HANDS && hand->id) {
            if (store_put(*hand)) { weapon_slot_remove(p, sel_hand); audio_play(SFX_PICKUP_WEAPON, 0.6f, 0, 0.9f); }
            else store_full();
        }
        list_recount();
    }
    if (act) {
        switch (foc) {
        case FOC_HANDS:   /* one on your back: in hand. The one in hand: into the bag */
            if (sel_hand != p->wslot) weapon_select(p, sel_hand);
            else if (hand->id) {
                if (inv_add(p, *hand)) { weapon_slot_remove(p, sel_hand); audio_play(SFX_PICKUP_WEAPON, 0.6f, 0, 0.9f); }
                else inv_note("No room in the bag.");
            }
            break;
        case FOC_BAG: bag_primary(sel_bag); break;
        case FOC_STORE: store_primary(sel_store); break;
        case FOC_CRAFT: {
            const Recipe *r = &RECIPES[sel_craft];
            if (craft(p, r)) {
                char buf[64];
                SDL_snprintf(buf, sizeof buf, "Crafted: %s", r->flag == RF_REPAIR ? "repaired weapon" : ITEMS[r->out].name);
                inv_note(buf);
            } else {
                audio_play(SFX_UI_ERROR, 0.5f, 0, 1);
                inv_note(craft_list_blocked(p, r) ? "Those are on your shopping list." : "Missing ingredients.");
            }
            break;
        }
        }
        list_recount();
    }
    if (drop) {
        if (foc == FOC_BAG) bag_drop(sel_bag);
        else if (foc == FOC_HANDS && hand->id) {
            pickup_spawn(weapon_slot_remove(p, sel_hand), p->pos, v2(frange(-30, 30), frange(-30, 30)));
            audio_play(SFX_UI_BACK, 0.85f, 0, 1);
        }
        else if (foc == FOC_STORE && st.items && sel_store < *st.n) {
            int pi = pickup_spawn(st.items[sel_store], st.pos, v2(frange(-30, 30), frange(-30, 30)));
            if (pi >= 0 && W.hub) W.pickups[pi].dropped = true;
            store_remove(sel_store);
        }
        list_recount();
    }
}

static void slot(float x, float y, Stack *s, bool sel, bool list_item) {
    gfx_fill(x, y, 24, 24, sel ? rgba(255, 212, 71, 60) : rgba(255, 255, 255, 18));
    gfx_rect(x, y, 24, 24, sel ? COL_YELLOW : rgba(255, 255, 255, 40));
    if (!s || !s->id) return;
    if (list_item) gfx_glow(x + 12, y + 12, 14, COL_YELLOW, 0.35f);
    gfx_spr(ITEMS[s->id].spr, x + 12, y + 12);
    char buf[8];
    if (s->count > 1) {
        SDL_snprintf(buf, sizeof buf, "%d", s->count);
        gfx_text(FONT_SMALL, buf, x + 23, y + 15, COL_WHITE, TXT_RIGHT | TXT_OUTLINE);
    }
    if (s->mods) gfx_fill(x + 2, y + 2, 3, 3, COL_YELLOW);   /* a workbench job on it */
}

static bool on_list(ItemId id) {
    for (int i = 0; i < W.nlist; i++) if (W.list[i].id == id) return true;
    return false;
}

/* an item's description, with the workbench mods on a weapon listed after it */
static void stack_desc(const Stack *s, char *buf, int n) {
    SDL_strlcpy(buf, ITEMS[s->id].desc, n);
    if (!s->mods) return;
    SDL_strlcat(buf, "  ^yMODS:^0", n);
    for (int m = 0; m < MOD_COUNT; m++)
        if ((s->mods >> m) & 1) { SDL_strlcat(buf, " ", n); SDL_strlcat(buf, MODS[m].name, n); }
}

void inventory_draw(void) {
    Actor *p = player();
    if (!st.items) store_find();
    gfx_fill(0, 0, VIEW_W, VIEW_H, rgba(11, 10, 16, 228));
    gfx_text(FONT_BIG, "BAG", 24, 10, COL_YELLOW, TXT_SHADOW);
    char buf[160];
    SDL_snprintf(buf, sizeof buf, "%s  %d/%d", ITEMS[RUN.bag].name, inv_used(p), inv_capacity(p));
    gfx_text(FONT_SMALL, buf, 60, 14, COL_WHITE, 0);
    /* the weapons you carry */
    gfx_text(FONT_SMALL, "WEAPONS", 24 + WSLOTS * 26, 38, COL_GREY, 0);
    for (int k = 0; k < WSLOTS; k++) {
        float x = 24 + k * 26;
        slot(x, 30, weapon_slot(p, k), foc == FOC_HANDS && sel_hand == k, false);
        if (!IN.pad_active) {
            SDL_snprintf(buf, sizeof buf, "%d", k + 1);
            gfx_text(FONT_SMALL, buf, x + 22, 31, k == p->wslot ? COL_YELLOW : COL_GREY, TXT_RIGHT | TXT_OUTLINE);
        }
        if (k == p->wslot) gfx_text(FONT_SMALL, "IN HAND", x + 12, 56, COL_YELLOW, TXT_CENTER);
    }
    float bx = 24, by = 66;
    for (int k = 0; k < 24; k++) {
        Stack *s = k < p->ninv ? &p->inv[k] : NULL;
        slot(bx + (k % 4) * 26, by + (k / 4) * 26, s, foc == FOC_BAG && sel_bag == k, s && on_list((ItemId)s->id));
    }
    if (st.items) {
        SDL_snprintf(buf, sizeof buf, "%s %d/%d", st.name, store_used(), st.cap);
        gfx_text(FONT_SMALL, buf, 136, 56, COL_KRAFT, 0);
        for (int k = 0; k < st.slots; k++) {
            Stack *s = k < *st.n ? &st.items[k] : NULL;
            slot(136 + (k % 4) * 26, by + (k / 4) * 26, s, foc == FOC_STORE && sel_store == k, s && on_list((ItemId)s->id));
        }
    }
    /* crafting */
    gfx_text(FONT_BIG, "CRAFT", 252, 10, COL_KRAFT, TXT_SHADOW);
    for (int r = 0; r < NUM_RECIPES; r++) {
        const Recipe *rc = &RECIPES[r];
        float y = 34 + r * 14;
        bool ok = can_craft(p, rc);
        bool sel = foc == FOC_CRAFT && sel_craft == r;
        if (sel) gfx_fill(250, y - 1, 222, 14, rgba(255, 212, 71, 46));
        Color c = ok ? COL_WHITE : rgba(110, 104, 120, 255);
        const char *name = rc->flag == RF_REPAIR ? "Tape up weapon" : (rc->flag == RF_REFUEL ? "Refuel chainsaw" : ITEMS[rc->out].name);
        if (rc->out_n > 1) SDL_snprintf(buf, sizeof buf, "%s x%d", name, rc->out_n);
        else SDL_snprintf(buf, sizeof buf, "%s", name);
        if (rc->out) gfx_spr_c(ITEMS[rc->out].spr, 260, y + 6, ok ? TINT_NONE : rgba(120, 120, 120, 160));
        else gfx_spr_c(SPR_I_DUCTTAPE, 260, y + 6, ok ? TINT_NONE : rgba(120, 120, 120, 160));
        gfx_text(FONT_SMALL, buf, 272, y + 2, c, 0);
        float ix = 470;
        for (int k = 2; k >= 0; k--) {
            if (!rc->in[k].id) continue;
            ItemId id = rc->in[k].id;
            int have = craft_have(p, id);
            bool enough = have >= rc->in[k].n;
            if (rc->in[k].n > 1) {
                SDL_snprintf(buf, sizeof buf, "%d", rc->in[k].n);
                gfx_text(FONT_SMALL, buf, ix, y + 2, enough ? COL_GREEN : COL_RED, TXT_RIGHT);
                ix -= 6;
            }
            gfx_spr_c(ITEMS[id].spr, ix - 8, y + 6, enough ? TINT_NONE : rgba(130, 90, 100, 150));
            ix -= 19;
        }
    }
    /* description */
    const Stack *ds = NULL;
    const char *desc = NULL;
    const char *title = NULL;
    if (foc == FOC_BAG && sel_bag < p->ninv) ds = &p->inv[sel_bag];
    if (foc == FOC_HANDS && weapon_slot(p, sel_hand)->id) ds = weapon_slot(p, sel_hand);
    if (foc == FOC_STORE && st.items && sel_store < *st.n) ds = &st.items[sel_store];
    if (ds) {
        title = ITEMS[ds->id].name;
        stack_desc(ds, buf, sizeof buf);
        desc = buf;
    }
    if (foc == FOC_CRAFT) { title = "Recipe"; desc = RECIPES[sel_craft].hint; }
    gfx_nine(SPR_UI_PANEL, 18, 232, 444, 34, TINT_NONE);
    if (title) gfx_text(FONT_SMALL, title, 26, 237, COL_YELLOW, 0);
    if (desc) gfx_text_wrap(FONT_SMALL, desc, 26, 247, 420, COL_WHITE, 0, 9);
    const char *help = ctl("^yLMB/E^0 use/equip/move  ^yRMB/R^0 drop  ^yTAB^0 close", "^yA^0 use/equip/move  ^yRB^0 drop  ^yB^0 close");
    if (st.items && !strcmp(st.name, "LOCKER"))
        help = ctl("^yLMB/E^0 use/equip  ^yQ^0 bag/locker  ^yRMB/R^0 drop  ^yTAB^0 close", "^yA^0 use/equip  ^yY^0 bag/locker  ^yRB^0 drop  ^yB^0 close");
    if (st.items && !strcmp(st.name, "VAN"))
        help = ctl("^yLMB/E^0 use/equip  ^yQ^0 bag/van  ^yRMB/R^0 drop  ^yTAB^0 close", "^yA^0 use/equip  ^yY^0 bag/van  ^yRB^0 drop  ^yB^0 close");
    if (foc == FOC_CRAFT) help = ctl("^yLMB/E^0 craft  ^yTAB^0 close", "^yA^0 craft  ^yB^0 close");
    char hbuf[128];
    if (foc == FOC_HANDS) {
        const char *what = sel_hand != p->wslot ? "in hand" : "into the bag";
        if (IN.pad_active) SDL_snprintf(hbuf, sizeof hbuf, "^yA^0 %s  ^yRB^0 drop  ^yB^0 close", what);
        else SDL_snprintf(hbuf, sizeof hbuf, "^yLMB/E^0 %s  ^y1-3^0 in hand  ^yRMB/R^0 drop  ^yTAB^0 close", what);
        help = hbuf;
    }
    gfx_text(FONT_SMALL, help, 454, 237, COL_GREY, TXT_RIGHT);
    if (inv_msg_t > 0) gfx_text(FONT_SMALL, inv_msg, 130, 224, COL_ORANGE, TXT_CENTER | TXT_OUTLINE);
    hud_cursor();
}

/* ================================================================ pause */
static int pause_sel;

void pause_open(void) {
    g_paused = true;
    pause_sel = 0;
}

void hud_reset_level_state(void) {
    pause_sel = 0;
    foc = FOC_BAG;
    sel_bag = sel_store = sel_craft = 0;
    inv_msg_t = 0;
    memset(&st, 0, sizeof st);
}

/* at the Greenhouse there's no store to restart or give up on */
static const char *const *pause_items(int *n) {
    static const char *const story[4] = {"RESUME", "OPTIONS", "RESTART LEVEL", "QUIT TO TITLE"};
    static const char *const rogue[4] = {"RESUME", "OPTIONS", "GIVE UP", "QUIT TO TITLE"};
    static const char *const camp[3] = {"RESUME", "OPTIONS", "QUIT TO TITLE"};
    if (W.hub) { *n = 3; return camp; }
    *n = 4;
    return RUN.mode == MODE_STORY ? story : rogue;
}

void pause_update(float dt) {
    (void)dt;
    int n;
    pause_items(&n);
    if (IN.repeat[ACT_MENU_UP]) { pause_sel = (pause_sel + n - 1) % n; audio_play(SFX_UI_MOVE, 0.8f, 0, 1); }
    if (IN.repeat[ACT_MENU_DOWN]) { pause_sel = (pause_sel + 1) % n; audio_play(SFX_UI_MOVE, 0.8f, 0, 1); }
    V2 m = IN.mouse_view;
    int hit = -1;
    for (int i = 0; i < n; i++) {
        float y = 120 + i * 20;
        if (m.y >= y - 4 && m.y < y + 14 && fabsf(m.x - VIEW_W / 2) < 90) hit = i;
    }
    if (IN.mouse_moved && hit >= 0) pause_sel = hit;
    if (IN.pressed[ACT_PAUSE] || IN.pressed[ACT_BACK]) {
        g_paused = false;
        audio_play(SFX_UI_BACK, 0.85f, 0, 1);
        return;
    }
    bool go = IN.pressed[ACT_CONFIRM] || IN.pressed[ACT_INTERACT];
    if (IN.click && hit >= 0) { pause_sel = hit; go = true; }   /* a click only counts on an entry */
    if (go) {
        audio_play(SFX_UI_SELECT, 0.9f, 0, 1);
        if (W.hub && pause_sel == 2) {   /* the camp: everything you have now goes into the save */
            g_paused = false;
            hub_sync_run();
            run_save();
            world_free();
            scene_set(SC_TITLE);
            return;
        }
        switch (pause_sel) {
        case 0: g_paused = false; break;
        case 1: scene_set(SC_OPTIONS); break;
        case 2:
            g_paused = false;
            if (RUN.mode == MODE_STORY) {
                RUN.retries++;
                run_restore_snapshot();
                start_level();
            } else {
                player_died();
            }
            break;
        case 3:
            g_paused = false;
            run_save();
            world_free();
            scene_set(SC_TITLE);
            break;
        }
    }
}

void pause_draw(void) {
    gfx_fill(0, 0, VIEW_W, VIEW_H, rgba(11, 10, 16, 170));
    gfx_text_big(FONT_BIG, "PAUSED", VIEW_W / 2, 56, 3, COL_YELLOW, TXT_CENTER | TXT_SHADOW);
    int n;
    const char *const *items = pause_items(&n);
    for (int i = 0; i < n; i++) {
        bool s = i == pause_sel;
        float tw = gfx_text_w(FONT_SMALL, items[i]);
        if (s) gfx_marker(VIEW_W / 2 - tw / 2 - 4, 118 + i * 20, tw + 8, 11, COL_YELLOW);
        gfx_text(FONT_SMALL, items[i], VIEW_W / 2, 120 + i * 20, s ? COL_BLACK : COL_WHITE, TXT_CENTER | (s ? 0 : TXT_OUTLINE));
    }
    /* say what quitting keeps: the run is saved, this store starts over */
    char buf[96];
    if (W.hub) {
        if (pause_sel == 2) gfx_text(FONT_SMALL, "^kYour run is saved here at the Greenhouse.", VIEW_W / 2, 204, COL_GREY, TXT_CENTER);
        SDL_snprintf(buf, sizeof buf, "%s - THE GREENHOUSE - BEFORE LEVEL %d", RUN.mode == MODE_STORY ? "STORY" : "ROGUELIKE", RUN.level + 1);
        gfx_text(FONT_SMALL, buf, VIEW_W / 2, 230, COL_GREY, TXT_CENTER);
        hud_cursor();
        return;
    }
    if (pause_sel == 3)
        gfx_text(FONT_SMALL, RUN.mode == MODE_STORY ? "^kYour run is saved. This store starts over when you continue."
                                                    : "^kYour run is saved, with your wounds. Anything found here is lost.",
                 VIEW_W / 2, 204, COL_GREY, TXT_CENTER);
    SDL_snprintf(buf, sizeof buf, "%s - LEVEL %d - %s", RUN.mode == MODE_STORY ? "STORY" : "ROGUELIKE", W.level + 1, W.def->name);
    gfx_text(FONT_SMALL, buf, VIEW_W / 2, 230, COL_GREY, TXT_CENTER);
    hud_cursor();
}
