cmake_minimum_required(VERSION 3.20)

project(url_shortener VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(SQLite3 REQUIRED)

set(WEB_ASSET_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/web/index.html")
set(WEB_ASSET_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(WEB_ASSET_HEADER
    "${WEB_ASSET_GENERATED_DIR}/embedded_web_asset.h"
)

set(HTTP_PUBLIC_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/include/http.h")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${HTTP_PUBLIC_HEADER}"
)
file(STRINGS "${HTTP_PUBLIC_HEADER}" HTTP_MAX_BODY_DEFINITION
    REGEX "^#define[ \t]+HTTP_MAX_BODY_BYTES[ \t]+[0-9]+U?$"
    LIMIT_COUNT 1
)
if(NOT HTTP_MAX_BODY_DEFINITION MATCHES
   "^#define[ \t]+HTTP_MAX_BODY_BYTES[ \t]+([0-9]+)U?$")
    message(FATAL_ERROR
        "Could not determine HTTP_MAX_BODY_BYTES from ${HTTP_PUBLIC_HEADER}"
    )
endif()
set(WEB_ASSET_MAX_BYTES "${CMAKE_MATCH_1}")

add_custom_command(
    OUTPUT "${WEB_ASSET_HEADER}"
    COMMAND "${CMAKE_COMMAND}"
            "-DINPUT_FILE=${WEB_ASSET_SOURCE}"
            "-DOUTPUT_FILE=${WEB_ASSET_HEADER}"
            "-DMAX_BYTES=${WEB_ASSET_MAX_BYTES}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_web_asset.cmake"
    DEPENDS
        "${WEB_ASSET_SOURCE}"
        "${HTTP_PUBLIC_HEADER}"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_web_asset.cmake"
    COMMENT "Embedding web/index.html (${WEB_ASSET_MAX_BYTES}-byte limit)"
    VERBATIM
)

add_library(url_shortener_core STATIC
    "${WEB_ASSET_HEADER}"
    src/base62.c
    src/database.c
    src/http.c
    src/router.c
    src/server.c
    src/shortener.c
)
target_include_directories(url_shortener_core PUBLIC include)
target_include_directories(url_shortener_core PRIVATE
    "${WEB_ASSET_GENERATED_DIR}"
)
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
