# Qt 6.12.0 LTS из C:\Qt\6.12.0\msvc2022_64 (эталон 5.3–5.5). В Debug — release-DLL Qt
# (CMAKE_MAP_IMPORTED_CONFIG_DEBUG=Release в корневом CMakeLists): весь проект /MD.
if(OTTISK_QT_ROOT)
  set(QT_ROOT "${OTTISK_QT_ROOT}")
elseif(DEFINED ENV{QT_ROOT} AND NOT "$ENV{QT_ROOT}" STREQUAL "")
  set(QT_ROOT "$ENV{QT_ROOT}")
else()
  set(QT_ROOT "C:/Qt/6.12.0/msvc2022_64")
endif()
file(TO_CMAKE_PATH "${QT_ROOT}" QT_ROOT)
set(QT_ROOT "${QT_ROOT}" CACHE PATH "Qt kit" FORCE)
list(PREPEND CMAKE_PREFIX_PATH "${QT_ROOT}")
find_package(Qt6 6.12 REQUIRED COMPONENTS Core Gui Widgets PrintSupport Svg Concurrent Network Test)
find_package(Qt6 6.12 REQUIRED COMPONENTS LinguistTools)  # qt_add_translations (эталон 5.7)

# Общие настройки Qt-целей: moc, строгие QString.
function(ottisk_qt target)
  set_target_properties(${target} PROPERTIES AUTOMOC ON)
  target_compile_definitions(${target} PRIVATE QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII
    QT_NO_URL_CAST_FROM_STRING QT_USE_QSTRINGBUILDER)
endfunction()

# Qt-рантайм рядом с exe в build/<пресет>/bin: тесты (gtest_discover_tests PRE_TEST запускает exe
# без ENVIRONMENT), --shots и --self-check из дерева сборки работают без PATH. Плагины — в подпапках
# рядом с exe (как windeployqt): platforms (qwindows, qoffscreen), styles, imageformats, iconengines.
set(_qt_bin "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
set(_qt_dlls)
foreach(_m Core Gui Widgets PrintSupport Svg Concurrent Network Test)
  list(APPEND _qt_dlls "$<TARGET_FILE:Qt6::${_m}>")
endforeach()
add_custom_target(qt_runtime
  COMMAND ${CMAKE_COMMAND} -E make_directory "${_qt_bin}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_qt_dlls} "${_qt_bin}"
  COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different "${QT_ROOT}/plugins/platforms" "${_qt_bin}/platforms"
  COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different "${QT_ROOT}/plugins/styles" "${_qt_bin}/styles"
  COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different "${QT_ROOT}/plugins/imageformats" "${_qt_bin}/imageformats"
  COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different "${QT_ROOT}/plugins/iconengines" "${_qt_bin}/iconengines"
  COMMENT "Qt runtime → ${_qt_bin}"
  VERBATIM)
message(STATUS "Qt: ${QT_ROOT} (${Qt6_VERSION})")
