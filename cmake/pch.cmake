# PCH на цель (эталон 5.5): общего PCH нет, чтобы Qt не утёк в core мимо стража boundaries.
# UNITY_BUILD, батчи по 12; mocs_compilation.cpp — вне unity.
function(ottisk_pch target header)
  target_precompile_headers(${target} PRIVATE "${header}")
  set_target_properties(${target} PROPERTIES UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE 12)
  get_target_property(_automoc ${target} AUTOMOC)
  if(_automoc)
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/${target}_autogen/mocs_compilation.cpp"
      TARGET_DIRECTORY ${target} PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
  endif()
endfunction()
