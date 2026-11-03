execute_process(
    COMMAND "${INDICODE}" ast-json "${SOURCE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE ast_json
    ERROR_VARIABLE error_output
)

if(NOT result EQUAL 0)
    message(FATAL_ERROR "AST JSON command failed: ${error_output}")
endif()

string(JSON schema_version ERROR_VARIABLE json_error GET "${ast_json}" schemaVersion)
if(json_error OR NOT schema_version EQUAL 1)
    message(FATAL_ERROR "AST JSON schema version is missing or invalid: ${ast_json}")
endif()

string(JSON record_name ERROR_VARIABLE json_error GET "${ast_json}" records 0 name)
string(JSON field_name ERROR_VARIABLE field_error GET "${ast_json}" records 0 fields 0 name)
string(JSON function_name ERROR_VARIABLE function_error GET "${ast_json}" functions 0 name)
string(JSON parameter_name ERROR_VARIABLE parameter_error GET "${ast_json}" functions 0 parameters 0 name)
string(JSON statement_kind ERROR_VARIABLE statement_error GET "${ast_json}" statements 0 kind)
string(JSON statement_line ERROR_VARIABLE span_error GET "${ast_json}" statements 0 span start line)
if(json_error OR field_error OR function_error OR parameter_error OR statement_error OR span_error OR
   NOT record_name STREQUAL "Point" OR NOT field_name STREQUAL "x" OR
   NOT function_name STREQUAL "horizontal" OR NOT parameter_name STREQUAL "point" OR
   NOT statement_kind STREQUAL "assignment" OR NOT statement_line EQUAL 10)
    message(FATAL_ERROR "AST JSON structure or source spans are inconsistent: ${ast_json}")
endif()
