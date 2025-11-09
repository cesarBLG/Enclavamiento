cmake_minimum_required (VERSION 3.14)
project (Enclavamiento)
set(CMAKE_CXX_STANDARD 17)

set(SOURCES main.cpp mqtt.cpp log.cpp enums.cpp items.cpp cv.cpp bloqueo.cpp signal.cpp ruta.cpp topology.cpp remota.cpp dependencia.cpp)

add_executable(enclavamiento ${SOURCES})

target_link_libraries(enclavamiento PRIVATE mosquitto)
