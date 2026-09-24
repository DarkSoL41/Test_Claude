# Supaplex (Amiga, 1991) — полный разбор и порт на Windows

В репозитории лежит образ дискеты Amiga‑версии Supaplex (`Supaplex (1991).adf`,
взломанная DOS-версия от CRYSTAL) и архив с предварительным анализом.
Здесь сделаны:

* **полный разбор игры**: формат дискеты, загрузчик, распаковщик, карта памяти,
  все подпрограммы основной программы (PHIL_01) и интро (PHIL_00);
* **аннотированный дизассемблированный листинг** — [`disasm/PHIL_01.asm`](disasm/PHIL_01.asm),
  [`disasm/PHIL_00.asm`](disasm/PHIL_00.asm);
* **порт на C++17/SDL2** для Windows (и Linux), логика которого совпадает с Amiga
  побайтно — это проверяется автоматически, кадр за кадром, против оригинального
  кода 68000.

Документация (на русском) — в каталоге [`docs/`](docs/README.md).

## Как устроен порт

Код игры переведён с 68000 на C++ подпрограмма за подпрограммой
(`port/src/game/generated/`). Переменные игры живут в эмулированной chip-RAM
Amiga (512 КБ) по тем же адресам, что и в оригинале, поэтому воспроизводятся все
особенности оригинала, включая обращения за границы карты уровня. Небольшое
«железо», которым пользуется игра, реализовано нативно
(`port/src/amiga/`): блиттер, copper и вывод изображения, спрайт курсора, звук
Paula, джойстик, мышь и клавиатура (CIA). Графика, уровни и музыка читаются
прямо из образа дискеты.

Точность проверяется программой `difftest`: оригинальный код 68000 исполняется на
эмуляторе процессора Musashi поверх того же «железа», порт — рядом с ним, на
одинаковом вводе. После каждого кадра сравнивается вся память (512 КБ) и
картинка. Подробности — в [`docs/05-port.md`](docs/05-port.md).

## Сборка

Нужны CMake ≥ 3.16, компилятор C++17 и SDL2.

**Windows, Visual Studio + vcpkg**

```bat
vcpkg install sdl2:x64-windows
cmake -S port -B build -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake -DSUPAPLEX_BUILD_TESTS=OFF
cmake --build build --config Release
```

**Windows, MSYS2 / MinGW-w64**

```sh
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-SDL2
cmake -S port -B build -G "MinGW Makefiles" -DSUPAPLEX_BUILD_TESTS=OFF
cmake --build build
```

**Кросс-сборка Windows-версии на Linux** (MinGW-w64 и пакет
`SDL2-devel-2.x-mingw.tar.gz` с libsdl.org):

```sh
cmake -S port -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake \
      -DSDL2_DIR=<путь>/SDL2-2.30.9/x86_64-w64-mingw32/lib/cmake/SDL2 -DSUPAPLEX_BUILD_TESTS=OFF
cmake --build build-win
```

Рядом с `supaplex.exe` нужно положить `SDL2.dll` (из пакета SDL2).

**Linux** (вместе с тестами точности):

```sh
sudo apt install cmake g++ libsdl2-dev
cmake -S port -B port/build && cmake --build port/build -j
```

## Запуск

Положите `Supaplex (1991).adf` рядом с программой (или укажите путь):

```
supaplex [--adf "Supaplex (1991).adf"] [--scale 3] [--fullscreen] [--save hiscores.sav]
         [--reset-hiscores] [--original-hiscores]
```

Таблица рекордов сохраняется не на образ дискеты, а в отдельный файл
(по умолчанию — в папке настроек пользователя,
`%APPDATA%\Supaplex\SupaplexAmiga\hiscores.sav`).

Новый файл рекордов создаётся **чистым**: без игроков и без записей
`CRYSTAL!` из взломанного образа (в формате, который сама игра получает при
удалении игроков). Ключи:

* `--reset-hiscores` — начать заново (стереть игроков и рекорды);
* `--original-hiscores` — новый файл взять с дискеты как есть (игроки ALLEN,
  ME, KIP и рекорды CRYSTAL); вместе с `--reset-hiscores` — вернуть их.

| Действие | Клавиши |
|---|---|
| Джойстик (Murphy) | стрелки или цифровой блок; геймпад — крестовина/левый стик |
| Кнопка «огонь» | Пробел, Ctrl, 0 на цифровом блоке; геймпад — A/B/X/Y |
| Меню | мышь; «огонь» джойстика запускает выбранный уровень |
| В уровне | правая кнопка мыши — пауза, левая — сдаться (как на Amiga) |
| Ввод имени игрока | клавиатура (как на Amiga), Enter — готово, Backspace — стереть; пока вводится имя, клавиатура не работает как джойстик |
| Полный экран | F11 или Alt+Enter |
| Пауза эмуляции | Pause |
| Выход | F12 или закрыть окно |

## Проверка точности

```sh
cd port
./build/difftest --adf "../Supaplex (1991).adf" --frames 20000 --random 1 --level 1
./tests/fuzz_all.sh build "../Supaplex (1991).adf" fuzz_out 15000 1 4   # все 111 уровней
```

`difftest` сообщает первый кадр и адреса, где порт разошёлся с оригиналом, либо
`OK: N frames identical (memory and picture)`.

## Структура репозитория

| Путь | Содержимое |
|---|---|
| `Supaplex (1991).adf` | образ дискеты (данные игры) |
| `Supaplex_Amiga_analysis.zip` | исходный архив анализа (карты уровней, CSV) |
| `docs/` | документация разбора и порта |
| `disasm/` | аннотированные листинги PHIL_00 и PHIL_01 |
| `tools/` | декодер 68000, транслятор 68000→C++, генератор листинга, аннотации, чтение ADF, распаковщик ByteKiller |
| `port/src/amiga/` | «железо» Amiga: шина, блиттер, copper/дисплей, спрайты, Paula, CIA, чтение ADF |
| `port/src/game/` | рантайм переведённого кода и сгенерированные подпрограммы игры |
| `port/src/platform/` | фронтенд SDL2 (окно, звук, ввод) |
| `port/tests/` | эталонный эмулятор (Musashi), `difftest`, `refrun`, фаззинг |
| `port/third_party/musashi/` | ядро 68000 Musashi (MIT), только для тестов |
