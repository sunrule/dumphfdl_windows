# RESUME — состояние разработки (пауза 2026-10-14)

## Где что лежит

- **Репозиторий**: `C:\dev\sunrule-dumphfdl` (remote: `sunrule/dumphfdl_windows`, ветка `master`)
- **Последний коммит**: `d2729b0` — «Add Windows DLL API, SQLite persistence, and one-command build script»
  (запушен на GitHub, master подтверждён `d2729b0258aeaee8c0255e65efecff2bd612866b`)
- Предыдущие: `eb45199` (MCR auto-select), `591c771` (MinGW compat), `b303f1d` (MCR input)

## Готовые артефакты (все проверены на реальных MCR-записях из C:\dev\mcr_files\)

- **Скриптовая сборка**: `C:\dev\test-build\dist\x64\` и `C:\dev\test-build\dist\x86\`
  (собраны `scripts/build-all.sh`; x64 — liquid/acars статические: 6 рантайм-DLL; x86 — 4 DLL)
- **Старые дистрибутивы** (shared x64 deps, 8 DLL): `C:\dev\dist\x64\`, `C:\dev\dist\x86\`
- **Префиксы зависимостей**: x64 shared — `C:\dev\build\prefix`; i686 — `C:\dev\build\prefix-i686`
- Тесты: `test/hfdl_dll_test.c`, `test_mcr_autoselect.py`, `test_mcr_functional.py`

## Что сделано в этом коммите

1. **DLL API** (`src/hfdl_api.c/h`, `dll_entry.c`, `dumphfdl.def`): `HFDL_DecodeFile`, `HFDL_Version`,
   stdcall-экспорт для Delphi 7 / Borland C / C++; колбэк вывода (`output-callback.c/h`)
2. **SQLite-персистентность**: `ac_cache_db` (AC-кэш) + `systable_db` (части system table из потока)
3. **MCR**: авто-подбор каналов из `etc/systable.conf` по заголовку записи; UNC-пути
4. **Сборка**: `scripts/build-all.sh` (4 варианта одной командой, pacman-deps + liquid/acars из
   исходников + dist/), `cmake/toolchain-mingw32.cmake`
5. **Документация**: `doc/dll-usage.md` (806 строк: API, коды возврата, форматы humantext/json/
   basestation, примеры Delphi 7 / Borland C / C++, разбор JSON, SQLite, UNC)
6. **Примеры**: `delphi/` (hfdl.pas, Unit1.pas/dfm, hfdl_demo.dpr), `test/`

## Известные грабли сборки (уже решены в скрипте)

- ninja→cmd.exe теряет TMP/TEMP → collect2 падает на C:\Windows → wrapper в cmd.exe
- i686 pkg-config только из /usr/bin; liquid 1.8.3 timer.c патчится (getrusage шим)
- libacars 2.2.1 shared ломается → static; линкеру нужен -pipe; cmake требует pwd -W пути

## Отложено по решению пользователя

- **Доработки режима обработки IQ и MCR файлов** — пользователь имеет предложения, реализация
  отложена до его команды. Не начато.

## Сделано: реальное время начала MPDU (коммит после d2729b0)

- `--file-start-time USEC` (µs, UTC) для IQ; для MCR берётся из заголовка (`beg_time`)
  автоматически, опция — только override
- Время считается от счётчика канала (`c->sample_cnt`), НЕ от читателя → не зависит
  от `--read-buffer-size` (проверено: 256/дефолт идентичны); MCR и IQ дают одинаковые
  timestamp'ы при равных bearing/elevation
- JSON: `mpdu_sec`/`mpdu_usec` (+`sec`/`usec` = то же время в файловом режиме)
- Все 4 варианта (x64/x86 exe+dll) собраны и протестированы на реальных MCR
- Конвейерная групповая задержка ~9.5 мс (FIR 256 + ресемплер ~40 сэмплов) — по решению
  пользователя не компенсируется (TDOA не требуется)

## Как продолжить

1. `cd /c/dev/sunrule-dumphfdl && git pull` (убедиться, что master = d2729b0)
2. Пересборка: `BUILD_ROOT=C:/dev/test-build bash scripts/build-all.sh --no-deps`
   (deps уже собраны; полная — без `--no-deps`, но pacman-deps уже стоят)
3. Тесты: `python test_mcr_functional.py`, `test/hfdl_dll_test.c` через dist/x86/dumphfdl.dll
4. Дальше — по команде пользователя: доработки IQ/MCR-режимов
