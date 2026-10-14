object Form1: TForm1
  Left = 200
  Top = 120
  Width = 700
  Height = 500
  Caption = 'dumphfdl DLL demo'
  Color = clBtnFace
  Font.Charset = DEFAULT_CHARSET
  Font.Color = clWindowText
  Font.Height = -11
  Font.Name = 'MS Sans Serif'
  Font.Style = []
  OldCreateOrder = False
  OnCreate = FormCreate
  OnDestroy = FormDestroy
  OnCloseQuery = FormCloseQuery
  PixelsPerInch = 96
  TextHeight = 13
  object Panel1: TPanel
    Left = 0
    Top = 0
    Width = 692
    Height = 113
    Align = alTop
    BevelOuter = bvNone
    TabOrder = 0
    object lblMcr: TLabel
      Left = 8
      Top = 12
      Width = 55
      Height = 13
      Caption = 'MCR-файл:'
    end
    object lblSystable: TLabel
      Left = 8
      Top = 40
      Width = 55
      Height = 13
      Caption = 'systable:'
    end
    object lblCacheDB: TLabel
      Left = 8
      Top = 68
      Width = 62
      Height = 13
      Caption = 'кэш (db):'
    end
    object lblSystableDB: TLabel
      Left = 8
      Top = 92
      Width = 62
      Height = 13
      Caption = 'systable db:'
    end
    object edMcrFile: TEdit
      Left = 76
      Top = 8
      Width = 496
      Height = 21
      TabOrder = 0
    end
    object edSystable: TEdit
      Left = 76
      Top = 36
      Width = 496
      Height = 21
      TabOrder = 1
    end
    object edCacheDB: TEdit
      Left = 76
      Top = 64
      Width = 496
      Height = 21
      TabOrder = 2
    end
    object edSystableDB: TEdit
      Left = 76
      Top = 90
      Width = 496
      Height = 21
      TabOrder = 3
    end
    object btnBrowse: TButton
      Left = 580
      Top = 6
      Width = 100
      Height = 25
      Caption = 'Обзор...'
      TabOrder = 4
      OnClick = btnBrowseClick
    end
    object btnDecode: TButton
      Left = 580
      Top = 34
      Width = 100
      Height = 25
      Caption = 'Декодировать'
      TabOrder = 5
      OnClick = btnDecodeClick
    end
    object lblStatus: TLabel
      Left = 8
      Top = 112
      Width = 200
      Height = 13
      Caption = 'Готов'
    end
  end
  object Memo1: TMemo
    Left = 0
    Top = 113
    Width = 692
    Height = 360
    Align = alClient
    Font.Name = 'Courier New'
    ParentFont = False
    ScrollBars = ssVertical
    TabOrder = 1
  end
  object OpenDialog1: TOpenDialog
    Left = 640
    Top = 8
  end
end
