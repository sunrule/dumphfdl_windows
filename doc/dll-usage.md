# Использование dumphfdl.dll из Delphi 7, Borland C и Borland C++

Документ описывает экспортируемый C-API DLL-библиотеки декодера HFDL
(`dumphfdl.dll`, 32-бит) и содержит гарантированно работающие примеры для
Delphi 7, Borland C и Borland C++.

## Содержание

1. [Файлы DLL и зависимости](#1-файлы-dll-и-зависимости)
2. [API: функции и параметры](#2-api-функции-и-параметры)
3. [Коды возврата](#3-коды-возврата)
4. [Форматы вывода](#4-форматы-вывода)
5. [Delphi 7](#5-delphi-7)
6. [Borland C](#6-borland-c)
7. [Borland C++](#7-borland-c)
8. [Разбор JSON](#8-разбор-json)
9. [База SQLite (персистентность)](#9-база-sqlite-персистентность)
10. [UNC-пути (загрузка по сети)](#10-unc-пути-загрузка-по-сети)
11. [Важные ограничения](#11-важные-ограничения)

---

## 1. Файлы DLL и зависимости

Рядом с `dumphfdl.dll` должны лежать (в той же папке или в PATH):

| Файл | Откуда |
|---|---|
| `libwinpthread-1.dll` | MSYS2 mingw32 (`C:\msys64\mingw32\bin\`) |
| `libgcc_s_dw2-1.dll` | MSYS2 mingw32 |
| `zlib1.dll` | MSYS2 mingw32 |
| `libsqlite3-0.dll` | MSYS2 mingw32 (нужен для персистентности) |

`msvcrt.dll`, `KERNEL32.dll`, `WS2_32.dll` — системные, всегда есть.

---

## 2. API: функции и параметры

Все функции используют соглашение вызова **stdcall**, строки — ANSI
(`PAnsiChar` в Delphi, `char*` в C). Имена экспорта без декорации
(чистые, благодаря .def-файлу).

### HFDL_DecodeFile

```c
int32_t __stdcall HFDL_DecodeFile(
    const char *mcr_path,        // путь к .mcr файлу (ANSI, UNC ок)
    double bearing,              // пеленг антенны, градусы (0..360)
    double elevation,            // угол места, градусы
    const char *systable_path,   // systable.conf или NULL
    const char *cache_db_path,   // SQLite-кэш бортов или NULL
    const char *systable_db_path,// SQLite для PDU system table или NULL
    int32_t output_format,       // 1=TEXT, 2=JSON, 3=BASESTATION
    void (__stdcall *callback)(const char *msg, void *userdata),
    void *userdata);             // вернётся в колбэк
```

**Возвращает:** `0` — успех, отрицательное — код ошибки (см. ниже).

**Что делает:**
1. Читает заголовок MCR: частоту дискретизации, центральную частоту,
   число каналов.
2. Если задан `systable_path` — загружает system table и **автоматически
   выбирает каналы**, частоты которых попадают в полосу записи
   (центр ± половина частоты дискретизации). Вручную частоты указывать
   не нужно.
3. Применяет фазировку антенной решётки по `bearing`/`elevation`
   (калибровка из файла).
4. Декодирует HFDL-кадры, на каждое сообщение зовёт `callback`.
5. Блокирует до конца файла, затем корректно гасит все потоки.

**Колбэк:**
```c
void __stdcall MyCallback(const char *msg, void *userdata);
```
- `msg` — строка в выбранном формате, завершена нулём.
  Валидна **только во время вызова** — копируйте, если нужно сохранить.
- Вызывается **из рабочего потока** декодера.

### HFDL_Version

```c
const char * __stdcall HFDL_Version(void);
```
Возвращает строку вида `"dumphfdl 1.7.0"`. Указатель на статическую
память — освобождать не нужно.

---

## 3. Коды возврата

| Код | Константа (Delphi) | Значение |
|---|---|---|
| 0 | `HFDL_OK` | успех |
| -1 | `HFDL_ERR_ARGS` | неверные аргументы (NULL путь/колбэк, плохой формат) |
| -2 | `HFDL_ERR_MCR` | не удалось открыть/разобрать MCR |
| -3 | `HFDL_ERR_NO_FREQ` | нет частот каналов |
| -4 | `HFDL_ERR_INIT` | ошибка инициализации декодера |
| -5 | `HFDL_ERR_ABORT` | прервано (ошибка потока) |

---

## 4. Форматы вывода

Параметр `output_format`:

### 1 — HFDL_FMT_TEXT (человекочитаемый)

```
[ ] [8942.0 kHz] [-2.5 Hz] [-55.5/-57.9 dBFS] [2.4 dB] [300 bps] [S]
Downlink LPDU:
 Src AC: 211
 Dst GS: Shannon, Ireland
 Type: Unnumbered data
 Performance data:
  Version: 2
  Flight ID: ABY817
  Lat: 21.4896421
  ...
```

### 2 — HFDL_FMT_JSON

Один JSON-объект на сообщение, по одному в строке:

```json
{"hfdl":{"app":{"name":"dumphfdl","ver":"1.7.0"},"t":{"sec":1791905553,"usec":736153},"freq":8942000,"bit_rate":300,"sig_level":-55.510788,"noise_level":-57.912601,"freq_skew":-2.467604,"slot":"S","lpdu":{"err":false,"src":{"type":"Aircraft","id":211},"dst":{"type":"Ground station","id":7,"name":"Shannon, Ireland"},"type":{"id":13,"name":"Unnumbered data"},"hfnpdu":{"err":false,"type":{"id":209,"name":"Performance data"},"version":2,"flight_id":"ABY817","pos":{"lat":21.489642,"lon":65.198489},...
```

### 3 — HFDL_FMT_BASESTATION

CSV в формате Basestation (для plane-tracker'ов):

```
MSG,3,1,1,7810D3,1,2025/10/13,00:25:31.000,00:25:31.000,,,30.4294404,105.4569348,,,0,,,,
```

**Внимание:** basestation отдаёт **только сообщения с позицией ВС И
известным ICAO-адресом**. ICAO берётся из кэша, который наполняется при
приёме Logon Confirm (аплинк от станции). Если в записи логон-confirm не
встречался, а только отчёты о позиции — строк будет 0. Это поведение
оригинального декодера. Решение — база SQLite (раздел 9): собрать
соответствия ID→ICAO на записи с логоном, потом использовать на записях
без него.

---

## 5. Delphi 7

### Юнит импорта (hfdl.pas)

Полный юнит лежит в `delphi/hfdl.pas`. Ключевые части:

```pascal
unit hfdl;

interface

const
  HFDL_OK            =  0;
  HFDL_ERR_ARGS      = -1;
  HFDL_ERR_MCR       = -2;
  HFDL_ERR_NO_FREQ   = -3;
  HFDL_ERR_INIT      = -4;
  HFDL_ERR_ABORT     = -5;

  HFDL_FMT_TEXT       = 1;
  HFDL_FMT_JSON       = 2;
  HFDL_FMT_BASESTATION = 3;

type
  // Колбэк: вызывается из РАБОЧЕГО ПОТОКА.
  THFDLMsgCallback = procedure(Msg: PAnsiChar; UserData: Pointer); stdcall;

function HFDL_DecodeFile(MCRPath: PAnsiChar;
                         Bearing: Double;
                         Elevation: Double;
                         SystablePath: PAnsiChar;
                         CacheDBPath: PAnsiChar;
                         SystableDBPath: PAnsiChar;
                         OutputFormat: Integer;
                         Callback: THFDLMsgCallback;
                         UserData: Pointer): Integer; stdcall;

function HFDL_Version: PAnsiChar; stdcall;

implementation

const
  HFDL_DLL = 'dumphfdl.dll';

function HFDL_DecodeFile(...): Integer;
  external HFDL_DLL name 'HFDL_DecodeFile';

function HFDL_Version: PAnsiChar;
  external HFDL_DLL name 'HFDL_Version';

end.
```

### Пример: минимальное консольное приложение

```pascal
program hfdl_minimal;

{$APPTYPE CONSOLE}

uses
  SysUtils, hfdl;

// ОБЯЗАТЕЛЬНО обычная процедура, НЕ метод класса!
procedure MsgCallback(Msg: PAnsiChar; UserData: Pointer); stdcall;
begin
  // Вызывается из рабочего потока - VCL тут нет, консоль можно.
  WriteLn(Msg);
end;

var
  rc: Integer;
begin
  WriteLn('Version: ', string(HFDL_Version));

  rc := HFDL_DecodeFile(
    'C:\records\session.mcr',   // MCR-файл
    45.0,                        // bearing
    10.0,                        // elevation
    'C:\hfdl\systable.conf',     // systable (или nil)
    nil,                         // SQLite-кэш (или путь)
    nil,                         // SQLite systable (или путь)
    HFDL_FMT_TEXT,               // формат
    @MsgCallback,
    nil);                        // userdata

  case rc of
    HFDL_OK: WriteLn('Готово');
  else
    WriteLn('Ошибка: ', rc);
  end;
end.
```

### Пример: GUI-приложение (правильный паттерн)

Три золотых правила:

1. **Колбэк — обычная процедура**, не метод. Для доступа к форме
   передавайте `Self` в `userdata`.
2. **В колбэке НЕ трогать VCL** — он прилетает с рабочего потока.
   Только `PostMessage`.
3. **HFDL_DecodeFile блокирует поток** — запускайте из `TThread`,
   иначе интерфейс подвиснет.

```pascal
unit Unit1;

interface

uses
  Windows, Messages, SysUtils, Classes, Graphics, Controls, Forms,
  Dialogs, StdCtrls, hfdl;

const
  WM_HFDL_MSG = WM_USER + 100;

type
  TForm1 = class(TForm)
    Memo1: TMemo;
    btnDecode: TButton;
    procedure FormCreate(Sender: TObject);
    procedure btnDecodeClick(Sender: TObject);
  private
    FThread: TThread;
    // Обработка сообщения из колбэка (в ГЛАВНОМ потоке)
    procedure OnDecodedMessage(var Msg: TMessage); message WM_HFDL_MSG;
  public
  end;

var
  Form1: TForm1;

implementation

{$R *.dfm}

// ---- Колбэк: вызывается из РАБОЧЕГО ПОТОКА ----
procedure HFDLMsgCallback(Msg: PAnsiChar; UserData: Pointer); stdcall;
var
  p: PAnsiChar;
begin
  if (Msg = nil) or (UserData = nil) then
    Exit;
  // Копируем строку (Msg валидна только во время вызова)
  p := StrNew(Msg);
  // Отправляем главному потоку формы
  PostMessage(TForm1(UserData).Handle, WM_HFDL_MSG, 0, Integer(p));
end;

// ---- Поток декодирования ----
type
  TDecodeThread = class(TThread)
  private
    FOwner: TForm1;
    FRC: Integer;
    procedure SyncDone;
  protected
    procedure Execute; override;
  public
    constructor Create(AOwner: TForm1);
  end;

constructor TDecodeThread.Create(AOwner: TForm1);
begin
  inherited Create(True);
  FreeOnTerminate := True;
  FOwner := AOwner;
  Resume;
end;

procedure TDecodeThread.Execute;
begin
  FRC := HFDL_DecodeFile(
    PAnsiChar(AnsiString('C:\records\session.mcr')),
    45.0, 10.0,
    PAnsiChar(AnsiString('C:\hfdl\systable.conf')),
    nil, nil,
    HFDL_FMT_TEXT,
    @HFDLMsgCallback,
    FOwner);           // userdata = Self формы
  Synchronize(SyncDone);
end;

procedure TDecodeThread.SyncDone;
begin
  FOwner.Memo1.Lines.Add(Format('rc=%d', [FRC]));
end;

// ---- Обработка в главном потоке ----
procedure TForm1.OnDecodedMessage(var Msg: TMessage);
var
  p: PAnsiChar;
begin
  p := PAnsiChar(Msg.LParam);
  if p <> nil then
  begin
    try
      Memo1.Lines.Add(string(p));
    finally
      StrDispose(p);    // освобождаем копию
    end;
  end;
end;

procedure TForm1.FormCreate(Sender: TObject);
begin
  Memo1.Clear;
  Memo1.Lines.Add('Версия: ' + string(HFDL_Version));
end;

procedure TForm1.btnDecodeClick(Sender: TObject);
begin
  FThread := TDecodeThread.Create(Self);
end;

end.
```

Готовый работающий пример с формой (выбор файлов, статус, защита от
закрытия во время декодирования) — в `delphi/` (`Unit1.pas`,
`Unit1.dfm`, `hfdl_demo.dpr`).

---

## 6. Borland C

Консольное приложение, загружающее DLL динамически
(`LoadLibrary`/`GetProcAddress`).

```c
/* hfdl_c_demo.c - Borland C (bcc32) */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

typedef int32_t (__stdcall *DECODE_FN)(
    const char *, double, double, const char *,
    const char *, const char *, int32_t,
    void (__stdcall *)(const char *, void *), void *);
typedef const char * (__stdcall *VERSION_FN)(void);

static int msg_count = 0;

/* Колбэк: вызывается из РАБОЧЕГО ПОТОКА декодера */
static void __stdcall msg_callback(const char *msg, void *userdata)
{
    (void)userdata;
    msg_count++;
    printf("%s\n", msg);
}

int main(void)
{
    HMODULE h;
    DECODE_FN decode;
    VERSION_FN version;
    int32_t rc;

    h = LoadLibrary("dumphfdl.dll");
    if (h == NULL) {
        printf("LoadLibrary failed: %lu\n", GetLastError());
        return 1;
    }

    version = (VERSION_FN)GetProcAddress(h, "HFDL_Version");
    decode  = (DECODE_FN)GetProcAddress(h, "HFDL_DecodeFile");
    if (version == NULL || decode == NULL) {
        printf("Exports not found\n");
        FreeLibrary(h);
        return 1;
    }

    printf("Version: %s\n", version());

    rc = decode(
        "C:\\records\\session.mcr",  /* mcr_path */
        45.0,                         /* bearing */
        10.0,                         /* elevation */
        "C:\\hfdl\\systable.conf",   /* systable_path или NULL */
        NULL,                         /* cache_db_path или путь */
        NULL,                         /* systable_db_path или путь */
        1,                            /* 1=TEXT, 2=JSON, 3=BASESTATION */
        msg_callback,
        NULL);                        /* userdata */

    printf("rc=%d, messages=%d\n", rc, msg_count);
    FreeLibrary(h);
    return (rc == 0) ? 0 : 1;
}
```

Сборка (Borland C++ 5.5 / Turbo C):
```
bcc32 -W hfdl_c_demo.c cw32.lib
```

`cw32.lib` — импортная библиотека Windows API (идёт с компилятором).

---

## 7. Borland C++

Аналогично C, но с классом-обёрткой и обработкой ошибок.

```cpp
// hfdl_cpp_demo.cpp - Borland C++
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string>

extern "C" {
    typedef int32_t (__stdcall *DECODE_FN)(
        const char *, double, double, const char *,
        const char *, const char *, int32_t,
        void (__stdcall *)(const char *, void *), void *);
    typedef const char * (__stdcall *VERSION_FN)(void);
}

class HfdlDecoder {
public:
    HfdlDecoder() : lib_(NULL), decode_(NULL), version_(NULL) {}

    ~HfdlDecoder() {
        if (lib_) FreeLibrary(lib_);
    }

    bool load(const char* dll_path = "dumphfdl.dll") {
        lib_ = LoadLibrary(dll_path);
        if (!lib_) return false;
        decode_  = (DECODE_FN)GetProcAddress(lib_, "HFDL_DecodeFile");
        version_ = (VERSION_FN)GetProcAddress(lib_, "HFDL_Version");
        return decode_ && version_;
    }

    const char* version() const {
        return version_ ? version_() : "unknown";
    }

    /* fmt: 1=text, 2=json, 3=basestation */
    int32_t decode(const char* mcr, double bearing, double elev,
                   const char* systable, const char* cache_db,
                   const char* systable_db, int32_t fmt,
                   void (__stdcall *cb)(const char*, void*), void* ud) {
        if (!decode_) return -1;
        return decode_(mcr, bearing, elev, systable, cache_db,
                       systable_db, fmt, cb, ud);
    }

private:
    HMODULE    lib_;
    DECODE_FN  decode_;
    VERSION_FN version_;
};

/* Колбэк (рабочий поток!): складываем сообщения в буфер */
static std::string g_messages;
static void __stdcall collect(const char* msg, void* ud) {
    (void)ud;
    if (msg) g_messages += msg;
}

int main() {
    HfdlDecoder dec;
    if (!dec.load()) {
        printf("Failed to load dumphfdl.dll\n");
        return 1;
    }
    printf("Version: %s\n", dec.version());

    int32_t rc = dec.decode(
        "C:\\records\\session.mcr",
        45.0, 10.0,
        "C:\\hfdl\\systable.conf",
        NULL, NULL,
        2,              /* JSON */
        collect, NULL);

    printf("rc=%d\n", rc);
    /* JSON-строки, по одной в g_messages */
    printf("%s\n", g_messages.c_str());
    return rc == 0 ? 0 : 1;
}
```

Сборка:
```
bcc32 -W hfdl_cpp_demo.cpp cw32.lib
```

**Важно для Borland C++:** колбэк должен объявляться `__stdcall` —
иначе соглашение вызова не совпадёт и произойдёт повреждение стека.

---

## 8. Разбор JSON

JSON приходит **одним объектом в строке** (JSONL). Пример сообщения
(сокращён):

```json
{"hfdl":{"app":{"name":"dumphfdl","ver":"1.7.0"},
 "t":{"sec":1791905553,"usec":736153},"freq":8942000,"bit_rate":300,
 "sig_level":-55.5,"noise_level":-57.9,"freq_skew":-2.5,"slot":"S",
 "lpdu":{"src":{"type":"Aircraft","id":211},
  "dst":{"type":"Ground station","id":7,"name":"Shannon, Ireland"},
  "type":{"id":13,"name":"Unnumbered data"},
  "hfnpdu":{"type":{"id":209,"name":"Performance data"},
   "version":2,"flight_id":"ABY817",
   "pos":{"lat":21.489642,"lon":65.198489},
   "time":{"hour":17,"min":18,"sec":36}}}}}
```

### Delphi 7 (без сторонних библиотек)

В Delphi 7 нет встроенного JSON. Достаточно простого извлечения полей:

```pascal
// Извлечь строковое значение поля: ищет "key":"value"
function JsonGetStr(const Json, Key: string): string;
var
  P: Integer;
  S: string;
begin
  Result := '';
  P := Pos('"' + Key + '":"', Json);
  if P = 0 then Exit;
  S := Copy(Json, P + Length(Key) + 4, MaxInt);
  P := Pos('"', S);
  if P > 0 then
    Result := Copy(S, 1, P - 1);
end;

// Извлечь числовое значение поля: ищет "key":123.45
function JsonGetNum(const Json, Key: string): Double;
var
  P, I: Integer;
  S: string;
  Code: Integer;
begin
  Result := 0;
  P := Pos('"' + Key + '":', Json);
  if P = 0 then Exit;
  S := Copy(Json, P + Length(Key) + 3, MaxInt);
  I := 1;
  while (I <= Length(S)) and (S[I] in ['0'..'9', '-', '+', '.', 'e', 'E']) do
    Inc(I);
  if I > 1 then
    Val(Copy(S, 1, I - 1), Result, Code);
end;

// Применение в колбэке:
procedure OnJsonMessage(const Json: string);
begin
  // Позывной
  Memo1.Lines.Add('Flight: ' + JsonGetStr(Json, 'flight_id'));
  // Позиция
  if Pos('"pos"', Json) > 0 then
    Memo1.Lines.Add(Format('Pos: %f, %f',
      [JsonGetNum(Json, 'lat'), JsonGetNum(Json, 'lon')]));
  // Частота
  Memo1.Lines.Add(Format('Freq: %.0f Hz', [JsonGetNum(Json, 'freq')]));
end;
```

Работает для плоских полей любого уровня вложенности (ищет по всему
JSON). Для строгого разбора вложенных структур берите любую библиотеку
(uJson, SuperObject, XSuperObject — совместимы с D7).

### Borland C / C++

Минимальный поиск полей (достаточно для простых случаев):

```c
/* Найти строковое значение "key":"value" в JSON.
   Возвращает длину или -1. Буфер должен быть достаточного размера. */
static int json_get_str(const char *json, const char *key,
                        char *out, size_t out_size)
{
    char pattern[128];
    const char *p, *end;
    size_t len = 0;

    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    p = strstr(json, pattern);
    if (p == NULL) return -1;
    p += strlen(pattern);
    end = strchr(p, '"');
    if (end == NULL) return -1;

    len = (size_t)(end - p);
    if (len >= out_size) len = out_size - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    return (int)len;
}

/* Найти числовое "key":123.45 */
static double json_get_num(const char *json, const char *key)
{
    char pattern[128];
    const char *p;
    char buf[64];
    size_t i = 0;

    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    p = strstr(json, pattern);
    if (p == NULL) return 0.0;
    p += strlen(pattern);

    while (*p == ' ' || *p == '\t') p++;
    while (i < sizeof(buf) - 1 &&
           ((*p >= '0' && *p <= '9') || *p == '-' || *p == '+' ||
            *p == '.' || *p == 'e' || *p == 'E')) {
        buf[i++] = *p++;
    }
    buf[i] = '\0';
    return atof(buf);
}

/* Использование в колбэке: */
static void __stdcall on_msg(const char *msg, void *ud)
{
    char flight[32];
    double lat, lon, freq;

    if (json_get_str(msg, "flight_id", flight, sizeof(flight)) > 0)
        printf("Flight: %s\n", flight);
    lat  = json_get_num(msg, "lat");
    lon  = json_get_num(msg, "lon");
    freq = json_get_num(msg, "freq");
    printf("Freq: %.0f Hz\n", freq);
    if (lat != 0.0 || lon != 0.0)
        printf("Pos: %f, %f\n", lat, lon);
}
```

Для строгого разбора: в C — `cJSON` (один .c/.h, компилируется чем
угодно); в C++ — `nlohmann/json` (header-only, но требует C++11 —
Borland C++ 5.5 не потянет, берите cJSON).

---

## 9. База SQLite (персистентность)

### Зачем

Две проблемы при работе с **файлами** (а не с SDR-потоком):

1. **ICAO-адреса:** кэш `ID борта → ICAO` живёт только в памяти. Запись
   с логоном даёт соответствие, но следующая запись (с отчётами о
   позиции, без логона) его не видит — basestation пуст.
2. **System table:** передаётся набором PDU. Если набор не собрался до
   конца файла — таблица теряется.

Решение: SQLite-база, переживающая запуски.

### Как использовать

Передайте пути в `HFDL_DecodeFile`:

```pascal
rc := HFDL_DecodeFile(
  'C:\records\file1.mcr', 45.0, 10.0,
  'C:\hfdl\systable.conf',
  'C:\hfdl\ac_cache.db',    // <-- кэш бортов
  'C:\hfdl\systable.db',    // <-- PDU system table
  HFDL_FMT_TEXT, @MsgCallback, nil);
```

CLI-аналоги: `--ac-cache-db <file>`, `--systable-db <file>`.

### Что происходит

**Кэш бортов** (`ac_cache.db`):
- При Logon Confirm соответствие `(частота, ID) → ICAO` пишется в БД.
- При следующем запуске (другой файл) промах в памяти → чтение из БД.
- Устаревшие записи (> TTL, по умолчанию 3600 с) удаляются при закрытии.
- Таблица: `ac_cache(freq, id, icao_address, created_time)`.

**System table** (`systable.db`):
- Каждый принятый PDU набора пишется в БД.
- При частичном наборе в stderr: `System table version N: received PDU
  2 of 5 (2 of 5 parts collected)`.
- Следующий запуск: PDU восстанавливаются из БД, набор достраивается.
- После успешного декодирования набор удаляется из БД.
- Таблица: `systable_pdu(version, total_pdu_cnt, seq_num, data)`.

### Типичный сценарий (серия записей)

```
file1.mcr  --ac-cache-db shared.db   → логон, ICAO запомнен
file2.mcr  --ac-cache-db shared.db   → позиции + ICAO из БД → basestation работает!
file3.mcr  --ac-cache-db shared.db   → то же
```

### Просмотр базы

```
sqlite3 ac_cache.db "SELECT freq, id, printf('%06X', icao_address) FROM ac_cache;"
sqlite3 systable.db "SELECT version, total_pdu_cnt, COUNT(*) FROM systable_pdu GROUP BY 1,2;"
```

---

## 10. UNC-пути (загрузка по сети)

Все пути (mcr, systable, базы) поддерживают UNC — чтение по сети
работает нативно:

```
\\server\share\records\session.mcr
\\192.168.31.157\C$\records\session.mcr
\\?\UNC\server\share\file.mcr
```

Проверено на `\\192.168.31.157\C$\dev\sunrule-dumphfdl\README.md` —
чтение успешно (32-бит и 64-бит). Кода для этого не требуется.

---

## 11. Важные ограничения

1. **Колбэк — с рабочего потока.** В Delphi (GUI) не трогайте VCL
   напрямую: `PostMessage` + обработка `WM_USER+N` в главном потоке.
   В консольных C/C++ приложениях писать в stdout из колбэка безопасно.

2. **HFDL_DecodeFile блокирует** вызывающий поток до конца файла.
   Для GUI — только из `TThread`.

3. **API не реэнтрантен:** один вызов за раз (глобальное состояние
   декодера). Параллельные вызовы из разных потоков не поддерживаются.

4. **Прервать нельзя:** поток с `HFDL_DecodeFile` нельзя корректно
   остановить извне. Закрытие формы во время декодирования блокируйте
   (`FormCloseQuery` + `WaitFor`).

5. **basestation пуст без логона:** если в записи нет Logon Confirm,
   ICAO неизвестен → 0 строк. Используйте SQLite-кэш (раздел 9).

6. **Delphi 7 — ANSI:** строки передавайте как `PAnsiChar`
   (`PChar` в D7). Пути с не-ASCII символами в не-латинских именах
   пользователей Windows могут не открываться — используйте латиницу
   или короткие пути.

7. **32-бит:** DLL собрана под i386. Загружать только в 32-битный
   процесс (Delphi 7 — 32-битный, всё ок).

---

## Быстрый старт (Delphi 7)

1. Скопировать рядом с exe: `dumphfdl.dll`, `libwinpthread-1.dll`,
   `libgcc_s_dw2-1.dll`, `zlib1.dll`, `libsqlite3-0.dll`.
2. Добавить `hfdl.pas` в проект.
3. Вызвать `HFDL_DecodeFile` из `TThread`, колбэк через `PostMessage`.
4. Готовый пример: `delphi/hfdl_demo.dpr` — собрать и запустить.
