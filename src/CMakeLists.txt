add_executable(weaver item.cpp value.cpp event.cpp main.cpp calculator.cpp port.cpp tcp.cpp modbus.cpp http.cpp mqtt.cpp config.cpp basic.cpp knx.cpp logger.cpp link.cpp generator.cpp tr064.cpp storage.cpp sml.cpp)

target_link_libraries(weaver mosquitto curl)

set(CMAKE_CXX_FLAGS "-fconcepts")

install(TARGETS weaver EXPORT waever DESTINATION usr/bin)
