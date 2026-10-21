#pragma once
#include <3ds.h>
#include <stdbool.h>
#include <stddef.h>

void util_init(void);
void util_exit(void);

// «mar 21 oct · 18:42» según el idioma
void util_clock(char *out, size_t n);
int util_battery(bool *charging);

// Configuración en sdmc:/3ds/Steam3DS/config.ini (clave=valor)
typedef struct {
	char lang[8];        // "sys", "es", "en", "ja"
	char country[4];     // último país que dijo Steam
	char birth[12];      // AAAA-MM-DD (por si algún día Steam la pide)
} Config;

extern Config g_cfg;
void config_load(void);
bool config_save(void);
