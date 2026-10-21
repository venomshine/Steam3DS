#pragma once

// La clave es el texto en español; _() devuelve la traducción del idioma activo.
typedef enum { LANG_ES = 0, LANG_EN, LANG_JA, LANG_COUNT } Lang;

void i18n_set(Lang l);
Lang i18n_get(void);
Lang i18n_system(void);
const char *tr(const char *es);
#define _(s) tr(s)

// Idioma para la API de Steam ("spanish", "latam", "english", "japanese")
const char *i18n_steam_lang(const char *country);
