/* LAST AISLE - audio: the soundtrack, as data.
   Pattern language is documented at the top of audio_synth.c.
   One token = one 16th note; 16 steps per bar. Chord tokens are one per bar.

   MUS_MENU       D minor     96 bpm  "Closing Time"      moody title theme, half-time
   MUS_LEVEL_A    E minor    118 bpm  "Aisle Five"        driving darksynth
   MUS_LEVEL_B    F minor    105 bpm  "Fluorescent"       hypnotic, detuned, swung
   MUS_LEVEL_C    A minor    132 bpm  "Loading Dock"      fast industrial
   MUS_BOSS       E phrygian 140 bpm  "Manager"           boss, b2 menace + phrygian dominant
   MUS_SAFEHOUSE  C / A min   80 bpm  "Back Room"         warm, sad, lo-fi keys
   MUS_ENDING     A min -> C  88 bpm  "After Hours"       title motif returns, minor -> major */
#include "audio_internal.h"

/* ======================================================================== */
/* instruments                                                               */
/* ======================================================================== */
static const AuPatch P_BASS = {
    .w1 = AW_SAW, .w2 = AW_SQUARE, .mix2 = 0.5f, .det2 = 7, .uni = 1, .sub = 0.45f,
    .cutoff = 150, .reso = 0.3f, .fenv = 3.4f, .keytrk = 0.3f, .fa = 0.001f, .fd = 0.22f, .fs = 0.12f, .fr = 0.1f,
    .aa = 0.002f, .ad = 0.4f, .as = 0.8f, .ar = 0.06f, .glide = 0.07f, .drive = 1.6f, .gain = 0.25f, .mono = 1, .poles4 = 1 };
static const AuPatch P_BASS_PLUCK = {
    .w1 = AW_SAW, .w2 = AW_SAW, .mix2 = 0.7f, .det2 = 9, .uni = 1, .sub = 0.4f,
    .cutoff = 170, .reso = 0.35f, .fenv = 3.8f, .keytrk = 0.3f, .fa = 0.001f, .fd = 0.11f, .fs = 0.05f, .fr = 0.05f,
    .aa = 0.001f, .ad = 0.18f, .as = 0.4f, .ar = 0.04f, .glide = 0.05f, .drive = 2.0f, .gain = 0.5f, .mono = 1, .poles4 = 1 };
static const AuPatch P_BASS_DIST = {
    .w1 = AW_SAW, .w2 = AW_SQUARE, .mix2 = 0.8f, .det2 = 12, .uni = 1, .sub = 0.5f,
    .cutoff = 300, .reso = 0.45f, .fenv = 3.0f, .keytrk = 0.3f, .fa = 0.001f, .fd = 0.13f, .fs = 0.2f, .fr = 0.05f,
    .aa = 0.001f, .ad = 0.25f, .as = 0.6f, .ar = 0.04f, .glide = 0.05f, .drive = 5.0f, .gain = 0.36f, .mono = 1, .poles4 = 1 };
static const AuPatch P_BASS_SUB = {
    .w1 = AW_SINE, .w2 = AW_TRI, .mix2 = 0.35f, .uni = 1,
    .cutoff = 1100, .reso = 0.0f, .fenv = 0.5f, .fa = 0.002f, .fd = 0.3f, .fs = 0.3f, .fr = 0.3f,
    .aa = 0.01f, .ad = 0.6f, .as = 0.75f, .ar = 0.25f, .glide = 0.08f, .drive = 1.2f, .gain = 0.3f, .mono = 1 };
static const AuPatch P_BASS_WOOZY = {
    .w1 = AW_SAW, .w2 = AW_SQUARE, .mix2 = 0.6f, .det2 = 6, .uni = 1, .sub = 0.4f,
    .cutoff = 210, .reso = 0.35f, .fenv = 2.8f, .keytrk = 0.3f, .fa = 0.002f, .fd = 0.25f, .fs = 0.15f, .fr = 0.1f,
    .aa = 0.003f, .ad = 0.5f, .as = 0.7f, .ar = 0.1f, .glide = 0.1f, .wow = 9, .drive = 1.8f, .gain = 0.25f, .mono = 1, .poles4 = 1 };

static const AuPatch P_LEAD_SAW = {
    .w1 = AW_SAW, .uni = 3, .uni_det = 16, .uni_width = 0.35f, .w2 = AW_SAW, .semi2 = 12, .det2 = 4, .mix2 = 0.22f,
    .cutoff = 1700, .reso = 0.22f, .fenv = 1.6f, .keytrk = 0.5f, .fa = 0.005f, .fd = 0.35f, .fs = 0.45f, .fr = 0.3f,
    .aa = 0.006f, .ad = 0.4f, .as = 0.85f, .ar = 0.28f, .glide = 0.08f, .vib = 0.22f, .vib_rate = 5.6f, .vib_delay = 0.3f,
    .drive = 1.3f, .gain = 0.3f, .mono = 1 };
static const AuPatch P_LEAD_PWM = {
    .w1 = AW_PULSE, .pw = 0.5f, .pwm = 0.6f, .pwm_rate = 0.7f, .uni = 1, .w2 = AW_PULSE, .det2 = 9, .mix2 = 0.7f,
    .cutoff = 1500, .reso = 0.2f, .fenv = 1.2f, .keytrk = 0.4f, .fa = 0.005f, .fd = 0.4f, .fs = 0.5f, .fr = 0.3f,
    .aa = 0.012f, .ad = 0.4f, .as = 0.85f, .ar = 0.35f, .glide = 0.12f, .vib = 0.3f, .vib_rate = 4.8f, .vib_delay = 0.25f,
    .wow = 16, .drive = 1.2f, .gain = 0.28f, .mono = 1 };
static const AuPatch P_LEAD_SCREAM = {
    .w1 = AW_SAW, .uni = 2, .uni_det = 14, .uni_width = 0.3f, .w2 = AW_SQUARE, .det2 = -7, .mix2 = 0.6f,
    .cutoff = 2400, .reso = 0.4f, .fenv = 1.2f, .keytrk = 0.4f, .fa = 0.002f, .fd = 0.25f, .fs = 0.5f, .fr = 0.2f,
    .aa = 0.004f, .ad = 0.3f, .as = 0.9f, .ar = 0.2f, .glide = 0.05f, .vib = 0.35f, .vib_rate = 6.5f, .vib_delay = 0.18f,
    .drive = 3.0f, .gain = 0.2f, .mono = 1 };
static const AuPatch P_LEAD_SOFT = {
    .w1 = AW_TRI, .uni = 1, .w2 = AW_SINE, .semi2 = 12, .mix2 = 0.2f, .noise = 0.015f,
    .cutoff = 3000, .fenv = 0.4f, .keytrk = 0.3f, .fa = 0.01f, .fd = 0.4f, .fs = 0.6f, .fr = 0.4f,
    .aa = 0.035f, .ad = 0.5f, .as = 0.8f, .ar = 0.4f, .glide = 0.09f, .vib = 0.18f, .vib_rate = 5.0f, .vib_delay = 0.3f,
    .wow = 4, .gain = 0.42f, .mono = 1 };
static const AuPatch P_SIREN = {
    .w1 = AW_SQUARE, .uni = 1, .w2 = AW_SAW, .det2 = 10, .mix2 = 0.5f,
    .cutoff = 2200, .reso = 0.3f, .fenv = 0.3f, .fa = 0.01f, .fd = 0.5f, .fs = 0.8f, .fr = 0.3f,
    .aa = 0.03f, .ad = 0.5f, .as = 0.8f, .ar = 0.3f, .glide = 0.18f, .vib = 0.5f, .vib_rate = 7.0f, .vib_delay = 0.1f,
    .drive = 2.0f, .gain = 0.16f, .mono = 1 };

static const AuPatch P_ARP = {
    .w1 = AW_SAW, .uni = 1, .w2 = AW_SQUARE, .det2 = 6, .mix2 = 0.4f,
    .cutoff = 420, .reso = 0.35f, .fenv = 3.6f, .keytrk = 0.4f, .fa = 0.001f, .fd = 0.13f, .fs = 0.0f, .fr = 0.15f,
    .aa = 0.001f, .ad = 0.28f, .as = 0.0f, .ar = 0.12f, .gain = 0.28f };
static const AuPatch P_ARP_BRIGHT = {
    .w1 = AW_PULSE, .pw = 0.28f, .uni = 1, .w2 = AW_SAW, .det2 = 8, .mix2 = 0.5f,
    .cutoff = 900, .reso = 0.3f, .fenv = 2.8f, .keytrk = 0.4f, .fa = 0.001f, .fd = 0.09f, .fs = 0.0f, .fr = 0.08f,
    .aa = 0.001f, .ad = 0.18f, .as = 0.0f, .ar = 0.08f, .gain = 0.24f };
static const AuPatch P_PLUCK_SOFT = {
    .w1 = AW_TRI, .uni = 1, .w2 = AW_SINE, .semi2 = 12, .mix2 = 0.3f,
    .cutoff = 1800, .fenv = 1.0f, .fd = 0.2f, .fs = 0.0f, .fr = 0.3f,
    .aa = 0.002f, .ad = 0.45f, .as = 0.0f, .ar = 0.3f, .gain = 0.3f };

static const AuPatch P_PAD = {
    .w1 = AW_SAW, .uni = 5, .uni_det = 26, .uni_width = 0.85f, .w2 = AW_SAW, .semi2 = -12, .mix2 = 0.25f,
    .cutoff = 1100, .reso = 0.12f, .fenv = 0.7f, .keytrk = 0.2f, .fa = 1.2f, .fd = 2.0f, .fs = 0.5f, .fr = 1.5f,
    .aa = 0.45f, .ad = 1.0f, .as = 0.85f, .ar = 1.4f, .flfo = 0.35f, .flfo_rate = 0.13f, .gain = 0.1f };
static const AuPatch P_PAD_WARM = {
    .w1 = AW_SAW, .uni = 3, .uni_det = 14, .uni_width = 0.7f, .w2 = AW_TRI, .mix2 = 0.6f,
    .cutoff = 800, .reso = 0.1f, .fenv = 0.5f, .fa = 1.5f, .fd = 2.0f, .fs = 0.6f, .fr = 2.0f,
    .aa = 0.9f, .ad = 1.0f, .as = 0.85f, .ar = 2.0f, .flfo = 0.3f, .flfo_rate = 0.09f, .wow = 6, .gain = 0.12f };
static const AuPatch P_PAD_DARK = {
    .w1 = AW_SAW, .uni = 4, .uni_det = 20, .uni_width = 0.8f, .w2 = AW_SQUARE, .semi2 = -12, .mix2 = 0.35f,
    .cutoff = 650, .reso = 0.35f, .fenv = 0.6f, .fa = 1.0f, .fd = 2.0f, .fs = 0.5f, .fr = 1.0f,
    .aa = 0.3f, .ad = 1.0f, .as = 0.85f, .ar = 1.0f, .flfo = 0.7f, .flfo_rate = 0.21f, .drive = 1.4f, .gain = 0.1f };
static const AuPatch P_DRONE = {
    .w1 = AW_SAW, .uni = 2, .uni_det = 10, .uni_width = 0.5f, .sub = 0.5f,
    .cutoff = 260, .reso = 0.3f, .flfo = 1.0f, .flfo_rate = 0.07f, .fs = 0.0f,
    .aa = 2.0f, .ad = 1.0f, .as = 1.0f, .ar = 2.5f, .wow = 5, .gain = 0.25f };
static const AuPatch P_STAB = {
    .w1 = AW_SAW, .uni = 3, .uni_det = 18, .uni_width = 0.6f, .w2 = AW_SQUARE, .mix2 = 0.4f,
    .cutoff = 700, .reso = 0.25f, .fenv = 2.8f, .fa = 0.001f, .fd = 0.14f, .fs = 0.15f, .fr = 0.1f,
    .aa = 0.002f, .ad = 0.3f, .as = 0.25f, .ar = 0.12f, .drive = 1.8f, .gain = 0.11f };

static const AuPatch P_EPIANO = {
    .w1 = AW_SINE, .fm_ratio = 1.0f, .fm_index = 2.4f, .fm_sus = 0.3f, .fm_decay = 0.3f, .uni = 1,
    .w2 = AW_SINE, .semi2 = 24, .mix2 = 0.05f,
    .cutoff = 5000, .fs = 1.0f,
    .aa = 0.002f, .ad = 2.2f, .as = 0.0f, .ar = 0.35f, .gain = 0.2f };
static const AuPatch P_BELL = {
    .w1 = AW_SINE, .fm_ratio = 3.5f, .fm_index = 3.0f, .fm_sus = 0.15f, .fm_decay = 0.22f, .uni = 1,
    .cutoff = 9000, .fs = 1.0f,
    .aa = 0.001f, .ad = 1.6f, .as = 0.0f, .ar = 0.9f, .gain = 0.35f };
static const AuPatch P_KEYS_WOOZY = {
    .w1 = AW_SAW, .uni = 1, .w2 = AW_TRI, .det2 = 7, .mix2 = 0.8f,
    .cutoff = 800, .reso = 0.25f, .fenv = 2.2f, .keytrk = 0.3f, .fa = 0.001f, .fd = 0.25f, .fs = 0.1f, .fr = 0.2f,
    .aa = 0.002f, .ad = 0.55f, .as = 0.15f, .ar = 0.3f, .wow = 22, .gain = 0.25f };

/* common drum lane snippets */
#define CRASH8  "|x:X(.)127"
#define CRASH4  "|x:X(.)63"
#define RISE4   "|n:(.)48 X(.)15|z:(.)56 x(.)7"
#define RISE8   "|n:(.)112 X(.)15|z:(.)120 x(.)7"
/* tom fills in the last bar of an 8-bar section (lanes loop independently) */
#define FILL8   "|m:(.)120 x.x. ....|t:(.)124 x.xx"
#define FILL8B  "|m:(.)116 x.x. x... ....|t:(.)122 x.x. xX"

/* ======================================================================== */
/* MENU - D minor, 96 bpm                                                    */
/* ======================================================================== */
static const char MN_CH_A[]  = "Dm Bb F C";
static const char MN_CH_A2[] = "(Dm Bb F C)2";
static const char MN_CH_B2[] = "(Gm Dm Bb A)2";
static const char MN_CH_BR[] = "(Bbmaj7 Am7 Gm7 Asus4_A)2";
static const char MN_CH_T[]  = "Dm Bb Gm A";

#define MN_DR_A \
    "k:X... ..x. ..x. .... X... ..x. ..x. .x..|" \
    "s:.... .... X... .... .... .... X... ....|" \
    "h:x.X. x.X. x.X. x.X. x.X. x.X. x.X. x...|" \
    "o:.... .... .... .... .... .... .... ..x."
#define MN_DR_B \
    "k:X... ..x. ..x. .... X... ..x. ..x. .x..|" \
    "s:.... .... X... .... .... .... X... ....|" \
    "c:.... .... X... .... .... .... X... ....|" \
    "h:xoXo xoXo xoXo xoXo xoXo xoXo xoXo xo..|" \
    "o:.... .... .... .... .... .... .... ..x."
static const char MN_DR_A8[] = MN_DR_A CRASH8;
static const char MN_DR_B8[] = MN_DR_B CRASH8 FILL8;
static const char MN_DR_IN2[] = "k:(.)64" RISE4;
static const char MN_DR_BRK[] = "b:X(.)127|k:(.)96 X... .... .... .... X... .... X... X..." RISE8;
static const char MN_DR_T[] =
    "k:X... ..x. ..x. .... X... ..x. ..x. .... X... ..x. ..x. .... X... .... X... ....|"
    "s:.... .... X... .... .... .... X... .... .... .... X... .... .... .... X.x. xxXX|"
    "m:(....)12 ..x. .x.. .... ....|"
    "t:(....)12 .... ..x. .... ....|"
    "h:(x.X.)12 x.x. x.x. .... ....";

static const char MN_BASS_A[] = "1!:3 1:3 1':2 1:3 1:3 3:1 1':1";
static const char MN_BASS_B[] = "1!:3 1:3 1':2 1:2 3:2 2':2 1':2";
static const char MN_BASS_BR[] = "1:12 3,:4";
static const char MN_ARP[]  = "1 3 4 2 3 5 4 3 1 3 4 2 3 5 6 5";
static const char MN_PAD[]  = "x:16";
static const char MN_PADX[] = "X:16";
static const char MN_BELL[] = "5:3 4:3 3:4 .:6";
static const char MN_DRONE[] = "1:16";
static const char MN_LEAD_A[] =
    "A4:3 D5:3 F5:4 E5:2 D5:2 C5:2  D5:10 .:2 C5:2 Bb4:2  A4:3 C5:3 F5:4 E5:2 F5:2 G5:2  E5:8 ~D5:4 C5:4 "
    "A4:3 D5:3 F5:4 E5:2 D5:2 C5:2  D5:6 F5:4 G5:2 A5:4  C6:3 A5:3 F5:4 G5:2 A5:2 G5:2  G5:4 E5:4 ~D5:6 .:2";
static const char MN_LEAD_B[] =
    "Bb5:6 A5:2 G5:4 D5:4  F5:6 E5:2 D5:4 A4:4  D5:4 F5:4 Bb5:4 A5:2 G5:2  E5:6 C#5:2 ~E5:4 A5:4 "
    "Bb5:6 A5:2 G5:4 Bb5:4  A5:6 G5:2 F5:4 D5:4  F5:6 G5:2 F5:4 D5:4  C#5:8 E5:4 .:4";
static const char MN_LEAD_T[] =
    "A4:3 D5:3 F5:4 E5:2 D5:2 C5:2  D5:16  Bb4:3 D5:3 G5:4 F5:2 E5:2 D5:2  C#5:8 E5:8";

static const AuSection menu_secs[] = {
    { 4, MN_CH_A,  { NULL, NULL, NULL, NULL, MN_PAD, NULL, MN_DRONE, NULL }, 0.12f, 0.45f, 0 },
    { 4, MN_CH_A,  { MN_DR_IN2, NULL, NULL, MN_ARP, MN_PAD, NULL, MN_DRONE, NULL }, 0.45f, 1.0f, 0 },
    { 8, MN_CH_A2, { MN_DR_A8, MN_BASS_A, NULL, MN_ARP, MN_PAD, NULL, NULL, NULL }, 0, 0, 0 },
    { 8, MN_CH_A2, { MN_DR_A8, MN_BASS_A, MN_LEAD_A, MN_ARP, MN_PAD, NULL, NULL, NULL }, 0, 0, 0 },
    { 8, MN_CH_B2, { MN_DR_B8, MN_BASS_B, MN_LEAD_B, MN_ARP, MN_PAD, MN_BELL, NULL, NULL }, 0, 0, 0 },
    { 8, MN_CH_BR, { MN_DR_BRK, MN_BASS_BR, NULL, MN_ARP, MN_PADX, MN_BELL, MN_DRONE, NULL }, 0.35f, 0.9f, 0 },
    { 8, MN_CH_A2, { MN_DR_B8, MN_BASS_A, MN_LEAD_A, MN_ARP, MN_PAD, MN_BELL, NULL, MN_LEAD_A }, 0, 0, 0 },
    { 4, MN_CH_T,  { MN_DR_T, MN_BASS_B, MN_LEAD_T, MN_ARP, MN_PAD, NULL, NULL, NULL }, 0, 0, 0 },
};
static const AuSong song_menu = {
    .name = "menu", .bpm = 96, .swing = 0.0f, .kit = AU_KIT_SYNTHWAVE,
    .patch  = { NULL, &P_BASS, &P_LEAD_SAW, &P_ARP, &P_PAD, &P_BELL, &P_DRONE, &P_LEAD_SOFT },
    .center = { 0, 38, 0, 57, 65, 79, 38, 0 },
    .xp     = { 0, 0, 0, 0, 0, 0, 0, -12 },
    .gain   = { 0.76f, 0.56f, 0.57f, 0.62f, 0.95f, 0.6f, 0.42f, 0.28f },
    .pan    = { 0, 0, 0.05f, -0.25f, 0, 0.3f, 0, -0.1f },
    .dsend  = { 0, 0, 0.35f, 0.45f, 0.05f, 0.4f, 0, 0.2f },
    .rsend  = { 0.12f, 0.02f, 0.25f, 0.25f, 0.35f, 0.45f, 0.3f, 0.3f },
    .duck   = { 0, 0.2f, 0, 0.2f, 0.35f, 0, 0.3f, 0 },
    .delay_beats = 0.75f, .delay_fb = 0.42f, .delay_wow = 0.6f,
    .rev_decay = 3.2f, .rev_damp = 5500.0f, .duck_time = 0.16f, .master = 0.55f,
    .sec = menu_secs, .nsec = 8, .loop_to = 2 };

/* ======================================================================== */
/* LEVEL A - E minor, 118 bpm, driving                                       */
/* ======================================================================== */
static const char LA_CH_A[]  = "Em Em C D";
static const char LA_CH_A2[] = "(Em Em C D)2";
static const char LA_CH_B2[] = "(Am C Em B)2";
static const char LA_CH_BR[] = "(C D Bm Em)2";
static const char LA_CH_T[]  = "Em C D B";

#define LA_DR_A \
    "k:X... x... X... x...|" \
    "s:.... X... .... X...|" \
    "h:oo.o oo.o oo.o oo.o|" \
    "o:..x. ..x. ..x. ..x."
#define LA_DR_B \
    "k:X... x... X... x... X... x... X... x.x.|" \
    "s:.... X... .... X... .... X... .... X..o|" \
    "c:.... X... .... X... .... X... .... X...|" \
    "h:oo.o oo.o oo.o oo.o oo.o oo.o oo.o ooXo|" \
    "o:..x. ..x. ..x. ..x. ..x. ..x. ..x. ...."
static const char LA_DR_A8[] = LA_DR_A CRASH8 FILL8;
static const char LA_DR_B8[] = LA_DR_B CRASH8 FILL8B;
static const char LA_DR_IN[] = "k:(X... x... X... x...)4|h:(oo.o oo.o oo.o oo.o)4" RISE4;
static const char LA_DR_BRK[] =
    "b:X(.)127|k:(X... .... .... ....)7 X... X... X... X...|"
    "s:(.)112 x.x. x.x. xxxx XXXX|h:(..o. ..o. ..o. ..o.)8" RISE8;
static const char LA_DR_T[] =
    "k:(X... x... X... x...)3 X... .... X... X...|"
    "s:(.... X... .... X...)3 .... X.x. xxXx XXXX|"
    "h:(oo.o oo.o oo.o oo.o)3 x.x. x.x. .... ....|"
    "o:(..x. ..x. ..x. ..x.)3 .... .... .... ....";

static const char LA_BASS[] = "(1! 1 1' 1)4 (1! 1 1' 1)3 1! 1' 3 1'";
static const char LA_BASS_BR[] = "1:16";
static const char LA_ARP[] = "1 2 3 4 3 2 1 2 3 4 3 2 1 2 3 4";
static const char LA_PAD[] = "x:16";
static const char LA_STAB[] = ".:2 x:1 .:3 x:1 .:1 x:2 .:2 x:1 .:3";
static const char LA_RIFF[] =
    "E5:2 E5 B4 E5:2 G5:2 F#5:2 E5 D5 E5:2 B4:2 "
    "E5:2 E5 B4 E5:2 G5:2 A5:2 G5 F#5 G5:2 B5:2 "
    "E5:2 E5 C5 E5:2 G5:2 F#5:2 E5 D5 E5:2 C5:2 "
    "F#5:2 F#5 D5 F#5:2 A5:2 G5:2 F#5 E5 D5:4";
static const char LA_MEL[] =
    "B5:8 A5:4 G5:4  F#5:4 G5:4 E5:8  G5:8 E5:4 D5:4  F#5:6 G5:2 A5:8 "
    "B5:8 A5:4 G5:4  F#5:4 G5:4 B5:8  C6:6 B5:2 A5:4 G5:4  F#5:8 D5:8";
static const char LA_BRK_MEL[] =
    "G5:12 E5:4  F#5:12 A5:4  B5:8 F#5:8  G5:16 "
    "E6:8 D6:4 C6:4  D6:8 A5:8  B5:8 D6:4 F#6:4  E6:16";
static const char LA_LEADB[] =
    "C6:6 B5:2 A5:8  G5:6 A5:2 E5:8  B5:6 A5:2 G5:4 E5:4  D#5:8 F#5:4 B5:4 "
    "C6:6 D6:2 E6:8  D6:6 C6:2 G5:8  B5:6 C6:2 B5:4 G5:4  F#5:8 D#5:4 B4:4";
static const char LA_LEAD_T[] = "E5:4 G5:4 B5:8  C6:4 B5:4 G5:8  A5:4 F#5:4 D5:8  D#5:8 F#5:8";

static const AuSection levela_secs[] = {
    { 4, LA_CH_A,  { LA_DR_IN, LA_BASS, NULL, LA_ARP, NULL, NULL, NULL, NULL }, 0.2f, 0.85f, 0 },
    { 8, LA_CH_A2, { LA_DR_A8, LA_BASS, NULL, LA_ARP, LA_PAD, NULL, NULL, NULL }, 0, 0, 0 },
    { 8, LA_CH_A2, { LA_DR_A8, LA_BASS, LA_RIFF, LA_ARP, LA_PAD, NULL, NULL, NULL }, 0, 0, 0 },
    { 8, LA_CH_B2, { LA_DR_B8, LA_BASS, LA_LEADB, LA_ARP, LA_PAD, LA_STAB, NULL, NULL }, 0, 0, 0 },
    { 8, LA_CH_BR, { LA_DR_BRK, LA_BASS_BR, LA_BRK_MEL, LA_ARP, LA_PAD, NULL, NULL, NULL }, 0.3f, 1.0f, 0 },
    { 8, LA_CH_A2, { LA_DR_B8, LA_BASS, LA_MEL, LA_ARP, LA_PAD, NULL, LA_MEL, NULL }, 0, 0, 0 },
    { 8, LA_CH_B2, { LA_DR_B8, LA_BASS, LA_LEADB, LA_ARP, LA_PAD, LA_STAB, LA_LEADB, NULL }, 0, 0, 0 },
    { 4, LA_CH_T,  { LA_DR_T, LA_BASS, LA_LEAD_T, LA_ARP, LA_PAD, LA_STAB, NULL, NULL }, 0, 0, 0 },
};
static const AuSong song_levela = {
    .name = "level_a", .bpm = 118, .swing = 0.0f, .kit = AU_KIT_SYNTHWAVE,
    .patch  = { NULL, &P_BASS_PLUCK, &P_LEAD_SAW, &P_ARP, &P_PAD, &P_STAB, &P_LEAD_SOFT, NULL },
    .center = { 0, 40, 0, 64, 64, 64, 0, 0 },
    .xp     = { 0, 0, 0, 0, 0, 0, -12, 0 },
    .gain   = { 0.72f, 0.54f, 0.64f, 0.71f, 1.0f, 1.1f, 0.28f, 0 },
    .pan    = { 0, 0, 0, 0.3f, 0, -0.2f, 0.1f, 0 },
    .dsend  = { 0, 0, 0.3f, 0.4f, 0.05f, 0.2f, 0.15f, 0 },
    .rsend  = { 0.1f, 0.0f, 0.22f, 0.2f, 0.3f, 0.2f, 0.25f, 0 },
    .duck   = { 0, 0.35f, 0, 0.3f, 0.5f, 0.2f, 0, 0 },
    .delay_beats = 0.75f, .delay_fb = 0.35f, .delay_wow = 0.25f,
    .rev_decay = 2.4f, .rev_damp = 6000.0f, .duck_time = 0.11f, .master = 0.5f,
    .sec = levela_secs, .nsec = 8, .loop_to = 1 };

/* ======================================================================== */
/* LEVEL B - F minor, 105 bpm, hypnotic & detuned                            */
/* ======================================================================== */
static const char LB_CH_A2[] = "(Fm9 Fm9 Dbmaj7 Dbmaj7)2";
static const char LB_CH_A[]  = "Fm9 Fm9 Dbmaj7 Dbmaj7";
static const char LB_CH_B2[] = "(Bbm9 Ab Gbmaj7 Csus4_C)2";
static const char LB_CH_O[]  = "Fm9 Fm9 Dbmaj7 Csus4_C";

#define LB_DR \
    "k:X... ...x ..x. .... X... ...x .... ..x.|" \
    "s:.... .... X... .... .... .... X... ....|" \
    "r:...x ..x. ...x .... ...x ..x. .... x...|" \
    "y:oxox oxox oxox oxox oxox oxox oxox oxox|" \
    "o:.... .... .... ..x. .... .... .... ..x."
static const char LB_DR8[] = LB_DR CRASH8;
static const char LB_DR_MIN[] = "k:(X... .... .... ....)4|y:(o.o. o.o. o.o. o.o.)4" RISE4;
static const char LB_DR_OUT[] = LB_DR;

static const char LB_OST_A[] = "C5! Ab4 Eb5 C5! Ab4 F5 C5! Ab4 Eb5 C5! Ab4 G5 C5! Ab4 F5 Eb5";
static const char LB_OST_B[] = "1! 3 5 1! 3 5 1! 3 5 1! 3 5 1! 3 4 3";
static const char LB_BASS[] = "1!:3 1:3 1':2 .:2 1:2 ~3,:2 ~1:2";
static const char LB_PAD[] = "x:16";
static const char LB_DRONE[] = "1:16";
static const char LB_BELL[] = ".:6 5:2 .:4 4:4";
static const char LB_ARP[] = "1 2 3 4 5 4 3 2 1 2 3 4 5 6 5 4";
static const char LB_LEAD_A[] =
    "C5:6 ~Eb5:2 F5:8  Eb5:4 C5:4 Ab4:8  F4:6 ~Ab4:2 C5:8  Bb4:6 C5:2 ~Ab4:8 "
    "C5:6 ~Eb5:2 F5:6 G5:2  Ab5:8 G5:4 Eb5:4  F5:6 ~Eb5:2 C5:8  Db5:4 C5:4 ~Ab4:8";
static const char LB_LEAD_B[] =
    "Db5:6 C5:2 Bb4:8  Eb5:6 C5:2 Ab4:8  F5:8 ~Db5:8  F5:8 ~E5:8 "
    "Db5:6 Eb5:2 F5:8  Eb5:6 F5:2 Ab5:8  Gb5:6 F5:2 Db5:8  F5:8 ~E5:8";

static const AuSection levelb_secs[] = {
    { 8, LB_CH_A2, { NULL, NULL, NULL, LB_OST_A, LB_PAD, NULL, NULL, LB_DRONE }, 0.15f, 0.8f, 0 },
    { 8, LB_CH_A2, { LB_DR8, LB_BASS, NULL, LB_OST_A, LB_PAD, NULL, NULL, LB_DRONE }, 0, 0, 0 },
    { 8, LB_CH_A2, { LB_DR8, LB_BASS, LB_LEAD_A, LB_OST_A, LB_PAD, NULL, NULL, NULL }, 0, 0, 0 },
    { 8, LB_CH_B2, { LB_DR8, LB_BASS, LB_LEAD_B, LB_OST_B, LB_PAD, LB_BELL, LB_ARP, NULL }, 0, 0, 0 },
    { 4, LB_CH_A,  { LB_DR_MIN, NULL, NULL, LB_OST_A, NULL, LB_BELL, NULL, LB_DRONE }, 0.4f, 1.0f, 0 },
    { 8, LB_CH_A2, { LB_DR8, LB_BASS, LB_LEAD_A, LB_OST_A, LB_PAD, LB_BELL, LB_ARP, NULL }, 0, 0, 0 },
    { 4, LB_CH_O,  { LB_DR_OUT, LB_BASS, NULL, LB_OST_B, LB_PAD, NULL, NULL, LB_DRONE }, 0, 0, 0 },
};
static const AuSong song_levelb = {
    .name = "level_b", .bpm = 105, .swing = 0.12f, .kit = AU_KIT_HYPNO,
    .patch  = { NULL, &P_BASS_WOOZY, &P_LEAD_PWM, &P_KEYS_WOOZY, &P_PAD_WARM, &P_BELL, &P_ARP, &P_DRONE },
    .center = { 0, 41, 0, 65, 65, 79, 65, 41 },
    .xp     = { 0 },
    .gain   = { 0.68f, 0.68f, 0.42f, 0.6f, 0.57f, 0.7f, 0.55f, 0.44f },
    .pan    = { 0, 0, 0, -0.2f, 0, 0.35f, 0.3f, 0 },
    .dsend  = { 0, 0, 0.4f, 0.5f, 0.05f, 0.45f, 0.4f, 0 },
    .rsend  = { 0.15f, 0.02f, 0.3f, 0.3f, 0.35f, 0.5f, 0.3f, 0.3f },
    .duck   = { 0, 0.15f, 0, 0.15f, 0.3f, 0, 0.15f, 0.25f },
    .delay_beats = 0.75f, .delay_fb = 0.5f, .delay_wow = 1.0f,
    .rev_decay = 3.6f, .rev_damp = 4500.0f, .duck_time = 0.18f, .master = 0.55f,
    .sec = levelb_secs, .nsec = 7, .loop_to = 1 };

/* ======================================================================== */
/* LEVEL C - A minor, 132 bpm, industrial                                    */
/* ======================================================================== */
static const char LC_CH_A[]  = "A5 A5 F5 G5";
static const char LC_CH_A2[] = "(A5 A5 F5 G5)2";
static const char LC_CH_B2[] = "(Dm F Am E)2";
static const char LC_CH_BLD[] = "F5 G5 F5 E";
static const char LC_CH_O[]  = "A5 F5 G5 E";

#define LC_DR_A \
    "k:X... X... X... X..x|" \
    "s:.... X... .... X...|" \
    "c:.... X... .... X...|" \
    "h:xXxX xXxX xXxX xXxX|" \
    "i:..x. .... ..x. .x.."
#define LC_DR_B \
    "k:X..x X... X..x X... X..x X... X..x X.xx|" \
    "s:.... X... .... X... .... X... .... X.XX|" \
    "c:.... X... .... X... .... X... .... X...|" \
    "h:xXxX xXxX xXxX xXxX xXxX xXxX xXxX xXxX|" \
    "i:..x. ..x. ..x. ..x. ..x. ...x ..x. x.x."
static const char LC_DR_A8[] = LC_DR_A CRASH8;
static const char LC_DR_B8[] = LC_DR_B CRASH8 FILL8B;
static const char LC_DR_A4[] = LC_DR_A;
static const char LC_DR_IN[] = "k:(X... X... X... X...)4|i:(..x. .... ..x. ....)4|h:(x.x. x.x. x.x. x.x.)4" RISE4;
static const char LC_DR_BRK[] = "b:X(.)63|k:(X... .... .... ....)4|i:(..x. ..x. .x.. x...)4";
static const char LC_DR_BLD[] =
    "k:(X... X... X... X...)4|"
    "s:(x... x... x... x...)2 x.x. x.x. x.x. x.x. xxxx xxxx XXXX XXXX|"
    "h:(x.x. x.x. x.x. x.x.)4" RISE4;

static const char LC_BASS[] = "1! 1 1 1 1' 1 1! 1 1 1 1' 1 1! 1 3 1'";
static const char LC_BASS_L[] = "1:16";
static const char LC_ARP[] = "1 2 3 2 4 3 2 3 1 2 3 4 3 2 3 4";
static const char LC_PAD[] = "x:16";
static const char LC_STAB[] = "x:1 .:2 x:1 .:2 x:1 .:3 x:1 .:1 x:1 .:3";
static const char LC_RIFF[] =
    "A4:2 A4 C5 A4:2 E5:2 D5 C5 D5:2 E5:2 G5:2 "
    "A5:2 G5 E5 G5:2 A5:2 C6:2 B5 A5 G5:2 E5:2 "
    "A4:2 A4 C5 A4:2 F5:2 E5 C5 E5:2 F5:2 A5:2 "
    "B4:2 B4 D5 B4:2 G5:2 F5 D5 F5:2 G5:2 B5:2";
static const char LC_LEADB[] =
    "F5:4 E5:4 D5:4 A5:4  C6:4 A5:4 F5:4 C5:4  E5:4 A5:4 C6:4 B5:4  G#5:8 E5:4 B4:4 "
    "F5:4 E5:4 D5:4 F5:4  A5:4 G5:4 F5:4 A5:4  C6:4 B5:4 A5:4 E6:4  D6:4 C6:4 B5:4 G#5:4";

static const AuSection levelc_secs[] = {
    { 4, LC_CH_A,   { LC_DR_IN, LC_BASS, NULL, NULL, NULL, NULL, NULL, NULL }, 0.3f, 0.9f, 0 },
    { 8, LC_CH_A2,  { LC_DR_A8, LC_BASS, NULL, LC_ARP, LC_PAD, NULL, NULL, NULL }, 0, 0, 0 },
    { 8, LC_CH_A2,  { LC_DR_A8, LC_BASS, LC_RIFF, LC_ARP, LC_PAD, NULL, NULL, NULL }, 0, 0, 0 },
    { 8, LC_CH_B2,  { LC_DR_B8, LC_BASS, LC_LEADB, LC_ARP, LC_PAD, LC_STAB, NULL, NULL }, 0, 0, 0 },
    { 4, LC_CH_A,   { LC_DR_BRK, LC_BASS_L, NULL, NULL, LC_PAD, NULL, NULL, NULL }, 0.35f, 0.6f, 0 },
    { 4, LC_CH_BLD, { LC_DR_BLD, LC_BASS, NULL, LC_ARP, LC_PAD, NULL, NULL, NULL }, 0.6f, 1.0f, 0 },
    { 8, LC_CH_A2,  { LC_DR_B8, LC_BASS, LC_RIFF, LC_ARP, LC_PAD, LC_STAB, NULL, NULL }, 0, 0, 0 },
    { 8, LC_CH_B2,  { LC_DR_B8, LC_BASS, LC_LEADB, LC_ARP, LC_PAD, LC_STAB, NULL, NULL }, 0, 0, 0 },
    { 4, LC_CH_O,   { LC_DR_A4, LC_BASS, NULL, LC_ARP, LC_PAD, NULL, NULL, NULL }, 0, 0, 0 },
};
static const AuSong song_levelc = {
    .name = "level_c", .bpm = 132, .swing = 0.0f, .kit = AU_KIT_INDUSTRIAL,
    .patch  = { NULL, &P_BASS_DIST, &P_LEAD_SCREAM, &P_ARP_BRIGHT, &P_PAD_DARK, &P_STAB, NULL, NULL },
    .center = { 0, 33, 0, 69, 64, 64, 0, 0 },
    .xp     = { 0 },
    .gain   = { 0.34f, 0.5f, 0.67f, 0.71f, 0.9f, 1.1f, 0, 0 },
    .pan    = { 0, 0, 0, -0.3f, 0, 0.25f, 0, 0 },
    .dsend  = { 0, 0, 0.25f, 0.3f, 0.0f, 0.15f, 0, 0 },
    .rsend  = { 0.08f, 0.0f, 0.18f, 0.15f, 0.25f, 0.2f, 0, 0 },
    .duck   = { 0, 0.3f, 0, 0.25f, 0.45f, 0.2f, 0, 0 },
    .delay_beats = 0.5f, .delay_fb = 0.3f, .delay_wow = 0.0f,
    .rev_decay = 1.8f, .rev_damp = 5000.0f, .duck_time = 0.09f, .master = 0.5f,
    .sec = levelc_secs, .nsec = 9, .loop_to = 1 };

/* ======================================================================== */
/* BOSS - E phrygian, 140 bpm                                                */
/* ======================================================================== */
static const char BS_CH_IN[] = "Em Em F F";
static const char BS_CH_A2[] = "(Em F Em F)2";
static const char BS_CH_B2[] = "(E F G F)2";
static const char BS_CH_C2[] = "(Dm C Bb B)2";
static const char BS_CH_BR[] = "(Em Em F F)2";
static const char BS_CH_O[]  = "Em F G B";

#define BS_DR_A \
    "k:X... X... X... X...|" \
    "s:.... X... .... X...|" \
    "c:.... X... .... X...|" \
    "h:xxXx xxXx xxXx xxXx|" \
    "i:.... ..x. .... ..x."
#define BS_DR_B \
    "k:X.xx X... X.xx X... X.xx X... X.xx X.X.|" \
    "s:.... X... .... X... .... X... .... X.xX|" \
    "c:.... X... .... X... .... X... .... X...|" \
    "h:xxXx xxXx xxXx xxXx xxXx xxXx xxXx xxXx|" \
    "i:..x. .... ..x. .... ..x. .... ..x. x..."
static const char BS_DR_A8[] = BS_DR_A CRASH8;
static const char BS_DR_B8[] = BS_DR_B CRASH8 FILL8B;
static const char BS_DR_IN[] = "b:X(.)63|i:(..x. .... .... ....)4|k:(.)32 (X... X... X... X...)2" RISE4;
static const char BS_DR_BRK[] =
    "b:X(.)127|k:(X... .... .... .... X... .... ..x. ....)4|s:(.... .... X... .... .... .... X... ....)4|"
    "h:(x.x. x.x. x.x. x.x.)8" RISE8;
static const char BS_DR_T[] =
    "k:(X... X... X... X...)3 X... .... X... ....|"
    "s:(.... X... .... X...)3 .... .... x.x. XXXX|"
    "c:(.... X... .... X...)3 (.)16|"
    "m:(.)48 x.x. x... .... ....|"
    "t:(.)48 .... ..x. x... ....|"
    "h:(xxXx xxXx xxXx xxXx)3 (.)16";

static const char BS_BASS[] = "1! 1 1' 1 1 1 1' 1 1! 1 1' 1 1! 1' 1 1'";
static const char BS_BASS_H[] = "1:8 1':8";
static const char BS_ARP[] = "1 2 3 1' 3 2 1 2 3 1' 3 2 1 2 3 2";
static const char BS_PAD[] = "x:16";
static const char BS_STAB[] = ".:2 x:2 .:2 x:2 .:2 x:2 .:1 x:1 x:2";
static const char BS_DRONE[] = "1:16";
static const char BS_SIREN[] = "E6:8 ~F6:8";
static const char BS_LEAD_A[] =
    "E5:2 F5:2 E5:2 D5:2 E5:4 B4:4  F5:2 G5:2 F5:2 E5:2 F5:4 C5:4  E5:2 F5:2 E5:2 D5:2 E5:2 G5:2 B5:4  C6:4 B5:2 A5:2 G5:4 F5:4 "
    "B5:2 C6:2 B5:2 A5:2 B5:4 E5:4  C6:2 D6:2 C6:2 B5:2 C6:4 F5:4  E6:4 D6:2 C6:2 B5:4 G5:4  A5:4 G5:2 F5:2 E5:8";
static const char BS_LEAD_B[] = "G#5:4 F5:4 E5:8  A5:4 G5:4 F5:8  B5:4 A5:4 G5:4 F5:4  E5:8 ~F5:4 ~E5:4";
static const char BS_LEAD_C[] = "F5:8 A5:8  G5:8 E5:8  F5:8 D5:8  D#5:8 F#5:8";

static const AuSection boss_secs[] = {
    { 4, BS_CH_IN, { BS_DR_IN, NULL, NULL, NULL, BS_PAD, NULL, BS_SIREN, BS_DRONE }, 0.3f, 1.0f, 0 },
    { 8, BS_CH_A2, { BS_DR_A8, BS_BASS, NULL, BS_ARP, BS_PAD, BS_STAB, NULL, NULL }, 0, 0, 0 },
    { 8, BS_CH_A2, { BS_DR_A8, BS_BASS, BS_LEAD_A, BS_ARP, BS_PAD, NULL, NULL, NULL }, 0, 0, 0 },
    { 8, BS_CH_B2, { BS_DR_B8, BS_BASS, BS_LEAD_B, BS_ARP, BS_PAD, BS_STAB, NULL, NULL }, 0, 0, 0 },
    { 8, BS_CH_BR, { BS_DR_BRK, BS_BASS_H, NULL, NULL, BS_PAD, NULL, BS_SIREN, BS_DRONE }, 0.45f, 1.0f, 0 },
    { 8, BS_CH_C2, { BS_DR_B8, BS_BASS, BS_LEAD_C, BS_ARP, BS_PAD, BS_STAB, NULL, NULL }, 0, 0, 0 },
    { 8, BS_CH_A2, { BS_DR_B8, BS_BASS, BS_LEAD_A, BS_ARP, BS_PAD, BS_STAB, BS_SIREN, NULL }, 0, 0, 0 },
    { 4, BS_CH_O,  { BS_DR_T, BS_BASS, NULL, BS_ARP, BS_PAD, BS_STAB, NULL, NULL }, 0, 0, 0 },
};
static const AuSong song_boss = {
    .name = "boss", .bpm = 140, .swing = 0.0f, .kit = AU_KIT_INDUSTRIAL,
    .patch  = { NULL, &P_BASS_DIST, &P_LEAD_SCREAM, &P_ARP_BRIGHT, &P_PAD_DARK, &P_STAB, &P_SIREN, &P_DRONE },
    .center = { 0, 40, 0, 64, 64, 64, 0, 40 },
    .xp     = { 0 },
    .gain   = { 0.38f, 0.5f, 0.64f, 0.71f, 0.8f, 1.06f, 0.5f, 0.6f },
    .pan    = { 0, 0, 0, 0.3f, 0, -0.25f, 0.15f, 0 },
    .dsend  = { 0, 0, 0.25f, 0.3f, 0.0f, 0.15f, 0.35f, 0 },
    .rsend  = { 0.1f, 0.0f, 0.2f, 0.15f, 0.25f, 0.2f, 0.35f, 0.3f },
    .duck   = { 0, 0.3f, 0, 0.25f, 0.45f, 0.2f, 0, 0.3f },
    .delay_beats = 0.75f, .delay_fb = 0.32f, .delay_wow = 0.2f,
    .rev_decay = 2.2f, .rev_damp = 5000.0f, .duck_time = 0.09f, .master = 0.5f,
    .sec = boss_secs, .nsec = 8, .loop_to = 1 };

/* ======================================================================== */
/* SAFEHOUSE - C major / A minor colours, 80 bpm, warm & sad                 */
/* ======================================================================== */
static const char SH_CH_A[]  = "Fmaj7 Em7 Dm9 Cmaj7";
static const char SH_CH_A2[] = "(Fmaj7 Em7 Dm9 Cmaj7)2";
static const char SH_CH_B2[] = "(Dm9 Am7 Bbmaj7 Gsus4_G)2";
static const char SH_CH_O[]  = "Fmaj7 Em7 Dm9 Gsus4_G";

#define SH_DR \
    "k:X... .... ..x. .... X... .... .... .x..|" \
    "r:.... X... .... X... .... X... .... X...|" \
    "y:ox.x ox.x ox.x ox.x ox.x ox.x ox.x ox.x"
static const char SH_DR8[] = SH_DR;
static const char SH_DR_LIGHT[] = "k:X... .... .... .... X... .... .... ....|y:ox.x ox.x ox.x ox.x ox.x ox.x ox.x ox.x";

static const char SH_EP[] = "x!:6 x?:2 .:2 x:6";
static const char SH_BASS[] = "1:12 3:4";
static const char SH_PAD[] = "x:16";
static const char SH_BELL[] = ".:8 4:4 5:4";
static const char SH_ARP[] = "1:2 2:2 3:2 4:2 5:2 4:2 3:2 2:2";
static const char SH_LEAD_A[] =
    "E5:6 C5:2 A4:8  G4:4 B4:4 D5:8  C5:6 A4:2 F4:4 E4:4  G4:16 "
    "E5:6 F5:2 G5:4 A5:4  G5:6 E5:2 D5:8  F5:4 E5:4 D5:4 C5:4  B4:8 ~C5:8";
static const char SH_LEAD_B[] =
    "A5:6 G5:2 F5:4 E5:4  E5:8 C5:4 A4:4  D5:6 F5:2 A5:8  G5:6 F5:2 ~D5:8 "
    "A5:6 C6:2 A5:4 F5:4  G5:6 E5:2 C5:8  D5:4 F5:4 A5:4 C6:4  C6:8 B5:8";

static const AuSection safe_secs[] = {
    { 4, SH_CH_A,  { NULL, NULL, NULL, SH_EP, SH_PAD, NULL, NULL, NULL }, 0.3f, 0.9f, 0 },
    { 8, SH_CH_A2, { SH_DR_LIGHT, SH_BASS, NULL, SH_EP, SH_PAD, NULL, NULL, NULL }, 0, 0, 0 },
    { 8, SH_CH_A2, { SH_DR8, SH_BASS, SH_LEAD_A, SH_EP, SH_PAD, NULL, NULL, NULL }, 0, 0, 0 },
    { 8, SH_CH_B2, { SH_DR8, SH_BASS, SH_LEAD_B, SH_EP, SH_PAD, SH_BELL, SH_ARP, NULL }, 0, 0, 0 },
    { 8, SH_CH_A2, { SH_DR8, SH_BASS, SH_LEAD_A, SH_EP, SH_PAD, SH_BELL, NULL, NULL }, 0, 0, 0 },
    { 4, SH_CH_O,  { SH_DR_LIGHT, SH_BASS, NULL, SH_EP, SH_PAD, SH_BELL, NULL, NULL }, 0, 0, 0 },
};
static const AuSong song_safe = {
    .name = "safehouse", .bpm = 80, .swing = 0.15f, .kit = AU_KIT_SOFT,
    .patch  = { NULL, &P_BASS_SUB, &P_LEAD_SOFT, &P_EPIANO, &P_PAD_WARM, &P_BELL, &P_PLUCK_SOFT, NULL },
    .center = { 0, 36, 0, 62, 62, 79, 67, 0 },
    .xp     = { 0 },
    .gain   = { 0.84f, 0.5f, 0.48f, 0.76f, 0.53f, 0.64f, 0.9f, 0 },
    .pan    = { 0, 0, 0.05f, -0.1f, 0, 0.35f, 0.3f, 0 },
    .dsend  = { 0, 0, 0.3f, 0.15f, 0.0f, 0.4f, 0.35f, 0 },
    .rsend  = { 0.15f, 0.02f, 0.35f, 0.3f, 0.35f, 0.5f, 0.35f, 0 },
    .duck   = { 0 },
    .delay_beats = 0.75f, .delay_fb = 0.38f, .delay_wow = 0.8f,
    .rev_decay = 3.4f, .rev_damp = 4000.0f, .duck_time = 0.15f, .master = 0.55f,
    .sec = safe_secs, .nsec = 6, .loop_to = 1 };

/* ======================================================================== */
/* ENDING - A minor -> C major (-> D), 88 bpm. The title motif comes home.   */
/* ======================================================================== */
static const char EN_CH_SAD[]  = "Am F C G";
static const char EN_CH_SAD2[] = "(Am F C G)2";
static const char EN_CH_SADB[] = "(Dm Am F E)2";
static const char EN_CH_TURN[] = "F G Esus4 E";
static const char EN_CH_HOPE[] = "(C G Am F)2";
static const char EN_CH_OUT[]  = "Fmaj7 G Em7 Am9 Fmaj7 G Csus2 C";

static const char EN_DR_SOFT[] =
    "k:X... .... .... .... X... .... ..x. ....|r:.... .... X... .... .... .... X... ....|"
    "h:..x. ..x. ..x. ..x. ..x. ..x. ..x. ..x.";
#define EN_DR_H \
    "k:X... ..x. X... .... X... ..x. X... ..x.|" \
    "s:.... X... .... X... .... X... .... X...|" \
    "h:x.X. x.X. x.X. x.X. x.X. x.X. x.X. x.X.|" \
    "o:.... .... .... ..x. .... .... .... ..x."
static const char EN_DR_HOPE[] = EN_DR_H CRASH8 FILL8;
static const char EN_DR_TURN[] = "k:(X... .... X... ....)3 X... X... X... X...|s:(.)48 x.x. x.x. xxxx XXXX" RISE4;
static const char EN_DR_OUT[] = "k:(X... .... .... ....)8|h:(..x. ..x. ..x. ..x.)8";

static const char EN_BASS_L[] = "1:16";
static const char EN_BASS_B[] = "1:8 1:4 3,:4";
static const char EN_BASS_8[] = "1:2 1':2 1:2 1':2 1:2 1':2 1:2 1':2";
static const char EN_PAD[] = "x:16";
static const char EN_EP[] = "x:8 x?:8";
static const char EN_ARP[] = "1 3 4 2 3 5 4 3 1 3 4 2 3 5 6 5";
static const char EN_BELL[] = "5:3 4:3 3:4 .:6";
static const char EN_SAD_A[] =
    "E5:3 A5:3 C6:4 B5:2 A5:2 G5:2  A5:10 .:2 G5:2 F5:2  E5:3 G5:3 C6:4 B5:2 C6:2 D6:2  B5:8 ~A5:4 G5:4 "
    "E5:3 A5:3 C6:4 B5:2 A5:2 G5:2  A5:6 C6:4 D6:2 E6:4  G6:3 E6:3 C6:4 D6:2 E6:2 D6:2  D6:4 B5:4 A5:8";
static const char EN_SAD_B[] =
    "F6:6 E6:2 D6:4 A5:4  C6:6 B5:2 A5:4 E5:4  A5:4 C6:4 F6:4 E6:2 D6:2  B5:6 G#5:2 ~B5:4 E6:4 "
    "F6:6 E6:2 D6:4 F6:4  E6:6 D6:2 C6:4 A5:4  C6:6 D6:2 C6:4 A5:4  G#5:8 B5:4 .:4";
static const char EN_TURN[] = "A5:16 B5:16 A5:16 ~G#5:16";
static const char EN_HOPE_A[] =
    "G5:3 C6:3 E6:4 D6:2 C6:2 D6:2  D6:10 .:2 E6:2 D6:2  C6:3 E6:3 A6:4 G6:2 E6:2 D6:2  C6:8 ~A5:4 C6:4 "
    "G5:3 C6:3 E6:4 D6:2 C6:2 D6:2  D6:6 E6:4 D6:2 B5:4  C6:6 E6:4 G6:2 A6:4  A6:8 G6:4 E6:4";
static const char EN_HOPE_B[] =
    "E6:4 G6:4 C7:6 B6:2  B6:4 A6:4 G6:4 D6:4  C7:4 B6:4 A6:4 E6:4  F6:6 G6:2 A6:8 "
    "E6:4 G6:4 C7:6 D7:2  B6:6 A6:2 G6:8  A6:4 B6:4 C7:4 E7:4  D7:8 C7:8";
static const char EN_OUT[] = "E6:16 D6:16 B5:16 C6:16 A5:8 C6:8 B5:8 D6:8 C6:32";

static const AuSection ending_secs[] = {
    { 4, EN_CH_SAD,  { NULL, NULL, NULL, NULL, EN_PAD, NULL, NULL, EN_EP }, 0.3f, 0.8f, 0 },
    { 8, EN_CH_SAD2, { NULL, EN_BASS_L, EN_SAD_A, NULL, EN_PAD, NULL, NULL, EN_EP }, 0.8f, 1.0f, 0 },
    { 8, EN_CH_SADB, { EN_DR_SOFT, EN_BASS_B, EN_SAD_B, EN_ARP, EN_PAD, NULL, NULL, EN_EP }, 0, 0, 0 },
    { 4, EN_CH_TURN, { EN_DR_TURN, EN_BASS_L, EN_TURN, EN_ARP, EN_PAD, NULL, NULL, NULL }, 0, 0, 0 },
    { 8, EN_CH_HOPE, { EN_DR_HOPE, EN_BASS_8, NULL, EN_ARP, EN_PAD, NULL, EN_HOPE_A, NULL }, 0, 0, 0 },
    { 8, EN_CH_HOPE, { EN_DR_HOPE, EN_BASS_8, NULL, EN_ARP, EN_PAD, EN_BELL, EN_HOPE_B, NULL }, 0, 0, 2 },
    { 8, EN_CH_OUT,  { EN_DR_OUT, EN_BASS_L, EN_OUT, NULL, EN_PAD, EN_BELL, NULL, EN_EP }, 1.0f, 0.55f, 0 },
};
static const AuSong song_ending = {
    .name = "ending", .bpm = 88, .swing = 0.0f, .kit = AU_KIT_SYNTHWAVE,
    .patch  = { NULL, &P_BASS, &P_LEAD_SOFT, &P_ARP, &P_PAD, &P_BELL, &P_LEAD_SAW, &P_EPIANO },
    .center = { 0, 33, 0, 64, 64, 79, 0, 62 },
    .xp     = { 0, 0, -12, 0, 0, 0, -12, 0 },
    .gain   = { 0.72f, 0.6f, 0.44f, 0.64f, 0.95f, 0.62f, 0.6f, 1.0f },
    .pan    = { 0, 0, 0, -0.25f, 0, 0.3f, 0, 0.1f },
    .dsend  = { 0, 0, 0.35f, 0.45f, 0.05f, 0.4f, 0.35f, 0.15f },
    .rsend  = { 0.12f, 0.02f, 0.35f, 0.25f, 0.35f, 0.5f, 0.28f, 0.3f },
    .duck   = { 0, 0.2f, 0, 0.2f, 0.35f, 0, 0, 0 },
    .delay_beats = 0.75f, .delay_fb = 0.42f, .delay_wow = 0.5f,
    .rev_decay = 3.4f, .rev_damp = 5000.0f, .duck_time = 0.16f, .master = 0.55f,
    .sec = ending_secs, .nsec = 7, .loop_to = 1 };

const AuSong *const au_songs[MUS_COUNT] = {
    [MUS_NONE] = NULL,
    [MUS_MENU] = &song_menu,
    [MUS_LEVEL_A] = &song_levela,
    [MUS_LEVEL_B] = &song_levelb,
    [MUS_LEVEL_C] = &song_levelc,
    [MUS_BOSS] = &song_boss,
    [MUS_SAFEHOUSE] = &song_safe,
    [MUS_ENDING] = &song_ending,
};
