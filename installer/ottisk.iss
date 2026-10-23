; Установщик «Оттиска» (Inno Setup 6.7, per-user, без прав администратора).
; Собирает tools\build.ps1 после cmake --install и докладки CRT/qpdf/tessdata/licenses:
;   "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" /Qp "/DAppVer=0.0.1" "/DDeployDir=<абс>\build\deploy" installer\ottisk.iss
; Результат — dist\Ottisk-<версия>-setup.exe. Установка в %LOCALAPPDATA%\Programs\Оттиск для текущего
; пользователя; /ALLUSERS разрешён только из командной строки (PrivilegesRequiredOverridesAllowed).
; Обновление — новый установщик поверх: AppId один во всех версиях, менять нельзя.
; Ассоциация .pdf (эталон 10.4): ProgID Ottisk.pdf, OpenWithProgids (текущий обработчик не отбирается),
; Capabilities + RegisteredApplications (Параметры → Приложения по умолчанию). Задача «fileassoc» включена
; по умолчанию; tests\build\install_test.py ставит с /TASKS="" (ассоциации в реестр не пишутся; свою
; запись удаления …\Uninstall\{AppId}_is1 Inno пишет всегда), пока не задан OTTISK_ALLOW_REGISTRY=1.
; При удалении уходят ProgID, Capabilities и RegisteredApplications; значение
; .pdf\OpenWithProgids\Ottisk.pdf остаётся (правило Windows — привязки расширений не удалять), данные
; %LOCALAPPDATA%\Ottisk остаются, кроме thumbs, autosave и lo-prof-*.
; Файл в UTF-8 с BOM — без него компилятор читает кириллицу в кодировке системы.

#ifndef AppVer
  #error Нужна версия: ISCC "/DAppVer=x.y.z" (tools\build.ps1 берёт её из cmake\product.cmake)
#endif
#ifndef DeployDir
  #define DeployDir "..\build\deploy"
#endif
#define AppExe "Ottisk.exe"
#define ProgId "Ottisk.pdf"

[Setup]
AppId={{A32A240B-4F06-4CF3-BDA4-9B358D7CF925}
AppName=Оттиск
AppVersion={#AppVer}
AppVerName=Оттиск {#AppVer}
VersionInfoVersion={#AppVer}
VersionInfoProductName=Оттиск
VersionInfoDescription=Установщик «Оттиска»
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=commandline
DefaultDirName={localappdata}\Programs\Оттиск
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.18362
ChangesAssociations=yes
Compression=lzma2/max
SolidCompression=yes
SetupIconFile=app.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName=Оттиск
OutputDir=..\dist
OutputBaseFilename=Ottisk-{#AppVer}-setup
WizardStyle=modern
ShowLanguageDialog=auto
UsedUserAreasWarning=no

[Languages]
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
ru.AppDescription=Просмотр, правка, комментарии, формы, подпись и печать PDF
en.AppDescription=View, edit, comment, fill, sign and print PDF documents
ru.PdfTypeName=Документ PDF
en.PdfTypeName=PDF Document
ru.AssocTask=Добавить «Оттиск» в «Открыть с помощью» и в «Приложения по умолчанию» для файлов PDF
en.AssocTask=Add Ottisk to "Open with" and to "Default apps" for PDF files
ru.AssocGroup=Файлы PDF:
en.AssocGroup=PDF files:

[Tasks]
Name: "fileassoc"; Description: "{cm:AssocTask}"; GroupDescription: "{cm:AssocGroup}"
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#DeployDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Оттиск"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\Оттиск"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
; HKA = HKCU при установке для пользователя (HKLM — только при /ALLUSERS).
; ProgID — удаляется при деинсталляции целиком.
Root: HKA; Subkey: "Software\Classes\{#ProgId}"; ValueType: string; ValueName: ""; ValueData: "{cm:PdfTypeName}"; Flags: uninsdeletekey; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\{#ProgId}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"",0"; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\{#ProgId}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\{#ProgId}\shell\print\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" /p ""%1"""; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\{#ProgId}\shell\printto\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" /pt ""%1"" ""%2"" ""%3"" ""%4"""; Tasks: fileassoc
; .pdf → «Открыть с помощью»: только значение в OpenWithProgids, (По умолчанию) у .pdf не трогаем;
; при удалении значение остаётся (привязка расширения). ValueType: none здесь не годится — с ним Inno
; создаёт только ключ, без значения; пустая строка REG_SZ — обычная форма записи OpenWithProgids.
Root: HKA; Subkey: "Software\Classes\.pdf\OpenWithProgids"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Tasks: fileassoc
; Capabilities — для «Параметры → Приложения по умолчанию» (ms-settings:defaultapps?registeredAppUser=Ottisk).
; Software\Ottisk держит только Capabilities (настройки — settings.ini, не реестр).
Root: HKA; Subkey: "Software\Ottisk"; Flags: uninsdeletekeyifempty; Tasks: fileassoc
Root: HKA; Subkey: "Software\Ottisk\Capabilities"; ValueType: string; ValueName: "ApplicationName"; ValueData: "Оттиск"; Flags: uninsdeletekey; Tasks: fileassoc
Root: HKA; Subkey: "Software\Ottisk\Capabilities"; ValueType: string; ValueName: "ApplicationDescription"; ValueData: "{cm:AppDescription}"; Tasks: fileassoc
Root: HKA; Subkey: "Software\Ottisk\Capabilities"; ValueType: string; ValueName: "ApplicationIcon"; ValueData: """{app}\{#AppExe}"",0"; Tasks: fileassoc
Root: HKA; Subkey: "Software\Ottisk\Capabilities\FileAssociations"; ValueType: string; ValueName: ".pdf"; ValueData: "{#ProgId}"; Tasks: fileassoc
Root: HKA; Subkey: "Software\RegisteredApplications"; ValueType: string; ValueName: "Ottisk"; ValueData: "Software\Ottisk\Capabilities"; Flags: uninsdeletevalue; Tasks: fileassoc

[Run]
; SHChangeNotify(SHCNE_ASSOCCHANGED) — Проводник перечитывает ассоциации без перезагрузки.
Filename: "{app}\{#AppExe}"; Parameters: "--register-notify"; Flags: runhidden waituntilterminated; Tasks: fileassoc
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,Оттиск}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Данные программы остаются (настройки, недавние, подписи, штампы, доверие), кроме кешей:
Type: filesandordirs; Name: "{localappdata}\Ottisk\thumbs"
Type: filesandordirs; Name: "{localappdata}\Ottisk\autosave"
Type: filesandordirs; Name: "{localappdata}\Ottisk\lo-prof-*"
Type: dirifempty; Name: "{app}"
