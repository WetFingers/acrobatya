# «АкроБатя» — настольный PDF-редактор уровня Acrobat Standard (эталон docs/design.md 5.2–5.5).
# Сборка: cmd /c "tools\vcenv.cmd && cmake --preset msvc-debug && cmake --build --preset msvc-debug"
# Исходники целей — GLOB_RECURSE … CONFIGURE_DEPENDS по каталогам: новый файл кладётся в каталог
# своей цели, CMake не правится. Все цели /MD; в Debug — release-DLL Qt и Debug-библиотеки MuPDF.
cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(OTTISK_VCPKG_ROOT)
    set(_vcpkg "${OTTISK_VCPKG_ROOT}")
  else()
    set(_vcpkg "C:/Tools/vcpkg")  # не %LOCALAPPDATA%: её перенаправляет приложение Claude
  endif()
  file(TO_CMAKE_PATH "${_vcpkg}" _vcpkg)
  set(CMAKE_TOOLCHAIN_FILE "${_vcpkg}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "vcpkg toolchain")
endif()

include(cmake/product.cmake)
project(AcroBatya VERSION ${OTTISK_VERSION} DESCRIPTION "PDF editor" LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)
set(CMAKE_MAP_IMPORTED_CONFIG_DEBUG Release)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set_property(GLOBAL PROPERTY JOB_POOLS compile=10 link=2)
set(CMAKE_JOB_POOL_COMPILE compile)
set(CMAKE_JOB_POOL_LINK link)
# Release MuPDF собран без /GL (tools/mupdf-build.ps1 -NoLtcg, этап 0: LTCG-линковка render_refs шла 238 с).
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")

if(OTTISK_PYTHON)
  set(_py "${OTTISK_PYTHON}")
elseif(DEFINED ENV{OTTISK_PYTHON} AND NOT "$ENV{OTTISK_PYTHON}" STREQUAL "")
  set(_py "$ENV{OTTISK_PYTHON}")
else()
  set(_py "C:/Tools/venv-test/Scripts/python.exe")
endif()
file(TO_CMAKE_PATH "${_py}" _py)
set(OTTISK_PYTHON "${_py}" CACHE FILEPATH "venv тестов (C:/Tools/venv-test)" FORCE)

include(cmake/asan.cmake)
include(cmake/warnings.cmake)
include(cmake/pch.cmake)
include(cmake/mupdf.cmake)
include(cmake/qt.cmake)

find_package(OpenSSL REQUIRED)
find_package(TIFF REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)

set(OTTISK_VERSION_DEFS
  OTTISK_VERSION_STR="${OTTISK_VERSION}"
  OTTISK_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
  OTTISK_VERSION_MINOR=${PROJECT_VERSION_MINOR}
  OTTISK_VERSION_PATCH=${PROJECT_VERSION_PATCH})

# --- core: чистый C++20 без Qt (MuPDF, OpenSSL, libtiff, Tesseract/Leptonica, fmt, spdlog, json, sqlite3)
file(GLOB_RECURSE OTTISK_CORE_SRC CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/core/*.cpp" "${CMAKE_SOURCE_DIR}/src/core/*.h")
add_library(ottisk_core STATIC ${OTTISK_CORE_SRC})
target_include_directories(ottisk_core PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_compile_definitions(ottisk_core PUBLIC ${OTTISK_VERSION_DEFS} NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_link_libraries(ottisk_core PUBLIC mupdf::mupdf OpenSSL::Crypto TIFF::TIFF fmt::fmt spdlog::spdlog
  nlohmann_json::nlohmann_json unofficial::sqlite3::sqlite3)
ottisk_tesseract(ottisk_core)
ottisk_warnings(ottisk_core STRICT)
ottisk_pch(ottisk_core "${CMAKE_SOURCE_DIR}/src/core/pch.h")

# --- services: Qt Core/Gui/Network/PrintSupport/Concurrent без Widgets
file(GLOB_RECURSE OTTISK_SERVICES_SRC CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/services/*.cpp"
  "${CMAKE_SOURCE_DIR}/src/services/*.h")
add_library(ottisk_services STATIC ${OTTISK_SERVICES_SRC})
target_link_libraries(ottisk_services PUBLIC ottisk_core Qt6::Core Qt6::Gui Qt6::Network Qt6::PrintSupport
  Qt6::Concurrent)
ottisk_qt(ottisk_services)
ottisk_warnings(ottisk_services STRICT)
ottisk_pch(ottisk_services "${CMAKE_SOURCE_DIR}/src/services/pch.h")

# --- ui: Qt Widgets; всё, кроме app/main.cpp, — статическая ottisk_ui_lib (её берут ui_tests)
file(GLOB_RECURSE OTTISK_UI_SRC CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/ui/*.cpp" "${CMAKE_SOURCE_DIR}/src/ui/*.h")
list(FILTER OTTISK_UI_SRC EXCLUDE REGEX "/src/ui/app/main\\.cpp$")
add_library(ottisk_ui_lib STATIC ${OTTISK_UI_SRC})
target_link_libraries(ottisk_ui_lib PUBLIC ottisk_services Qt6::Widgets Qt6::Svg)
ottisk_qt(ottisk_ui_lib)
ottisk_warnings(ottisk_ui_lib)
ottisk_pch(ottisk_ui_lib "${CMAKE_SOURCE_DIR}/src/ui/pch.h")

set(OTTISK_APP_SRC "${CMAKE_SOURCE_DIR}/src/ui/app/main.cpp")
if(EXISTS "${CMAKE_SOURCE_DIR}/installer/app.rc")
  list(APPEND OTTISK_APP_SRC "${CMAKE_SOURCE_DIR}/installer/app.rc")
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/installer/AcroBatya.manifest")
  list(APPEND OTTISK_APP_SRC "${CMAKE_SOURCE_DIR}/installer/AcroBatya.manifest")
endif()
add_executable(AcroBatya WIN32 ${OTTISK_APP_SRC})
target_link_libraries(AcroBatya PRIVATE ottisk_ui_lib shell32)
ottisk_qt(AcroBatya)
ottisk_warnings(AcroBatya)
add_dependencies(AcroBatya qt_runtime)

# --- инструменты: imgcmp (метрики как pdfium_diff), render_refs (эталоны рендера), bench
file(GLOB_RECURSE OTTISK_IMGCMP_SRC CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/tools/imgcmp/*.cpp")
add_executable(imgcmp ${OTTISK_IMGCMP_SRC})
target_link_libraries(imgcmp PRIVATE ottisk_core)
ottisk_warnings(imgcmp)

add_executable(render_refs "${CMAKE_SOURCE_DIR}/tools/render_refs.cpp")
target_link_libraries(render_refs PRIVATE ottisk_core)
target_compile_definitions(render_refs PRIVATE OTTISK_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
ottisk_warnings(render_refs)

# bench (план 1.2, эталон 5.8) — тайлы идут через RenderPool, поэтому services; файлы ищет в исходниках,
# корпусе и <сборка>/gen.
file(GLOB_RECURSE OTTISK_BENCH_SRC CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/tests/bench/*.cpp")
add_executable(bench ${OTTISK_BENCH_SRC})
target_link_libraries(bench PRIVATE ottisk_services)
target_compile_definitions(bench PRIVATE OTTISK_SOURCE_DIR="${CMAKE_SOURCE_DIR}" OTTISK_BINARY_DIR="${CMAKE_BINARY_DIR}")
ottisk_qt(bench)
ottisk_warnings(bench)
add_dependencies(bench qt_runtime)

if(OTTISK_FUZZ)
  # Харнес идёт через ottisk_core; MuPDF не инструментирован — покрытие для libFuzzer даёт наш код.
  target_compile_options(ottisk_core PRIVATE /fsanitize-coverage=inline-8bit-counters /fsanitize-coverage=edge
    /fsanitize-coverage=trace-cmp)
  file(GLOB_RECURSE OTTISK_FUZZ_SRC CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/fuzz/*.cpp")
  add_executable(pdf_open_fuzzer ${OTTISK_FUZZ_SRC})
  target_link_libraries(pdf_open_fuzzer PRIVATE ottisk_core)
  target_compile_options(pdf_open_fuzzer PRIVATE /fsanitize=fuzzer)
  target_link_options(pdf_open_fuzzer PRIVATE /fsanitize=fuzzer)
endif()

# --- тесты (эталон 9.1)
enable_testing()
include(cmake/tests.cmake)
ottisk_gtest_exe(unit_tests tests/unit unit "unit" 60 ottisk_services)
# Корпус и мутанты (план 1.1): фикстура corpus — сверка скачанного без записи индекса (нет корпуса — код 77,
# пропуск; тесты сами дают SKIP), mutants — 100 своих битых PDF в build/<пресет>/gen; после первой строки
# «progress:» у теста 120 с на остаток. Процесс engine_tests ограничен по памяти (tests/engine/support/
# memory_cap.cpp) и занимает 2 слота ctest -j: десять шардов fuzz без предела уронили Windows (23.10.2026).
ottisk_gtest_exe(engine_tests tests/engine engine "engine;slow" 600 ottisk_services
  PROPERTIES FIXTURES_REQUIRED "corpus\;mutants" TIMEOUT_AFTER_MATCH "120\;^progress:" PROCESSORS 2)
if(TARGET engine_tests AND CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_test(NAME engine.corpus.verify
    COMMAND "${OTTISK_PYTHON}" -X utf8 -I "${CMAKE_SOURCE_DIR}/tools/fetch_corpus.py" --verify --dry-run --missing-code 77
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
  set_tests_properties(engine.corpus.verify PROPERTIES LABELS "engine;slow" FIXTURES_SETUP corpus
    SKIP_RETURN_CODE 77 TIMEOUT 300)
  add_test(NAME engine.mutants.generate
    COMMAND "${OTTISK_PYTHON}" -X utf8 -I "${CMAKE_SOURCE_DIR}/tools/gen/make_mutants.py"
      --corpus "${CMAKE_BINARY_DIR}/gen" --tmp "${CMAKE_BINARY_DIR}/gen-tmp"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
  set_tests_properties(engine.mutants.generate PROPERTIES LABELS "engine;slow" FIXTURES_SETUP mutants TIMEOUT 300)
endif()
ottisk_gtest_exe(render_tests tests/render render "render;slow" 900 ottisk_core)
# Замеры движка (план 1.2): только Release — числа Debug с целями 5.8 не сравнимы. big-1200 и images-200 —
# make_big.py в <сборка>/gen (фикстура, пропускает готовые), PDF32000_2008 — только из корпуса (нет — SKIP 77).
# JSON — в build/bench/<имя>.json; сравнение с базой — tools/bench_compare.py docs/bench/baseline-*.json.
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  add_test(NAME bench.engine.generate
    COMMAND "${OTTISK_PYTHON}" -X utf8 -I "${CMAKE_SOURCE_DIR}/tools/gen/make_big.py"
      --corpus "${CMAKE_BINARY_DIR}/gen" --skip-existing
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
  set_tests_properties(bench.engine.generate PROPERTIES LABELS "bench;slow" FIXTURES_SETUP bench_files TIMEOUT 900)
  foreach(_b hello pdf32000 big-1200 images-200 enc-r6)
    add_test(NAME bench.engine.${_b} COMMAND bench ${_b} --json "${CMAKE_SOURCE_DIR}/build/bench/${_b}.json")
    set_tests_properties(bench.engine.${_b} PROPERTIES LABELS "bench;slow" FIXTURES_REQUIRED bench_files
      SKIP_RETURN_CODE 77 TIMEOUT 900 RUN_SERIAL TRUE)
  endforeach()
endif()
ottisk_gtest_exe(e2e_tests tests/e2e e2e "e2e;slow" 900 ottisk_services)

file(GLOB_RECURSE OTTISK_UI_TEST_SRC CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/tests/ui/*.cpp" "${CMAKE_SOURCE_DIR}/tests/ui/*.h")
add_executable(ui_tests ${OTTISK_UI_TEST_SRC})
target_link_libraries(ui_tests PRIVATE ottisk_ui_lib Qt6::Test)
target_compile_definitions(ui_tests PRIVATE OTTISK_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_include_directories(ui_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests/ui")
ottisk_qt(ui_tests)
ottisk_warnings(ui_tests)
add_dependencies(ui_tests qt_runtime)
add_test(NAME ui.main_window COMMAND ui_tests -o "${CMAKE_BINARY_DIR}/qtest-ui.xml,junitxml" -o -,txt)
set_tests_properties(ui.main_window PROPERTIES LABELS "ui" TIMEOUT 120)

ottisk_guard(i18n i18n_guard.py)
ottisk_guard(fztry fztry_guard.py)
ottisk_guard(boundaries boundaries_guard.py)
ottisk_guard(scope scope_check.py)
ottisk_guard(encoding encoding_guard.py)
ottisk_guard(no_registry no_registry_guard.py)
ottisk_guard(format format_guard.py)

add_test(NAME build.install
  COMMAND "${OTTISK_PYTHON}" -X utf8 -I "${CMAKE_SOURCE_DIR}/tests/build/install_test.py" --root "${CMAKE_SOURCE_DIR}"
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(build.install PROPERTIES LABELS "build;slow" TIMEOUT 900)

ottisk_finalize_tests()
include(cmake/features.cmake)
if(EXISTS "${CMAKE_SOURCE_DIR}/cmake/deploy.cmake")
  include(cmake/deploy.cmake)
endif()
