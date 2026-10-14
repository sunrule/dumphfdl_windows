unit Unit1;

interface

uses
  Windows, Messages, SysUtils, Classes, Graphics, Controls, Forms,
  Dialogs, StdCtrls, ExtCtrls, hfdl;

const
  WM_HFDL_MSG = WM_USER + 100;

type
  TForm1 = class(TForm)
    Memo1: TMemo;
    Panel1: TPanel;
    edMcrFile: TEdit;
    edSystable: TEdit;
    edCacheDB: TEdit;
    edSystableDB: TEdit;
    lblMcr: TLabel;
    lblSystable: TLabel;
    lblCacheDB: TLabel;
    lblSystableDB: TLabel;
    btnBrowse: TButton;
    btnDecode: TButton;
    lblStatus: TLabel;
    OpenDialog1: TOpenDialog;
    procedure btnBrowseClick(Sender: TObject);
    procedure btnDecodeClick(Sender: TObject);
    procedure FormCreate(Sender: TObject);
    procedure FormDestroy(Sender: TObject);
    procedure FormCloseQuery(Sender: TObject; var CanClose: Boolean);
  private
    FThread: TThread;
    FDecoding: Boolean;
    procedure OnDecodedMessage(var Msg: TMessage); message WM_HFDL_MSG;
    procedure SetDecoding(AValue: Boolean);
  public
  end;

var
  Form1: TForm1;

implementation

{$R *.dfm}

// Форвард-декларация: колбэк используется в TDecodeThread.Execute
// (выше по файлу), а определён ниже.
procedure HFDLMsgCallback(Msg: PAnsiChar; UserData: Pointer); stdcall; forward;

// ---------------------------------------------------------------------------
// Поток декодирования.
// HFDL_DecodeFile блокирует вызывающий поток до конца файла, поэтому
// запускаем его в отдельном потоке, чтобы не подвешивать интерфейс.
// Доступ к VCL из Execute только через Synchronize.
// ---------------------------------------------------------------------------
type
  TDecodeThread = class(TThread)
  private
    FOwner: TForm1;
    FMcrFile: string;
    FSystable: string;
    FCacheDB: string;
    FSystableDB: string;
    FRC: Integer;
    procedure SyncReportResult;
  protected
    procedure Execute; override;
  public
    constructor Create(AOwner: TForm1; const AMcrFile, ASystable,
            ACacheDB, ASystableDB: string);
  end;

constructor TDecodeThread.Create(AOwner: TForm1; const AMcrFile, ASystable,
    ACacheDB, ASystableDB: string);
begin
  inherited Create(True);       // создаём приостановленным, чтобы поля
                                // были заполнены до старта Execute
  FreeOnTerminate := True;
  FOwner := AOwner;
  FMcrFile := AMcrFile;
  FSystable := ASystable;
  FCacheDB := ACacheDB;
  FSystableDB := ASystableDB;
  FRC := 0;
  Resume;                       // теперь можно запускать
end;

// Вызывается в ГЛАВНОМ потоке через Synchronize - здесь можно трогать VCL.
procedure TDecodeThread.SyncReportResult;
begin
  case FRC of
    HFDL_OK:
      FOwner.Memo1.Lines.Add(Format('--- Готово: декодирование завершено (rc=%d)', [FRC]));
    HFDL_ERR_ARGS:
      FOwner.Memo1.Lines.Add('Ошибка: неверные аргументы');
    HFDL_ERR_MCR:
      FOwner.Memo1.Lines.Add('Ошибка: не удалось открыть или разобрать MCR-файл');
    HFDL_ERR_NO_FREQ:
      FOwner.Memo1.Lines.Add('Ошибка: нет частот каналов (проверьте systable.conf)');
    HFDL_ERR_INIT:
      FOwner.Memo1.Lines.Add('Ошибка: не удалось инициализировать декодер');
    HFDL_ERR_ABORT:
      FOwner.Memo1.Lines.Add('Ошибка: декодирование прервано');
  else
    FOwner.Memo1.Lines.Add(Format('Неизвестный код возврата: %d', [FRC]));
  end;
  FOwner.SetDecoding(False);
end;

procedure TDecodeThread.Execute;
var
  rc: Integer;
  st, acdb, stdb: PAnsiChar;
begin
  if FSystable = '' then
    st := nil
  else
    st := PAnsiChar(AnsiString(FSystable));
  if FCacheDB = '' then
    acdb := nil
  else
    acdb := PAnsiChar(AnsiString(FCacheDB));
  if FSystableDB = '' then
    stdb := nil
  else
    stdb := PAnsiChar(AnsiString(FSystableDB));

  // Вызов DLL. Блокирует до полного декодирования файла.
  // Колбэк HFDLMsgCallback прилетает из рабочего потока декодера -
  // там только PostMessage, VCL не трогаем.
  rc := HFDL_DecodeFile(
    PAnsiChar(AnsiString(FMcrFile)),
    45.0,                    // bearing, градусы
    10.0,                    // elevation, градусы
    st,                      // systable.conf или nil
    acdb,                    // SQLite-кэш бортов или nil
    stdb,                    // SQLite для PDU system table или nil
    HFDL_FMT_TEXT,           // формат: TEXT / JSON / BASESTATION
    @HFDLMsgCallback,        // ОБЫЧНАЯ процедура (не метод класса!)
    FOwner);                 // userdata = Self формы

  FRC := rc;
  Synchronize(SyncReportResult);
end;

// ---------------------------------------------------------------------------
// Колбэк сообщений. Вызывается из РАБОЧЕГО ПОТОКА декодера.
// ЗДЕСЬ НЕЛЬЗЯ ТРОГАТЬ VCL НАПРЯМУЮ - только PostMessage.
// UserData = Self формы (передан в HFDL_DecodeFile).
// ---------------------------------------------------------------------------
procedure HFDLMsgCallback(Msg: PAnsiChar; UserData: Pointer); stdcall;
var
  p: PAnsiChar;
begin
  if (Msg = nil) or (UserData = nil) then
    Exit;
  // Копируем строку в новую память (Msg валидна только во время вызова)
  p := StrNew(Msg);
  // Отправляем указатель главному потоку; там строка будет освобождена.
  // Integer(p) - прямое приведение указателя (32-бит, D7)
  PostMessage(TForm1(UserData).Handle, WM_HFDL_MSG, 0, Integer(p));
end;

// Обработка в главном потоке: безопасно добавляем текст в мемо
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
      StrDispose(p);   // освобождаем копию строки
    end;
  end;
end;

procedure TForm1.SetDecoding(AValue: Boolean);
begin
  FDecoding := AValue;
  if AValue then
    lblStatus.Caption := 'Декодирование...'
  else
    lblStatus.Caption := 'Готов';
  btnDecode.Enabled := not AValue;
  btnBrowse.Enabled := not AValue;
end;

procedure TForm1.FormCreate(Sender: TObject);
begin
  Memo1.Clear;
  Memo1.Lines.Add('dumphfdl DLL demo. Версия: ' + string(HFDL_Version));
  Memo1.Lines.Add('Выберите MCR-файл и нажмите "Декодировать".');
  Memo1.Lines.Add('');
  FDecoding := False;
end;

procedure TForm1.FormCloseQuery(Sender: TObject; var CanClose: Boolean);
begin
  // Не даём закрыть форму во время декодирования: HFDL_DecodeFile
  // блокирует поток, и поток нельзя корректно прервать извне.
  if FDecoding then
  begin
    CanClose := False;
    ShowMessage('Дождитесь окончания декодирования');
  end;
end;

procedure TForm1.FormDestroy(Sender: TObject);
begin
  // FThread с FreeOnTerminate := True освобождает себя сам;
  // здесь только ждём, если поток ещё жив.
  if FThread <> nil then
    FThread.WaitFor;
end;

procedure TForm1.btnBrowseClick(Sender: TObject);
begin
  OpenDialog1.Filter := 'MCR recordings (*.mcr)|*.mcr|All files (*.*)|*.*';
  if OpenDialog1.Execute then
    edMcrFile.Text := OpenDialog1.FileName;
end;

procedure TForm1.btnDecodeClick(Sender: TObject);
begin
  if not FileExists(edMcrFile.Text) then
  begin
    ShowMessage('Укажите существующий MCR-файл');
    Exit;
  end;
  Memo1.Clear;
  SetDecoding(True);
  FThread := TDecodeThread.Create(Self, edMcrFile.Text, edSystable.Text,
          edCacheDB.Text, edSystableDB.Text);
end;

end.
