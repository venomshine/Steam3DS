#pragma once
// Conexión corta con los servidores de Steam (CM), como la del chat web de steamcommunity.com:
// WebSocket a un servidor oficial, sesión de chat web (token de /chat/clientjstoken) y una llamada
// a un servicio. Sirve para lo que la web de Steam no da por su API: la colección «Ocultos».
// Todo es bloqueante: se llama desde un hilo aparte (cm_hidden_start / cm_hidden_poll).

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Tus juegos ocultos de Steam. jar: las cookies de la sesión web (auth_web_jar). Devuelve cuántos
// (y *ids, que se libera con free) o -1 si falla (err dice por qué, sin datos de la cuenta)
int cm_fetch_hidden(const char *jar, uint32_t **ids, char *err, size_t errsz);

// Lo mismo en un hilo: cm_hidden_start lo lanza (si no hay otro en marcha); cm_hidden_poll da
// true cuando terminó, con el resultado (n < 0 si falló)
bool cm_hidden_start(const char *jar);
bool cm_hidden_poll(uint32_t **ids, int *n, char *err, size_t errsz);
// Y en la misma conexión, las apps de tus licencias (como las ve el cliente de Steam): tras
// cm_hidden_poll, una vez (*apps se libera con free); -1 si Steam no las mandó
int cm_licensed(uint32_t **apps);

// Cuáles de estos playtests ya cerró su estudio (como los ve el cliente de Steam: la ficha de la app,
// con el token de acceso de tu cuenta). En un hilo, como los ocultos; *ids, los cerrados
int cm_fetch_closed(const char *jar, const uint32_t *apps, int n, uint32_t **ids, char *err, size_t errsz);
bool cm_closed_start(const char *jar, const uint32_t *apps, int n);
bool cm_closed_poll(uint32_t **ids, int *n, char *err, size_t errsz);

// «En línea» en Steam mientras usas la app (como el chat web abierto). want: conectarse o no; jar:
// las cookies de la sesión web (se copian; se puede llamar a menudo para tenerlas al día); steamid:
// tu cuenta (si cambia, se reconecta con la nueva). cm_presence_state: 0 no, 1 conectando, 2 en línea
void cm_presence_set(bool want, const char *jar, uint64_t steamid);
int cm_presence_state(void);
void cm_presence_stop(void);   // al salir: cierra la sesión (Steam te pasa a «Desconectado»)
