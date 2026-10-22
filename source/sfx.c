#include "sfx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SFX_RATE 22050
#define SFX_CH   6

static const char *names[SFX_COUNT] = {"impact", "hum", "flicker", "creak0", "creak", "steam0", "steam"};
static struct { s16 *pcm; u32 n; } clips[SFX_COUNT];
static ndspWaveBuf wb[SFX_CH][2];
static bool on;

static bool load(int i)
{
	char path[48];
	snprintf(path, sizeof path, "romfs:/sfx/%s.pcm", names[i]);
	FILE *f = fopen(path, "rb");
	if (!f) return false;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	// El DSP lee de la memoria lineal
	clips[i].pcm = n > 0 ? linearAlloc(n) : NULL;
	bool ok = clips[i].pcm && fread(clips[i].pcm, 1, n, f) == (size_t)n;
	fclose(f);
	if (!ok) {
		if (clips[i].pcm) linearFree(clips[i].pcm);
		clips[i].pcm = NULL;
		return false;
	}
	clips[i].n = n / 2;
	DSP_FlushDataCache(clips[i].pcm, n);
	return true;
}

bool sfx_init(void)
{
	if (on) return true;
	if (R_FAILED(ndspInit())) return false;
	on = true;
	ndspSetOutputMode(NDSP_OUTPUT_STEREO);
	for (int i = 0; i < SFX_COUNT; i++) load(i);
	for (int ch = 0; ch < SFX_CH; ch++) {
		ndspChnReset(ch);
		ndspChnSetInterp(ch, NDSP_INTERP_LINEAR);
		ndspChnSetRate(ch, SFX_RATE);
		ndspChnSetFormat(ch, NDSP_FORMAT_MONO_PCM16);
		sfx_volume(ch, 0, 0);
	}
	return true;
}

void sfx_exit(void)
{
	if (!on) return;
	for (int ch = 0; ch < SFX_CH; ch++) ndspChnWaveBufClear(ch);
	// Se espera a que el DSP suelte las muestras antes de liberarlas
	svcSleepThread(20 * 1000 * 1000);
	ndspExit();
	for (int i = 0; i < SFX_COUNT; i++) {
		if (clips[i].pcm) linearFree(clips[i].pcm);
		clips[i].pcm = NULL;
	}
	on = false;
}

static void queue(int ch, int slot, SfxClip c, bool loop)
{
	ndspWaveBuf *w = &wb[ch][slot];
	memset(w, 0, sizeof *w);
	w->data_vaddr = clips[c].pcm;
	w->nsamples = clips[c].n;
	w->looping = loop;
	ndspChnWaveBufAdd(ch, w);
}

void sfx_play(int ch, SfxClip clip, SfxClip then)
{
	if (!on || ch < 0 || ch >= SFX_CH || !clips[clip].pcm) return;
	ndspChnWaveBufClear(ch);
	queue(ch, 0, clip, false);
	if ((int)then >= 0 && clips[then].pcm) queue(ch, 1, then, true);
}

void sfx_loop(int ch, SfxClip clip)
{
	if (!on || ch < 0 || ch >= SFX_CH || !clips[clip].pcm) return;
	ndspChnWaveBufClear(ch);
	queue(ch, 0, clip, true);
}

void sfx_volume(int ch, float vol, float pan)
{
	if (!on || ch < 0 || ch >= SFX_CH) return;
	float mix[12] = {0};
	if (vol < 0) vol = 0;
	mix[0] = vol * (pan > 0 ? 1 - pan : 1);
	mix[1] = vol * (pan < 0 ? 1 + pan : 1);
	ndspChnSetMix(ch, mix);
}
