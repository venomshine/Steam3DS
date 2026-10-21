#include "i18n.h"
#include <3ds.h>
#include <string.h>

typedef struct { const char *es, *en, *ja; } TrEntry;

// Tabla generada por tools/gen_i18n.py a partir de tools/i18n_tr.py
#include "i18n_table.h"

static Lang cur = LANG_ES;

void i18n_set(Lang l) { cur = (l >= 0 && l < LANG_COUNT) ? l : LANG_ES; }
Lang i18n_get(void) { return cur; }

Lang i18n_system(void)
{
	u8 l = CFG_LANGUAGE_EN;
	if (R_SUCCEEDED(cfguInit())) {
		CFGU_GetSystemLanguage(&l);
		cfguExit();
	}
	if (l == CFG_LANGUAGE_ES) return LANG_ES;
	if (l == CFG_LANGUAGE_JP) return LANG_JA;
	return LANG_EN;
}

const char *tr(const char *es)
{
	if (cur == LANG_ES || !es) return es;
	// Búsqueda binaria: la tabla viene ordenada por la clave
	int lo = 0, hi = (int)(sizeof tr_table / sizeof *tr_table) - 1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		int c = strcmp(es, tr_table[mid].es);
		if (c == 0) {
			const char *t = cur == LANG_EN ? tr_table[mid].en : tr_table[mid].ja;
			return (t && *t) ? t : es;
		}
		if (c < 0) hi = mid - 1; else lo = mid + 1;
	}
	return es;
}

const char *i18n_steam_lang(const char *country)
{
	static const char *latam[] = {"MX", "AR", "CL", "CO", "PE", "VE", "EC", "BO", "PY", "UY",
		"CR", "PA", "GT", "HN", "SV", "NI", "DO", "CU", "PR", NULL};
	switch (cur) {
	case LANG_EN: return "english";
	case LANG_JA: return "japanese";
	default:
		for (int i = 0; latam[i]; i++)
			if (country && !strcmp(country, latam[i])) return "latam";
		return "spanish";
	}
}
