cmake_minimum_required (VERSION 3.14)
project (Enclavamiento)
set(CMAKE_CXX_STANDARD 17)

cmake_minimum_required (VERSION 3.14)
project (Enclavamiento)
set(CMAKE_CXX_STANDARD 20)

set(SOURCES src/main.cpp src/mqtt.cpp src/log.cpp src/items.cpp src/cv.cpp src/bloqueo.cpp src/signal.cpp src/frontera.cpp
    src/ruta.cpp src/deslizamiento.cpp src/topology.cpp src/aguja.cpp src/remota.cpp src/dependencia.cpp src/pn_enclavado.cpp
    src/serialization.cpp src/serialization_remota.cpp )

add_executable(enclavamiento ${SOURCES})

target_include_directories(enclavamiento PRIVATE include)
target_include_directories(enclavamiento PRIVATE include/nlohmann)

target_link_libraries(enclavamiento PRIVATE mosquitto)

add_subdirectory(gestor_conexion)
add_subdirectory(campo)
