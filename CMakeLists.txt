cmake_minimum_required (VERSION 3.14)
project (Enclavamiento)
set(CMAKE_CXX_STANDARD 17)

set(SOURCES main.cpp mqtt.cpp log.cpp items.cpp cv.cpp bloqueo.cpp signal.cpp
    ruta.cpp topology.cpp remota.cpp dependencia.cpp pn_enclavado.cpp
    serialization.cpp serialization_remota.cpp )

add_executable(enclavamiento ${SOURCES})

target_include_directories(enclavamiento PRIVATE include)
target_include_directories(enclavamiento PRIVATE include/nlohmann)

target_link_libraries(enclavamiento PRIVATE mosquitto)

add_subdirectory(gestor_conexion)
