cmake_minimum_required(VERSION 3.20)

project(url_shortener VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(SQLite3 REQUIRED)

add_library(url_shortener_core STATIC
    src/base62.c
    src/database.c
    src/http.c
    src/router.c
    src/server.c
    src/shortener.c
)
target_include_directories(url_shortener_core PUBLIC include)
target_link_libraries(url_shortener_core PUBLIC SQLite::SQLite3)

add_executable(url-shortener src/main.c)
target_link_libraries(url-shortener PRIVATE url_shortener_core)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(url_shortener_core PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
    target_compile_options(url-shortener PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
endif()

include(CTest)

if(BUILD_TESTING)
    find_program(CURL_EXECUTABLE curl REQUIRED)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    foreach(test_name IN ITEMS base62 database http shortener router)
        add_executable(test_${test_name} tests/test_${test_name}.c)
        target_link_libraries(test_${test_name} PRIVATE url_shortener_core)
        if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(test_${test_name} PRIVATE
                -Wall -Wextra -Wpedantic -Werror -UNDEBUG
            )
        endif()
        add_test(NAME ${test_name} COMMAND test_${test_name})
    endforeach()

    add_test(
        NAME acceptance
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_acceptance.sh
                $<TARGET_FILE:url-shortener>
                ${CURL_EXECUTABLE}
                ${Python3_EXECUTABLE}
    )
    set_tests_properties(acceptance PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
endif()
