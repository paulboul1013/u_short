if(NOT DEFINED INPUT_FILE OR INPUT_FILE STREQUAL "")
    message(FATAL_ERROR "INPUT_FILE is required")
endif()
if(NOT DEFINED OUTPUT_FILE OR OUTPUT_FILE STREQUAL "")
    message(FATAL_ERROR "OUTPUT_FILE is required")
endif()
if(NOT DEFINED MAX_BYTES OR NOT MAX_BYTES MATCHES "^[0-9]+$")
    message(FATAL_ERROR "MAX_BYTES must be a non-negative integer")
endif()
if(NOT EXISTS "${INPUT_FILE}")
    message(FATAL_ERROR "Web asset does not exist: ${INPUT_FILE}")
endif()

file(SIZE "${INPUT_FILE}" ASSET_LENGTH)
if(ASSET_LENGTH GREATER MAX_BYTES)
    message(FATAL_ERROR
        "Web asset ${INPUT_FILE} is ${ASSET_LENGTH} bytes; "
        "HTTP_MAX_BODY_BYTES permits at most ${MAX_BYTES} bytes"
    )
endif()

file(READ "${INPUT_FILE}" ASSET_HEX HEX)
set(ASSET_BYTES "")
if(ASSET_LENGTH GREATER 0)
    math(EXPR LAST_BYTE_INDEX "${ASSET_LENGTH} - 1")
    foreach(BYTE_INDEX RANGE 0 ${LAST_BYTE_INDEX})
        math(EXPR HEX_INDEX "${BYTE_INDEX} * 2")
        string(SUBSTRING "${ASSET_HEX}" ${HEX_INDEX} 2 BYTE_HEX)
        string(APPEND ASSET_BYTES "0x${BYTE_HEX}U,")
        math(EXPR LINE_POSITION "${BYTE_INDEX} % 16")
        if(LINE_POSITION EQUAL 15)
            string(APPEND ASSET_BYTES "\n    ")
        else()
            string(APPEND ASSET_BYTES " ")
        endif()
    endforeach()
endif()
string(APPEND ASSET_BYTES "0x00U")

get_filename_component(OUTPUT_DIRECTORY "${OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(WRITE "${OUTPUT_FILE}"
"#ifndef URL_SHORTENER_EMBEDDED_WEB_ASSET_H\n"
"#define URL_SHORTENER_EMBEDDED_WEB_ASSET_H\n"
"\n"
"#include <stddef.h>\n"
"\n"
"static const unsigned char embedded_web_index_html[] = {\n"
"    ${ASSET_BYTES}\n"
"};\n"
"static const size_t embedded_web_index_html_length = ${ASSET_LENGTH}U;\n"
"\n"
"#endif\n"
)
