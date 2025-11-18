cmake_minimum_required (VERSION 3.14)
project (gestor_conexion)
set(CMAKE_CXX_STANDARD 17)

set(SOURCES gestor_conexion.cpp)

add_executable(gestor_conexion ${SOURCES})

target_link_libraries(gestor_conexion PRIVATE mosquitto)
