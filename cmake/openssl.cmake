# OpenSSL 3.5 LTS (эталон 3.8, 5.4; план 4.1): vcpkg, триплет x64-windows-static-md-release, точная версия — в
# overrides vcpkg.json. Нижняя граница 3.5.0: CMS_NO_SIGNING_TIME (CAdES-B без signing-time) появился только в 3.5;
# CMS_CADES — с 3.0, CMS_final_digest — с 3.2. Статическая сборка vcpkg — no-shared no-module: провайдер legacy
# (RC2-40 старых .pfx) встроен в libcrypto и грузится OSSL_PROVIDER_try_load без файла модуля.
find_package(OpenSSL REQUIRED)

if(DEFINED OPENSSL_VERSION_MAJOR AND DEFINED OPENSSL_VERSION_MINOR AND NOT OPENSSL_VERSION_MAJOR STREQUAL "")
  set(_ossl_version "${OPENSSL_VERSION_MAJOR}.${OPENSSL_VERSION_MINOR}")
else()
  string(REGEX MATCH "^[0-9]+\\.[0-9]+" _ossl_version "${OPENSSL_VERSION}")
endif()
if(NOT _ossl_version OR _ossl_version VERSION_LESS "3.5")
  message(FATAL_ERROR "OpenSSL ${OPENSSL_VERSION} found, 3.5 or newer required (CMS_NO_SIGNING_TIME)."
    " Check overrides in vcpkg.json.")
endif()
message(STATUS "OpenSSL ${OPENSSL_VERSION}: ${OPENSSL_CRYPTO_LIBRARY}")
