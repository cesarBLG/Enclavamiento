set(ARDUINO_LIB_DIR
    "${CMAKE_BINARY_DIR}/campo"
)

set(ARDUINO_SRC_DIR
    "${ARDUINO_LIB_DIR}/src"
)

file(GLOB CAMPO_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

file(GLOB ENCLAVAMIENTO_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/*.h"
)

set(ENCLAVAMIENTO_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/serialization.cpp"
)

add_custom_target(campo ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${ARDUINO_SRC_DIR}"

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CAMPO_SOURCES}
        "${ARDUINO_SRC_DIR}"

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${ENCLAVAMIENTO_HEADERS}
        "${ARDUINO_SRC_DIR}"

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${ENCLAVAMIENTO_SOURCES}
        "${ARDUINO_SRC_DIR}"

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/library.properties"
        "${ARDUINO_LIB_DIR}/library.properties"
)
