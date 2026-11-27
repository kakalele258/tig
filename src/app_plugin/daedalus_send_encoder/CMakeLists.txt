set(DAEDALUS_SEND_TARGET daedalus_send_encoder_plugin_lib)

find_package(DaedalusSimSdk REQUIRED)

add_library(${DAEDALUS_SEND_TARGET} SHARED
    src/DaedalusSendEncoder.cpp
)

target_include_directories(${DAEDALUS_SEND_TARGET} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/core/app/interface/type
)

target_link_libraries(${DAEDALUS_SEND_TARGET} PUBLIC
    app_interface_lib
    class_loader_lib
    google_logger_lib
    DaedalusSimSdk::DaedalusSimSdk
)
