#pragma once
#include <3ds.h>
#include <stdbool.h>
#include "steam.h"

// Acciones con tu cuenta: carrito, deseados y juegos gratis. Una a la vez; la interfaz
// pregunta cada frame y enseña el resultado con shop_event().

typedef enum { SH_CART_ADD, SH_CART_REMOVE, SH_WISH_ADD, SH_WISH_REMOVE, SH_FREE } ShopKind;

typedef struct {
	ShopKind kind;
	bool ok;
	u32 appid;
	int code;           // EResult (o el detalle de la compra) si falló
	char what[96];      // nombre de lo que se tocó
} ShopEvent;

void shop_update(void);
void shop_reset(void);            // al cerrar sesión
bool shop_busy(void);
bool shop_event(ShopEvent *ev);   // true una vez por cada acción terminada

// Formas de comprar el juego abierto
void shop_buyopts_load(u32 appid);
// Devuelve las opciones si ya llegaron (n = 0 si no hay a la venta); NULL mientras carga o si falló
const BuyOpt *shop_buyopts(u32 appid, int *n, bool *failed);

// Carrito
void shop_cart_load(void);
const CartInfo *shop_cart(void);
bool shop_cart_loaded(void);
bool shop_cart_loading(void);
u32 shop_cart_version(void);      // cambia cada vez que cambia el carrito
bool shop_in_cart(u32 appid, const BuyOpt *opts, int nopts);
void shop_cart_add(u32 appid, const BuyOpt *o);
void shop_cart_remove(int line);

// Deseados y gratis
void shop_wish(u32 appid, const char *name, bool add);
void shop_free(u32 appid, const char *name);
bool shop_busy_with(ShopKind k, u32 appid);
