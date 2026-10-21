#pragma once
#include <citro2d.h>
#include <stdbool.h>

// Imágenes de la tienda: se descargan cuando se piden y se guardan unas pocas en memoria.

typedef struct {
	C2D_Image img;
	int w, h;           // tamaño real de la imagen
} Pic;

void img_init(void);
void img_exit(void);
void img_update(void);                          // una vez por frame: sube a la GPU lo que llegó
const Pic *img_get(const char *url, int max_w); // NULL mientras se descarga
bool img_failed(const char *url);

// Dibuja la imagen para que cubra el rectángulo (recortando lo que sobre) o que quepa dentro.
void img_draw_cover(const Pic *p, float x, float y, float w, float h, float focus_y);
void img_draw_fit(const Pic *p, float x, float y, float w, float h);
