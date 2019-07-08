add_executable(weaver main.cpp port.cpp mqtt.cpp config.cpp basic.cpp knx.cpp logger.cpp link.cpp generator.cpp tr064.cpp storage.cpp)

target_link_libraries(weaver mosquitto)

install(TARGETS weaver EXPORT waever DESTINATION usr/bin)
