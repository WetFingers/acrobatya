; Установщик «АкроБати» (Inno Setup 6.7, per-user, без прав администратора).
; Собирает tools\build.ps1 после cmake --install и докладки CRT/qpdf/tessdata/licenses:
;   "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" /Qp "/DAppVer=0.0.1" "/DDeployDir=<абс>\build\deploy" installer\ottisk.iss
; Результат — dist\AcroBatya-<версия>-setup.exe. Установка в %LOCALAPPDATA%\Programs\АкроБатя для текущего
; пользователя; /ALLUSERS разрешён только из командной строки (PrivilegesRequiredOverridesAllowed).
; Обновление — новый установщик поверх: AppId один во всех версиях, менять нельзя.
; Ассоциация .pdf (эталон 10.4): ProgID AcroBatya.pdf, OpenWithProgids (текущий обработчик не отбирается),
; Capabilities + RegisteredApplications (Параметры → Приложения по умолчанию). Задача «fileassoc» включена
; по умолчанию; tests\build\install_test.py ставит с /TASKS="" (ассоциации в реестр не пишутся; свою
; запись удаления …\Uninstall\{AppId}_is1 Inno пишет всегда), пока не задан OTTISK_ALLOW_REGISTRY=1.
; При удалении уходят ProgID, Capabilities и RegisteredApplications; значение
; .pdf\OpenWithProgids\AcroBatya.pdf остаётся (правило Windows — привязки расширений не удалять), данные
; %LOCALAPPDATA%\AcroBatya остаются, кроме thumbs, autosave и lo-prof-*.
; Файл в UTF-8 с BOM — без него компилятор читает кириллицу в кодировке системы.

#ifndef AppVer
  #error Нужна версия: ISCC "/DAppVer=x.y.z" (tools\build.ps1 берёт её из cmake\product.cmake)
#endif
#ifndef DeployDir
  #define DeployDir "..\build\deploy"
#endif
#define AppExe "AcroBatya.exe"
#define ProgId "AcroBatya.pdf"

[Setup]
AppId={{A32A240B-4F06-4CF3-BDA4-9B358D7CF925}
AppName=АкроБатя
AppVersion={#AppVer}
AppVerName=АкроБатя {#AppVer}
VersionInfoVersion={#AppVer}
VersionInfoProductName=АкроБатя
VersionInfoDescription=Установщик «АкроБати»
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=commandline
DefaultDirName={localappdata}\Programs\АкроБатя
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.18362
ChangesAssociations=yes
Compression=lzma2/max
SolidCompression=yes
SetupIconFile=app.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName=АкроБатя
OutputDir=..\dist
OutputBaseFilename=AcroBatya-{#AppVer}-setup
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
ru.AssocTask=Добавить «АкроБатю» в «Открыть с помощью» и в «Приложения по умолчанию» для файлов PDF
en.AssocTask=Add AcroBatya to "Open with" and to "Default apps" for PDF files
ru.AssocGroup=Файлы PDF:
en.AssocGroup=PDF files:
ru.LaunchApp=Запустить «АкроБатю»
en.LaunchApp=Launch AcroBatya

[Messages]
; Имя «АкроБатя» склоняется, а Inno подставляет [name]/%1 как есть («установить АкроБатя»). Русские
; сообщения с именем переписаны через родовое слово: «программа «АкроБатя»» — имя в кавычках не склоняется.
ru.SetupLdrStartupMessage=Данная программа установит программу «%1» на ваш компьютер, продолжить?
ru.SetupAppRunningError=Обнаружен запущенный экземпляр программы «%1».%n%nПожалуйста, закройте все экземпляры приложения, затем нажмите «OK», чтобы продолжить, или «Отмена», чтобы выйти.
ru.UninstallAppRunningError=Деинсталлятор обнаружил запущенный экземпляр программы «%1».%n%nПожалуйста, закройте все экземпляры приложения, затем нажмите «OK», чтобы продолжить, или «Отмена», чтобы выйти.
ru.WelcomeLabel1=Вас приветствует Мастер установки программы «[name]»
ru.SelectDirDesc=В какую папку установить программу «[name]»?
ru.SelectDirLabel3=Программа «[name]» будет установлена в следующую папку.
ru.SelectTasksLabel2=Выберите дополнительные задачи, которые должны выполниться при установке программы «[name]», после этого нажмите «Далее»:
ru.ReadyLabel1=Программа установки готова начать установку программы «[name]» на ваш компьютер.
ru.PreparingDesc=Программа установки подготавливается к установке программы «[name]» на ваш компьютер.
ru.PreviousInstallNotCompleted=Установка или удаление предыдущей программы не были завершены. Вам потребуется перезагрузить компьютер, чтобы завершить ту установку.%n%nПосле перезагрузки запустите вновь Программу установки, чтобы завершить установку программы «[name]».
ru.PrepareToInstallNeedsRestart=Программе установки требуется перезагрузить ваш компьютер. Когда перезагрузка завершится, пожалуйста, запустите программу установки вновь, чтобы завершить установку программы «[name]».%n%nПроизвести перезагрузку сейчас?
ru.InstallingLabel=Пожалуйста, подождите, пока программа «[name]» установится на ваш компьютер.
ru.FinishedHeadingLabel=Завершение Мастера установки программы «[name]»
ru.FinishedLabelNoIcons=Программа «[name]» установлена на ваш компьютер.
ru.FinishedLabel=Программа «[name]» установлена на ваш компьютер. Приложение можно запустить с помощью соответствующего значка.
ru.FinishedRestartLabel=Для завершения установки программы «[name]» требуется перезагрузить компьютер. Произвести перезагрузку сейчас?
ru.FinishedRestartMessage=Для завершения установки программы «[name]» требуется перезагрузить компьютер.%n%nПроизвести перезагрузку сейчас?
ru.ConfirmUninstall=Вы действительно хотите удалить программу «%1» и все её компоненты?
ru.UninstallStatusLabel=Пожалуйста, подождите, пока программа «%1» будет удалена с вашего компьютера.
ru.UninstalledAll=Программа «%1» была полностью удалена с вашего компьютера.
ru.UninstalledMost=Деинсталляция программы «%1» завершена.%n%nЧасть элементов не удалось удалить. Вы можете удалить их самостоятельно.
ru.UninstalledAndNeedsRestart=Для завершения деинсталляции программы «%1» необходимо произвести перезагрузку вашего компьютера.%n%nВыполнить перезагрузку сейчас?
ru.StatusUninstalling=Деинсталляция программы «%1»...
ru.ShutdownBlockReasonInstallingApp=Установка программы «%1».
ru.ShutdownBlockReasonUninstallingApp=Деинсталляция программы «%1».
ru.LaunchApp=Запустить «АкроБатю»
en.LaunchApp=Launch AcroBatya

[Messages]
; Имя «АкроБатя» склоняется, а Inno подставляет [name]/%1 как есть («установить АкроБатя»). Русские
; сообщения с именем переписаны через родовое слово: «программа «АкроБатя»» — имя в кавычках не склоняется.
ru.SetupLdrStartupMessage=Данная программа установит программу «%1» на ваш компьютер, продолжить?
ru.SetupAppRunningError=Обнаружен запущенный экземпляр программы «%1».%n%nПожалуйста, закройте все экземпляры приложения, затем нажмите «OK», чтобы продолжить, или «Отмена», чтобы выйти.
ru.UninstallAppRunningError=Деинсталлятор обнаружил запущенный экземпляр программы «%1».%n%nПожалуйста, закройте все экземпляры приложения, затем нажмите «OK», чтобы продолжить, или «Отмена», чтобы выйти.
ru.WelcomeLabel1=Вас приветствует Мастер установки программы «[name]»
ru.SelectDirDesc=В какую папку установить программу «[name]»?
ru.SelectDirLabel3=Программа «[name]» будет установлена в следующую папку.
ru.SelectTasksLabel2=Выберите дополнительные задачи, которые должны выполниться при установке программы «[name]», после этого нажмите «Далее»:
ru.ReadyLabel1=Программа установки готова начать установку программы «[name]» на ваш компьютер.
ru.PreparingDesc=Программа установки подготавливается к установке программы «[name]» на ваш компьютер.
ru.PreviousInstallNotCompleted=Установка или удаление предыдущей программы не были завершены. Вам потребуется перезагрузить компьютер, чтобы завершить ту установку.%n%nПосле перезагрузки запустите вновь Программу установки, чтобы завершить установку программы «[name]».
ru.PrepareToInstallNeedsRestart=Программе установки требуется перезагрузить ваш компьютер. Когда перезагрузка завершится, пожалуйста, запустите программу установки вновь, чтобы завершить установку программы «[name]».%n%nПроизвести перезагрузку сейчас?
ru.InstallingLabel=Пожалуйста, подождите, пока программа «[name]» установится на ваш компьютер.
ru.FinishedHeadingLabel=Завершение Мастера установки программы «[name]»
ru.FinishedLabelNoIcons=Программа «[name]» установлена на ваш компьютер.
ru.FinishedLabel=Программа «[name]» установлена на ваш компьютер. Приложение можно запустить с помощью соответствующего значка.
ru.FinishedRestartLabel=Для завершения установки программы «[name]» требуется перезагрузить компьютер. Произвести перезагрузку сейчас?
ru.FinishedRestartMessage=Для завершения установки программы «[name]» требуется перезагрузить компьютер.%n%nПроизвести перезагрузку сейчас?
ru.ConfirmUninstall=Вы действительно хотите удалить программу «%1» и все её компоненты?
ru.UninstallStatusLabel=Пожалуйста, подождите, пока программа «%1» будет удалена с вашего компьютера.
ru.UninstalledAll=Программа «%1» была полностью удалена с вашего компьютера.
ru.UninstalledMost=Деинсталляция программы «%1» завершена.%n%nЧасть элементов не удалось удалить. Вы можете удалить их самостоятельно.
ru.UninstalledAndNeedsRestart=Для завершения деинсталляции программы «%1» необходимо произвести перезагрузку вашего компьютера.%n%nВыполнить перезагрузку сейчас?
ru.StatusUninstalling=Деинсталляция программы «%1»...
ru.ShutdownBlockReasonInstallingApp=Установка программы «%1».
ru.ShutdownBlockReasonUninstallingApp=Деинсталляция программы «%1».
ru.LaunchApp=Запустить «АкроБатю»
en.LaunchApp=Launch AcroBatya

[Messages]
; Имя «АкроБатя» склоняется, а Inno подставляет [name]/%1 как есть («установить АкроБатя»). Русские
; сообщения с именем переписаны через родовое слово: «программа «АкроБатя»» — имя в кавычках не склоняется.
ru.SetupLdrStartupMessage=Данная программа установит программу «%1» на ваш компьютер, продолжить?
ru.SetupAppRunningError=Обнаружен запущенный экземпляр программы «%1».%n%nПожалуйста, закройте все экземпляры приложения, затем нажмите «OK», чтобы продолжить, или «Отмена», чтобы выйти.
ru.UninstallAppRunningError=Деинсталлятор обнаружил запущенный экземпляр программы «%1».%n%nПожалуйста, закройте все экземпляры приложения, затем нажмите «OK», чтобы продолжить, или «Отмена», чтобы выйти.
ru.WelcomeLabel1=Вас приветствует Мастер установки программы «[name]»
ru.SelectDirDesc=В какую папку установить программу «[name]»?
ru.SelectDirLabel3=Программа «[name]» будет установлена в следующую папку.
ru.SelectTasksLabel2=Выберите дополнительные задачи, которые должны выполниться при установке программы «[name]», после этого нажмите «Далее»:
ru.ReadyLabel1=Программа установки готова начать установку программы «[name]» на ваш компьютер.
ru.PreparingDesc=Программа установки подготавливается к установке программы «[name]» на ваш компьютер.
ru.PreviousInstallNotCompleted=Установка или удаление предыдущей программы не были завершены. Вам потребуется перезагрузить компьютер, чтобы завершить ту установку.%n%nПосле перезагрузки запустите вновь Программу установки, чтобы завершить установку программы «[name]».
ru.PrepareToInstallNeedsRestart=Программе установки требуется перезагрузить ваш компьютер. Когда перезагрузка завершится, пожалуйста, запустите программу установки вновь, чтобы завершить установку программы «[name]».%n%nПроизвести перезагрузку сейчас?
ru.InstallingLabel=Пожалуйста, подождите, пока программа «[name]» установится на ваш компьютер.
ru.FinishedHeadingLabel=Завершение Мастера установки программы «[name]»
ru.FinishedLabelNoIcons=Программа «[name]» установлена на ваш компьютер.
ru.FinishedLabel=Программа «[name]» установлена на ваш компьютер. Приложение можно запустить с помощью соответствующего значка.
ru.FinishedRestartLabel=Для завершения установки программы «[name]» требуется перезагрузить компьютер. Произвести перезагрузку сейчас?
ru.FinishedRestartMessage=Для завершения установки программы «[name]» требуется перезагрузить компьютер.%n%nПроизвести перезагрузку сейчас?
ru.ConfirmUninstall=Вы действительно хотите удалить программу «%1» и все её компоненты?
ru.UninstallStatusLabel=Пожалуйста, подождите, пока программа «%1» будет удалена с вашего компьютера.
ru.UninstalledAll=Программа «%1» была полностью удалена с вашего компьютера.
ru.UninstalledMost=Деинсталляция программы «%1» завершена.%n%nЧасть элементов не удалось удалить. Вы можете удалить их самостоятельно.
ru.UninstalledAndNeedsRestart=Для завершения деинсталляции программы «%1» необходимо произвести перезагрузку вашего компьютера.%n%nВыполнить перезагрузку сейчас?
ru.StatusUninstalling=Деинсталляция программы «%1»...
ru.ShutdownBlockReasonInstallingApp=Установка программы «%1».
ru.ShutdownBlockReasonUninstallingApp=Деинсталляция программы «%1».

[Tasks]
Name: "fileassoc"; Description: "{cm:AssocTask}"; GroupDescription: "{cm:AssocGroup}"
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#DeployDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\АкроБатя"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\АкроБатя"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

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
; Capabilities — для «Параметры → Приложения по умолчанию» (ms-settings:defaultapps?registeredAppUser=AcroBatya).
; Software\AcroBatya держит только Capabilities (настройки — settings.ini, не реестр).
Root: HKA; Subkey: "Software\AcroBatya"; Flags: uninsdeletekeyifempty; Tasks: fileassoc
Root: HKA; Subkey: "Software\AcroBatya\Capabilities"; ValueType: string; ValueName: "ApplicationName"; ValueData: "АкроБатя"; Flags: uninsdeletekey; Tasks: fileassoc
Root: HKA; Subkey: "Software\AcroBatya\Capabilities"; ValueType: string; ValueName: "ApplicationDescription"; ValueData: "{cm:AppDescription}"; Tasks: fileassoc
Root: HKA; Subkey: "Software\AcroBatya\Capabilities"; ValueType: string; ValueName: "ApplicationIcon"; ValueData: """{app}\{#AppExe}"",0"; Tasks: fileassoc
Root: HKA; Subkey: "Software\AcroBatya\Capabilities\FileAssociations"; ValueType: string; ValueName: ".pdf"; ValueData: "{#ProgId}"; Tasks: fileassoc
Root: HKA; Subkey: "Software\RegisteredApplications"; ValueType: string; ValueName: "AcroBatya"; ValueData: "Software\AcroBatya\Capabilities"; Flags: uninsdeletevalue; Tasks: fileassoc

[Run]
; SHChangeNotify(SHCNE_ASSOCCHANGED) — Проводник перечитывает ассоциации без перезагрузки.
Filename: "{app}\{#AppExe}"; Parameters: "--register-notify"; Flags: runhidden waituntilterminated; Tasks: fileassoc
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchApp}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Данные программы остаются (настройки, недавние, подписи, штампы, доверие), кроме кешей:
Type: filesandordirs; Name: "{localappdata}\AcroBatya\thumbs"
Type: filesandordirs; Name: "{localappdata}\AcroBatya\autosave"
Type: filesandordirs; Name: "{localappdata}\AcroBatya\lo-prof-*"
Type: dirifempty; Name: "{app}"
