#pragma once
#include <3ds.h>
#include <stdbool.h>

// Sonidos de la pantalla de carga (romfs:/sfx, hechos con tools/gen_sfx.py). Si la consola no
// tiene el firmware del DSP (sdmc:/3ds/dspfirm.cdc), no suena nada y todo sigue igual.
typedef enum { SFX_IMPACT, SFX_HUM, SFX_FLICKER, SFX_CREAK0, SFX_CREAK, SFX_STEAM0, SFX_STEAM, SFX_COUNT } SfxClip;

bool sfx_init(void);
void sfx_exit(void);   // también si sfx_init no llegó a arrancar
// Suena clip en el canal ch (corta lo que hubiera); then, si no es -1, sigue detrás en bucle
void sfx_play(int ch, SfxClip clip, SfxClip then);
void sfx_loop(int ch, SfxClip clip);
// Volumen (0..1) y lado (-1 izquierda, 1 derecha) del canal
void sfx_volume(int ch, float vol, float pan);
