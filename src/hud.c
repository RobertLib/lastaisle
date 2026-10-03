/* LAST AISLE - HUD, inventory + crafting, pause */
#include "world.h"
#include "gfx.h"
#include "input.h"
#include "audio.h"
#include "game.h"

static const Color INK = {30, 45, 110, 255};
static const Color INK_FADE = {120, 110, 120, 255};

static void upper(char *s) { for (; *s; s++) if (*s >= 'a' && *s <= 'z') *s -= 32; }

void hud_cursor(void) {
    if (IN.pad_active) return;
    V2 m = IN.mouse_view;
    gfx_spr(SPR_UI_CURSOR, floorf(m.x), floorf(m.y));
}

/* ------------------------------------------------------------ the list */
static void draw_list(float x, float y) {
    float w = 116, rh = 11;
    float h = 18 + W.nlist * rh + (W.nbonus ? 13 : 0) + 3;
    gfx_nine(SPR_UI_PAPER, x, y, w, h, TINT_NONE);
    gfx_spr(SPR_UI_TAPE, x + w / 2, y + 1);
    gfx_text(FONT_SMALL, "SHOPPING LIST", x + 9, y + 6, INK, 0);
    float ry = y + 17;
    for (int i = 0; i < W.nlist; i++) {
        ListEntry *e = &W.list[i];
        float fl = e->flash > 0 ? e->flash : 0;
        float ix = x + 5 + (fl > 0 ? sinf(fl * 40) * 1.5f : 0);
        gfx_spr(ITEMS[e->id].spr, ix + 8, ry + 4);
        Color c = e->done ? INK_FADE : INK;
        if (fl > 0) c = color_lerp(c, rgb(200, 30, 60), fl);
        char nm[40];
        SDL_strlcpy(nm, ITEMS[e->id].name, sizeof nm);
        char cnt0[16];
        SDL_snprintf(cnt0, sizeof cnt0, "%d/%d", MINF(e->have, e->need), e->need);
        int maxw = (int)w - 23 - 7 - (e->done ? 14 : gfx_text_w(FONT_SMALL, cnt0)) - 4;
        while (gfx_text_w(FONT_SMALL, nm) > maxw && strlen(nm) > 3) {
            size_t l = strlen(nm);
            nm[l - 1] = 0;
            if (nm[l - 2] == ' ') nm[l - 2] = 0;
            l = strlen(nm);
            nm[l - 1] = '.';
        }
        gfx_text(FONT_SMALL, nm, x + 23, ry, c, 0);
        char cnt[16];
        SDL_snprintf(cnt, sizeof cnt, "%d/%d", MINF(e->have, e->need), e->need);
        if (e->done) {
            int tw = gfx_text_w(FONT_SMALL, nm);
            gfx_fill(x + 22, ry + 3, tw + 2, 1, rgba(200, 30, 60, 220));
            gfx_spr(SPR_UI_CHECK, x + w - 17, ry - 1);
        } else {
            gfx_text(FONT_SMALL, cnt, x + w - 7, ry, c, TXT_RIGHT);
        }
        ry += rh;
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
        gfx_text(FONT_SMALL, "^kLMB shove - E let go", x + 24, y + 16, COL_GREY, 0);
        return;
    }
    if (!p->weapon.id) {
        gfx_text(FONT_SMALL, "BARE HANDS", x + 8, y + 6, COL_WHITE, 0);
        gfx_text(FONT_SMALL, "^kLMB punch - E pick up", x + 8, y + 16, COL_GREY, 0);
        return;
    }
    const ItemDef *it = &ITEMS[p->weapon.id];
    const WeaponDef *wd = item_weapon(p->weapon.id);
    gfx_spr(it->spr, x + 14, y + 14);
    char name[48];
    SDL_strlcpy(name, it->name, sizeof name);
    upper(name);
    gfx_text(FONT_SMALL, name, x + 26, y + 5, COL_WHITE, 0);
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
        SDL_snprintf(buf, sizeof buf, "x%d  ^kLMB throw", p->weapon.count);
        gfx_text(FONT_SMALL, buf, bx, by, COL_YELLOW, 0);
    } else {
        int maxd = wd->durability * ((RUN.perks[PK_TINKERER] && wd->kind == WK_MELEE) ? 2 : 1);
        float k = maxd > 0 ? CLAMP((float)p->weapon.cond / maxd, 0.0f, 1.0f) : 1;
        gfx_fill(bx, by + 1, 52, 5, rgba(11, 10, 16, 255));
        Color c = k > 0.5f ? COL_GREEN : (k > 0.2f ? COL_YELLOW : COL_RED);
        if (wd->kind == WK_CHAINSAW || wd->kind == WK_FLAME) c = COL_ORANGE;
        gfx_fill(bx + 1, by + 2, 50 * k, 3, c);
        gfx_text(FONT_SMALL, wd->kind == WK_MELEE ? "^kRMB throw" : "^kfuel", bx + 57, by, COL_GREY, 0);
    }
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
    SDL_snprintf(buf, sizeof buf, "^yF^0 +%d", meds);
    gfx_text(FONT_SMALL, buf, x + 52, y + 7, meds ? COL_GREEN : COL_GREY, 0);
    gfx_text(FONT_SMALL, "^kTAB bag/craft", x + 6, y + 17, COL_GREY, 0);
}

static void draw_score(void) {
    char buf[32];
    SDL_snprintf(buf, sizeof buf, "%d", W.score);
    gfx_text(FONT_BIG, buf, VIEW_W - 8, 6, COL_WHITE, TXT_RIGHT | TXT_SHADOW | TXT_RAINBOW);
    gfx_text(FONT_SMALL, "PTS", VIEW_W - 8, 20, COL_GREY, TXT_RIGHT);
    if (W.kills > 0) {
        char kb[16];
        SDL_snprintf(kb, sizeof kb, "%d", W.kills);
        int kw = gfx_text_w(FONT_SMALL, kb);
        gfx_text(FONT_SMALL, kb, VIEW_W - 30, 20, COL_WHITE, TXT_RIGHT);
        gfx_spr(SPR_UI_SKULL, VIEW_W - 44 - kw, 18);
    }
    if (W.combo >= 2 && W.combo_t > 0) {
        SDL_snprintf(buf, sizeof buf, "%dX", W.combo);
        float s = 1.0f + 0.15f * sinf(G.time * 12);
        (void)s;
        gfx_text(FONT_BIG, buf, VIEW_W - 8, 32, COL_PINK, TXT_RIGHT | TXT_SHADOW | TXT_WAVE);
        gfx_text(FONT_SMALL, "COMBO", VIEW_W - 8, 46, COL_PINK, TXT_RIGHT | TXT_OUTLINE);
        gfx_fill(VIEW_W - 48, 57, 40 * CLAMP(W.combo_t / 3.2f, 0.0f, 1.0f), 2, COL_PINK);
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
    gfx_fill(0, VIEW_H / 2 - 30, VIEW_W, 58, rgba(11, 10, 16, (Uint8)(170 * a)));
    gfx_text(FONT_SMALL, buf, VIEW_W / 2 + slide * 0.5f, VIEW_H / 2 - 24, c, TXT_CENTER);
    Color pc = gfx_rainbow(G.time * 0.3f);
    pc.a = c.a;
    gfx_text_big(FONT_BIG, W.def->name, VIEW_W / 2 + slide, VIEW_H / 2 - 15, 2, pc, TXT_CENTER | TXT_SHADOW | TXT_WAVE);
    Color tc = COL_YELLOW;
    tc.a = c.a;
    gfx_text(FONT_SMALL, W.def->tagline, VIEW_W / 2 - slide * 0.5f, VIEW_H / 2 + 12, tc, TXT_CENTER);
}

void hud_draw(void) {
    Actor *p = player();
    draw_hearts(6, 5);
    draw_list(3, 15);
    draw_score();
    draw_weapon_panel();
    draw_bag_panel();
    draw_exit_arrow();
    draw_boss_bar();
    if (W.msg_t > 0) {
        Color c = W.msg_col;
        c.a = (Uint8)(255 * CLAMP(W.msg_t * 2, 0.0f, 1.0f));
        gfx_text(FONT_BIG, W.msg, VIEW_W / 2, 52, c, TXT_CENTER | TXT_SHADOW | TXT_WAVE | TXT_OUTLINE);
    }
    if (W.hint_t > 0 && SET.hints) {
        Color c = COL_WHITE;
        c.a = (Uint8)(255 * CLAMP(W.hint_t * 2, 0.0f, 1.0f));
        gfx_text(FONT_SMALL, W.hint, VIEW_W / 2, VIEW_H - 46, c, TXT_CENTER | TXT_OUTLINE);
    }
    /* first-level help */
    if (W.level == 0 && W.time < 22 && SET.hints && W.intro_t > 3.6f) {
        Color c = COL_WHITE;
        c.a = (Uint8)(255 * CLAMP((22 - W.time) / 2, 0.0f, 1.0f));
        gfx_text(FONT_SMALL, "^yWASD^0 move  ^yMOUSE^0 aim  ^yLMB^0 attack  ^yRMB^0 throw  ^yE^0 search/take  ^ySHIFT^0 look",
                 VIEW_W / 2, VIEW_H - 58, c, TXT_CENTER | TXT_OUTLINE);
    }
    if (W.intro_t < 3.6f) level_intro_draw();
    if (W.player_dead && W.dead_t > 0.8f) {
        float a = CLAMP((W.dead_t - 0.8f) * 2, 0.0f, 1.0f);
        gfx_fill(0, 0, VIEW_W, VIEW_H, rgba(40, 0, 10, (Uint8)(120 * a)));
        Color c = COL_RED;
        c.a = (Uint8)(255 * a);
        gfx_text_big(FONT_BIG, "YOU'RE DEAD", VIEW_W / 2, VIEW_H / 2 - 34, 3, c, TXT_CENTER | TXT_SHADOW | TXT_SHAKE);
        Color w = COL_WHITE;
        w.a = c.a;
        if (W.dead_t > 1.6f)
            gfx_text(FONT_SMALL, RUN.mode == MODE_STORY ? "^yENTER^0 / ^yR^0 to try again" : "^yENTER^0 to face it",
                     VIEW_W / 2, VIEW_H / 2 + 6, w, TXT_CENTER | TXT_OUTLINE);
    }
    if (W.exiting) {
        float a = CLAMP(W.exit_t / 1.4f, 0.0f, 1.0f);
        gfx_fill(0, 0, VIEW_W, VIEW_H, rgba(11, 10, 16, (Uint8)(255 * a)));
    }
    (void)p;
    hud_cursor();
}

/* ======================================================= inventory screen */
typedef enum { FOC_HANDS, FOC_BAG, FOC_CART, FOC_CRAFT } Focus;
static Focus foc = FOC_BAG;
static int sel_bag, sel_cart, sel_craft;
static int near_cart = -1;
static char inv_msg[64];
static float inv_msg_t;

static int cart_used(Cart *c) {
    int u = 0;
    for (int k = 0; k < c->n; k++) u += ITEMS[c->items[k].id].size * c->items[k].count;
    return u;
}

static void inv_note(const char *s) { SDL_strlcpy(inv_msg, s, sizeof inv_msg); inv_msg_t = 2; }

static void cart_remove(Cart *c, int k) {
    memmove(&c->items[k], &c->items[k + 1], sizeof(Stack) * (c->n - k - 1));
    c->n--;
}

static void bag_primary(int k) {
    Actor *p = player();
    if (k < 0 || k >= p->ninv) return;
    Stack s = p->inv[k];
    const ItemDef *it = &ITEMS[s.id];
    if (it->cat == CAT_WEAPON) {
        memmove(&p->inv[k], &p->inv[k + 1], sizeof(Stack) * (p->ninv - k - 1));
        p->ninv--;
        if (p->weapon.id && !inv_add(p, p->weapon)) pickup_spawn(p->weapon, p->pos, v2(0, 0));
        p->weapon = s;
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
    if (near_cart >= 0) {
        Cart *c = &W.carts[near_cart];
        if (cart_used(c) + it->size * s.count > CART_CAP || c->n >= 16) { inv_note("The cart is full."); return; }
        c->items[c->n++] = s;
        memmove(&p->inv[k], &p->inv[k + 1], sizeof(Stack) * (p->ninv - k - 1));
        p->ninv--;
        audio_play(SFX_CART_ROLL, 0.6f, 0, 1.2f);
        return;
    }
    inv_note(it->desc);
}

static void bag_drop(int k) {
    Actor *p = player();
    if (k < 0 || k >= p->ninv) return;
    Stack s = p->inv[k];
    memmove(&p->inv[k], &p->inv[k + 1], sizeof(Stack) * (p->ninv - k - 1));
    p->ninv--;
    int pi = pickup_spawn(s, p->pos, v2(frange(-30, 30), frange(10, 40)));
    if (pi >= 0) W.pickups[pi].dropped = true;
    audio_play(SFX_UI_BACK, 0.85f, 0, 1);
}

static void cart_primary(int k) {
    if (near_cart < 0) return;
    Cart *c = &W.carts[near_cart];
    if (k < 0 || k >= c->n) return;
    Actor *p = player();
    if (ITEMS[c->items[k].id].cat == CAT_WEAPON && !p->weapon.id) {
        p->weapon = c->items[k];
        cart_remove(c, k);
        return;
    }
    if (!inv_add(p, c->items[k])) { inv_note("No room in the bag."); audio_play(SFX_UI_ERROR, 0.5f, 0, 1); return; }
    cart_remove(c, k);
    audio_play(SFX_PICKUP, 0.9f, 0, 1);
}

void inventory_update(float dt) {
    Actor *p = player();
    inv_msg_t -= dt;
    near_cart = p->cart >= 0 ? p->cart : cart_near(p->pos, 40);
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
        else if (foc == FOC_CART) sel_cart = CLAMP(sel_cart + dir, 0, 15);
        audio_play(SFX_UI_MOVE, 0.75f, 0, 1);
    }
    if (IN.repeat[ACT_MENU_UP] || IN.repeat[ACT_MENU_DOWN]) {
        int dir = IN.repeat[ACT_MENU_DOWN] ? 1 : -1;
        if (foc == FOC_CRAFT) sel_craft = (sel_craft + dir + NUM_RECIPES) % NUM_RECIPES;
        else if (foc == FOC_BAG) {
            if (dir < 0 && sel_bag < 4) foc = FOC_HANDS;
            else if (dir > 0 && sel_bag + 4 > 23 && near_cart >= 0) foc = FOC_CART;
            else sel_bag = CLAMP(sel_bag + dir * 4, 0, 23);
        } else if (foc == FOC_HANDS && dir > 0) foc = FOC_BAG;
        else if (foc == FOC_CART && dir < 0) foc = FOC_BAG;
        audio_play(SFX_UI_MOVE, 0.75f, 0, 1);
    }
    bool act = IN.pressed[ACT_CONFIRM] || IN.pressed[ACT_INTERACT];
    bool drop = IN.pressed[ACT_RELOAD];
    /* mouse */
    V2 m = IN.mouse_view;
    if (IN.mouse_moved || IN.click || IN.rclick) {
        float bx = 24, by = 66;
        for (int k = 0; k < 24; k++) {
            float x = bx + (k % 4) * 26, y = by + (k / 4) * 26;
            if (m.x >= x && m.x < x + 24 && m.y >= y && m.y < y + 24) { foc = FOC_BAG; sel_bag = k; }
        }
        if (m.x >= 24 && m.x < 48 && m.y >= 30 && m.y < 54) foc = FOC_HANDS;
        if (near_cart >= 0)
            for (int k = 0; k < 16; k++) {
                float x = 136 + (k % 4) * 26, y = by + (k / 4) * 26;
                if (m.x >= x && m.x < x + 24 && m.y >= y && m.y < y + 24) { foc = FOC_CART; sel_cart = k; }
            }
        for (int r = 0; r < NUM_RECIPES; r++) {
            float y = 34 + r * 14;
            if (m.x >= 252 && m.x < 470 && m.y >= y - 1 && m.y < y + 13) { foc = FOC_CRAFT; sel_craft = r; }
        }
        if (IN.click) act = true;
        if (IN.rclick) drop = true;
    }
    if (act) {
        switch (foc) {
        case FOC_HANDS:
            if (p->weapon.id) {
                if (inv_add(p, p->weapon)) { p->weapon.id = IT_NONE; audio_play(SFX_PICKUP_WEAPON, 0.6f, 0, 0.9f); }
                else inv_note("No room in the bag.");
            }
            break;
        case FOC_BAG: bag_primary(sel_bag); break;
        case FOC_CART: cart_primary(sel_cart); break;
        case FOC_CRAFT: {
            const Recipe *r = &RECIPES[sel_craft];
            if (craft(p, r)) {
                char buf[64];
                SDL_snprintf(buf, sizeof buf, "Crafted: %s", r->flag == RF_REPAIR ? "repaired weapon" : ITEMS[r->out].name);
                inv_note(buf);
            } else {
                audio_play(SFX_UI_ERROR, 0.5f, 0, 1);
                inv_note("Missing ingredients.");
            }
            break;
        }
        }
        list_recount();
    }
    if (drop) {
        if (foc == FOC_BAG) bag_drop(sel_bag);
        else if (foc == FOC_HANDS && p->weapon.id) { drop_weapon(p, false); audio_play(SFX_UI_BACK, 0.85f, 0, 1); }
        else if (foc == FOC_CART && near_cart >= 0) {
            Cart *c = &W.carts[near_cart];
            if (sel_cart < c->n) { pickup_spawn(c->items[sel_cart], c->pos, v2(frange(-30, 30), frange(-30, 30))); cart_remove(c, sel_cart); }
        }
        list_recount();
    }
}

static void slot(float x, float y, Stack *s, bool sel, bool list_item) {
    gfx_fill(x, y, 24, 24, sel ? rgba(255, 95, 149, 70) : rgba(255, 255, 255, 18));
    gfx_rect(x, y, 24, 24, sel ? COL_PINK : rgba(255, 255, 255, 40));
    if (!s || !s->id) return;
    if (list_item) gfx_glow(x + 12, y + 12, 14, COL_YELLOW, 0.35f);
    gfx_spr(ITEMS[s->id].spr, x + 12, y + 12);
    char buf[8];
    if (s->count > 1) {
        SDL_snprintf(buf, sizeof buf, "%d", s->count);
        gfx_text(FONT_SMALL, buf, x + 23, y + 15, COL_WHITE, TXT_RIGHT | TXT_OUTLINE);
    }
}

static bool on_list(ItemId id) {
    for (int i = 0; i < W.nlist; i++) if (W.list[i].id == id) return true;
    return false;
}

void inventory_draw(void) {
    Actor *p = player();
    gfx_fill(0, 0, VIEW_W, VIEW_H, rgba(11, 10, 16, 228));
    gfx_text(FONT_BIG, "BAG", 24, 10, COL_PINK, TXT_SHADOW);
    char buf[96];
    SDL_snprintf(buf, sizeof buf, "%s  %d/%d", ITEMS[RUN.bag].name, inv_used(p), inv_capacity(p));
    gfx_text(FONT_SMALL, buf, 60, 14, COL_WHITE, 0);
    /* hands */
    gfx_text(FONT_SMALL, "HANDS", 52, 38, COL_GREY, 0);
    slot(24, 30, &p->weapon, foc == FOC_HANDS, false);
    float bx = 24, by = 66;
    for (int k = 0; k < 24; k++) {
        Stack *s = k < p->ninv ? &p->inv[k] : NULL;
        slot(bx + (k % 4) * 26, by + (k / 4) * 26, s, foc == FOC_BAG && sel_bag == k, s && on_list((ItemId)s->id));
    }
    if (near_cart >= 0) {
        Cart *c = &W.carts[near_cart];
        SDL_snprintf(buf, sizeof buf, "CART %d/%d", cart_used(c), CART_CAP);
        gfx_text(FONT_SMALL, buf, 136, 56, COL_CYAN, 0);
        for (int k = 0; k < 16; k++) {
            Stack *s = k < c->n ? &c->items[k] : NULL;
            slot(136 + (k % 4) * 26, by + (k / 4) * 26, s, foc == FOC_CART && sel_cart == k, s && on_list((ItemId)s->id));
        }
    }
    /* crafting */
    gfx_text(FONT_BIG, "CRAFT", 252, 10, COL_CYAN, TXT_SHADOW);
    for (int r = 0; r < NUM_RECIPES; r++) {
        const Recipe *rc = &RECIPES[r];
        float y = 34 + r * 14;
        bool ok = can_craft(p, rc);
        bool sel = foc == FOC_CRAFT && sel_craft == r;
        if (sel) gfx_fill(250, y - 1, 222, 14, rgba(98, 236, 208, 50));
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
            int have = inv_count(p, id) + (p->weapon.id == id ? p->weapon.count : 0);
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
    const char *desc = NULL;
    const char *title = NULL;
    if (foc == FOC_BAG && sel_bag < p->ninv) { title = ITEMS[p->inv[sel_bag].id].name; desc = ITEMS[p->inv[sel_bag].id].desc; }
    if (foc == FOC_HANDS && p->weapon.id) { title = ITEMS[p->weapon.id].name; desc = ITEMS[p->weapon.id].desc; }
    if (foc == FOC_CART && near_cart >= 0 && sel_cart < W.carts[near_cart].n) {
        title = ITEMS[W.carts[near_cart].items[sel_cart].id].name;
        desc = ITEMS[W.carts[near_cart].items[sel_cart].id].desc;
    }
    if (foc == FOC_CRAFT) { title = "Recipe"; desc = RECIPES[sel_craft].hint; }
    gfx_nine(SPR_UI_PANEL, 18, 232, 444, 34, TINT_NONE);
    if (title) gfx_text(FONT_SMALL, title, 26, 237, COL_YELLOW, 0);
    if (desc) gfx_text_wrap(FONT_SMALL, desc, 26, 247, 420, COL_WHITE, 0, 9);
    const char *help = "^yLMB/E^0 use/equip/move  ^yRMB/R^0 drop  ^yTAB^0 close";
    if (foc == FOC_CRAFT) help = "^yLMB/E^0 craft  ^yTAB^0 close";
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
    sel_bag = sel_cart = sel_craft = 0;
    inv_msg_t = 0;
}

void pause_update(float dt) {
    (void)dt;
    const int n = 4;
    if (IN.repeat[ACT_MENU_UP]) { pause_sel = (pause_sel + n - 1) % n; audio_play(SFX_UI_MOVE, 0.8f, 0, 1); }
    if (IN.repeat[ACT_MENU_DOWN]) { pause_sel = (pause_sel + 1) % n; audio_play(SFX_UI_MOVE, 0.8f, 0, 1); }
    V2 m = IN.mouse_view;
    for (int i = 0; i < n; i++) {
        float y = 120 + i * 20;
        if (IN.mouse_moved && m.y >= y - 4 && m.y < y + 14 && fabsf(m.x - VIEW_W / 2) < 90) pause_sel = i;
    }
    if (IN.pressed[ACT_PAUSE] || IN.pressed[ACT_BACK]) {
        g_paused = false;
        audio_play(SFX_UI_BACK, 0.85f, 0, 1);
        return;
    }
    if (IN.pressed[ACT_CONFIRM] || IN.pressed[ACT_INTERACT] || IN.click) {
        audio_play(SFX_UI_SELECT, 0.9f, 0, 1);
        switch (pause_sel) {
        case 0: g_paused = false; break;
        case 1: scene_set(SC_OPTIONS); break;
        case 2:
            g_paused = false;
            if (RUN.mode == MODE_STORY) {
                RUN.retries++;
                RUN.hp = RUN.snap_hp;
                RUN.weapon = RUN.snap_weapon;
                RUN.ninv = RUN.snap_ninv;
                memcpy(RUN.inv, RUN.snap_inv, sizeof RUN.inv);
                RUN.bag = RUN.snap_bag;
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
    gfx_text_big(FONT_BIG, "PAUSED", VIEW_W / 2, 56, 3, COL_PINK, TXT_CENTER | TXT_SHADOW | TXT_WAVE);
    const char *items[4] = {"RESUME", "OPTIONS", RUN.mode == MODE_STORY ? "RESTART LEVEL" : "GIVE UP", "SAVE & QUIT TO TITLE"};
    for (int i = 0; i < 4; i++) {
        bool s = i == pause_sel;
        gfx_text(FONT_SMALL, items[i], VIEW_W / 2, 120 + i * 20, s ? COL_YELLOW : COL_WHITE,
                 TXT_CENTER | TXT_OUTLINE | (s ? TXT_WAVE : 0));
        if (s) gfx_spr(SPR_UI_ARROW, VIEW_W / 2 - gfx_text_w(FONT_SMALL, items[i]) / 2 - 10, 124 + i * 20);
    }
    char buf[96];
    SDL_snprintf(buf, sizeof buf, "%s - LEVEL %d - %s", RUN.mode == MODE_STORY ? "STORY" : "ROGUELIKE", W.level + 1, W.def->name);
    gfx_text(FONT_SMALL, buf, VIEW_W / 2, 230, COL_GREY, TXT_CENTER);
    hud_cursor();
}
