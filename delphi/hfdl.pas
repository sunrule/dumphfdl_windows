{ ============================================================================
  hfdl.pas - импорт DLL-библиотеки декодера HFDL (dumphfdl.dll)

  Использование из Delphi 7 (32-bit):

    uses hfdl;

    // 1. Объявить ОБЫЧНУЮ (не метод!) процедуру колбэка:
    procedure HFDLMsgCallback(Msg: PAnsiChar; UserData: Pointer); stdcall;

    // 2. Реализация. Колбэк прилетает из РАБОЧЕГО потока -
    //    НЕ трогать VCL напрямую, только PostMessage!
    //    UserData - это Self формы (передаётся в HFDL_DecodeFile):
    procedure HFDLMsgCallback(Msg: PAnsiChar; UserData: Pointer); stdcall;
    begin
      PostMessage(TForm1(UserData).Handle, WM_USER + 1, 0, LPARAM(StrNew(Msg)));
    end;

    // 3. Вызов декодирования (блокирующий - запускайте из TThread!):
    rc := HFDL_DecodeFile('D:\records\session.mcr',
                          45.0,      // bearing, градусы
                          10.0,      // elevation, градусы
                          'D:\hfdl\systable.conf',  // или nil
                          'D:\hfdl\ac_cache.db',    // SQLite-кэш или nil
                          'D:\hfdl\systable.db',    // SQLite systable или nil
                          HFDL_FMT_TEXT,   // формат: TEXT / JSON / BASESTATION
                          @HFDLMsgCallback,
                          Self);      // userdata = Self формы
    // rc = 0 - успех; < 0 - код ошибки (см. константы ниже)

  ВАЖНО:
    - Колбэк ДОЛЖЕН быть обычной процедурой (не методом класса!).
      Method pointer (@TForm1.SomeMethod) передавать нельзя -
      несовпадение соглашения вызова приведёт к краху.
      Для доступа к форме используйте UserData = Self.
    - Колбэк вызывается из РАБОЧЕГО ПОТОКА декодера.
      Нельзя обращаться к элементам VCL напрямую!
      Используйте PostMessage + собственную обработку WM_USER.
    - HFDL_DecodeFile блокирует поток до конца файла -
      вызывайте из TThread, иначе интерфейс подвиснет.
    - API не реэнтрантен: только один вызов HFDL_DecodeFile за раз.
    - Строки - ANSI (PAnsiChar), т.к. Delphi 7 не Unicode.
    - Поддерживаются UNC-пути: '\\server\share\file.mcr'
  ============================================================================ }

unit hfdl;

interface

const
  // Коды возврата HFDL_DecodeFile
  HFDL_OK           =  0;  // успех
  HFDL_ERR_ARGS     = -1;  // неверные аргументы (пустой путь, колбэк или формат)
  HFDL_ERR_MCR      = -2;  // не удалось открыть/разобрать MCR-файл
  HFDL_ERR_NO_FREQ  = -3;  // нет частот каналов (нет systable или полоса пуста)
  HFDL_ERR_INIT     = -4;  // ошибка инициализации декодера
  HFDL_ERR_ABORT    = -5;  // декодирование прервано (ошибка потока)

  // Форматы вывода (параметр OutputFormat)
  HFDL_FMT_TEXT       = 1;  // человекочитаемый текст (по умолчанию)
  HFDL_FMT_JSON       = 2;  // JSON-объект на сообщение, по одному в строке
  HFDL_FMT_BASESTATION = 3; // формат Basestation (CSV с позициями ВС)

type
  // Тип колбэка: вызывается на каждое декодированное сообщение.
  // Msg - строка ANSI, валидна ТОЛЬКО во время вызова (копируйте!).
  // UserData - тот же указатель, что передан в HFDL_DecodeFile.
  THFDLMsgCallback = procedure(Msg: PAnsiChar; UserData: Pointer); stdcall;

// Декодировать MCR-запись целиком (блокирует до конца файла).
// MCRPath       - путь к .mcr файлу (ANSI; поддерживается UNC)
// Bearing       - пеленг антенны приёма, градусы (0..360)
// Elevation     - угол места антенны, градусы
// SystablePath  - путь к systable.conf, или nil.
//                 Если задан: частоты каналов выбираются автоматически
//                 из system table (только те, что попадают в полосу
//                 записи MCR), плюс обогащение данными станций.
//                 Если nil: декодируется один канал в центре полосы.
// CacheDBPath   - путь к файлу SQLite для персистентного кэша
//                 соответствий "ID борта -> ICAO адрес", или nil.
//                 Если задан: логон-подтверждение из этого (или
//                 предыдущего) запуска запоминается, поэтому отчёты
//                 о позиции из последующих записей (без логона)
//                 всё равно резолвятся в ICAO адрес.
//                 Требует сборки DLL с поддержкой SQLite.
// SystableDBPath - путь к файлу SQLite для персистентного хранения
//                 частично принятых PDU system table, или nil.
//                 Если задан: system table, разбитая между несколькими
//                 записями, всё равно может быть собрана - PDU из
//                 предыдущих запусков восстанавливаются автоматически.
//                 Требует сборки DLL с поддержкой SQLite.
// OutputFormat  - HFDL_FMT_TEXT / HFDL_FMT_JSON / HFDL_FMT_BASESTATION
// Callback      - процедура приёма сообщений (не nil)
// UserData      - произвольный указатель, вернётся в колбэке
// Возвращает: 0 = успех, < 0 = код ошибки
function HFDL_DecodeFile(MCRPath: PAnsiChar;
                         Bearing: Double;
                         Elevation: Double;
                         SystablePath: PAnsiChar;
                         CacheDBPath: PAnsiChar;
                         SystableDBPath: PAnsiChar;
                         OutputFormat: Integer;
                         Callback: THFDLMsgCallback;
                         UserData: Pointer): Integer; stdcall;

// Версия библиотеки, например 'dumphfdl 1.7.0'.
// Указатель на статическую строку - освобождать НЕ нужно.
function HFDL_Version: PAnsiChar; stdcall;

implementation

const
  HFDL_DLL = 'dumphfdl.dll';

function HFDL_DecodeFile(MCRPath: PAnsiChar;
                         Bearing: Double;
                         Elevation: Double;
                         SystablePath: PAnsiChar;
                         CacheDBPath: PAnsiChar;
                         SystableDBPath: PAnsiChar;
                         OutputFormat: Integer;
                         Callback: THFDLMsgCallback;
                         UserData: Pointer): Integer;
  external HFDL_DLL name 'HFDL_DecodeFile';

function HFDL_Version: PAnsiChar;
  external HFDL_DLL name 'HFDL_Version';

end.
