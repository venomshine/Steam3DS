#include "util.h"
#include "app.h"
#include "i18n.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

Config g_cfg;
static bool have_mcu;

void util_init(void)
{
	have_mcu = R_SUCCEEDED(mcuHwcInit());
	ptmuInit();
}

void util_exit(void)
{
	if (have_mcu) mcuHwcExit();
	ptmuExit();
}

void util_clock(char *out, size_t n)
{
	static const char *dias_es[] = {"dom", "lun", "mar", "mié", "jue", "vie", "sáb"};
	static const char *mes_es[] = {"ene", "feb", "mar", "abr", "may", "jun", "jul", "ago", "sep", "oct", "nov", "dic"};
	static const char *days_en[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
	static const char *mon_en[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
	static const char *days_ja[] = {"日", "月", "火", "水", "木", "金", "土"};
	time_t t = time(NULL);
	struct tm *tm = gmtime(&t);   // la consola guarda la hora local
	switch (i18n_get()) {
	case LANG_EN:
		snprintf(out, n, "%s, %s %d · %02d:%02d", days_en[tm->tm_wday], mon_en[tm->tm_mon], tm->tm_mday, tm->tm_hour, tm->tm_min);
		break;
	case LANG_JA:
		snprintf(out, n, "%d/%d(%s) %02d:%02d", tm->tm_mon + 1, tm->tm_mday, days_ja[tm->tm_wday], tm->tm_hour, tm->tm_min);
		break;
	default:
		snprintf(out, n, "%s %d %s · %02d:%02d", dias_es[tm->tm_wday], tm->tm_mday, mes_es[tm->tm_mon], tm->tm_hour, tm->tm_min);
		break;
	}
}

int util_battery(bool *charging)
{
	static int cached = -1;
	static bool chg;
	static u64 last;
	u64 now = osGetTime();
	if (cached < 0 || now - last > 5000) {
		last = now;
		u8 lvl = 0, c = 0;
		if (have_mcu && R_SUCCEEDED(MCUHWC_GetBatteryLevel(&lvl))) cached = lvl > 100 ? 100 : lvl;
		else if (R_SUCCEEDED(PTMU_GetBatteryLevel(&lvl))) cached = lvl * 20;
		if (R_SUCCEEDED(PTMU_GetBatteryChargeState(&c))) chg = c != 0;
	}
	if (charging) *charging = chg;
	return cached < 0 ? 0 : cached;
}

// ----------------------------------------------------------------------------

void config_load(void)
{
	memset(&g_cfg, 0, sizeof g_cfg);
	strcpy(g_cfg.lang, "sys");
	FILE *f = fopen(APP_DIR "/config.ini", "r");
	if (!f) return;
	char line[160];
	while (fgets(line, sizeof line, f)) {
		char *eq = strchr(line, '=');
		if (!eq || line[0] == '#') continue;
		*eq = 0;
		char *v = eq + 1;
		v[strcspn(v, "\r\n")] = 0;
		if (!strcmp(line, "lang")) snprintf(g_cfg.lang, sizeof g_cfg.lang, "%s", v);
		else if (!strcmp(line, "country")) snprintf(g_cfg.country, sizeof g_cfg.country, "%s", v);
		else if (!strcmp(line, "birth")) snprintf(g_cfg.birth, sizeof g_cfg.birth, "%s", v);
		else if (!strcmp(line, "reqs")) g_cfg.reqs_mac = !strcmp(v, "mac");
	}
	fclose(f);
}

bool config_save(void)
{
	FILE *f = fopen(APP_DIR "/config.ini.tmp", "w");
	if (!f) return false;
	fprintf(f, "lang=%s\ncountry=%s\nbirth=%s\nreqs=%s\n", g_cfg.lang, g_cfg.country, g_cfg.birth, g_cfg.reqs_mac ? "mac" : "win");
	fclose(f);
	remove(APP_DIR "/config.ini");
	return rename(APP_DIR "/config.ini.tmp", APP_DIR "/config.ini") == 0;
}
