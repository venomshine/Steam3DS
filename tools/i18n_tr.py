# Traducciones de Steam3DS. La clave es el texto en español: (inglés, japonés).
# Después de tocar este archivo: python3 tools/gen_i18n.py

# Textos que llegan a _() a través de una variable (pestañas, apartados, pistas, clasificaciones)
INDIRECT = [
    "Inicio", "Ofertas", "Demos", "Betas",
    "Resumen", "Requisitos", "Opiniones", "Extras", "Similares",
    "Ficha", "Pestaña", "Salir", "Elegir", "Abrir ficha", "Apartado", "Volver", "Comprar en el móvil", "Cambiar",
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
    "Cargando ofertas…": ("Loading specials…", "セールを読み込み中…"),
    "No se pudieron cargar las ofertas": ("Couldn't load the specials", "セールを読み込めませんでした"),
    "Pulsa A para reintentar": ("Press A to retry", "Aで再試行"),
    "Oferta %d de %d": ("Deal %d of %d", "セール %d / %d"),
    "Esta pestaña llega en la próxima fase": ("This tab is coming in the next update", "このタブは次のアップデートで追加されます"),
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
    "Este juego no tiene contenido descargable": ("This game has no downloadable content", "このゲームにDLCはありません"),
    "MÁS COMO %s": ("MORE LIKE %s", "%s に似たゲーム"),
    "Steam no tiene juegos parecidos para este": ("Steam has no similar games for this one", "似たゲームは見つかりませんでした"),

    # Capturas
    "Captura %d de %d": ("Screenshot %d of %d", "スクリーンショット %d / %d"),
    "No se pudo cargar la captura": ("Couldn't load the screenshot", "スクリーンショットを読み込めませんでした"),
    "‹ Anterior": ("‹ Previous", "‹ 前へ"),
    "Siguiente ›": ("Next ›", "次へ ›"),

    # Salir
    "¿Salir de Steam3DS?": ("Exit Steam3DS?", "Steam3DSを終了しますか？"),
    "Cancelar": ("Cancel", "キャンセル"),
}
