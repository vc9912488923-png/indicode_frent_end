execute_process(
    COMMAND "${INDICODE}" typed-json "${SOURCE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE typed_hir
    ERROR_VARIABLE error_output
)

if(NOT result EQUAL 0)
    message(FATAL_ERROR "typed HIR JSON command failed: ${error_output}")
endif()

string(JSON schema_version ERROR_VARIABLE json_error GET "${typed_hir}" schemaVersion)
if(json_error OR NOT schema_version EQUAL 2)
    message(FATAL_ERROR "typed HIR JSON schema version is missing or invalid: ${typed_hir}")
endif()

string(JSON record_name GET "${typed_hir}" records 0 name)
string(JSON function_name GET "${typed_hir}" functions 0 name)
string(JSON parameter_symbol_id GET "${typed_hir}" functions 0 parameters 0 symbolId)
string(JSON record_symbol_id GET "${typed_hir}" records 0 symbolId)
string(JSON function_symbol_id GET "${typed_hir}" functions 0 symbolId)
string(JSON record_constructor_symbol_id GET "${typed_hir}" statements 0 expression symbolId)
string(JSON field_symbol_id GET "${typed_hir}" records 0 fields 0 symbolId)
string(JSON member_symbol_id GET "${typed_hir}" functions 0 body 0 expression symbolId)
string(JSON parameter_type_symbol_id GET "${typed_hir}" functions 0 parameters 0 typeSymbolId)
string(JSON return_kind GET "${typed_hir}" functions 0 body 0 kind)
string(JSON return_symbol_id GET "${typed_hir}" functions 0 body 0 expression left symbolId)
if(NOT record_name STREQUAL "Point" OR NOT function_name STREQUAL "horizontal" OR
   NOT parameter_symbol_id GREATER 0 OR NOT return_kind STREQUAL "return" OR
   NOT return_symbol_id EQUAL parameter_symbol_id OR
   NOT record_symbol_id GREATER 0 OR NOT function_symbol_id GREATER 0 OR
   NOT record_constructor_symbol_id EQUAL record_symbol_id OR
   NOT field_symbol_id GREATER 0 OR NOT member_symbol_id EQUAL field_symbol_id OR
   NOT parameter_type_symbol_id GREATER 0)
    message(FATAL_ERROR "typed HIR JSON contents are inconsistent: ${typed_hir}")
endif()
