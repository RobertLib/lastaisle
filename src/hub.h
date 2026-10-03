/* LAST AISLE - the Greenhouse: the camp you walk round before every store */
#ifndef HUB_H
#define HUB_H

#include "world.h"

/* what this evening has used up (Run.hub_done): a cleared store starts a new evening */
#define HD_TRAINED(stat) (1u << (stat))          /* bits 0-2: strength, aim, fitness */
#define HD_CONT(k)       (1u << (4 + (k)))       /* bits 4-9: the yard's crates and piles, searched */
#define HD_GIFT(n)       (1u << (12 + (n)))      /* bits 12-17: Rosa..Marta gave you something */
#define HD_HEARD(n)      (1u << (20 + (n)))      /* bits 20-25: Rosa..Marta said their piece */
#define HD_CREW(k)       (1u << (26 + (k)))      /* bits 26-30: the crew said theirs */

/* the yard as gen_hub laid it out (pixel positions) */
#define HUB_TARGETS 8
#define HUB_FLAGS 8
typedef struct {
    V2 bench, workbench, locker, range_line;
    V2 flags[HUB_FLAGS];          /* the running course, in order: [0] is the start and the finish */
    int flag_prop[HUB_FLAGS];
    int nflags;
    int target_prop[HUB_TARGETS]; /* props kept back for the range's bottles and boards */
    SDL_FRect range;              /* downrange: where the boards pop up */
    V2 planks[2];                 /* the planks the bottles stand on */
    V2 post[AR_COUNT];            /* where each of the camp's people hangs about (the crew: their place on the watch) */
    V2 crew_van[MAX_CREW];        /* where the crew wait when they're coming along tonight */
    V2 grave[MAX_CREW];           /* where the crew who died lie, under the trees */
    bool theo_sick;               /* the evening before Medimart: Theo's in bed with the fever */
} HubLayout;
extern HubLayout HUB;

void hub_enter(void);                         /* scene SC_HUB: lay out the yard, put the player in it */
void hub_update(float dt);
void hub_draw(void);
void hub_sync_run(void);                      /* what the player has on them now -> RUN (before a save or the van) */
bool hub_interact(Actor *p);                  /* [E]: talk, train, tinker, drive - true if it did something */
bool hub_prompt(char *buf, int n, V2 *pos);   /* the [E] prompt for that, if there is one here */
void hub_npc_update(Actor *a, int idx, float dt);
bool hub_near_locker(void);                   /* the bag screen shows the locker beside the bag */
const Stack *hub_packed(void);                /* the weapon packed for the run (nobody carries one round the camp) */
void hub_bot(float dt);                       /* --autoplay: the QA autopilot spends the evening */

#endif
