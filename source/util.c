#include "util.h"
#include "app.h"
#include "i18n.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

Config g_cfg;
static bool have_mcu;

// ----------------------------------------------------------------------------
// Disco en segundo plano

#define DISK_SAVES 8
#define DISK_LOADS 4
static struct { char path[96]; void *data; size_t len; bool used; } dsaves[DISK_SAVES];
static DiskLoad *dloads[DISK_LOADS];
static LightLock dlock;
static LightEvent dwake;
static Thread dthread;
static bool dquit;

static void write_file(const char *path, const void *data, size_t len)
{
	char tmp[100];
	snprintf(tmp, sizeof tmp, "%s.tmp", path);
	FILE *f = fopen(tmp, "wb");
	if (!f) return;
	bool ok = fwrite(data, 1, len, f) == len;
	ok = fclose(f) == 0 && ok;
	if (!ok) {
		remove(tmp);
		return;
	}
	remove(path);
	rename(tmp, path);
}

static void read_file(DiskLoad *l)
{
	FILE *f = fopen(l->path, "rb");
	if (!f) return;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (n >= 0 && (l->data = malloc(n + 1))) {
		l->len = fread(l->data, 1, n, f);
		l->data[l->len] = 0;
	}
	fclose(f);
}

static void disk_main(void *arg)
{
	(void)arg;
	for (;;) {
		// Primero las escrituras: así una lectura que llega después ve lo último guardado
		char path[96];
		void *data = NULL;
		size_t len = 0;
		DiskLoad *ld = NULL;
		bool quit;
		LightLock_Lock(&dlock);
		for (int i = 0; i < DISK_SAVES && !data; i++)
			if (dsaves[i].used) {
				memcpy(path, dsaves[i].path, sizeof path);
				data = dsaves[i].data;
				len = dsaves[i].len;
				dsaves[i].used = false;
				dsaves[i].data = NULL;
			}
		for (int i = 0; i < DISK_LOADS && !data && !ld; i++)
			if (dloads[i]) {
				ld = dloads[i];
				dloads[i] = NULL;
				ld->busy = true;
			}
		quit = dquit;
		LightLock_Unlock(&dlock);
		if (data) {
			write_file(path, data, len);
			free(data);
		} else if (ld) {
			read_file(ld);
			LightLock_Lock(&dlock);
			bool drop = ld->abandoned;
			ld->busy = false;
			ld->done = true;
			LightLock_Unlock(&dlock);
			if (drop) {
				free(ld->data);
				free(ld);
			}
		} else if (quit) {
			return;
		} else {
			LightEvent_Wait(&dwake);
		}
	}
}

void disk_save(const char *path, const void *data, size_t len)
{
	void *copy = malloc(len ? len : 1);
	if (!dthread || !copy) {
		free(copy);
		write_file(path, data, len);
		return;
	}
	memcpy(copy, data, len);
	LightLock_Lock(&dlock);
	int k = -1;
	for (int i = 0; i < DISK_SAVES; i++)
		if (dsaves[i].used && !strcmp(dsaves[i].path, path)) k = i;
	for (int i = 0; i < DISK_SAVES && k < 0; i++)
		if (!dsaves[i].used) k = i;
	if (k >= 0) {
		free(dsaves[k].data);
		snprintf(dsaves[k].path, sizeof dsaves[k].path, "%s", path);
		dsaves[k].data = copy;
		dsaves[k].len = len;
		dsaves[k].used = true;
	}
	LightLock_Unlock(&dlock);
	if (k < 0) {
		// Cola llena (no debería pasar): se guarda aquí mismo
		write_file(path, copy, len);
		free(copy);
	}
	LightEvent_Signal(&dwake);
}

DiskLoad *disk_load(const char *path)
{
	DiskLoad *l = calloc(1, sizeof *l);
	if (!l) return NULL;
	snprintf(l->path, sizeof l->path, "%s", path);
	int k = -1;
	if (dthread) {
		LightLock_Lock(&dlock);
		for (int i = 0; i < DISK_LOADS && k < 0; i++)
			if (!dloads[i]) {
				dloads[i] = l;
				k = i;
			}
		LightLock_Unlock(&dlock);
	}
	if (k < 0) {
		read_file(l);
		l->done = true;
	} else {
		LightEvent_Signal(&dwake);
	}
	return l;
}

void disk_load_free(DiskLoad *l)
{
	if (!l) return;
	bool later = false;
	if (dthread) {
		LightLock_Lock(&dlock);
		for (int i = 0; i < DISK_LOADS; i++)
			if (dloads[i] == l) dloads[i] = NULL;
		if (l->busy) later = l->abandoned = true;   // lo suelta el hilo de disco al acabar
		LightLock_Unlock(&dlock);
	}
	if (later) return;
	free(l->data);
	free(l);
}

// ----------------------------------------------------------------------------

void util_init(void)
{
	have_mcu = R_SUCCEEDED(mcuHwcInit());
	ptmuInit();
	LightLock_Init(&dlock);
	LightEvent_Init(&dwake, RESET_ONESHOT);
	// Con menos prioridad que la interfaz: lee y escribe mientras ella espera al siguiente frame
	s32 prio = 0x30;
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	dthread = threadCreate(disk_main, NULL, 32 * 1024, prio + 1, -2, false);
}

void util_exit(void)
{
	// Lo pendiente se guarda antes de salir
	if (dthread) {
		LightLock_Lock(&dlock);
		dquit = true;
		LightLock_Unlock(&dlock);
		LightEvent_Signal(&dwake);
		threadJoin(dthread, U64_MAX);
		threadFree(dthread);
		dthread = NULL;
	}
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
		else if (!strcmp(line, "sound")) g_cfg.quiet = !strcmp(v, "off");
	}
	fclose(f);
}

void config_save(void)
{
	char buf[160];
	int n = snprintf(buf, sizeof buf, "lang=%s\ncountry=%s\nbirth=%s\nreqs=%s\nsound=%s\n", g_cfg.lang, g_cfg.country, g_cfg.birth,
		g_cfg.reqs_mac ? "mac" : "win", g_cfg.quiet ? "off" : "on");
	disk_save(APP_DIR "/config.ini", buf, n);
}
