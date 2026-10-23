# Установка и развёртывание AcroBatya.exe:  cmake --install build/release --prefix build/deploy
#
# Подключается в самом конце корневого CMakeLists.txt, после создания цели AcroBatya и после
# find_package(Qt6 …):   include(cmake/deploy.cmake)
#
# Раскладка build/deploy (плоская, как в {app} установщика):
#   AcroBatya.exe, Qt6*.dll, qt.conf            — корень (QT_DEPLOY_BIN_DIR = ".")
#   plugins/platforms/qwindows.dll           — windeployqt (--plugindir plugins, по умолчанию Qt 6.12)
#   plugins/platforms/qoffscreen.dll         — здесь же явно: windeployqt его не берёт, а он нужен
#                                              --shots/--self-check под QT_QPA_PLATFORM=offscreen
#   plugins/{imageformats,styles,tls,iconengines,…}  — windeployqt
#   translations/*.qm (свои), assets/{help,stamps,icons} — если есть (см. ниже)
# CRT (Microsoft.VC143.CRT), qpdf.exe, tessdata/ и licenses/ кладёт tools/build.ps1 после install —
# они не из сборки CMake.
#
# windeployqt запускается на exe из дерева сборки ($<TARGET_FILE:AcroBatya>) с рабочим каталогом —
# префиксом установки; DLL Qt он кладёт в QT_DEPLOY_BIN_DIR. По умолчанию это CMAKE_INSTALL_BINDIR
# ("bin"), зафиксированный в QtDeploySupport.cmake ещё при find_package(Qt6), — тогда DLL Qt уехали бы
# в deploy/bin, отдельно от AcroBatya.exe. Документация Qt (QT_DEPLOY_BIN_DIR) советует задать
# CMAKE_INSTALL_BINDIR до find_package(Qt6); чтобы не зависеть от порядка в корневом CMakeLists.txt,
# здесь "." задаётся в скрипте установки до include(QtDeploySupport.cmake) — в нём стоит
# if(NOT QT_DEPLOY_BIN_DIR), так что наше значение остаётся. Проверено на build/release этапа 0
# (23.10.2026): Qt6*.dll и qt.conf («Prefix = .», «Binaries = .») — рядом с AcroBatya.exe,
# qwindows.dll и qoffscreen.dll — в plugins/platforms, AcroBatya.exe --self-check = 0 с системным PATH.

if(NOT TARGET AcroBatya)
  message(FATAL_ERROR "cmake/deploy.cmake: цели AcroBatya ещё нет — подключайте после add_executable/qt_add_executable(AcroBatya …)")
endif()
if(NOT COMMAND qt_generate_deploy_app_script)
  message(FATAL_ERROR "cmake/deploy.cmake: нет qt_generate_deploy_app_script — сначала find_package(Qt6 6.12 COMPONENTS Core …)")
endif()

install(TARGETS AcroBatya RUNTIME DESTINATION .)

# Плоская раскладка: Qt6*.dll и qt.conf — рядом с AcroBatya.exe.
install(CODE [[
  set(QT_DEPLOY_BIN_DIR ".")
]])

# Сигнатура сверена с doc.qt.io/qt-6/qt-generate-deploy-app-script.html (Qt 6.12) и с
# Qt6CoreMacros.cmake 6.12.0: NO_TRANSLATIONS / NO_COMPILER_RUNTIME — флаги, DEPLOY_TOOL_OPTIONS (с 6.7)
# передаётся windeployqt как есть, после --dir . --libdir <bin> --plugindir plugins --force.
# Ключи windeployqt проверены `windeployqt --help` из C:\Qt\6.12.0\msvc2022_64\bin.
qt_generate_deploy_app_script(
  TARGET AcroBatya
  OUTPUT_SCRIPT OTTISK_DEPLOY_SCRIPT
  NO_TRANSLATIONS
  NO_COMPILER_RUNTIME
  DEPLOY_TOOL_OPTIONS
    --no-opengl-sw
    --no-system-d3d-compiler
    --no-system-dxc-compiler
    --exclude-plugins qtuiotouchplugin,qicns,qtga,qwbmp
)
install(SCRIPT "${OTTISK_DEPLOY_SCRIPT}")

# qoffscreen.dll — туда же, где windeployqt кладёт qwindows.dll: <QT6_INSTALL_PLUGINS>/platforms
# (QT_DEPLOY_PLUGINS_DIR по умолчанию = QT6_INSTALL_PLUGINS = "plugins"). Release-DLL и для Debug:
# Qt в Debug-пресете — release-сборка (CMAKE_MAP_IMPORTED_CONFIG_DEBUG=Release).
set(_ottisk_qt_prefix "${QT6_INSTALL_PREFIX}")
if(NOT _ottisk_qt_prefix AND OTTISK_QT_ROOT)
  set(_ottisk_qt_prefix "${OTTISK_QT_ROOT}")
endif()
set(_ottisk_qt_plugins "${QT6_INSTALL_PLUGINS}")
if(NOT _ottisk_qt_plugins)
  set(_ottisk_qt_plugins "plugins")
endif()
file(TO_CMAKE_PATH "${_ottisk_qt_prefix}/${_ottisk_qt_plugins}/platforms/qoffscreen.dll" _ottisk_qoffscreen)
if(NOT EXISTS "${_ottisk_qoffscreen}")
  message(FATAL_ERROR "cmake/deploy.cmake: нет ${_ottisk_qoffscreen} (Qt 6.12 без платформы offscreen?)")
endif()
install(FILES "${_ottisk_qoffscreen}" DESTINATION "${_ottisk_qt_plugins}/platforms")

# Свои переводы: cmake/qt.cmake (или i18n) кладёт список .qm в OTTISK_QM_FILES
# (qt_add_translations(… QM_FILES_OUTPUT_VARIABLE OTTISK_QM_FILES)) — до include(cmake/deploy.cmake).
if(OTTISK_QM_FILES)
  install(FILES ${OTTISK_QM_FILES} DESTINATION translations)
endif()

# Ресурсы программы, если каталоги уже есть (справка — 9.1, штампы — 5.x, значки Lucide — 2.1).
foreach(_ottisk_asset IN ITEMS help stamps icons)
  if(IS_DIRECTORY "${CMAKE_SOURCE_DIR}/assets/${_ottisk_asset}")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/${_ottisk_asset}" DESTINATION assets)
  endif()
endforeach()

unset(_ottisk_qt_prefix)
unset(_ottisk_qt_plugins)
unset(_ottisk_qoffscreen)
unset(_ottisk_asset)
