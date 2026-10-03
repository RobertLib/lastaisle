/* LAST AISLE - input */
#include "input.h"
#include "gfx.h"

Input IN;

static bool key_state[ACT_COUNT];
static bool mouse_state[ACT_COUNT];
static bool pad_state[ACT_COUNT];

static void set_act(bool *state, Action a, bool down) { state[a] = down; }

static void map_key(SDL_Scancode sc, bool down) {
    switch (sc) {
    case SDL_SCANCODE_W: set_act(key_state, ACT_UP, down); set_act(key_state, ACT_MENU_UP, down); break;
    case SDL_SCANCODE_S: set_act(key_state, ACT_DOWN, down); set_act(key_state, ACT_MENU_DOWN, down); break;
    case SDL_SCANCODE_A: set_act(key_state, ACT_LEFT, down); set_act(key_state, ACT_MENU_LEFT, down); break;
    case SDL_SCANCODE_D: set_act(key_state, ACT_RIGHT, down); set_act(key_state, ACT_MENU_RIGHT, down); break;
    case SDL_SCANCODE_UP: set_act(key_state, ACT_UP, down); set_act(key_state, ACT_MENU_UP, down); break;
    case SDL_SCANCODE_DOWN: set_act(key_state, ACT_DOWN, down); set_act(key_state, ACT_MENU_DOWN, down); break;
    case SDL_SCANCODE_LEFT: set_act(key_state, ACT_LEFT, down); set_act(key_state, ACT_MENU_LEFT, down); break;
    case SDL_SCANCODE_RIGHT: set_act(key_state, ACT_RIGHT, down); set_act(key_state, ACT_MENU_RIGHT, down); break;
    case SDL_SCANCODE_E: set_act(key_state, ACT_INTERACT, down); break;
    case SDL_SCANCODE_SPACE: set_act(key_state, ACT_EXECUTE, down); set_act(key_state, ACT_CONFIRM, down); break;
    case SDL_SCANCODE_LSHIFT:
    case SDL_SCANCODE_RSHIFT: set_act(key_state, ACT_LOOK, down); break;
    case SDL_SCANCODE_TAB:
    case SDL_SCANCODE_I: set_act(key_state, ACT_INVENTORY, down); break;
    case SDL_SCANCODE_F: set_act(key_state, ACT_HEAL, down); break;
    case SDL_SCANCODE_Q: set_act(key_state, ACT_SWAP, down); break;
    case SDL_SCANCODE_R: set_act(key_state, ACT_RELOAD, down); break;
    case SDL_SCANCODE_ESCAPE: set_act(key_state, ACT_PAUSE, down); set_act(key_state, ACT_BACK, down); break;
    case SDL_SCANCODE_P: set_act(key_state, ACT_PAUSE, down); break;
    case SDL_SCANCODE_BACKSPACE: set_act(key_state, ACT_BACK, down); break;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER: set_act(key_state, ACT_CONFIRM, down); break;
    default: break;
    }
}

static void map_pad_button(int b, bool down) {
    switch (b) {
    case SDL_GAMEPAD_BUTTON_SOUTH: set_act(pad_state, ACT_INTERACT, down); set_act(pad_state, ACT_CONFIRM, down); break;
    case SDL_GAMEPAD_BUTTON_EAST: set_act(pad_state, ACT_BACK, down); set_act(pad_state, ACT_HEAL, down); break;
    case SDL_GAMEPAD_BUTTON_WEST: set_act(pad_state, ACT_EXECUTE, down); break;
    case SDL_GAMEPAD_BUTTON_NORTH: set_act(pad_state, ACT_SWAP, down); break;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: set_act(pad_state, ACT_RELOAD, down); break;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: set_act(pad_state, ACT_HEAL, down); break;
    case SDL_GAMEPAD_BUTTON_BACK: set_act(pad_state, ACT_INVENTORY, down); break;
    case SDL_GAMEPAD_BUTTON_START: set_act(pad_state, ACT_PAUSE, down); break;
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: set_act(pad_state, ACT_LOOK, down); break;
    case SDL_GAMEPAD_BUTTON_DPAD_UP: set_act(pad_state, ACT_MENU_UP, down); break;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: set_act(pad_state, ACT_MENU_DOWN, down); break;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: set_act(pad_state, ACT_MENU_LEFT, down); break;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: set_act(pad_state, ACT_MENU_RIGHT, down); break;
    default: break;
    }
}

void input_init(void) {
    memset(&IN, 0, sizeof IN);
    int n = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&n);
    if (ids && n > 0) IN.pad = SDL_OpenGamepad(ids[0]);
    SDL_free(ids);
}

void input_clear(void) {
    memset(key_state, 0, sizeof key_state);
    memset(mouse_state, 0, sizeof mouse_state);
    memset(pad_state, 0, sizeof pad_state);
    memset(IN.down, 0, sizeof IN.down);
    memset(IN.pressed, 0, sizeof IN.pressed);
    memset(IN.repeat, 0, sizeof IN.repeat);
}

void input_begin_frame(void) {
    IN.click = IN.rclick = false;
    IN.wheel = 0;
    IN.mouse_moved = false;
    IN.any_pressed = false;
    IN.fullscreen_toggle = false;
}

void input_event(const SDL_Event *e) {
    switch (e->type) {
    case SDL_EVENT_QUIT: IN.quit = true; break;
    case SDL_EVENT_KEY_DOWN:
        if (e->key.repeat) break;
        if (e->key.scancode == SDL_SCANCODE_F11 ||
            (e->key.scancode == SDL_SCANCODE_RETURN && (e->key.mod & SDL_KMOD_ALT))) {
            IN.fullscreen_toggle = true;
            break;
        }
        map_key(e->key.scancode, true);
        IN.pad_active = false;
        IN.any_pressed = true;
        break;
    case SDL_EVENT_KEY_UP: map_key(e->key.scancode, false); break;
    case SDL_EVENT_MOUSE_MOTION:
        IN.mouse_screen = v2(e->motion.x, e->motion.y);
        IN.mouse_moved = true;
        if (fabsf(e->motion.xrel) + fabsf(e->motion.yrel) > 2) IN.pad_active = false;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        IN.pad_active = false;
        IN.any_pressed = true;
        if (e->button.button == SDL_BUTTON_LEFT) { mouse_state[ACT_ATTACK] = true; IN.click = true; }
        if (e->button.button == SDL_BUTTON_RIGHT) { mouse_state[ACT_THROW] = true; IN.rclick = true; }
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e->button.button == SDL_BUTTON_LEFT) mouse_state[ACT_ATTACK] = false;
        if (e->button.button == SDL_BUTTON_RIGHT) mouse_state[ACT_THROW] = false;
        break;
    case SDL_EVENT_MOUSE_WHEEL: IN.wheel += e->wheel.y; break;
    case SDL_EVENT_GAMEPAD_ADDED:
        if (!IN.pad) IN.pad = SDL_OpenGamepad(e->gdevice.which);
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        if (IN.pad && SDL_GetGamepadID(IN.pad) == e->gdevice.which) {
            SDL_CloseGamepad(IN.pad);
            IN.pad = NULL;
            memset(pad_state, 0, sizeof pad_state);
        }
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        map_pad_button(e->gbutton.button, true);
        IN.pad_active = true;
        IN.any_pressed = true;
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_UP: map_pad_button(e->gbutton.button, false); break;
    default: break;
    }
}

void input_update(float dt) {
    /* gamepad analog */
    V2 stick = v2(0, 0);
    IN.aim_stick = v2(0, 0);
    if (IN.pad) {
        const float dz = 0.22f;
        float lx = SDL_GetGamepadAxis(IN.pad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
        float ly = SDL_GetGamepadAxis(IN.pad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;
        float rx = SDL_GetGamepadAxis(IN.pad, SDL_GAMEPAD_AXIS_RIGHTX) / 32767.0f;
        float ry = SDL_GetGamepadAxis(IN.pad, SDL_GAMEPAD_AXIS_RIGHTY) / 32767.0f;
        float lt = SDL_GetGamepadAxis(IN.pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) / 32767.0f;
        float rt = SDL_GetGamepadAxis(IN.pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) / 32767.0f;
        if (sqrtf(lx * lx + ly * ly) > dz) { stick = v2(lx, ly); IN.pad_active = true; }
        if (sqrtf(rx * rx + ry * ry) > 0.3f) { IN.aim_stick = v2(rx, ry); IN.pad_active = true; }
        pad_state[ACT_ATTACK] = rt > 0.4f;
        pad_state[ACT_THROW] = lt > 0.4f;
        bool su = ly < -0.6f, sd = ly > 0.6f, sl = lx < -0.6f, sr = lx > 0.6f;
        pad_state[ACT_UP] = su; pad_state[ACT_DOWN] = sd; pad_state[ACT_LEFT] = sl; pad_state[ACT_RIGHT] = sr;
        /* stick also drives menus */
        static bool msu, msd, msl, msr;
        if (su != msu) { msu = su; } if (sd != msd) { msd = sd; }
        if (sl != msl) { msl = sl; } if (sr != msr) { msr = sr; }
        if (!SDL_GetGamepadButton(IN.pad, SDL_GAMEPAD_BUTTON_DPAD_UP)) pad_state[ACT_MENU_UP] = su;
        if (!SDL_GetGamepadButton(IN.pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) pad_state[ACT_MENU_DOWN] = sd;
        if (!SDL_GetGamepadButton(IN.pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) pad_state[ACT_MENU_LEFT] = sl;
        if (!SDL_GetGamepadButton(IN.pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) pad_state[ACT_MENU_RIGHT] = sr;
    }
    for (int a = 0; a < ACT_COUNT; a++) {
        bool d = key_state[a] || mouse_state[a] || pad_state[a];
        IN.pressed[a] = d && !IN.down[a];
        IN.released[a] = !d && IN.down[a];
        IN.down[a] = d;
        IN.repeat[a] = IN.pressed[a];
        if (IN.pressed[a]) IN.repeat_t[a] = 0.38f;
        else if (d) {
            IN.repeat_t[a] -= dt;
            if (IN.repeat_t[a] <= 0) { IN.repeat[a] = true; IN.repeat_t[a] = 0.09f; }
        }
    }
    V2 kb = v2((IN.down[ACT_RIGHT] ? 1.0f : 0.0f) - (IN.down[ACT_LEFT] ? 1.0f : 0.0f),
               (IN.down[ACT_DOWN] ? 1.0f : 0.0f) - (IN.down[ACT_UP] ? 1.0f : 0.0f));
    if (v2_len2(stick) > 0.0f) {
        float l = v2_len(stick);
        IN.move = v2_scale(stick, MINF(1.0f, (l - 0.22f) / 0.7f) / l);
    } else {
        IN.move = v2_norm(kb);
    }
    IN.mouse_view = gfx_screen_to_view(IN.mouse_screen.x, IN.mouse_screen.y);
}
