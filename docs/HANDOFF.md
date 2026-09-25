# Handoff: Supaplex (Amiga) — разбор, порт, редактор

Документ для продолжения работы в новой сессии. Язык общения с
пользователем — **русский**. Состояние на 2026-09-25.

## 1. Что это за проект

Репозиторий `DarkSoL41/Test_Claude`, ветка разработки
**`claude/vibrant-pasteur-sr0r0q`** (все изменения там; `main` не трогаем,
PR не создавали — пользователь не просил).

1. **Полный разбор** Amiga-версии Supaplex (1991, взлом CRYSTAL) с образа
   `Supaplex (1991).adf`: загрузчик, ByteKiller, карта памяти, вся логика
   (211 подпрограмм), аннотированный листинг `disasm/PHIL_01.asm`,
   `disasm/PHIL_00.asm`. Документация — `docs/01…09` (на русском).
2. **Порт на Windows** (C++17/SDL2): код 68000 механически переведён в C++
   подпрограмма за подпрограммой (`tools/m68k2cpp.py` →
   `port/src/game/generated/`), память игры — эмулированная chip-RAM по
   оригинальным адресам, «железо» Amiga (блиттер, copper, спрайты, Paula,
   CIA) — нативно в `port/src/amiga/`. **Главное требование пользователя:
   логика 100% как на Amiga, никакой «отсебятины».**
3. **Проверка точности**: эталонный эмулятор с оригинальным кодом на Musashi
   (`port/tests/refemu.*`), `difftest` сравнивает всю память и картинку
   каждого кадра. Фаззинг всех 111 уровней и сценарии — полное совпадение.
4. **Редактор уровней** `supaplex_editor` (C++17/SDL2, `port/src/editor/`).

## 2. Требования и предпочтения пользователя (важно)

* Точная логика Amiga; изменения допустимы только во вводе/выводе
  фронтенда, не в логике. Если сомнение — спросить.
* Всё хранится **в папке игры**: `data\` (LEVELS.DAT, INTRO.BIN, MAIN.BIN,
  GRAPHICS.BIN, HISCORE.BIN) и `hiscores.sav` рядом с exe. Никаких
  `%APPDATA%`. ADF нужен только для `--extract`.
* Плавно, без пауз (от модели тактов CPU/дисковода отказались — давала
  паузы при загрузках).
* Мышь не захватывать (курсор Windows ведёт указатель игры, можно нажать
  крестик окна).
* Клавиатура не запускает уровень (как в оригинале). Как джойстик —
  только в уровне: стрелки + Пробел. Геймпад — джойстик везде. Ctrl/Alt/Enter
  огнём **не** являются (пользователь явно просил убрать Ctrl).
* Новый файл рекордов — чистый (без CRYSTAL и игроков);
  `--original-hiscores --reset-hiscores` возвращает данные дискеты.
* Коммиты: сообщение на русском, в конце строки
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` и
  `Claude-Session: https://claude.ai/code/session_01Q7PdsaDtp4FhTtvypCCG1F`
  (в новой сессии — её собственная ссылка из системного напоминания).
  Не упоминать идентификатор модели в коде/коммитах/документах.
* Пользователь тестирует на Windows сам; мы проверяем в Linux-контейнере
  (dummy-драйверы SDL) и кросс-сборкой mingw.

## 3. Структура репозитория

| Путь | Что |
|---|---|
| `Supaplex (1991).adf` | образ дискеты (оригинал) |
| `Supaplex_Amiga_analysis.zip` | исходный архив анализа (CSV уровней, карты) |
| `data/` | файлы дискеты для порта (`--extract`), LEVELS.DAT = 111×1536 |
| `disasm/` | аннотированные листинги PHIL_00/PHIL_01 |
| `docs/` | документация 01–09 + этот файл |
| `tools/m68kdec.py`, `m68k2cpp.py`, `m68kdis.py` | декодер 68000, транслятор в C++, генератор листинга |
| `tools/translate.json`, `annotations.json` | конфигурация трансляции, имена и комментарии |
| `tools/gen_menu_tour.py`, `level_path.py`, `merge_coverage.py` | генераторы сценариев, покрытие |
| `tools/gen_editor_font.py` | шрифт редактора из DejaVu Sans Mono |
| `tools/adflib.py`, `bytekiller.py`, `export_assets.py` | чтение ADF, распаковщик, экспорт ресурсов |
| `port/src/amiga/` | «железо»: amiga.cpp (шина, блиттер, copper, спрайты, CIA), paula.cpp, adf.cpp, gamefiles.cpp (папка data или ADF) |
| `port/src/game/` | cpu.hpp (регистры/флаги), runtime.cpp (прерывания, нативные загрузчик/сохранение), hiscores.hpp (чистые рекорды), generated/ |
| `port/src/platform/main_sdl.cpp` | фронтенд игры (окно, звук, ввод, тест-режимы) |
| `port/src/editor/` | редактор: level.* (модель), gfx.* (тайлы), ui.* (интерфейс), app.cpp, process.* (запуск игры), font_data.hpp |
| `port/tests/` | refemu (Musashi), difftest, refrun, level_test, run_scenarios.sh, fuzz_all.sh, data/ (сценарии) |
| `port/third_party/musashi` | ядро 68000 для эталона (только тесты) |
| `port/cmake/mingw-w64-x86_64.cmake` | toolchain кросс-сборки |
| `.github/workflows/build.yml` | CI: Windows MSVC (пакет exe+редактор+SDL2.dll+data) и Linux (сборка, генерация, data=ADF, level_test, сценарии, фаззинг 7 уровней) |

## 4. Сборка и проверка

```sh
# Linux (с тестами точности)
sudo apt install cmake g++ libsdl2-dev
cmake -S port -B port/build && cmake --build port/build -j
cd port
./tests/run_scenarios.sh build "../Supaplex (1991).adf"          # 7 сценариев, должно быть 7× OK
./build/difftest --adf "../Supaplex (1991).adf" --frames 6000 --random 1 --level 1
./tests/fuzz_all.sh build "../Supaplex (1991).adf" out 15000 1 4   # все 111 уровней (~30 мин)
./build/level_test ../data/LEVELS.DAT                             # модель редактора
python3 tools/m68k2cpp.py tools/translate.json && git diff --exit-code port/src/game/generated

# кросс-сборка Windows (SDL2 mingw-пакет, см. архив handoff: sandbox/sdl2mingw.tgz)
cmake -S port -B port/build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake \
      -DSDL2_DIR=<...>/SDL2-2.30.9/x86_64-w64-mingw32/lib/cmake/SDL2 -DSUPAPLEX_BUILD_TESTS=OFF
cmake --build port/build-win
```

Проверка без экрана: `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy`.

* Игра: `--frames N --shot file.ppm`, `--test-input FILE` (строки
  `F mouse X Y`, `F lmb 0|1`, `F rmb 0|1`, `F key NAME 0|1`, `F print ADDR`),
  `--test-level N` (для редактора).
* Редактор: `--script FILE` (`F move X Y`, `F click X Y [l|m|r]`,
  `F down/up`, `F key NAME [ctrl] [shift] [alt]`, `F text STR`,
  `F wheel N`, `F drop FILE`, `F shot FILE.bmp`, `F save`, `F quit`).
  Перед тестом удалить `editor.ini` рядом с exe (иначе другой масштаб/уровень).
* difftest: `--input`, `--level L`, `--autopilot FILE`, `--poke F A V`,
  `--report A,B`, `--watch`, `--trace-var A`, `--hiscores clean|FILE`,
  `--port-data DIR`, `--dump DIR --every K`, `--coverage FILE`;
  `difftest_trace --trace-frame F` — трассы PC обеих сторон.

## 5. Ключевые решения и находки

* **Модель времени** (`docs/05-port.md` 5.4): время идёт только от опроса
  луча (VPOSR/VHPOSR +1 строка) и опросов железа (+16 тактов цвета);
  чтение возвращает значение до продвижения времени; загрузки мгновенные.
  Модель с тактами 68000 пробовалась и отклонена пользователем.
* **Клик OK/demo взрывал Murphy** (кнопка ещё нажата в первом кадре уровня;
  ЛКМ в уровне = «сдаться»). Решение во фронтенде: кнопки/огонь, нажатые в
  момент старта уровня (детектор — счётчик `$11294`, который пишет только
  `game_frame`), не передаются до отпускания. Первый `game_frame` всегда на
  кадр раньше первой проверки мыши.
* **Клавиатура Amiga** нужна игре только при вводе имени (обработчик $10214
  в векторе $68) — только тогда фронтенд и передаёт клавиши.
* **Мышь**: фронтенд подаёт в JOY0DAT смещения, чтобы указатель игры
  ($112EC/$112EE, пиксель = счётчик/2) шёл под курсор; не больше ±100 за кадр.
* **Формат уровня** (`docs/06-formats.md`, `docs/08-research-notes.md`):
  хвост 512 байт — мусор; счётчик особых портов (1471) Amiga не читает,
  таблица 256 записей, из файла — 10 записей + начало 11-й (1532–1535);
  байты 1440–1443 — стартовая камера (режимы 0 / 39–40, 0 / 11–12 / центр,
  без ограничения краями); Murphy не выходит за край (шаг ограничен экраном),
  объекты — выходят, под картой — буфер уровня, ниже — стек → зависание.
* **Редактор** нормализует только правленые уровни (камера, порты, байты
  1532–1535 и 1445), остальные — байт в байт. MinGW (win32 threads) не
  поддерживает std::thread → в редакторе SDL_Thread; дочерний процесс на
  POSIX снимает маску сигналов (SDL блокирует сигналы в своих потоках).

## 6. Состояние и что можно делать дальше

Всё закоммичено и запушено, CI зелёный (последний коммит — редактор).
Пользователь ещё **не проверял на Windows** последние версии игры (мышь без
захвата, ввод, data/, клик OK) и редактор. Ждём его отзыва.

Идеи/возможные задачи (не начаты, только если пользователь попросит):
* Отзывы по редактору с Windows (HiDPI, масштаб интерфейса `--scale 2`,
  шрифт, удобство).
* Редактор: правка демо-уровня (PHIL_02, `$1B5C4`), наборы уровней
  (несколько LEVELS.DAT), подсветка путей врагов, статистика сложности.
* Игра: опции оконного режима/масштаба в ini, vsync.
* Сценарии difftest используют абсолютные номера кадров (меню к ~520-му
  кадру, `--level` делает запись на 690-м, огонь на 700-м).

## 7. Архив handoff

Пользователь получил архив `supaplex_handoff_*.zip`:
* `HANDOFF.md` — этот файл;
* `repo/Test_Claude.bundle` — git bundle со всей историей
  (`git clone Test_Claude.bundle Test_Claude`), `repo/source_snapshot.zip`
  — рабочее дерево без .git;
* `windows/Supaplex/` — готовая сборка mingw: supaplex.exe,
  supaplex_editor.exe, SDL2.dll, data\, README (собрана кросс-компилятором в
  контейнере; официальный пакет MSVC — артефакт CI `supaplex-windows-x64`);
* `sandbox/` — материалы из песочницы: разбор загрузчика крэка
  (`analysis/`), покрытие кода (`coverage/`), итоги фаззинга (`fuzz/`),
  тестовые сценарии редактора и фронтенда (`scripts/`), снимки экрана
  (`screenshots/`), пакет SDL2 для mingw (`sdl2mingw.tgz`);
* `session/transcript.jsonl.xz` — полная стенограмма этой сессии.
