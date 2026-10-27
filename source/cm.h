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
