# Предупреждения (эталон 5.5): /W4 /permissive- /Zc:__cplusplus /utf-8 у наших целей; core и services — /WX.
# Чужие заголовки (Qt, MuPDF, Tesseract/Leptonica, vcpkg) подключаются как SYSTEM → /external:I + /external:W0.
function(ottisk_warnings target)
  cmake_parse_arguments(W "STRICT" "" "" ${ARGN})
  target_compile_options(${target} PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8 /external:W0 /EHsc /bigobj)
  # В пресете msvc-analyze /WX выключен: предупреждения /analyze собираются в SARIF и сверяются с
  # docs/analyze-baseline.json (tools/analyze_summary.py), а не обрывают сборку.
  if(W_STRICT AND NOT OTTISK_ANALYZE)
    set_target_properties(${target} PROPERTIES COMPILE_WARNING_AS_ERROR ON)
  endif()
endfunction()
