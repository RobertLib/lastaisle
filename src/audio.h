/* LAST AISLE - audio: fully synthesized SFX + music sequencer on SDL3 audio streams. */
#ifndef AUDIO_H
#define AUDIO_H

#include <stdbool.h>

typedef enum {
    /* combat - melee */
    SFX_SWING,          /* light whoosh */
    SFX_SWING_HEAVY,    /* slow heavy whoosh (sledge, axe) */
    SFX_PUNCH,          /* fist hit */
    SFX_HIT_BLUNT,      /* bat/pipe on body: thick thud + crack */
    SFX_HIT_BLADE,      /* knife/machete: wet slice */
    SFX_HIT_METAL,      /* weapon hits wall/metal: clang */
    SFX_GORE,           /* wet squelch / splatter */
    SFX_BONE_CRUNCH,    /* skull crack, execution */
    SFX_STUN,           /* frying-pan BONK, comedic metallic ring */
    /* combat - guns */
    SFX_PISTOL,
    SFX_REVOLVER,
    SFX_SHOTGUN,
    SFX_RIFLE,
    SFX_NAILGUN,        /* pneumatic thwack */
    SFX_EMPTY,          /* dry click */
    SFX_RELOAD,         /* mag out/in clack */
    SFX_SHELL,          /* brass casing tinkle */
    SFX_RICOCHET,
    /* throw / break / boom */
    SFX_THROW,
    SFX_GLASS_BREAK,    /* big window shatter */
    SFX_BOTTLE_BREAK,   /* small glass smash */
    SFX_EXPLOSION,
    SFX_IGNITE,         /* fwoosh of fire catching */
    SFX_FLAME_PUFF,     /* short burst of aerosol flame */
    /* bodies */
    SFX_HURT_PLAYER,    /* player takes damage: punchy low thump + distorted grunt */
    SFX_HURT_NPC,       /* npc pain grunt (pitch varied by caller) */
    SFX_DEATH,          /* death gurgle / exhale */
    SFX_BODYFALL,       /* body hits floor */
    SFX_FOOTSTEP,       /* soft step on hard floor */
    SFX_ALERT,          /* "!" spotted stinger */
    SFX_SURRENDER,      /* "please don't" - nervous whimper / short descending blip */
    /* world */
    SFX_DOOR_SLAM,
    SFX_DOOR_CREAK,
    SFX_CART_HIT,       /* shopping cart crash (metal rattle) */
    SFX_CART_ROLL,      /* one rattle tick while pushing */
    SFX_LOOT_RUMMAGE,   /* rummaging through shelf (short, repeatable) */
    SFX_LOOT_FOUND,     /* little bright pling */
    SFX_PICKUP,         /* pick up item */
    SFX_PICKUP_WEAPON,  /* pick up weapon: metallic grab */
    SFX_LIST_TICK,      /* shopping list item obtained: satisfying pen-tick + chime */
    SFX_LIST_DONE,      /* whole list complete: triumphant short arpeggio */
    SFX_CRAFT,          /* duct tape rip + clank */
    SFX_HEAL,           /* bandage / pills: soft rising tone */
    SFX_EXECUTE,        /* brutal finisher: heavy thud + crunch */
    SFX_COMBO,          /* combo counter up: short synth blip (caller raises pitch) */
    SFX_VAN_DOOR,       /* sliding van door */
    SFX_ENGINE,         /* van engine starting / driving off (2-3 s) */
    /* ui */
    SFX_UI_MOVE,
    SFX_UI_SELECT,
    SFX_UI_BACK,
    SFX_UI_ERROR,
    SFX_TYPE,           /* typewriter tick for story text */
    SFX_RADIO,          /* short radio static burst */
    SFX_LEVEL_CLEAR,    /* level complete jingle (~2 s) */
    SFX_GAME_OVER,      /* run over sting (~2.5 s) */
    SFX_HEARTBEAT,      /* single low heartbeat (looped by caller at low HP) */
    SFX_PERK,           /* perk chosen: shimmering chord */
    SFX_COUNT
} SfxId;

typedef enum {
    LOOP_CHAINSAW_IDLE, /* two-stroke idle putter */
    LOOP_CHAINSAW_CUT,  /* screaming cut */
    LOOP_FIRE,          /* crackling fire */
    LOOP_RAIN,          /* steady rain on roof */
    LOOP_WIND,          /* low outdoor wind ambience */
    LOOP_HUM,           /* creepy indoor electrical hum / drone */
    LOOP_COUNT
} LoopId;

typedef enum {
    MUS_NONE,
    MUS_MENU,       /* title theme: moody darksynth, ~96 bpm */
    MUS_LEVEL_A,    /* driving darksynth, ~118 bpm */
    MUS_LEVEL_B,    /* hypnotic, detuned, ~105 bpm */
    MUS_LEVEL_C,    /* fast industrial, ~132 bpm */
    MUS_BOSS,       /* boss fight, ~140 bpm */
    MUS_SAFEHOUSE,  /* calm warm ambient between levels, ~80 bpm */
    MUS_ENDING,     /* melancholic -> hopeful, credits */
    MUS_COUNT
} MusicId;

bool audio_init(void);                 /* SDL_INIT_AUDIO must already be initialised */
void audio_shutdown(void);

/* One-shot sound. vol 0..1, pan -1 (left) .. 1 (right), pitch multiplier (1 = normal). */
void audio_play(SfxId id, float vol, float pan, float pitch);

/* Continuous loops: call every frame with the desired volume (0 = silent).
   The mixer smooths volume/pitch changes and stops the voice after it fades out. */
void audio_loop(LoopId id, float vol, float pitch);

/* Switch music with a short crossfade (MUS_NONE fades out). Same id = no-op. */
void audio_music(MusicId id);

void audio_set_volumes(float master, float music, float sfx);   /* 0..1 each */

/* 0 = clear, 1 = heavily low-pass filtered (pause menu, death, inventory). Smoothed. */
void audio_set_muffle(float amount);

/* Global playback-rate bend for slow motion: 1 = normal, 0.5 = half speed & pitch. Smoothed. */
void audio_set_timescale(float scale);

#endif
