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
  OnCloseQuery = FormCloseQuery
  OnCreate = FormCreate
  OnDestroy = FormDestroy
  PixelsPerInch = 96
  TextHeight = 13
  object Panel1: TPanel
    Left = 0
    Top = 0
    Width = 684
    Height = 113
    Align = alTop
    BevelOuter = bvNone
    TabOrder = 0
    object lblMcr: TLabel
      Left = 8
      Top = 12
      Width = 56
      Height = 13
      Caption = 'MCR-'#1092#1072#1081#1083':'
    end
    object lblSystable: TLabel
      Left = 8
      Top = 40
      Width = 41
      Height = 13
      Caption = 'systable:'
    end
    object lblCacheDB: TLabel
      Left = 8
      Top = 68
      Width = 44
      Height = 13
      Caption = #1082#1101#1096' (db):'
    end
    object lblSystableDB: TLabel
      Left = 8
      Top = 92
      Width = 56
      Height = 13
      Caption = 'systable db:'
    end
    object lblStatus: TLabel
      Left = 8
      Top = 112
      Width = 29
      Height = 13
      Caption = #1043#1086#1090#1086#1074
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
      Text = 'C:\dev\sunrule-dumphfdl\etc\systable.conf '
    end
    object edCacheDB: TEdit
      Left = 76
      Top = 64
      Width = 496
      Height = 21
      TabOrder = 2
      Text = 'C:\dev\sunrule-dumphfdl\delphi\ac.sq3'
    end
    object edSystableDB: TEdit
      Left = 76
      Top = 90
      Width = 496
      Height = 21
      TabOrder = 3
      Text = 'C:\dev\sunrule-dumphfdl\delphi\st.sq3'
    end
    object btnBrowse: TButton
      Left = 580
      Top = 6
      Width = 100
      Height = 25
      Caption = #1054#1073#1079#1086#1088'...'
      TabOrder = 4
      OnClick = btnBrowseClick
    end
    object btnDecode: TButton
      Left = 580
      Top = 34
      Width = 100
      Height = 25
      Caption = #1044#1077#1082#1086#1076#1080#1088#1086#1074#1072#1090#1100
      TabOrder = 5
      OnClick = btnDecodeClick
    end
  end
  object Memo1: TMemo
    Left = 0
    Top = 113
    Width = 684
    Height = 348
    Align = alClient
    Font.Charset = DEFAULT_CHARSET
    Font.Color = clWindowText
    Font.Height = -11
    Font.Name = 'Courier New'
    Font.Style = []
    ParentFont = False
    ScrollBars = ssVertical
    TabOrder = 1
  end
  object OpenDialog1: TOpenDialog
    Left = 640
    Top = 8
  end
end
