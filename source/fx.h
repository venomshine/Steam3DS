#pragma once
#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>

// Efectos de la interfaz: fondo con textura, cristal, botones con bisel y destello, haz de luz
// en el borde de lo elegido, esquinas de HUD, onda al tocar y confeti. Todo pensado para que
// cueste poco en la consola (texturas generadas una vez, pocas figuras por frame).

void fx_init(void);
void fx_exit(void);
void fx_update(float dt);

// Fondo de pantalla: degradado, rejilla de puntos, ruido fino, viñeta y algún punto que parpadea
void fx_backdrop(gfxScreen_t scr, float w, float h);

// Panel de cristal (degradado, brillo arriba, borde fino y sombra)
void fx_panel(float x, float y, float w, float h, float r, float alpha);
// Barra (estado, pestañas, pistas): cristal oscuro con una línea de luz en un borde
void fx_bar(float x, float y, float w, float h, bool line_bottom);

// Botón: degradado con bisel, brillo que lo cruza de vez en cuando (shine) y halo si está elegido
typedef struct {
	u32 top, bot;       // colores del degradado
	float alpha;        // 1 normal, menos = apagado
	bool pressed;
	bool shine;         // destello periódico (botones principales)
	bool focus;         // haz de luz recorriendo el borde
	float seed;         // desfase del destello (para que no brillen todos a la vez)
} FxButton;
void fx_button(float x, float y, float w, float h, float r, const FxButton *b);

// Haz de luz que recorre el borde de un rectángulo (border beam)
void fx_beam(float x, float y, float w, float h, u32 col, float speed, float len);
// Esquinas de HUD: cuatro «L» finas
void fx_brackets(float x, float y, float w, float h, float len, u32 col);
// Línea de neón (con halo)
void fx_glow_line(float x, float y, float w, u32 col);
// Halo suave alrededor de un rectángulo redondeado
void fx_glow(float x, float y, float w, float h, float r, u32 col, float spread);

// Onda al tocar: fx_ripple() en el punto tocado; se dibuja sola en la pantalla de abajo
void fx_ripple(float x, float y);
void fx_ripples_draw(void);

// Confeti (al comprar): arranca en (x, y) y cae unos segundos
void fx_confetti(float x, float y);
void fx_confetti_draw(void);

// Número que sube hasta su valor (number ticker): devuelve el valor mostrado para un objetivo
float fx_ticker(float *shown, float target, float speed);
