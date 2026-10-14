{ ============================================================================
  hfdl_demo.dpr - пример использования dumphfdl.dll из Delphi 7

  Демонстрирует правильный паттерн:
    - HFDL_DecodeFile блокирует поток, поэтому вызывается из TThread
    - колбэк прилетает с рабочего потока - используем PostMessage,
      чтобы безопасно обновлять мемо в главном потоке

  Сборка: положите hfdl.pas и hfdl_demo.dpr в одну папку,
  скомпилируйте hfdl_demo.dpr. Рядом с exe положите dumphfdl.dll.
  ============================================================================ }

program hfdl_demo;

uses
  Forms,
  Unit1 in 'Unit1.pas' {Form1};

{$R *.res}

begin
  Application.Initialize;
  Application.CreateForm(TForm1, Form1);
  Application.Run;
end.
