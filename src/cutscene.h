/* LAST AISLE - cutscenes: the story told in moving pictures */
#ifndef CUTSCENE_H
#define CUTSCENE_H

typedef enum {
    CUT_INTRO,    /* the grid goes dark ... today, that's you */
    CUT_DRIVE,    /* the van on the road to the next store */
    CUT_HOME,     /* back at the Greenhouse after a store */
    CUT_ENDING,   /* the seeds come home, spring comes */
    CUT_COUNT
} CutsceneId;

/* plays a cutscene, then moves on: intro -> briefing, drive -> the store, home -> greenhouse, ending -> credits */
void cutscene_start(CutsceneId id, int level);
void cutscene_seek(float t);   /* debug: jump ahead (seconds from the start) */
void cutscene_update(float dt);
void cutscene_draw(void);

#endif
