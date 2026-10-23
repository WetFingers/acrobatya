# Пресеты msvc-asan и msvc-fuzz (эталон 9.5): /fsanitize=address без /RTC; libFuzzer — только fuzz.
option(OTTISK_ASAN "AddressSanitizer" OFF)
option(OTTISK_FUZZ "libFuzzer targets" OFF)
option(OTTISK_ANALYZE "cl /analyze + CppCoreCheck (SARIF)" OFF)
set(OTTISK_ASAN_BIN "")
if(OTTISK_ASAN)
  string(REGEX REPLACE "/RTC[1csu]*" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
  string(REGEX REPLACE "/RTC[1csu]*" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
  add_compile_options(/fsanitize=address)
  # MuPDF, gtest, spdlog, fmt собраны без ASan: без этого STL даёт LNK2038 «annotate_string/annotate_vector»
  # (сессия 1.1); проверки переполнения контейнеров теряются только на границе с чужим кодом.
  add_compile_definitions(_DISABLE_VECTOR_ANNOTATION _DISABLE_STRING_ANNOTATION)
  add_link_options(/INCREMENTAL:NO)
  get_filename_component(OTTISK_ASAN_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
endif()
if(OTTISK_ANALYZE)
  add_compile_options(/analyze /analyze:external- /analyze:log:format:sarif
    /analyze:log${CMAKE_BINARY_DIR}/sarif/ /analyze:plugin EspXEngine.dll)
  file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/sarif")
endif()
