# MuPDF 1.28.5 из C:\src\mupdf\platform\win32\mupdf.sln (эталон 5.4, 10.2).
# .lib — по конфигурации (Debug → x64/Debug, пересобранный tools/mupdf-build.ps1 с /MD и без _DEBUG;
# остальное → x64/Release с /GL), КРОМЕ libresources.lib: она всегда из x64/Release (в mupdf.sln
# Debug|x64 этого проекта отображена на Release|x64; bin2coff-объекты без CRT и /GL).
if(OTTISK_MUPDF_ROOT)
  set(MUPDF_ROOT "${OTTISK_MUPDF_ROOT}")
elseif(DEFINED ENV{MUPDF_ROOT} AND NOT "$ENV{MUPDF_ROOT}" STREQUAL "")
  set(MUPDF_ROOT "$ENV{MUPDF_ROOT}")
else()
  set(MUPDF_ROOT "C:/src/mupdf")
endif()
file(TO_CMAKE_PATH "${MUPDF_ROOT}" MUPDF_ROOT)
set(MUPDF_ROOT "${MUPDF_ROOT}" CACHE PATH "MuPDF source tree with platform/win32 builds" FORCE)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(_mu_cfg Debug)
else()
  set(_mu_cfg Release)
endif()
set(MUPDF_LIB_DIR "${MUPDF_ROOT}/platform/win32/x64/${_mu_cfg}")
set(MUPDF_LIBS libmupdf libthirdparty libmuthreads libharfbuzz libtesseract libleptonica libextract
    libmubarcode libzxing libpkcs7)
set(_mu_paths)
foreach(_l IN LISTS MUPDF_LIBS)
  if(NOT EXISTS "${MUPDF_LIB_DIR}/${_l}.lib")
    message(FATAL_ERROR "Нет ${MUPDF_LIB_DIR}/${_l}.lib — соберите MuPDF: tools/mupdf-build.ps1 -Config ${_mu_cfg}")
  endif()
  list(APPEND _mu_paths "${MUPDF_LIB_DIR}/${_l}.lib")
endforeach()
list(APPEND _mu_paths "${MUPDF_ROOT}/platform/win32/x64/Release/libresources.lib")

add_library(mupdf::mupdf INTERFACE IMPORTED)
target_include_directories(mupdf::mupdf INTERFACE "${MUPDF_ROOT}/include")
target_link_libraries(mupdf::mupdf INTERFACE ${_mu_paths})

# Tesseract/Leptonica из состава сборки MuPDF: заголовки и дефайны как у libtesseract.vcxproj —
# иначе раскладка TessBaseAPI разойдётся (эталон 5.4). Подключаются к ottisk_core.
function(ottisk_tesseract target)
  target_include_directories(${target} SYSTEM PRIVATE
    "${MUPDF_ROOT}/scripts/tesseract"
    "${MUPDF_ROOT}/thirdparty/tesseract/include"
    "${MUPDF_ROOT}/thirdparty/leptonica/src")
  target_compile_definitions(${target} PRIVATE DISABLED_LEGACY_ENGINE TESSERACT_IMAGEDATA_AS_PIX
    GRAPHICS_DISABLED TESSERACT_DISABLE_DEBUG_FONTS HAVE_LEPTONICA HAVE_TESSERACT)
endfunction()
message(STATUS "MuPDF: ${MUPDF_LIB_DIR} (libresources — x64/Release)")
