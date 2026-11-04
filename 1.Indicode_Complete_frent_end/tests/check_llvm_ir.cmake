execute_process(
    COMMAND "${INDICODE}" ir "${SOURCE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE llvm_ir
    ERROR_VARIABLE error_output
)

if(NOT result EQUAL 0)
    message(FATAL_ERROR "LLVM IR command failed: ${error_output}")
endif()

if(NOT llvm_ir MATCHES "define i32 @add" OR
   NOT llvm_ir MATCHES " = add i32 " OR
   NOT llvm_ir MATCHES " = icmp slt i16 " OR
   NOT llvm_ir MATCHES " = xor i1 " OR
   NOT llvm_ir MATCHES "if.then." OR
   NOT llvm_ir MATCHES "while.cond." OR
   NOT llvm_ir MATCHES "phi i1" OR
    NOT llvm_ir MATCHES "call i32 @add" OR
    NOT llvm_ir MATCHES "call void @done")
    message(FATAL_ERROR "LLVM IR output is missing expected scalar/control-flow instructions: ${llvm_ir}")
endif()