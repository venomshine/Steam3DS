#pragma once
#include <3ds.h>
#include <stdbool.h>

// Pagar el carrito desde la consola, como la página de pago de Steam (checkout.steampowered.com).
// Solo con lo que ya tienes en Steam: la cartera o la tarjeta guardada. Nunca se ven ni se
// cambian los datos de la tarjeta (solo los cuatro últimos números); el CVV, si Steam lo pide,
// se manda y se borra.

typedef enum {
	PAY_NONE,
	PAY_LOADING,        // leyendo la página de pago y la cartera
	PAY_CHOOSE,         // elegir cartera o tarjeta
	PAY_PREPARING,      // Steam prepara la compra y calcula el total
	PAY_REVIEW,         // total final: confirmar (manteniendo A)
	PAY_PAYING,         // pagando
	PAY_BANK,           // el banco pide verificar (QR para el móvil)
	PAY_DONE,
	PAY_FAILED,
} PayState;

typedef enum { PM_WALLET, PM_CARD } PayMethod;

typedef struct {
	PayState state;
	u64 since;                  // osGetTime() del último cambio de estado

	bool wallet;                // se puede pagar con la cartera
	long long wallet_cents;
	char wallet_fmt[24];
	bool card;
	char card_type[16], card_last4[8];

	PayMethod method;
	char total_fmt[24];
	long long total_cents;
	bool short_wallet;          // la cartera no llega para el total
	bool need_cvv, cvv_set;

	char bank_url[160];         // dónde se verifica la compra (en el QR)
	char receipt_total[24], receipt_id[32];
	bool approval;              // la compra quedó esperando aprobación (cuentas familiares)
	bool pending;               // Steam la terminará más tarde (recibo pendiente)

	int detail;                 // EPurchaseResultDetail si falló
	char err[64];               // fallo técnico (para el registro y la pantalla)
	char err_desc[192];         // el motivo que da Steam, si lo da (errorDescription, sin HTML)
	bool maybe_paid;            // falló al comprobar el estado: puede que se haya cobrado
} PayInfo;

void pay_open(void);
void pay_choose(PayMethod m);
void pay_set_cvv(const char *cvv);   // se copia y se borra tras usarlo
void pay_confirm(void);
void pay_close(void);                // cancela la compra si aún no se pagó
void pay_update(void);
const PayInfo *pay_info(void);
bool pay_busy(void);                 // pagando o esperando al banco: no se puede salir
