/* LAST AISLE - input mapping (keyboard + mouse + gamepad) */
#ifndef INPUT_H
#define INPUT_H

#include "common.h"

typedef enum {
    ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT,
    ACT_ATTACK, ACT_THROW, ACT_INTERACT, ACT_EXECUTE, ACT_LOOK,
    ACT_INVENTORY, ACT_HEAL, ACT_SWAP, ACT_RELOAD, ACT_PAUSE,
    ACT_CONFIRM, ACT_BACK, ACT_MENU_UP, ACT_MENU_DOWN, ACT_MENU_LEFT, ACT_MENU_RIGHT,
    ACT_COUNT
} Action;

typedef struct {
    bool down[ACT_COUNT], pressed[ACT_COUNT], released[ACT_COUNT];
    float repeat_t[ACT_COUNT];
    bool repeat[ACT_COUNT];      /* pressed or auto-repeat (menus) */
    V2 mouse_screen;             /* window points */
    V2 mouse_view;               /* 480x270 coords */
    bool mouse_moved;
    bool click, rclick;          /* mouse button pressed this frame */
    float wheel;
    V2 move;                     /* normalised movement intent */
    V2 aim_stick;                /* right stick, if any */
    bool pad_active;             /* last input came from the gamepad */
    SDL_Gamepad *pad;
    bool quit;
    bool any_pressed;
    bool fullscreen_toggle;
} Input;

extern Input IN;

void input_init(void);
void input_begin_frame(void);
void input_event(const SDL_Event *e);
void input_update(float dt);
void input_clear(void);   /* forget held state (after scene changes) */

#endif
