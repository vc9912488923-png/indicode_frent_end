file(REMOVE_RECURSE "${TEST_ROOT}")
set(install_prefix "${TEST_ROOT}/install")
set(consumer_build "${TEST_ROOT}/build")

execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${install_prefix}"
    RESULT_VARIABLE install_result
)
if(NOT install_result EQUAL 0)
    message(FATAL_ERROR "installing the Indicode front-end package failed")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${SOURCE_DIR}/tests/package_consumer"
            -B "${consumer_build}" "-DCMAKE_PREFIX_PATH=${install_prefix}"
    RESULT_VARIABLE configure_result
)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "configuring an installed-package consumer failed")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${consumer_build}"
    RESULT_VARIABLE build_result
)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "building an installed-package consumer failed")
endif()

if(WIN32)
    set(consumer_executable "${consumer_build}/frontend_consumer.exe")
else()
    set(consumer_executable "${consumer_build}/frontend_consumer")
endif()
execute_process(
    COMMAND "${consumer_executable}"
    RESULT_VARIABLE run_result
)
if(NOT run_result EQUAL 0)
    message(FATAL_ERROR "installed-package consumer failed at runtime")
endif()
