/* LAST AISLE - offline audio test tool.
   Renders every SFX, every loop and ~20 s of every music track to WAV files in build/audio/,
   prints level statistics, band balance, init time and CPU cost, and runs a
   limiter stress test.

   build: clang -std=c11 -O2 -Wall -Wextra src/audio*.c tools/audio_render.c \
            $(pkg-config --cflags --libs sdl3) -o build/audio_render
   run:   ./build/audio_render [seconds_of_music]                                 */
#include "../src/audio_internal.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OUTDIR "build/audio"

static void wr16(FILE *f, uint16_t v) { fputc(v & 0xFF, f); fputc(v >> 8, f); }
static void wr32(FILE *f, uint32_t v) { wr16(f, (uint16_t)(v & 0xFFFF)); wr16(f, (uint16_t)(v >> 16)); }

static int write_wav(const char *path, const float *data, int frames, int ch)
{
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); return 0; }
    uint32_t bytes = (uint32_t)frames * (uint32_t)ch * 2u;
    fwrite("RIFF", 1, 4, f); wr32(f, 36 + bytes); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); wr32(f, 16); wr16(f, 1); wr16(f, (uint16_t)ch); wr32(f, AU_RATE);
    wr32(f, AU_RATE * (uint32_t)ch * 2u); wr16(f, (uint16_t)(ch * 2)); wr16(f, 16);
    fwrite("data", 1, 4, f); wr32(f, bytes);
    for (int i = 0; i < frames * ch; i++) {
        float v = data[i];
        if (v > 1.0f) v = 1.0f; else if (v < -1.0f) v = -1.0f;
        int s = (int)lrintf(v * 32767.0f);
        wr16(f, (uint16_t)(int16_t)s);
    }
    fclose(f);
    return 1;
}

typedef struct { float peak, rms, loud, centroid, band[4], maxdiff; int bad; } Stats;

static float todb(float x) { return x > 1e-9f ? 20.0f * log10f(x) : -180.0f; }

/* peak, overall RMS, loudest 50 ms RMS window, band energy split, NaN check */
static Stats analyse(const float *d, int frames, int ch)
{
    Stats s; memset(&s, 0, sizeof s);
    double sum = 0.0;
    int win = AU_RATE / 20;
    double wsum = 0.0;
    AuSvf f1 = {0}, f2 = {0}, f3 = {0};
    au_svf_set(&f1, 150.0f, 0.707f); au_svf_set(&f2, 1000.0f, 0.707f); au_svf_set(&f3, 5000.0f, 0.707f);
    double be[4] = { 0 };
    float prev = 0.0f;
    float *mono = (float *)malloc(sizeof(float) * (size_t)(frames > 0 ? frames : 1));
    for (int i = 0; i < frames; i++) {
        float m = 0.0f;
        for (int c = 0; c < ch; c++) {
            float v = d[i * ch + c];
            if (v != v || v > 1e6f || v < -1e6f) s.bad++;
            if (fabsf(v) > s.peak) s.peak = fabsf(v);
            sum += (double)v * v;
            m += v;
        }
        m /= (float)ch;
        mono[i] = m;
        float df = fabsf(m - prev); if (df > s.maxdiff) s.maxdiff = df; prev = m;
        wsum += (double)m * m;
        if (i >= win) wsum -= (double)mono[i - win] * mono[i - win];
        if (i >= win - 1) { float r = (float)sqrt(wsum > 0 ? wsum / win : 0); if (r > s.loud) s.loud = r; }
        float lo = au_svf_lp(&f1, m);
        float b1 = au_svf_lp(&f2, m) - lo;
        float b2 = au_svf_lp(&f3, m) - lo - b1;
        float hi = m - lo - b1 - b2;
        be[0] += (double)lo * lo; be[1] += (double)b1 * b1; be[2] += (double)b2 * b2; be[3] += (double)hi * hi;
    }
    if (frames < win) {
        double t = 0; for (int i = 0; i < frames; i++) t += (double)mono[i] * mono[i];
        s.loud = (float)sqrt(frames ? t / frames : 0);
    }
    free(mono);
    s.rms = (float)sqrt(frames ? sum / ((double)frames * ch) : 0);
    double tot = be[0] + be[1] + be[2] + be[3] + 1e-20;
    for (int k = 0; k < 4; k++) s.band[k] = (float)(100.0 * be[k] / tot);
    s.centroid = (float)((be[0] * 80 + be[1] * 400 + be[2] * 2200 + be[3] * 9000) / tot);
    return s;
}

static double now_ms(void) { return (double)SDL_GetPerformanceCounter() * 1000.0 / (double)SDL_GetPerformanceFrequency(); }

int main(int argc, char **argv)
{
    float music_secs = argc > 1 ? (float)atof(argv[1]) : 20.0f;
    if (music_secs < 1.0f) music_secs = 20.0f;
    SDL_CreateDirectory(OUTDIR);

    double t0 = now_ms();
    if (!audio_init_offline()) { fprintf(stderr, "audio_init_offline failed\n"); return 1; }
    double t1 = now_ms();
    printf("== init: %.1f ms (internal %.1f ms)\n", t1 - t0, audio_init_ms());
    int problems = au_music_validate();
    printf("== pattern validation: %d problem(s)\n", problems);
    for (int id = 1; id < MUS_COUNT; id++)
        printf("   %-10s %6.1f s per pass\n", au_music_name(id), au_music_song_seconds(id));

    /* ---------------- SFX ---------------- */
    printf("\n== SFX                    len(s) ch   peak(dB)  rms(dB) loud50ms  <150  150-1k 1k-5k  >5k\n");
    size_t total_frames = 0;
    for (int i = 0; i < SFX_COUNT; i++) {
        const AuSample *s = audio_get_sfx((SfxId)i);
        if (!s || !s->data) { printf("   %-20s MISSING\n", au_sfx_name(i)); continue; }
        total_frames += (size_t)s->frames * (size_t)s->channels;
        Stats st = analyse(s->data, s->frames, s->channels);
        char path[256];
        snprintf(path, sizeof path, OUTDIR "/sfx_%02d_%s.wav", i, au_sfx_name(i));
        write_wav(path, s->data, s->frames, s->channels);
        int nv = 1;
        for (int v = 1; v < AU_SFX_MAXVAR; v++) {
            const AuSample *sv = audio_get_sfx_variant((SfxId)i, v);
            if (!sv || !sv->data) break;
            nv++;
            total_frames += (size_t)sv->frames * (size_t)sv->channels;
            Stats s2 = analyse(sv->data, sv->frames, sv->channels);
            if (s2.bad) st.bad += s2.bad;
            snprintf(path, sizeof path, OUTDIR "/sfx_%02d_%s_v%d.wav", i, au_sfx_name(i), v);
            write_wav(path, sv->data, sv->frames, sv->channels);
        }
        printf("   %-18s x%d %5.2f  %d   %7.1f  %7.1f  %7.1f   %4.0f%%  %4.0f%%  %4.0f%%  %4.0f%%%s\n",
               au_sfx_name(i), nv, (double)s->frames / AU_RATE, s->channels, todb(st.peak), todb(st.rms), todb(st.loud),
               st.band[0], st.band[1], st.band[2], st.band[3], st.bad ? "  NaN!" : "");
    }
    printf("   sfx memory: %.1f MB\n", (double)total_frames * 4.0 / 1048576.0);

    /* drum kits */
    for (int k = 0; k < AU_KIT_COUNT; k++) {
        for (int pc = 0; pc < 14; pc++) {
            const char *nm = NULL;
            const AuSample *ks = au_music_kit_sample(k, pc, &nm);
            if (!ks || !ks->data) continue;
            char path[256];
            snprintf(path, sizeof path, OUTDIR "/kit%d_%02d_%s.wav", k, pc, nm);
            write_wav(path, ks->data, ks->frames, ks->channels);
        }
    }

    int chunk = 256;
    float *buf = (float *)malloc(sizeof(float) * 2 * (size_t)(AU_RATE * (music_secs + 40.0f)));
    if (!buf) return 1;

    /* ---------------- loops ---------------- */
    printf("\n== loops (5 s at vol 1, pitch 1)   peak(dB)  rms(dB)\n");
    for (int l = 0; l < LOOP_COUNT; l++) {
        int frames = AU_RATE * 5;
        for (int f = 0; f < frames; f += chunk) {
            int n = frames - f < chunk ? frames - f : chunk;
            audio_loop((LoopId)l, 1.0f, l == LOOP_CHAINSAW_IDLE ? 1.0f + 0.3f * (float)f / (float)frames : 1.0f);
            audio_render_offline(buf + f * 2, n);
        }
        /* let it fade out */
        for (int f = 0; f < AU_RATE / 2; f += chunk) { audio_loop((LoopId)l, 0.0f, 1.0f); audio_render_offline(buf + frames * 2, chunk); }
        Stats st = analyse(buf + AU_RATE / 2 * 2, frames - AU_RATE / 2, 2);
        char path[256];
        snprintf(path, sizeof path, OUTDIR "/loop_%s.wav", au_loop_name(l));
        write_wav(path, buf, frames, 2);
        printf("   %-16s              %7.1f  %7.1f%s\n", au_loop_name(l), todb(st.peak), todb(st.rms), st.bad ? "  NaN!" : "");
    }

    /* ---------------- music ---------------- */
    printf("\n== music (%.0f s each through the full mixer, music vol 1)\n", (double)music_secs);
    printf("   track        peak(dB)  rms(dB) loud50ms  <150  150-1k 1k-5k  >5k   maxstep  cpu(%% of 1 core)\n");
    audio_set_volumes(1.0f, 1.0f, 1.0f);
    for (int id = 1; id < MUS_COUNT; id++) {
        audio_music(MUS_NONE);
        for (int f = 0; f < AU_RATE * 2; f += chunk) audio_render_offline(buf, chunk);
        audio_music((MusicId)id);
        int frames = (int)(AU_RATE * music_secs);
        au_music_meter(1, NULL, NULL);
        double c0 = now_ms();
        for (int f = 0; f < frames; f += chunk) audio_render_offline(buf + f * 2, frames - f < chunk ? frames - f : chunk);
        double c1 = now_ms();
        Stats st = analyse(buf, frames, 2);
        char path[256];
        snprintf(path, sizeof path, OUTDIR "/music_%d_%s.wav", id, au_music_name(id));
        write_wav(path, buf, frames, 2);
        printf("   %-12s %7.1f  %7.1f  %7.1f   %4.0f%%  %4.0f%%  %4.0f%%  %4.0f%%   %6.3f   %5.2f%%%s\n",
               au_music_name(id), todb(st.peak), todb(st.rms), todb(st.loud), st.band[0], st.band[1], st.band[2], st.band[3],
               st.maxdiff, 100.0 * (c1 - c0) / (music_secs * 1000.0), st.bad ? "  NaN!" : "");
        /* keep going until one complete pass has been metered */
        int full = (int)(AU_RATE * au_music_song_seconds(id)) - frames;
        double c2 = now_ms();
        double sq = 0.0; float fpk = 0.0f;
        for (int f = 0; f < full; f += chunk) {
            int nn = full - f < chunk ? full - f : chunk;
            audio_render_offline(buf, nn);
            for (int k = 0; k < nn * 2; k++) { sq += (double)buf[k] * buf[k]; if (fabsf(buf[k]) > fpk) fpk = fabsf(buf[k]); }
        }
        double c3 = now_ms();
        float mr[AU_MCH], mp[AU_MCH];
        au_music_meter(0, mr, mp);
        printf("      rest of pass: rms %.1f dB peak %.1f dB, cpu %.2f%%\n", todb((float)sqrt(sq / (2.0 * (full > 0 ? full : 1)))), todb(fpk),
               100.0 * (c3 - c2) / ((double)full / AU_RATE * 1000.0));
        printf("      ch active-rms/peak:");
        for (int c = 0; c < AU_MCH; c++) if (mp[c] > -170.0f) printf("  %d:%.0f/%.0f", c, mr[c], mp[c]);
        printf("\n");
    }

    /* full pass of the menu track including the loop point (seam check) */
    {
        audio_music(MUS_NONE);
        for (int f = 0; f < AU_RATE * 2; f += chunk) audio_render_offline(buf, chunk);
        audio_music(MUS_SAFEHOUSE);
        double len = au_music_song_seconds(MUS_SAFEHOUSE);
        int frames = (int)(AU_RATE * (len + 8.0));
        float *full = (float *)malloc(sizeof(float) * 2 * (size_t)frames);
        if (full) {
            double c0 = now_ms();
            for (int f = 0; f < frames; f += chunk) audio_render_offline(full + f * 2, frames - f < chunk ? frames - f : chunk);
            double c1 = now_ms();
            Stats st = analyse(full, frames, 2);
            write_wav(OUTDIR "/music_full_safehouse.wav", full, frames, 2);
            printf("   full safehouse pass incl. loop: %.1f s, peak %.1f dB, maxstep %.3f, cpu %.2f%%\n",
                   (double)frames / AU_RATE, todb(st.peak), st.maxdiff, 100.0 * (c1 - c0) / ((double)frames / AU_RATE * 1000.0));
            free(full);
        }
    }

    /* ---------------- crossfade, slow-mo, muffle demo ---------------- */
    {
        audio_music(MUS_NONE);
        for (int f = 0; f < AU_RATE * 2; f += chunk) audio_render_offline(buf, chunk);
        audio_music(MUS_LEVEL_A);
        int frames = AU_RATE * 24;
        for (int f = 0; f < frames; f += chunk) {
            float t = (float)f / AU_RATE;
            if (f == AU_RATE * 6) audio_play(SFX_SHOTGUN, 1.0f, 0.0f, 1.0f);
            if (f >= AU_RATE * 6 && f < AU_RATE * 10) audio_set_timescale(0.45f); else audio_set_timescale(1.0f);
            audio_set_muffle(t >= 12.0f && t < 16.0f ? 1.0f : 0.0f);
            if (f == AU_RATE * 17) audio_music(MUS_BOSS);
            if (f == AU_RATE * 8) audio_play(SFX_PISTOL, 1.0f, -0.5f, 1.0f);
            audio_render_offline(buf + f * 2, chunk);
        }
        audio_set_timescale(1.0f); audio_set_muffle(0.0f);
        Stats st = analyse(buf, frames, 2);
        write_wav(OUTDIR "/demo_slowmo_muffle_xfade.wav", buf, frames, 2);
        printf("\n== demo (slow-mo 6-10 s, muffle 12-16 s, crossfade to boss at 17 s): peak %.1f dB%s\n",
               todb(st.peak), st.bad ? "  NaN!" : "");
    }

    /* ---------------- stress test: massive simultaneous gunfire ---------------- */
    {
        audio_set_volumes(1.0f, 1.0f, 1.0f);
        int frames = AU_RATE * 4;
        int fired = 0;
        for (int f = 0; f < frames; f += chunk) {
            if (f < AU_RATE * 2) {
                for (int k = 0; k < 6; k++) {
                    static const SfxId guns[] = { SFX_SHOTGUN, SFX_PISTOL, SFX_REVOLVER, SFX_RIFLE, SFX_EXPLOSION, SFX_EXECUTE };
                    audio_play(guns[(f / chunk + k) % 6], 1.0f, (float)((k % 3) - 1) * 0.5f, 0.9f + 0.05f * (float)(k % 4));
                    fired++;
                }
            }
            audio_render_offline(buf + f * 2, chunk);
        }
        Stats st = analyse(buf, frames, 2);
        write_wav(OUTDIR "/stress_gunfire.wav", buf, frames, 2);
        printf("== stress (%d shots in 2 s over boss music): peak %.2f dB (must be <= 0), rms %.1f dB%s\n",
               fired, todb(st.peak), todb(st.rms), st.bad ? "  NaN!" : "");
    }

    /* worst case CPU: music crossfade (two players) + saturated voices + every loop */
    {
        audio_music(MUS_LEVEL_B);
        for (int f = 0; f < AU_RATE * 3; f += chunk) audio_render_offline(buf, chunk);
        audio_music(MUS_BOSS);
        double worst = 0.0, total = 0.0;
        int blocks = 0;
        for (int f = 0; f < AU_RATE; f += chunk) {
            for (int l = 0; l < LOOP_COUNT; l++) audio_loop((LoopId)l, 0.5f, 1.0f);
            for (int k = 0; k < 4; k++) audio_play((SfxId)((f / chunk * 4 + k) % SFX_COUNT), 0.5f, 0.0f, 1.0f);
            double a = now_ms();
            audio_render_offline(buf, chunk);
            double b = now_ms() - a;
            total += b; blocks++;
            if (b > worst) worst = b;
        }
        printf("== worst case (crossfade + 32 voices + 6 loops): avg %.3f ms, max %.3f ms per %d-frame block (%.2f ms budget) = %.1f%% avg load\n",
               total / blocks, worst, chunk, chunk * 1000.0 / AU_RATE, 100.0 * total / blocks / (chunk * 1000.0 / AU_RATE));
        for (int l = 0; l < LOOP_COUNT; l++) audio_loop((LoopId)l, 0.0f, 1.0f);
        for (int f = 0; f < AU_RATE; f += chunk) audio_render_offline(buf, chunk);
    }

    /* single shots over music: is music sitting under the sfx? */
    {
        audio_music(MUS_NONE);
        for (int f = 0; f < AU_RATE * 2; f += chunk) audio_render_offline(buf, chunk);
        audio_set_volumes(1.0f, 0.8f, 1.0f);
        audio_music(MUS_LEVEL_C);
        int frames = AU_RATE * 30;
        static const SfxId seq[] = { SFX_PISTOL, SFX_PUNCH, SFX_HIT_BLUNT, SFX_SHOTGUN, SFX_GORE, SFX_SWING, SFX_FOOTSTEP,
                                     SFX_PICKUP, SFX_LIST_TICK, SFX_HURT_PLAYER, SFX_STUN, SFX_EXPLOSION, SFX_UI_SELECT };
        int k = 0;
        for (int f = 0; f < frames; f += chunk) {
            if (f >= AU_RATE * 14 && (f - AU_RATE * 14) % (AU_RATE) < chunk && k < (int)(sizeof seq / sizeof seq[0]))
                audio_play(seq[k++], 1.0f, 0.0f, 1.0f);
            audio_render_offline(buf + f * 2, chunk);
        }
        write_wav(OUTDIR "/mix_sfx_over_music.wav", buf + AU_RATE * 12 * 2, AU_RATE * 18, 2);
        printf("== wrote mix_sfx_over_music.wav (level C music at 0.8 + one sfx per second)\n");
    }

    audio_music(MUS_NONE);
    free(buf);
    printf("\nwrote files to " OUTDIR "/\n");
    return problems ? 2 : 0;
}
