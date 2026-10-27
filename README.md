# Steam3DS

La tienda de Steam y tu cuenta, en tu Nintendo 3DS.

Steam3DS es una aplicación casera (homebrew) para Nintendo 3DS y New Nintendo 3DS que te permite
explorar la tienda de Steam, gestionar tu cuenta y comprar sin salir de la consola. Habla
directamente con los servicios oficiales de Steam: no necesita un PC encendido ni ningún servidor
intermedio.

## Qué puedes hacer

- **Tienda**: Inicio con Más vendidos, Novedades, Próximamente, Gratis y Para ti (tu cola de
  descubrimiento), Ofertas, Demos, Betas y Buscar (con filtro de precio y las búsquedas populares).
- **Ficha de cada juego**: capturas y vídeos, requisitos, opiniones (con filtros y en otros idiomas
  si no hay en el tuyo), logros (y comparar con un amigo), DLC y contenido similar, compatibilidad
  con Steam Deck, noticias oficiales, seguir e ignorar juegos.
- **Comprar**: carrito, lista de deseos, regalar a un amigo y pagar con tu monedero de Steam o tu
  tarjeta guardada (con la verificación del banco por QR). Añadir juegos y demos gratis y activar
  códigos de producto o del monedero.
- **Tu cuenta**: perfil (el tuyo y el de tus amigos) con nivel, insignias, vitrinas, actividad
  reciente, capturas y reseñas; biblioteca como la del cliente de Steam (sin tus juegos ocultos ni
  los playtests cerrados, con los juegos que van dentro de otro agrupados, tus demos y tu Familia de
  Steam; sale al momento y se actualiza por detrás); amigos y solicitudes; notificaciones; apareces
  «En línea» mientras usas la app; descargas de tu PC (pausar, reanudar, desinstalar) e instalación
  remota; tienda de puntos; historial de compras; tus reseñas; privacidad y tema del perfil; varias
  cuentas.
- **Idiomas**: español, inglés y japonés.

## Instalación

1. Necesitas una consola con firmware personalizado (Luma3DS).
2. Descarga `Steam3DS.cia` de la última versión publicada.
3. Instálalo con FBI (o cualquier instalador de CIA) y abre Steam3DS desde el menú HOME.

También se publica `Steam3DS.3dsx` para abrirlo desde el Homebrew Launcher.

## Condiciones de uso

Al iniciar sesión con una cuenta por primera vez (y cada vez que cambien) hay que leer las
condiciones de uso y el aviso legal hasta el final y aceptarlas; sin sesión, la tienda se puede ver
igual. Cada cuenta deja su registro firmado en la SD, en `/3ds/Steam3DS/aceptacion_<cuenta>.txt`,
con la versión, la fecha, la cuenta y una copia del texto aceptado. Si se cambia a mano o se copia de
otra consola deja de valer y Steam3DS vuelve a pedir las condiciones.

## Iniciar sesión

Abre el menú con START → Iniciar sesión y escanea el código QR con la app de Steam de tu móvil.
La sesión se guarda en la tarjeta SD (`/3ds/Steam3DS/`); si pierdes la tarjeta, cierra la sesión
desde la web de Steam (Ajustes de la cuenta → Gestionar dispositivos).

## Privacidad y seguridad

- Solo se conecta con Steam (tienda, comunidad, API web y sus servidores oficiales, estos últimos
  como lo hace el chat web de steamcommunity.com). No hay ningún servidor intermedio.
- «Aparecer en línea» se puede desactivar en Configuración. El estado es de toda tu cuenta: si
  estabas «Invisible» en otro dispositivo, con la opción activada pasas a «En línea».
- Ninguna acción que cambie tu cuenta (comprar, regalar, canjear, desinstalar…) se hace sin que la
  confirmes en la consola.
- El registro de diagnóstico (`/3ds/Steam3DS/net.log`) no guarda contraseñas, tokens ni cookies.

## Compilar

Con Docker y la imagen oficial de devkitPro:

```sh
./mk.sh                     # Steam3DS.3dsx
TOOLS=/ruta/a/bin ./tools/build_cia.sh   # Steam3DS.cia (bannertool y makerom)
```

Los registros de aceptación de las condiciones se firman con la clave de `secret/terms.key`, que no
está en el repositorio. Sin ella, la compilación firma con una clave de pruebas y sus registros no
pasan la comprobación de las versiones oficiales (`tools/verificar_aceptacion.py`).

Pruebas de la lectura de datos en el PC: `tests/unit` (ver `tests/unit/test_steam.c`).

## Aviso legal

Steam3DS es un proyecto independiente y no está afiliado, patrocinado ni respaldado por Valve
Corporation ni por Nintendo. Steam y el logotipo de Steam son marcas comerciales o marcas
registradas de Valve Corporation. Nintendo 3DS es una marca registrada de Nintendo.
