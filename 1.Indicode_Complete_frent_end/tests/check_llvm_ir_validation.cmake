get_filename_component(source_name "${SOURCE}" NAME_WE)
set(ir_file "${BUILD_DIR}/indicode-ir-validation-${source_name}.ll")
set(bitcode_file "${BUILD_DIR}/indicode-ir-validation-${source_name}.bc")

execute_process(
    COMMAND "${INDICODE}" ir "${SOURCE}"
    RESULT_VARIABLE ir_result
    OUTPUT_FILE "${ir_file}"
    ERROR_VARIABLE ir_error
)
if(NOT ir_result EQUAL 0)
    message(FATAL_ERROR "IR generation failed: ${ir_error}")
endif()

execute_process(
    COMMAND "${LLVM_AS}" "${ir_file}" -o "${bitcode_file}"
    RESULT_VARIABLE assemble_result
    ERROR_VARIABLE assemble_error
)
if(NOT assemble_result EQUAL 0)
    message(FATAL_ERROR "llvm-as rejected generated IR: ${assemble_error}")
endif()

execute_process(
    COMMAND "${LLVM_OPT}" -passes=verify -disable-output "${bitcode_file}"
    RESULT_VARIABLE verify_result
    ERROR_VARIABLE verify_error
)
if(NOT verify_result EQUAL 0)
    message(FATAL_ERROR "LLVM verifier rejected generated IR: ${verify_error}")
endif()

file(REMOVE "${ir_file}" "${bitcode_file}")
