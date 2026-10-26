#pragma once
#include <3ds.h>
#include <stdbool.h>
#include "steam.h"

// Acciones con tu cuenta: carrito, deseados y juegos gratis. Una a la vez; la interfaz
// pregunta cada frame y enseña el resultado con shop_event().

typedef enum { SH_CART_ADD, SH_CART_REMOVE, SH_WISH_ADD, SH_WISH_REMOVE, SH_FREE, SH_IGNORE, SH_UNIGNORE, SH_ACTIVATE, SH_WALLET, SH_REVIEW, SH_PLAYTEST } ShopKind;

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

// Ignorar un juego (como el botón de la tienda: deja de salir en tus recomendaciones). La lista de
// los que ya tienes ignorados llega con la de la tienda (rgIgnoredApps)
void shop_ignore(u32 appid, const char *name, bool ignore);
bool shop_ignored(u32 appid);
void shop_set_ignored(const u32 *ids, int n);

// Activar un producto: una clave de juego (XXXXX-XXXXX-XXXXX) o, si Steam dice que no es una clave
// válida, un código del monedero. El evento es SH_ACTIVATE (what: lo que se activó) o SH_WALLET
// (what: el saldo nuevo); si falla, code es 2000 + el detalle de Steam (14 no válido, 15 ya usado…)
void shop_activate(const char *code);
bool shop_busy_with(ShopKind k, u32 appid);

// Publicar o cambiar tu reseña (el formulario de la tienda web: friends/recommendgame). Si falla,
// what lleva el motivo que da Steam
void shop_review(u32 appid, const char *name, bool up, const char *text, bool is_public);

// Pedir acceso al playtest de un juego (el botón «Solicitar acceso» de la tienda:
// ajaxrequestplaytestaccess). Si sale bien, code es 1 si ya te lo han dado y 0 si queda pendiente
void shop_playtest(u32 appid, const char *name);
