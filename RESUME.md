# RESUME — состояние разработки

## Где что лежит

- **Репозиторий**: `sunrule/dumphfdl_windows`, ветка `master`
- **Последний коммит**: `e16970e` — «Add formatted text output to JSON and Windows fixes»
- **Предыдущий**: `b249ea9` — «Add real MPDU start time for IQ and MCR file decoding»
- **До этого**: `d2729b0` — «Add Windows DLL API, SQLite persistence, and one-command build script»

## Артефакты

- Сборка: 4 варианта одной командой (`scripts/build-all.sh`): x64 exe/dll, x86 exe/dll
- x64: liquid/acars статические (меньше рантайм-DLL); x86: 4 рантайм-DLL
- Префиксы зависимостей: x64 shared — `prefix-x64`; i686 — `prefix-i686` (в `BUILD_ROOT/`)
- Примеры: Delphi 7 (`delphi/`), тесты: `test/`

## Что сделано

1. **DLL API**: `HFDL_DecodeFile`, `HFDL_Version`, stdcall-экспорт, колбэк вывода
2. **SQLite-персистентность**: `ac_cache_db` + `systable_db`
3. **MCR**: авто-подбор каналов из `etc/systable.conf` по заголовку записи
4. **Сборка**: `scripts/build-all.sh` + `cmake/toolchain-mingw32.cmake`
5. **Документация**: `doc/dll-usage.md`
6. **Реальное время MPDU**:
   - `--file-start-time USEC` для IQ; для MCR — из заголовка (`beg_time`) автоматически
   - Время от `c->sample_cnt` → не зависит от `--read-buffer-size`
   - JSON: `mpdu_sec`/`mpdu_usec`
7. **JSON с текстом**: в JSON включён человекочитаемый текст PDU (`text` field) через общий text-форматтер
8. **Windows-фиксы**: `strftime %F` → `%Y-%m-%d %H:%M:%S` в text/basestation-форматтерах
9. **Delphi-примеры**: кодировка переведена в Windows-1251 для Delphi 7 IDE

## Известное

- Конвейерная групповая задержка ~9.5 мс (FIR 256 + ресемплер ~40 сэмплов) — не компенсируется
- DLL не реенрантна: параллельная обработка — через процессы/TThread+try/except

## Отложено

- Дальнейшие доработки режима обработки IQ и MCR файлов — до команды пользователя

## Как продолжить

1. `git pull` (master = `e16970e`)
2. Пересборка: `bash scripts/build-all.sh` (deps собираются автоматически; `--no-deps` если уже стоят)
3. Тесты: `test/hfdl_dll_test.c`, функциональные прогоны на MCR
4. Дальше — по команде пользователя
