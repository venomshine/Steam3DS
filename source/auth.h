#pragma once
#include <3ds.h>
#include <stdbool.h>

// Sesión de Steam con el QR oficial: la consola enseña un código, se aprueba en la app de Steam
// del móvil y Steam da un token. La contraseña y Steam Guard nunca pasan por la consola.

typedef enum {
	AUTH_OUT,          // sin sesión
	AUTH_QR,           // enseñando el QR, esperando a que se apruebe
	AUTH_STARTING,     // con sesión guardada, renovando el token
	AUTH_IN,           // dentro
} AuthState;

typedef struct {
	AuthState state;
	u64 steamid;
	char account[64];       // nombre de la cuenta (para iniciar sesión)
	char persona[64];       // nombre visible
	char avatar[128];       // URL del avatar mediano
	int level;
	char qr_url[96];        // lo que va en el QR
	bool qr_scanned;        // el móvil ya lo escaneó y falta aprobar
	char err[112];          // último error, para enseñarlo
	u64 changed;            // osGetTime() del último cambio (para animar)
} AuthInfo;

void auth_init(void);
void auth_exit(void);
void auth_update(void);           // una vez por frame
void auth_begin_qr(void);
void auth_cancel_qr(void);
void auth_logout(void);
bool auth_logged(void);
const AuthInfo *auth_info(void);
const char *auth_token(void);     // token de acceso para la Web API ("" sin sesión)
// Cabecera «Cookie: …» de la web de Steam para un dominio (p. ej. checkout.steampowered.com)
bool auth_web_cookies(const char *host, char *out, size_t sz);
// El avatar de 64 px a partir de sha_digest_avatar (base64), como lo dan las APIs de Steam
bool auth_avatar_url(const char *sha_b64, char *out, size_t n);
const char *auth_sessionid(void);  // el sessionid de esas cookies (va también en los formularios)
