cmake_minimum_required(VERSION 3.16)
project(ThetaExplorer VERSION 1.022 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 17)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum OS X deployment version")
    execute_process(
        COMMAND xcrun --show-sdk-path
        OUTPUT_VARIABLE MACOS_SDK_PATH
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(MACOS_SDK_PATH)
        set(CMAKE_OSX_SYSROOT "${MACOS_SDK_PATH}" CACHE PATH "macOS SDK path")
        message(STATUS "macOS SDK path: ${MACOS_SDK_PATH}")
    endif()
    # Universal en Apple Silicon (corre nativo; con Rosetta macOS avisa que la app Intel deja
    # de funcionar en una version futura). Mismo criterio que LGA_Base_QT_C_Py.
    if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "arm64")
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures for OS X")
    else()
        set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE STRING "Build architectures for OS X")
    endif()
    set(CMAKE_MACOSX_RPATH ON)
    set(CMAKE_INSTALL_RPATH "@executable_path/../Frameworks")
    set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -isysroot ${CMAKE_OSX_SYSROOT}")
    set(CMAKE_OBJCXX_FLAGS "${CMAKE_OBJCXX_FLAGS} -isysroot ${CMAKE_OSX_SYSROOT}")
endif()

# Debug por defecto; deploy.sh compila Release en build-release/ (compilar.sh --release).
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()
if(APPLE)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")
    set(CMAKE_OBJCXX_FLAGS_DEBUG "${CMAKE_OBJCXX_FLAGS_DEBUG} -g -O0")
endif()

set(CMAKE_AUTORCC ON)

if(APPLE)
    # AGL (Apple Graphics Library) es un framework legacy de OpenGL que Apple removió
    # en macOS 12+. Bloqueamos que FindOpenGL de CMake lo detecte o proponga.
    set(OPENGL_agl_LIBRARY "" CACHE STRING "AGL library" FORCE)
    set(OPENGL_AGL_INCLUDE_DIR "" CACHE PATH "AGL include dir" FORCE)
endif()

if(NOT CMAKE_PREFIX_PATH)
    if(WIN32)
        set(CMAKE_PREFIX_PATH "C:/Qt/6.5.3/mingw_64")
    else()
        set(CMAKE_PREFIX_PATH "$ENV{HOME}/Qt/6.5.3/macos")
    endif()
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Concurrent Svg Network)
qt_standard_project_setup(REQUIRES 6.5)
add_subdirectory(libs/lgaupdate)

if(APPLE)
    # Qt6 usa el interface target WrapOpenGL::WrapOpenGL para OpenGL en macOS
    # (definido en $QTDIR/lib/cmake/Qt6/FindWrapOpenGL.cmake). Ese archivo agrega
    # "-framework AGL" incondicionalmente como fallback cuando find_library no
    # encuentra AGL (removido en macOS 12+). Lo purgamos aca para que ni el
    # sistema ni ningun consumer de Qt6::Gui traiga AGL por transitividad.
    if(TARGET WrapOpenGL::WrapOpenGL)
        get_target_property(_wo_libs WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
        if(_wo_libs)
            set(_wo_new_libs "")
            set(_wo_changed FALSE)
            foreach(_wo_lib ${_wo_libs})
                if("${_wo_lib}" MATCHES "(^|/)AGL(\\.framework)?$" OR "${_wo_lib}" MATCHES "-framework[ \t]+AGL")
                    set(_wo_changed TRUE)
                else()
                    list(APPEND _wo_new_libs "${_wo_lib}")
                endif()
            endforeach()
            if(_wo_changed)
                set_target_properties(WrapOpenGL::WrapOpenGL PROPERTIES INTERFACE_LINK_LIBRARIES "${_wo_new_libs}")
                message(STATUS "AGL framework removed from WrapOpenGL::WrapOpenGL")
            endif()
        endif()
    endif()
endif()

# ---- C++ Sources ----
set(SOURCES
    src/main.cpp
    src/services/ThetaCameraService.cpp
    src/services/DownloadManager.cpp
    src/services/DownloadHistory.cpp
    src/services/CameraMediaCache.cpp
    src/services/HdrGroupingService.cpp
    src/ui/MainWindow.cpp
    src/ui/ThumbnailGridWidget.cpp
    src/ui/ThumbnailTileWidget.cpp
    src/ui/PreviewPanel.cpp
    src/ui/FileMetadataPanel.cpp
    src/ui/ConfirmDialog.cpp
    src/ui/HelpDialog.cpp
    src/ui/StatusWidgets.cpp
    src/ui/popover/MessagePopover.cpp
    src/ui/popover/PopoverCloseButton.cpp
    src/ui/popover/PopoverCountdownRail.cpp
    src/ui/popover/PopoverShadow.cpp
    src/utils/Logger.cpp
    src/utils/ColorUtils.cpp
    src/utils/UiIcons.cpp
    src/utils/AppPaths.cpp
    src/utils/DualFisheyePreview.cpp
)

# Backend nativo independiente por plataforma; la UI conserva el contrato Qt.
if(APPLE)
    set(OBJCXX_SOURCES src/bridge/ThetaBridge.mm)
    set_source_files_properties(${OBJCXX_SOURCES} PROPERTIES
        COMPILE_FLAGS "-x objective-c++ -fobjc-arc" SKIP_AUTOMOC ON)
elseif(WIN32)
    list(APPEND SOURCES src/bridge/ThetaBridgeWindows.cpp
        src/bridge/ThetaCameraWorker.cpp src/utils/CameraImageHeader.cpp
        resources/windows-icon.rc)
else()
    message(FATAL_ERROR "Theta Explorer supports macOS and Windows.")
endif()

set(HEADERS
    include/thetaexplorer/ColorUtils.h
    include/thetaexplorer/CameraFileInfo.h
    include/thetaexplorer/MediaAssetGroup.h
    include/thetaexplorer/ThetaBridge.h
    include/thetaexplorer/ThetaCameraService.h
    include/thetaexplorer/HdrGroupingService.h
    include/thetaexplorer/MainWindow.h
    include/thetaexplorer/ThumbnailGridWidget.h
    include/thetaexplorer/ThumbnailTileWidget.h
    include/thetaexplorer/PreviewPanel.h
    include/thetaexplorer/FileMetadataPanel.h
    include/thetaexplorer/DownloadManager.h
    include/thetaexplorer/DownloadHistory.h
    include/thetaexplorer/CameraMediaCache.h
    include/thetaexplorer/ConfirmDialog.h
    include/thetaexplorer/HelpDialog.h
    include/thetaexplorer/StatusWidgets.h
    include/thetaexplorer/UiIcons.h
    include/thetaexplorer/AppPaths.h
    include/thetaexplorer/DualFisheyePreview.h
    include/thetaexplorer/popover/MessagePopover.h
    include/thetaexplorer/popover/PopoverCloseButton.h
    include/thetaexplorer/popover/PopoverCountdownRail.h
    include/thetaexplorer/popover/PopoverShadow.h
    include/thetaexplorer/Logger.h
)

set(RESOURCES resources/resources.qrc)

qt_add_executable(${PROJECT_NAME}
    ${SOURCES}
    ${OBJCXX_SOURCES}
    ${HEADERS}
    ${RESOURCES}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Fuente única de verdad del número de versión: el `project(... VERSION ...)` de arriba
# se expone al código C++ como macro THETAEXPLORER_VERSION. Convención LGA — evita
# tener el número hardcodeado en main.cpp / Info.plist.
target_compile_definitions(${PROJECT_NAME} PRIVATE
    THETAEXPLORER_VERSION="${PROJECT_VERSION}"
    THETA_SOURCE_TREE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    THETA_BUILD_TREE_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::Svg
    LGAUpdate
)

if(APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        "-framework ImageCaptureCore"
        "-framework ImageIO"
        "-framework Foundation"
        "-framework AppKit"
        "-framework CoreFoundation"
        "-framework CoreGraphics"
    )

    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        OUTPUT_NAME "LGA ThetaExplorer"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "Theta Explorer"
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        # GUI_IDENTIFIER es el nombre que CMake conoce: con MACOSX_BUNDLE_IDENTIFIER el
        # CFBundleIdentifier del bundle salia vacio.
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.lga.thetamacexplorer"
        MACOSX_BUNDLE_EXECUTABLE_NAME "LGA ThetaExplorer"
        MACOSX_BUNDLE_ICON_FILE "AppIcon.icns"
    )

    # Icono de macOS: pipeline Icon Composer. El icono se disena en
    # resources/icons/Alta/<App>.icon y se compila con actool (ver ../LGA_IconLab).
    # Se copian DOS recursos y hacen falta los dos:
    #   Assets.car    el icono moderno completo, que es lo que consume macOS 26 via
    #                 CFBundleIconName. Sin esto no hay tratamiento moderno.
    #   AppIcon.icns  fallback de compatibilidad, via CFBundleIconFile. Se genera con
    #                 --standalone-icon-behavior all para que traiga TODOS los tamanos
    #                 (16..1024); por defecto actool solo mete un subconjunto chico que
    #                 llega hasta 256 px.
    # NO convertir el .icon a un .icns suelto descartando el Assets.car: eso pierde el
    # comportamiento moderno y reaparece el problema de tamano en Cmd+Tab.
    set(APP_ICON_FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/AppIcon.icns")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/Assets.car")
        list(APPEND APP_ICON_FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/Assets.car")
    else()
        message(WARNING "Falta resources/icons/Assets.car: el icono va a caer al .icns "
                        "de compatibilidad y se pierde el icono moderno.")
    endif()
    set_source_files_properties(${APP_ICON_FILES} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(${PROJECT_NAME} PRIVATE ${APP_ICON_FILES})
endif()



if(WIN32)
    target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/resources")
    include(CheckIncludeFileCXX)
    check_include_file_cxx("portabledeviceapi.h" THETA_HAS_WPD_API)
    check_include_file_cxx("portabledevice.h" THETA_HAS_WPD_PROPERTIES)
    if(NOT THETA_HAS_WPD_API OR NOT THETA_HAS_WPD_PROPERTIES)
        target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/wpd")
    endif()
    target_compile_definitions(${PROJECT_NAME} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A000000)
    target_link_libraries(${PROJECT_NAME} PRIVATE ole32 oleaut32 uuid cfgmgr32)
    set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE TRUE OUTPUT_NAME "LGA_ThetaExplorer")
endif()


