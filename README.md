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
- **Tu cuenta**: perfil con nivel, insignias, vitrinas, capturas y reseñas; biblioteca (con tus
  juegos ocultos de Steam y tu Familia de Steam); amigos y solicitudes; notificaciones;
  descargas de tu PC (pausar, reanudar, desinstalar) e instalación remota; tienda de puntos;
  historial de compras; tus reseñas; privacidad y tema del perfil; varias cuentas.
- **Idiomas**: español, inglés y japonés.

## Instalación

1. Necesitas una consola con firmware personalizado (Luma3DS).
2. Descarga `Steam3DS.cia` de la última versión publicada.
3. Instálalo con FBI (o cualquier instalador de CIA) y abre Steam3DS desde el menú HOME.

También se publica `Steam3DS.3dsx` para abrirlo desde el Homebrew Launcher.

## Condiciones de uso

La primera vez que abres Steam3DS (y cada vez que cambien) tienes que leer las condiciones de uso y
el aviso legal hasta el final y aceptarlas. La aceptación queda registrada en la SD, en
`/3ds/Steam3DS/aceptacion.txt`, con la versión, la fecha, una copia del texto aceptado y las cuentas
con las que inicias sesión después. El registro va firmado para esa consola: si se cambia a mano o
se copia de otra, deja de valer y Steam3DS vuelve a pedir las condiciones.

## Iniciar sesión

Abre el menú con START → Iniciar sesión y escanea el código QR con la app de Steam de tu móvil.
La sesión se guarda en la tarjeta SD (`/3ds/Steam3DS/`); si pierdes la tarjeta, cierra la sesión
desde la web de Steam (Ajustes de la cuenta → Gestionar dispositivos).

## Privacidad y seguridad

- Solo se conecta con Steam (tienda, comunidad, API web y sus servidores oficiales).
- Ninguna acción que cambie tu cuenta (comprar, regalar, canjear, desinstalar…) se hace sin que la
  confirmes en la consola.
- El registro de diagnóstico (`/3ds/Steam3DS/net.log`) no guarda contraseñas, tokens ni cookies.

## Compilar

Con Docker y la imagen oficial de devkitPro:

```sh
./mk.sh                     # Steam3DS.3dsx
TOOLS=/ruta/a/bin ./tools/build_cia.sh   # Steam3DS.cia (bannertool y makerom)
```

Pruebas de la lectura de datos en el PC: `tests/unit` (ver `tests/unit/test_steam.c`).

## Aviso legal

Steam3DS es un proyecto independiente y no está afiliado, patrocinado ni respaldado por Valve
Corporation ni por Nintendo. Steam y el logotipo de Steam son marcas comerciales o marcas
registradas de Valve Corporation. Nintendo 3DS es una marca registrada de Nintendo.
