# Traducciones de Steam3DS. La clave es el texto en español: (inglés, japonés).
# Después de tocar este archivo: python3 tools/gen_i18n.py

# Textos que llegan a _() a través de una variable (pestañas, apartados, pistas, clasificaciones)
INDIRECT = [
    "Inicio", "Ofertas", "Demos", "Betas",
    "Resumen", "Requisitos", "Opiniones", "Extras", "Similares",
    "Ficha", "Pestaña", "Salir", "Menú",
    "Iniciar sesión con el móvil", "Mi perfil", "Biblioteca", "Lista de deseos", "Cerrar sesión", "Salir de Steam3DS",
    "Abre la app de Steam en el móvil", "Toca el escudo de Steam Guard", "Elige «Escanear un código QR» y apunta aquí", "Elegir", "Abrir ficha", "Apartado", "Volver", "Comprar en el móvil", "Cambiar", "Abrir", "Borrar",
    "Lo más vendido", "Novedades populares", "Próximamente", "Gratis para jugar",
    "Violencia", "Sangre", "Sexo", "Desnudos", "Lenguaje soez", "Drogas", "Miedo", "Juegos de azar",
    "Discriminación", "Compras en el juego", "Humor adulto",
]

TR = {
    # Pestañas y apartados
    "Inicio": ("Home", "ホーム"),
    "Ofertas": ("Specials", "セール"),
    "Demos": ("Demos", "体験版"),
    "Betas": ("Betas", "ベータ"),
    "Buscar": ("Search", "検索"),
    "Resumen": ("Overview", "概要"),
    "Requisitos": ("Specs", "動作環境"),
    "Opiniones": ("Reviews", "レビュー"),
    "Extras": ("Extras", "追加"),
    "Similares": ("Similar", "類似"),

    # Pistas de botones
    "Ficha": ("Details", "詳細"),
    "Pestaña": ("Tab", "タブ"),
    "Salir": ("Exit", "終了"),
    "Elegir": ("Select", "選択"),
    "Abrir ficha": ("Open", "開く"),
    "Apartado": ("Section", "切替"),
    "Volver": ("Back", "戻る"),
    "Comprar en el móvil": ("Buy on phone", "スマホで購入"),
    "Cambiar": ("Change", "切替"),

    # Tienda
    "Lo más vendido": ("Top sellers", "トップセラー"),
    "Novedades populares": ("Popular new releases", "人気の新作"),
    "Gratis para jugar": ("Free to play", "基本プレイ無料"),
    "Demo %d de %d": ("Demo %d of %d", "体験版 %d / %d"),
    "Acceso anticipado": ("Early Access", "早期アクセス"),
    "%d resultados para «%s»": ("%d results for “%s”", "「%s」の検索結果 %d 件"),
    "No se pudo cargar esta lista": ("Couldn't load this list", "リストを読み込めませんでした"),
    "No hay resultados para «%s»": ("No results for “%s”", "「%s」の検索結果はありません"),
    "Prueba con otras palabras o con el nombre en inglés": ("Try other words or the English name", "別の言葉や英語名で試してください"),
    "Cargando…": ("Loading…", "読み込み中…"),
    "Busca juegos, DLC y demos": ("Search games, DLC and demos", "ゲーム・DLC・体験版を検索"),
    "Toca la barra de abajo o pulsa Y para escribir": ("Tap the bar below or press Y to type", "下のバーをタッチするかYで入力"),
    "Abajo, tus búsquedas recientes": ("Your recent searches are below", "下に最近の検索があります"),
    "Buscar en la tienda…": ("Search the store…", "ストアを検索…"),
    "Aún no has buscado nada": ("You haven't searched anything yet", "まだ検索していません"),
    "DEMO": ("DEMO", "体験版"),
    "Abrir": ("Open", "開く"),
    "Borrar": ("Clear", "消去"),
    "Nombre del juego": ("Game name", "ゲーム名"),
    "JUEGO COMPLETO": ("FULL GAME", "製品版"),
    "Esta demo es una muestra de este juego.": ("This demo is a sample of this game.", "この体験版はこのゲームの一部です。"),
    "Reintentando en %d s · A para ya": ("Retrying in %d s · A to retry now", "%d 秒後に再試行・Aですぐに"),
    "No se encuentra el servidor de Steam": ("Can't find the Steam server", "Steamのサーバーが見つかりません"),
    "No se pudo conectar con Steam": ("Couldn't connect to Steam", "Steamに接続できませんでした"),
    "Steam tarda demasiado en responder": ("Steam is taking too long to answer", "Steamの応答が遅すぎます"),
    "Falló la conexión segura con Steam": ("The secure connection to Steam failed", "Steamとの安全な接続に失敗しました"),
    "Se cortó la conexión": ("The connection dropped", "接続が切れました"),
    "Error de red": ("Network error", "ネットワークエラー"),
    "Steam pide esperar un poco": ("Steam asks to wait a moment", "Steamから少し待つよう求められました"),
    "Steam no responde bien ahora mismo": ("Steam isn't working well right now", "Steamが現在正常に応答していません"),
    "Steam respondió con un error": ("Steam answered with an error", "Steamがエラーを返しました"),
    "Steam mandó una respuesta inesperada": ("Steam sent an unexpected answer", "Steamから予期しない応答がありました"),
    "Oferta %d de %d": ("Deal %d of %d", "セール %d / %d"),
    "Próximamente": ("Coming soon", "近日公開"),
    "Reseñas: ": ("Reviews: ", "レビュー: "),

    # Red
    "Sin conexión Wi-Fi": ("No Wi-Fi connection", "Wi-Fiに接続されていません"),
    "Conectando al Wi-Fi…": ("Connecting to Wi-Fi…", "Wi-Fiに接続中…"),
    "La red de la consola no responde": ("The console's network isn't responding", "本体のネットワークが応答しません"),
    "Se reintenta solo": ("Retrying automatically", "自動で再試行します"),

    # Ficha
    "Cargando ficha…": ("Loading…", "読み込み中…"),
    "No se pudo cargar la ficha": ("Couldn't load this game", "このゲームを読み込めませんでした"),
    "Comprar": ("Buy", "購入"),
    "Jugar": ("Play", "プレイ"),
    "Gratis": ("Free", "無料"),
    "No disponible": ("Not available", "販売なし"),
    "Añadir al carrito": ("Add to cart", "カートに入れる"),
    "Añadir a la biblioteca": ("Add to library", "ライブラリに追加"),
    "Deseados": ("Wishlist", "ウィッシュリスト"),
    "Capturas (%d)": ("Screenshots (%d)", "スクリーンショット (%d)"),
    "Instalar en mi PC": ("Install on my PC", "PCにインストール"),
    "Inicia sesión para usar el carrito, deseados e instalar": (
        "Sign in to use the cart, wishlist and install", "カート・ウィッシュリスト・インストールはサインイン後に使えます"),
    "Disponible al iniciar sesión": ("Available after signing in", "サインイン後に使えます"),
    "Comprar en el móvil": ("Buy on phone", "スマホで購入"),
    "Escanea el código con la cámara: se abre este juego en la tienda de Steam y lo pagas desde ahí.": (
        "Scan the code with your camera: this game opens in the Steam store and you pay there.",
        "カメラでコードを読み取ると、Steamストアでこのゲームが開きます。そこで購入できます。"),
    "B para volver": ("B to go back", "Bで戻る"),

    # Características
    "1 jugador": ("Single-player", "シングルプレイヤー"),
    "Multijugador": ("Multiplayer", "マルチプレイヤー"),
    "Cooperativo": ("Co-op", "協力プレイ"),
    "JcJ": ("PvP", "PvP"),
    "%d logros": ("%d achievements", "実績 %d"),
    "Logros": ("Achievements", "実績"),
    "Cromos": ("Trading cards", "トレカ"),
    "Préstamo familiar": ("Family Sharing", "ファミリーシェア"),
    "Mando completo": ("Full controller", "コントローラ完全対応"),
    "Mando parcial": ("Partial controller", "コントローラ一部対応"),
    "Compras en el juego": ("In-game purchases", "ゲーム内購入"),

    # Clasificación por edades
    "Violencia": ("Violence", "暴力"),
    "Sangre": ("Blood", "流血"),
    "Sexo": ("Sex", "性的表現"),
    "Desnudos": ("Nudity", "ヌード"),
    "Lenguaje soez": ("Bad language", "汚い言葉"),
    "Drogas": ("Drugs", "薬物"),
    "Miedo": ("Fear", "恐怖"),
    "Juegos de azar": ("Gambling", "ギャンブル"),
    "Discriminación": ("Discrimination", "差別"),
    "Humor adulto": ("Mature humor", "大人向けユーモア"),

    # Requisitos
    " · Requisitos (Windows)": (" · System requirements (Windows)", " · 動作環境 (Windows)"),
    "MÍNIMO": ("MINIMUM", "最低"),
    "RECOMENDADO": ("RECOMMENDED", "推奨"),
    "Steam no indica requisitos para este juego": ("Steam doesn't list requirements for this game", "このゲームの動作環境は記載されていません"),
    "También en macOS": ("Also on macOS", "macOSにも対応"),
    "También en Linux": ("Also on Linux", "Linuxにも対応"),
    "También en macOS y Linux": ("Also on macOS and Linux", "macOSとLinuxにも対応"),

    # Opiniones
    "%d %% de %d reseñas": ("%d %% of %d reviews", "%d %% (%d 件)"),
    "Recomendado": ("Recommended", "おすすめ"),
    "No recomendado": ("Not recommended", "おすすめしません"),
    "%d h jugadas": ("%d h played", "%d 時間プレイ"),
    "Todavía no hay reseñas en tu idioma": ("No reviews in your language yet", "あなたの言語のレビューはまだありません"),

    # Extras y similares
    "CONTENIDO DESCARGABLE (%d)": ("DOWNLOADABLE CONTENT (%d)", "ダウンロードコンテンツ (%d)"),
    "CONTENIDO NECESARIO": ("REQUIRED CONTENT", "必要なコンテンツ"),
    "Para usar este contenido descargable necesitas tener el juego base en tu biblioteca.": (
        "To use this downloadable content you need the base game in your library.",
        "このダウンロードコンテンツを使うには、ライブラリに本編が必要です。"),
    "Este juego no tiene contenido descargable": ("This game has no downloadable content", "このゲームにDLCはありません"),
    "MÁS COMO %s": ("MORE LIKE %s", "%s に似たゲーム"),
    "Steam no tiene juegos parecidos para este": ("Steam has no similar games for this one", "似たゲームは見つかりませんでした"),

    # Capturas
    "No se pudo cargar la captura": ("Couldn't load the screenshot", "スクリーンショットを読み込めませんでした"),
    "‹ Anterior": ("‹ Previous", "‹ 前へ"),
    "Siguiente ›": ("Next ›", "次へ ›"),

    # Pantalla de carga
    "hecho por VENOMSHiNE": ("made by VENOMSHiNE", "VENOMSHiNE 作"),

    # Menú y sesión
    "Menú": ("Menu", "メニュー"),
    "Iniciar sesión con el móvil": ("Sign in with your phone", "スマホでサインイン"),
    "Mi perfil": ("My profile", "プロフィール"),
    "Biblioteca": ("Library", "ライブラリ"),
    "Lista de deseos": ("Wishlist", "ウィッシュリスト"),
    "Cerrar sesión": ("Sign out", "サインアウト"),
    "Salir de Steam3DS": ("Exit Steam3DS", "Steam3DSを終了"),
    "Sesión cerrada": ("Signed out", "サインアウトしました"),
    "Llega en la siguiente parte de esta fase": ("Coming in the next part of this update", "次のアップデートで追加されます"),
    "Nivel %d": ("Level %d", "レベル %d"),
    "Sin iniciar sesión": ("Not signed in", "サインインしていません"),
    "Hola, %s": ("Hi, %s", "こんにちは、%s"),
    "Ya tienes tu sesión de Steam en la consola": ("Your Steam session is now on the console", "Steamにサインインしました"),
    "No se pudo pedir el código a Steam": ("Couldn't get the code from Steam", "Steamからコードを取得できませんでした"),
    "Pidiendo el código a Steam…": ("Getting the code from Steam…", "Steamからコードを取得中…"),
    "Inicia sesión con Steam": ("Sign in with Steam", "Steamにサインイン"),
    "Abre la app de Steam en el móvil": ("Open the Steam app on your phone", "スマホでSteamアプリを開く"),
    "Toca el escudo de Steam Guard": ("Tap the Steam Guard shield", "Steamガードの盾をタップ"),
    "Elige «Escanear un código QR» y apunta aquí": ("Choose “Scan a QR code” and point it here", "「QRコードをスキャン」を選んでここに向ける"),
    "Escaneado: apruébalo en el móvil": ("Scanned: approve it on your phone", "スキャン完了：スマホで承認してください"),
    "Esperando al móvil…": ("Waiting for your phone…", "スマホを待っています…"),
    "Tu contraseña nunca pasa por la consola": ("Your password never goes through the console", "パスワードが本体を通ることはありません"),
    "Apruebas la entrada desde la app oficial de Steam. La sesión se guarda en la tarjeta SD; puedes cerrarla cuando quieras desde el menú (START) o desde Steam, en Dispositivos autorizados.": (
        "You approve the sign-in from the official Steam app. The session is saved on the SD card; you can end it any time from the menu (START) or in Steam, under Authorized devices.",
        "サインインは公式Steamアプリで承認します。セッションはSDカードに保存され、メニュー（START）またはSteamの「承認済みデバイス」からいつでも終了できます。"),
    "Tu sesión de Steam caducó: vuelve a iniciarla": ("Your Steam session expired: sign in again", "Steamのセッションが切れました。もう一度サインインしてください"),

    # Perfil, biblioteca y deseados
    "Has jugado %d h": ("You've played %d h", "%d 時間プレイ済み"),
    "Aún no lo has jugado": ("You haven't played it yet", "まだプレイしていません"),
    "Deseado %d de %d": ("Wishlisted %d of %d", "ウィッシュリスト %d / %d"),
    "Biblioteca · %d juegos": ("Library · %d games", "ライブラリ · %d 本"),
    "Lista de deseos · %d": ("Wishlist · %d", "ウィッシュリスト · %d"),
    "Tu lista de deseos está vacía": ("Your wishlist is empty", "ウィッシュリストは空です"),
    "Añade juegos desde su ficha con el botón Deseados": ("Add games from their page with the Wishlist button", "ゲームのページのウィッシュリストボタンで追加できます"),
    "No hay juegos en tu biblioteca": ("There are no games in your library", "ライブラリにゲームがありません"),
    "Nivel de Steam": ("Steam level", "Steamレベル"),
    "Cargando tu biblioteca…": ("Loading your library…", "ライブラリを読み込み中…"),
    "%d juegos · %ld h jugadas": ("%d games · %ld h played", "%d 本 · %ld 時間プレイ"),
    "LO MÁS JUGADO": ("MOST PLAYED", "よくプレイしたゲーム"),
    "Tu cuenta de Steam": ("Your Steam account", "あなたのSteamアカウント"),
    "Desde aquí ves lo tuyo; la tienda funciona igual": ("Your own stuff is here; the store works the same", "ここで自分の情報を確認できます"),
    "Biblioteca (%d)": ("Library (%d)", "ライブラリ (%d)"),
    "Lista de deseos (%d)": ("Wishlist (%d)", "ウィッシュリスト (%d)"),

    # Salir
    "¿Salir de Steam3DS?": ("Exit Steam3DS?", "Steam3DSを終了しますか？"),
    "Cancelar": ("Cancel", "キャンセル"),
    "Volverás al menú HOME.": ("You'll go back to the HOME Menu.", "HOMEメニューに戻ります。"),
}
