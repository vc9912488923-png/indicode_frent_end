execute_process(
    COMMAND "${INDICODE}" --json-diagnostics check "${SOURCE}"
    RESULT_VARIABLE result
    ERROR_VARIABLE diagnostics
)

if(result EQUAL 0)
    message(FATAL_ERROR "type-error fixture unexpectedly passed")
endif()

string(JSON schema_version ERROR_VARIABLE json_error GET "${diagnostics}" schemaVersion)
if(json_error OR NOT schema_version EQUAL 1)
    message(FATAL_ERROR "expected diagnostics JSON schema version 1, got: ${diagnostics}")
endif()

string(JSON severity ERROR_VARIABLE json_error GET "${diagnostics}" diagnostics 0 severity)
if(json_error OR NOT severity STREQUAL "error")
    message(FATAL_ERROR "expected a JSON error diagnostic, got: ${diagnostics}")
endif()

string(JSON code GET "${diagnostics}" diagnostics 0 code)
string(JSON file GET "${diagnostics}" diagnostics 0 file)
string(JSON line GET "${diagnostics}" diagnostics 0 start line)
if(DEFINED EXPECT_CODE)
    set(expected_code "${EXPECT_CODE}")
else()
    set(expected_code "E-SEMA-005")
endif()
if(NOT code STREQUAL expected_code OR NOT file STREQUAL "${SOURCE}" OR NOT line EQUAL 1)
    message(FATAL_ERROR "JSON diagnostic fields are incorrect: ${diagnostics}")
endif()

if(DEFINED EXPECT_FIXIT)
    string(JSON replacement GET "${diagnostics}" diagnostics 0 fixIt replacement)
    if(NOT replacement STREQUAL EXPECT_FIXIT)
        message(FATAL_ERROR "JSON fix-it replacement is incorrect: ${diagnostics}")
    endif()
elseif(expected_code STREQUAL "E-PARSE-001")
    string(JSON replacement GET "${diagnostics}" diagnostics 0 fixIt replacement)
    if(NOT replacement MATCHES "^:[ ]$")
        message(FATAL_ERROR "JSON fix-it replacement is incorrect: ${diagnostics}")
    endif()
endif()