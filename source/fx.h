#pragma once
#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>

// Estética de cristal (estilo «Liquid Glass» de Apple, en sencillo): fondo oscuro con manchas de
// color difuminadas, superficies translúcidas con un borde de luz fino que viene de arriba a la
// izquierda, y un leve reflejo de color (cian/naranja) en el contorno de lo elegido.
// Sin brillos que se mueven, rejillas ni marcos: lo que se mueve es el contenido.

void fx_init(void);
void fx_exit(void);

// Fondo de pantalla: degradado oscuro con manchas de color muy suaves que se desplazan despacio
void fx_backdrop(gfxScreen_t scr, float w, float h);

// Borde de luz de una forma redondeada: más claro donde da la luz (arriba a la izquierda).
// chroma: añade el reflejo de color en el contorno (para lo elegido)
void fx_rim(float x, float y, float w, float h, float r, float alpha, bool chroma);

// Superficie de cristal: relleno translúcido (tint 0 = neutro), sombra suave y borde de luz
void fx_glass(float x, float y, float w, float h, float r, float alpha, u32 tint);

// Panel flotante (menús, diálogos): cristal más opaco para que el texto se lea bien
void fx_panel(float x, float y, float w, float h, float r, float alpha);

// Barra superior o inferior: solo un velo y una línea finísima
void fx_bar(float x, float y, float w, float h, bool line_bottom);

// Botón en forma de cápsula o de rectángulo redondeado
typedef struct {
	u32 tint;           // 0 = cristal neutro; si no, cristal teñido de ese color (botones principales)
	float alpha;        // 1 normal, menos = apagado
	bool pressed;       // se encoge un poco y se aclara
	bool focus;         // reflejo de color en el borde
} FxButton;
void fx_button(float x, float y, float w, float h, float r, const FxButton *b);

// Selección (filas, opciones de menú): cápsula de cristal claro con el reflejo de color
void fx_select(float x, float y, float w, float h, float r, float alpha);

// Número que se acerca a su valor (para contadores animados)
float fx_ticker(float *shown, float target, float speed);
