# Покрытие объёма (эталон 9.1): макрос OT_FEATURE("id") в core/util/features.h пишет ID в свойство
# gtest и строку OT_FEATURE:<id> в stdout; tools/scope_report.py собирает build/<пресет>/features-covered.json
# из gtest-xml/*.xml и ctest.xml и печатает «закрыто N из M» по docs/scope.json.
add_custom_target(scope_report
  COMMAND "${OTTISK_PYTHON}" -X utf8 -I "${CMAKE_SOURCE_DIR}/tools/scope_report.py" --build "${CMAKE_BINARY_DIR}"
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  COMMENT "scope_report: закрыто N из M"
  VERBATIM)
