cmake_minimum_required(VERSION 3.20)
project(indicode_frontend VERSION 0.9.8 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(indicode_frontend
    src/source.cpp
    src/lexer.cpp
    src/parser.cpp
    src/incremental_parser.cpp
    src/sema.cpp
    src/effects.cpp
    src/ownership.cpp
    src/ast_printer.cpp
    src/ast_json.cpp
    src/ast_visitor.cpp
    src/hir.cpp
    src/symbol_index.cpp
    src/resolution.cpp
    src/diagnostic.cpp
    src/const_eval.cpp
)
target_include_directories(indicode_frontend PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
set_target_properties(indicode_frontend PROPERTIES EXPORT_NAME frontend)
target_compile_options(indicode_frontend PRIVATE -Wall -Wextra -Wpedantic)

add_executable(indicode src/driver/main.cpp)
target_link_libraries(indicode PRIVATE indicode_frontend)

configure_package_config_file(
    cmake/indicodeConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/indicodeConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/indicode
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/indicodeConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(TARGETS indicode_frontend indicode
    EXPORT indicodeTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT indicodeTargets
    FILE indicodeTargets.cmake
    NAMESPACE indicode::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/indicode
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/indicodeConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/indicodeConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/indicode
)

include(CTest)
if(BUILD_TESTING)
    add_executable(indicode_lexer_tests tests/lexer_tests.cpp)
    target_link_libraries(indicode_lexer_tests PRIVATE indicode_frontend)
    add_test(NAME lexer_tests COMMAND indicode_lexer_tests)

    add_executable(indicode_parser_tests tests/parser_tests.cpp)
    target_link_libraries(indicode_parser_tests PRIVATE indicode_frontend)
    add_test(NAME parser_tests COMMAND indicode_parser_tests)

    add_executable(indicode_ast_printer_tests tests/ast_printer_tests.cpp)
    target_link_libraries(indicode_ast_printer_tests PRIVATE indicode_frontend)
    add_test(NAME ast_printer_tests COMMAND indicode_ast_printer_tests)

    add_executable(indicode_ast_visitor_tests tests/ast_visitor_tests.cpp)
    target_link_libraries(indicode_ast_visitor_tests PRIVATE indicode_frontend)
    add_test(NAME ast_visitor_tests COMMAND indicode_ast_visitor_tests)

    add_executable(indicode_ast_scalability_tests tests/ast_scalability_tests.cpp)
    target_link_libraries(indicode_ast_scalability_tests PRIVATE indicode_frontend)
    add_test(NAME ast_scalability_tests COMMAND indicode_ast_scalability_tests)

    add_executable(indicode_grammar_tests tests/grammar_tests.cpp)
    target_link_libraries(indicode_grammar_tests PRIVATE indicode_frontend)
    target_compile_definitions(indicode_grammar_tests PRIVATE
        INDICODE_SPEC_PATH="${CMAKE_CURRENT_SOURCE_DIR}/docs/LANGUAGE_SPEC_V1.md"
    )
    add_test(NAME grammar_tests COMMAND indicode_grammar_tests)

    add_executable(indicode_sema_tests tests/sema_tests.cpp)
    target_link_libraries(indicode_sema_tests PRIVATE indicode_frontend)
    add_test(NAME sema_tests COMMAND indicode_sema_tests)

    add_executable(indicode_hir_tests tests/hir_tests.cpp)
    target_link_libraries(indicode_hir_tests PRIVATE indicode_frontend)
    add_test(NAME hir_tests COMMAND indicode_hir_tests)

    add_executable(indicode_symbol_index_tests tests/symbol_index_tests.cpp)
    target_link_libraries(indicode_symbol_index_tests PRIVATE indicode_frontend)
    add_test(NAME symbol_index_tests COMMAND indicode_symbol_index_tests)

    add_executable(indicode_resolution_tests tests/resolution_tests.cpp)
    target_link_libraries(indicode_resolution_tests PRIVATE indicode_frontend)
    add_test(NAME resolution_tests COMMAND indicode_resolution_tests)

    add_executable(indicode_diagnostic_tests tests/diagnostic_tests.cpp)
    target_link_libraries(indicode_diagnostic_tests PRIVATE indicode_frontend)
    add_test(NAME diagnostic_tests COMMAND indicode_diagnostic_tests)

    add_executable(indicode_source_tests tests/source_tests.cpp)
    target_link_libraries(indicode_source_tests PRIVATE indicode_frontend)
    add_test(NAME source_tests COMMAND indicode_source_tests)

    add_executable(indicode_parser_robustness_tests tests/parser_robustness_tests.cpp)
    target_link_libraries(indicode_parser_robustness_tests PRIVATE indicode_frontend)
    add_test(NAME parser_robustness_tests COMMAND indicode_parser_robustness_tests)

    add_executable(indicode_incremental_parser_tests tests/incremental_parser_tests.cpp)
    target_link_libraries(indicode_incremental_parser_tests PRIVATE indicode_frontend)
    add_test(NAME incremental_parser_tests COMMAND indicode_incremental_parser_tests)

    add_test(NAME cli_check COMMAND indicode check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/hello.indi)
    add_test(NAME cli_record_check COMMAND indicode check ${CMAKE_CURRENT_SOURCE_DIR}/examples/records.indi)
    add_test(NAME cli_tensor_shape_check COMMAND indicode check ${CMAKE_CURRENT_SOURCE_DIR}/examples/tensor_shapes.indi)
    add_test(NAME cli_ast_json
        COMMAND ${CMAKE_COMMAND}
            -DINDICODE=$<TARGET_FILE:indicode>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/records.indi
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_ast_json.cmake
    )
    add_test(NAME cli_typed_json
        COMMAND ${CMAKE_COMMAND}
            -DINDICODE=$<TARGET_FILE:indicode>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/records.indi
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_typed_json.cmake
    )
    add_test(NAME cli_rejects_type_error COMMAND indicode check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/type_error.indi)
    set_tests_properties(cli_rejects_type_error PROPERTIES WILL_FAIL TRUE)
    add_test(NAME cli_json_diagnostics
        COMMAND ${CMAKE_COMMAND}
            -DINDICODE=$<TARGET_FILE:indicode>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/type_error.indi
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_json_diagnostics.cmake
    )
    add_test(NAME cli_json_parser_fixit
        COMMAND ${CMAKE_COMMAND}
            -DINDICODE=$<TARGET_FILE:indicode>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/missing_parameter_colon.indi
            -DEXPECT_CODE=E-PARSE-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_json_diagnostics.cmake
    )
    add_test(NAME install_package_consumer
        COMMAND ${CMAKE_COMMAND}
            -DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/package-consumer-test
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_install_package.cmake
    )
endif()