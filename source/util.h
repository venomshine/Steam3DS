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
	bool reqs_mac;       // requisitos de macOS en vez de los de Windows
	bool quiet;          // sin el sonido de la pantalla de carga
	bool hide_online;    // no aparecer «En línea» en Steam mientras se usa la app
} Config;

extern Config g_cfg;
void config_load(void);
void config_save(void);

// Disco en segundo plano: abrir un archivo de la SD tarda 30-50 ms y, en el hilo de la interfaz,
// se notaba como un tirón. disk_save guarda una copia de data (si ya había otra pendiente para el
// mismo archivo, gana la última); disk_load lee el archivo entero y avisa con done
typedef struct DiskLoad {
	char path[96];
	u8 *data;               // termina en 0 (para leerlo como texto); NULL si no existe
	size_t len;
	volatile bool done;
	bool busy, abandoned;   // del hilo de disco
} DiskLoad;

void disk_save(const char *path, const void *data, size_t len);
DiskLoad *disk_load(const char *path);
void disk_load_free(DiskLoad *l);   // también si aún no ha terminado
