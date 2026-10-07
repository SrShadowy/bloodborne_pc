# SPDX-License-Identifier: GPL-2.0-or-later
"""Launcher translations. The Russian text is the key; English is looked up by it.

ui_language in the launcher settings: "ru", "en" or "" (the system locale: Russian for ru_*,
English otherwise).
"""
import os

EN = {
    # Window, pages, buttons
    "Запустить": "Play",
    "Остановить": "Stop",
    "Настройки": "Settings",
    "Журнал": "Log",
    "Не удалось запустить: {}": "Could not start: {}",
    "\n— игра завершилась (код {}) —\n": "\n— the game exited (code {}) —\n",
    # Launcher language
    "Язык лаунчера": "Launcher language",
    "Как в системе": "System",
    "Применится после перезапуска лаунчера, пока игра запущена":
        "Applies after restarting the launcher while the game is running",
    # Game
    "Игра": "Game",
    "Папка игры (CUSA03173)": "Game folder (CUSA03173)",
    "Выбрать папку с eboot.bin": "Choose the folder with eboot.bin",
    "Открыть в файловом менеджере": "Open in the file manager",
    "Папка сохранений": "Saves folder",
    "Выбрать папку сохранений": "Choose the saves folder",
    "Вернуть папку по умолчанию": "Back to the default folder",
    "Язык системы": "System language",
    "По умолчанию: {}": "Default: {}",
    "Найдены сохранения": "Saves found",
    "Сохранений пока нет: игра создаст их здесь": "No saves yet: the game will create them here",
    "не выбрана": "not chosen",
    "Найден eboot.bin": "eboot.bin found",
    "Нет eboot.bin в папке": "No eboot.bin in the folder",
    "Управление": "Controls",
    "Клавиатура": "Keyboard",
    "Геймпад": "Gamepad",
    "Назначение кнопок; применяется при запуске игры": "Button assignments; applied when the game starts",
    "Назначить": "Assign",
    "Сбросить": "Reset",
    "{} (по умолчанию)": "{} (default)",
    "не назначено": "not assigned",
    "Нажмите клавишу или кнопку… (Esc — отмена)": "Press a key or a button… (Esc cancels)",
    "Крест": "Cross",
    "Круг": "Circle",
    "Квадрат": "Square",
    "Треугольник": "Triangle",
    "Тачпад, левая половина": "Touchpad, left half",
    "Тачпад, правая половина": "Touchpad, right half",
    "Крестовина вверх": "D-pad up",
    "Крестовина вниз": "D-pad down",
    "Крестовина влево": "D-pad left",
    "Крестовина вправо": "D-pad right",
    "Движение вперёд": "Move forward",
    "Движение назад": "Move back",
    "Движение влево": "Move left",
    "Движение вправо": "Move right",
    "Камера вверх": "Camera up",
    "Камера вниз": "Camera down",
    "Камера влево": "Camera left",
    "Камера вправо": "Camera right",
    "Контроллер": "Controller",
    "Выбранный берётся, как только подключится": "The chosen one is used as soon as it connects",
    "Первый подключённый": "First connected",
    "{} (не подключён)": "{} (not connected)",
    "Нужно обновление 1.09: скопируйте файлы дампа обновления 1.09 в папку игры с заменой (найдена версия {})":
        "The 1.09 update is needed: copy the dumped 1.09 update into the game folder, replacing files (found version {})",
    "eboot.bin не от версии 1.09: скопируйте eboot.bin из дампа обновления 1.09 в папку игры с заменой":
        "eboot.bin is not from 1.09: copy eboot.bin from the dumped 1.09 update into the game folder, replacing it",
    "Поддерживается только CUSA03173 с обновлением 1.09 (найдено {})":
        "Only CUSA03173 with update 1.09 is supported (found {})",
    "eboot.bin не читается как расшифрованный исполняемый файл PS4: сделайте дамп заново":
        "eboot.bin cannot be read as a decrypted PS4 executable: dump the game again",
    "Bloodborne CUSA03173, версия 1.09": "Bloodborne CUSA03173, version 1.09",
    "Папка игры (с eboot.bin)": "Game folder (with eboot.bin)",
    # Languages
    "Английский": "English",
    "Русский": "Russian",
    "Японский": "Japanese",
    "Французский": "French",
    "Испанский": "Spanish",
    "Немецкий": "German",
    "Итальянский": "Italian",
    # Mods
    "Моды": "Mods",
    "Распакуйте каждый мод в отдельную папку (с dvdroot_ps4 или сразу с chr/, parts/ и т. п.). "
    "При совпадении файлов побеждает мод ниже в списке. Применяется при запуске.":
        "Extract each mod into its own folder (with dvdroot_ps4, or chr/, parts/, ... directly). "
        "When files collide, the mod lower in the list wins. Applied at start.",
    "Загружать моды": "Load mods",
    "Папка модов": "Mods folder",
    "Выбрать папку модов": "Choose the mods folder",
    "Открыть папку модов": "Open the mods folder",
    "Обновить список": "Refresh the list",
    "Загрузить раньше": "Load earlier",
    "Загрузить позже": "Load later",
    "Модов нет": "No mods",
    # Patches
    "Сторонние патчи": "Third-party patches",
    "XML-патчи в формате shadPS4 для версии 01.09 из папки патчей. Применяются при запуске.":
        "shadPS4-format XML patches for version 01.09 from the patches folder. Applied at start.",
    "Папка патчей": "Patches folder",
    "Выбрать папку патчей": "Choose the patches folder",
    "Открыть папку патчей": "Open the patches folder",
    "Патчей нет": "No patches",
    "Автор: {}": "Author: {}",
    # Screen
    "Экран": "Display",
    "Разрешение вывода": "Output resolution",
    "Апскейлер дорисовывает кадр; Steam Deck — 720p": "The upscaler fills the frame; Steam Deck: 720p",
    "Полноэкранный режим": "Fullscreen",
    "Смена разрешения на лету": "Live resolution changes",
    "Без перезапуска, но медленнее на Steam Deck и старых GPU":
        "No restart needed, but slower on the Steam Deck and older GPUs",
    "Авто (по видеокарте)": "Auto (by GPU)",
    "Выключена (быстрее)": "Off (faster)",
    "Включена": "On",
    "Режим показа кадров": "Present mode",
    "Разрешить HDR": "Allow HDR",
    # Upscaler
    "Апскейлер": "Upscaler",
    "Хранится в bbport.ini; в игре меняется через меню (Insert или L3+R3)":
        "Stored in bbport.ini; in game, change it in the menu (Insert or L3+R3)",
    "TAA (нативное сглаживание)": "TAA (native anti-aliasing)",
    "Выключен": "Off",
    "Пресет": "Preset",
    "Резкость (RCAS)": "Sharpening (RCAS)",
    "Сила резкости": "Sharpness",
    "Векторы движения объектов": "Object motion vectors",
    "Меньше гостинга на персонажах; стоит около 10% FPS": "Less ghosting on characters; costs about 10% FPS",
    "Показывать FPS": "Show FPS",
    "Ассеты найдены": "Assets found",
    "Нет ассетов: tools/fetch_fsr4_assets.sh": "No assets: tools/fetch_fsr4_assets.sh",
    "Ассеты для выбранного режима найдены": "Assets for the selected mode found",
    "{}. Установите полный набор fsr4_411 в {}.": "{}. Install the full fsr4_411 set into {}.",
    "Сглаживание в разрешении вывода без модели FSR": "Anti-aliasing at the output resolution, no FSR model",
    "Нет или повреждён файл {}": "Missing or damaged file {}",
    # Effects
    "Эффекты игры": "Game effects",
    "Патчи игры, применяются при запуске": "Game patches, applied at start",
    "Детализация моделей": "Model detail",
    "Как в игре": "As in the game",
    "Максимальная (-2)": "Highest (-2)",
    "Ниже (1)": "Lower (1)",
    "Минимальная (2)": "Lowest (2)",
    "Хроматическая аберрация": "Chromatic aberration",
    "Глубина резкости (DoF)": "Depth of field (DoF)",
    "Размытие в движении": "Motion blur",
    "Затенение SSAO": "SSAO",
    "Собственное сглаживание игры": "The game's own anti-aliasing",
    "Тени от динамических источников": "Dynamic light shadows",
    "Отражения SSR (не было в игре)": "SSR reflections (not in the original game)",
    "Пропуск заставок при запуске": "Skip the intro videos",
    "Свободная камера (Cross + L3 / Space + Z)": "Free camera (Cross + L3 / Space + Z)",
    "Debug menu (левый touchpad / Tab; нужны шрифты)": "Debug menu (left touchpad / Tab; needs fonts)",
    "Установите DbgFont14h.ccm и DbgFont14h.tpf в dvdroot_ps4/font из мода Nexus #253":
        "Install DbgFont14h.ccm and DbgFont14h.tpf into dvdroot_ps4/font from Nexus mod #253",
    # Frame rate
    "Частота кадров": "Frame rate",
    "Режим": "Mode",
    "Какой патч частоты кадров применить к игре": "Which frame rate patch to apply to the game",
    "Без ограничения (патч)": "Unlocked (patch)",
    "30 (как на PS4)": "30 (as on PS4)",
    "Ограничение FPS": "FPS limit",
    "0 — без ограничения; укажите число, чтобы ограничить FPS":
        "0: no limit; set a number to cap the frame rate",
    # Performance
    "Производительность": "Performance",
    "Двухстадийный конвейер GPU": "Draw Pipe (Two-stage GPU pipeline)",
    "Быстрее на 20–30%; при нестабильности выключите": "20–30% faster; turn off for vanilla shadPS4 compatibility",
    "Гибридный (Рекомендуется)": "Hybrid (Recommended)",
    "Ускорение в игре, стабильность в кат-сценах": "Faster in gameplay, stable in cutscenes",
    "Авто": "Auto",
    "Включён при 8 и более потоках процессора": "On with 8 or more CPU threads",
    "Включён": "On",
    "Стабильнее, но медленнее": "More stable, but slower",
    "Стабильнее, режим shadPS4 vanilla": "More stable, vanilla shadPS4 mode",
    "Чтение данных GPU процессором": "GPU data readbacks by the CPU",
    "По умолчанию": "Default",
    "Быстрее, но лица могут искажаться": "Faster, but faces may glitch",
    "Экспериментально; может зависать при запуске": "Experimental; may freeze at startup",
    "Фоновая загрузка в видеопамять": "Background pre-upload into VRAM",
    "Меньше рывков при подгрузке зон": "Fewer hitches when areas stream in",
    "Обычная": "Normal",
    "Без лишней видеопамяти": "No extra VRAM",
    "Полная": "Full",
    "Около 3 ГБ видеопамяти сверху": "About 3 GB more VRAM",
    "Новая модель памяти (экспериментально)": "New memory model (experimental)",
    "Как у игры для ПК: быстрее и меньше рывков. Проверена только на RX 7800 XT и Steam Deck, может вылетать, на NVIDIA работает неправильно":
        "As a PC game: faster, fewer stutters. Tested only on an RX 7800 XT and the Steam Deck; may crash, does not work properly on NVIDIA",
    "Выключена": "Off",
    "Выключены": "Off",
    # Developer
    "Для разработчика": "Developer",
    "Статистика кадров в журнале": "Frame statistics in the log",
    "Сохранять журнал и статистику в файл": "Save the log and statistics to a file",
    "Диагностика вылетов": "Crash diagnostics",
    "Проверяет кучу игры и записывает записи в её память; немного медленнее":
        "Checks the game's heap and logs writes into its memory; a little slower",
    "В папку logs в каталоге данных: для разбора рывков и вылетов":
        "Into the logs folder of the data directory: to look into stutters and crashes",
    "Профиль GPU в журнале": "GPU profile in the log",
    "Слои валидации Vulkan": "Vulkan validation layers",
    "Сильно замедляет": "Much slower",
    "Доп. переменные (ИМЯ=значение через пробел)": "Extra variables (NAME=value, space-separated)",
}
PT = {
    # Window, pages, buttons
    "Запустить": "Jogar",
    "Остановить": "Parar",
    "Настройки": "Configurações",
    "Журнал": "Registro",
    "Не удалось запустить: {}": "Não foi possível iniciar: {}",
    "\n— игра завершилась (код {}) —\n": "\n— o jogo encerrou (código {}) —\n",
    # Launcher language
    "Язык лаунчера": "Idioma do inicializador",
    "Как в системе": "Como no sistema",
    "Применится после перезапуска лаунчера, пока игра запущена":
        "Aplica após reiniciar o inicializador",
    # Game
    "Игра": "Jogo",
    "Папка игры (CUSA03173)": "Pasta do jogo (CUSA03173)",
    "Выбрать папку с eboot.bin": "Escolher pasta com eboot.bin",
    "Открыть в файловом менеджере": "Abrir no gerenciador de arquivos",
    "Папка сохранений": "Pasta de salvamentos",
    "Выбрать папку сохранений": "Escolher pasta de salvamentos",
    "Вернуть папку по умолчанию": "Restaurar pasta padrão",
    "Язык системы": "Idioma do sistema",
    "По умолчанию: {}": "Padrão: {}",
    "Найдены сохранения": "Salvamentos encontrados",
    "Сохранений пока нет: игра создаст их здесь": "Sem salvamentos ainda: o jogo os criará aqui",
    "не выбрана": "não selecionada",
    "Найден eboot.bin": "eboot.bin encontrado",
    "Нет eboot.bin в папке": "Nenhum eboot.bin na pasta",
    "Управление": "Controles",
    "Клавиатура": "Teclado",
    "Геймпад": "Controle",
    "Назначение кнопок; применяется при запуске игры": "Mapeamento de botões; aplicado ao iniciar o jogo",
    "Назначить": "Mapear",
    "Сбросить": "Redefinir",
    "{} (по умолчанию)": "{} (padrão)",
    "не назначено": "não mapeado",
    "Нажмите клавишу или кнопку… (Esc — отмена)": "Pressione uma tecla ou botão… (Esc cancela)",
    "Крест": "Cruz",
    "Круг": "Círculo",
    "Квадрат": "Quadrado",
    "Треугольник": "Triângulo",
    "Тачпад, левая половина": "Touchpad, metade esquerda",
    "Тачпад, правая половина": "Touchpad, metade direita",
    "Крестовина вверх": "D-pad Cima",
    "Крестовина вниз": "D-pad Baixo",
    "Крестовина влево": "D-pad Esquerda",
    "Крестовина вправо": "D-pad Direita",
    "Движение вперёд": "Mover para frente",
    "Движение назад": "Mover para trás",
    "Движение влево": "Mover para esquerda",
    "Движение вправо": "Mover para direita",
    "Камера вверх": "Câmera para cima",
    "Камера вниз": "Câmera para baixo",
    "Камера влево": "Câmera para esquerda",
    "Камера вправо": "Câmera para direita",
    "Контроллер": "Controle",
    "Выбранный берётся, как только подключится": "O selecionado é usado assim que conectar",
    "Первый подключённый": "Primeiro conectado",
    "{} (не подключён)": "{} (não conectado)",
    "Bloodborne CUSA03173, версия 1.09": "Bloodborne CUSA03173, versão 1.09",
    "Папка игры (с eboot.bin)": "Pasta do jogo (com eboot.bin)",
    # Languages
    "Английский": "Inglês",
    "Русский": "Russo",
    "Японский": "Japonês",
    "Французский": "Francês",
    "Испанский": "Espanhol",
    "Немецкий": "Alemão",
    "Итальянский": "Italiano",
    # Mods
    "Моды": "Mods",
    "Загружать моды": "Carregar mods",
    "Папка модов": "Pasta de mods",
    "Выбрать папку модов": "Escolher pasta de mods",
    "Открыть папку модов": "Abrir pasta de mods",
    "Обновить список": "Atualizar lista",
    "Загрузить раньше": "Carregar antes",
    "Загрузить позже": "Carregar depois",
    "Модов нет": "Nenhum mod",
    # Patches
    "Сторонние патчи": "Patches de terceiros",
    "Папка патчей": "Pasta de patches",
    "Выбрать папку патчей": "Escolher pasta de patches",
    "Открыть папку патчей": "Abrir pasta de patches",
    "Патчей нет": "Nenhum patch",
    "Автор: {}": "Autor: {}",
    # Screen
    "Экран": "Tela",
    "Разрешение вывода": "Resolução de saída",
    "Полноэкранный режим": "Tela cheia",
    "Смена разрешения на лету": "Mudança de resolução dinâmica",
    "Авто (по видеокарте)": "Automático (pela GPU)",
    "Выключена (быстрее)": "Desativada (mais rápido)",
    "Включена": "Ativada",
    "Режим показа кадров": "Modo de apresentação",
    "Разрешить HDR": "Permitir HDR",
    # Upscaler
    "Апскейлер": "Upscaler",
    "Хранится в bbport.ini; в игре меняется через меню (Insert или L3+R3)":
        "Salvo em bbport.ini; no jogo mude pelo menu (Insert ou L3+R3)",
    "TAA (нативное сглаживание)": "TAA (anti-aliasing nativo)",
    "Выключен": "Desligado",
    "Пресет": "Predefinição",
    "Резкость (RCAS)": "Nitidez (RCAS)",
    "Сила резкости": "Intensidade da nitidez",
    "Векторы движения объектов": "Vetores de movimento de objetos",
    "Показывать FPS": "Mostrar FPS",
    "Ассеты найдены": "Arquivos encontrados",
    # Effects
    "Эффекты игры": "Efeitos do jogo",
    "Патчи игры, применяются при запуске": "Patches do jogo, aplicados ao iniciar",
    "Детализация моделей": "Detalhes dos modelos (LOD)",
    "Как в игре": "Padrão do jogo",
    "Максимальная (-2)": "Máximo (-2)",
    "Ниже (1)": "Mais baixo (1)",
    "Минимальная (2)": "Mínimo (2)",
    "Хроматическая аберрация": "Aberração cromática",
    "Глубина резкости (DoF)": "Profundidade de campo (DoF)",
    "Размытие в движении": "Desfoque de movimento",
    "Затенение SSAO": "Oclusão de ambiente (SSAO)",
    "Собственное сглаживание игры": "Anti-aliasing original do jogo",
    "Тени от динамических источников": "Sombras de luzes dinâmicas",
    "Отражения SSR (не было в игре)": "Reflexos SSR (não existia no jogo original)",
    "Пропуск заставок при запуске": "Pular vídeos de introdução",
    "Свободная камера (Cross + L3 / Space + Z)": "Câmera livre (Cross + L3 / Space + Z)",
    "Debug menu (левый touchpad / Tab; нужны шрифты)": "Menu de depuração (Touchpad esquerdo / Tab)",
    # Frame rate
    "Частота кадров": "Taxa de quadros (FPS)",
    "Режим": "Modo",
    "Без ограничения (патч)": "Ilimitado (patch)",
    "30 (как на PS4)": "30 (como no PS4)",
    "Ограничение FPS": "Limite de FPS",
    "0 — без ограничения; укажите число, чтобы ограничить FPS":
        "0: sem limite; informe um valor para limitar os quadros",
    # Performance
    "Производительность": "Desempenho",
    "Двухстадийный конвейер GPU": "Draw Pipe (Pipeline de GPU em 2 estágios)",
    "Быстрее на 20–30%; при нестабильности выключите":
        "20–30% mais rápido; desligue para compatibilidade com shadPS4 vanilla",
    "Гибридный (Рекомендуется)": "Híbrido (Recomendado)",
    "Ускорение в игре, стабильность в кат-сценах": "Mais rápido no gameplay, estável em cutscenes",
    "Авто": "Automático",
    "Включён при 8 и более потоках процессора": "Ligado com 8 ou mais threads de CPU",
    "Включён": "Ligado",
    "Стабильнее, но медленнее": "Mais estável (modo shadPS4 vanilla), porém mais lento",
    "Стабильнее, режим shadPS4 vanilla": "Mais estável, modo shadPS4 vanilla",
    "Чтение данных GPU процессором": "Leitura de dados da GPU pela CPU (Readbacks)",
    "По умолчанию": "Padrão (Relaxed)",
    "Быстрее, но лица могут искажаться": "Mais rápido, mas rostos podem distorcer",
    "Экспериментально; может зависать при запуске": "Experimental; pode travar na inicialização",
    "Фоновая загрузка в видеопамять": "Pré-carregamento em segundo plano na VRAM",
    "Меньше рывков при подгрузке зон": "Menos engasgos ao carregar áreas",
    "Обычная": "Normal",
    "Без лишней видеопамяти": "Sem VRAM adicional",
    "Полная": "Completo",
    "Около 3 ГБ видеопамяти сверху": "Cerca de 3 GB a mais de VRAM",
    "Новая модель памяти (экспериментально)": "Novo modelo de memória (experimental)",
    "Выключена": "Desativado",
    "Выключены": "Desativados",
    # Developer
    "Для разработчика": "Desenvolvedor",
    "Статистика кадров в журнале": "Estatísticas de quadros no registro",
    "Сохранять журнал и статистику в файл": "Salvar registro e estatísticas em arquivo",
    "Диагностика вылетов": "Diagnóstico de falhas",
    "Профиль GPU в журнале": "Perfil de GPU no registro",
    "Слои валидации Vulkan": "Camadas de validação Vulkan",
    "Сильно замедляет": "Muito mais lento",
    "Доп. переменные (ИМЯ=значение через пробел)": "Variáveis extras (NOME=valor separados por espaço)",
}


def system_language():
    for key in ("LC_ALL", "LC_MESSAGES", "LANG", "LANGUAGE"):
        value = os.environ.get(key, "")
        if value:
            low = value.lower()
            if low.startswith("ru"):
                return "ru"
            if low.startswith("pt"):
                return "pt"
            return "en"
    return "en"


_language = "ru"


def set_language(choice):
    """choice: "ru", "pt", "en" or "" (the system's)."""
    global _language
    _language = choice if choice in ("ru", "pt", "en") else system_language()


def language():
    return _language


def tr(text):
    if _language == "pt":
        return PT.get(text, EN.get(text, text))
    if _language == "en":
        return EN.get(text, text)
    return text
