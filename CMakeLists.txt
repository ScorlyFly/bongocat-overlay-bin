cmake_minimum_required(VERSION 3.22)
project(bongocat-overlay VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick)
find_package(LayerShellQt REQUIRED)

qt_policy(SET QTP0001 NEW)

qt_add_executable(bongocat-overlay main.cpp)
qt_add_qml_module(bongocat-overlay
    URI BongaCatOverlay
    VERSION 1.0
    QML_FILES
        Overlay.qml
    RESOURCES
        assets/bongo-cat-both-up.png
        assets/bongo-cat-left-down.png
        assets/bongo-cat-right-down.png
        assets/bongo-cat-both-down.png
)

target_compile_definitions(bongocat-overlay PRIVATE
    BONGO_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(bongocat-overlay PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick LayerShellQt::Interface
)

install(TARGETS bongocat-overlay RUNTIME DESTINATION bin)
install(PROGRAMS helper/bongo-input DESTINATION share/bongocat-overlay/helper)
install(FILES config.ini.example DESTINATION share/bongocat-overlay)
install(FILES packaging/bongocat-overlay.desktop DESTINATION share/applications)
install(FILES packaging/bongocat-overlay.svg DESTINATION share/icons/hicolor/scalable/apps)
