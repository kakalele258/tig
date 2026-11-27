set(DAEDALUS_CAMERA_TARGET daedalus_camera_manager_plugin_lib)

find_package(OpenCV REQUIRED COMPONENTS core imgproc)
find_package(DaedalusSimSdk REQUIRED)

add_library(${DAEDALUS_CAMERA_TARGET} SHARED
    src/DaedalusCameraManager.cpp
    src/DaedalusImageReceiver.cpp
)

target_include_directories(${DAEDALUS_CAMERA_TARGET} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/core/app/interface/type
)

target_link_libraries(${DAEDALUS_CAMERA_TARGET} PUBLIC
    app_interface_lib
    class_loader_lib
    google_logger_lib
    opencv_core
    opencv_imgproc
    DaedalusSimSdk::DaedalusSimSdk
)
