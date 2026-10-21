cmake_minimum_required(VERSION 3.24)
project(EStudioUI VERSION 0.4.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ESTUDIOUI_BUILD_TESTS "Build core and GPU integration checks" ON)
option(ESTUDIOUI_BUILD_DEMO "Build the interactive widget gallery" ON)
option(ESTUDIOUI_GPU_TESTS "Enable Linux OSMesa integration checks in CTest" ON)
option(ESTUDIOUI_ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)

# Runtime libraries are supplied by the pinned vcpkg manifest. Optional package
# features are disabled there; the generated GL loader remains a small source file.
find_package(glfw3 3.4 CONFIG REQUIRED)
find_package(Freetype 2.13 REQUIRED)
find_package(harfbuzz CONFIG REQUIRED)

# The pinned utf8proc 2.10 port exports the unnamespaced target. Also accept the
# newer upstream package name for users integrating with a newer registry.
find_package(utf8proc CONFIG QUIET)
if(TARGET utf8proc::utf8proc)
    set(ESTUDIOUI_UTF8PROC_TARGET utf8proc::utf8proc)
else()
    find_package(unofficial-utf8proc CONFIG REQUIRED)
    set(ESTUDIOUI_UTF8PROC_TARGET utf8proc)
endif()

add_library(EStudioUIGl STATIC third_party/glad/src/gl.c)
target_include_directories(EStudioUIGl PUBLIC third_party/glad/include)

add_library(EStudioUI STATIC
    src/Style.cpp src/TextBuffer.cpp src/FontFace.cpp src/Renderer.cpp
    src/Widget.cpp src/UiContext.cpp src/Label.cpp src/Button.cpp src/TextBox.cpp src/Window.cpp
    src/SizeRule.cpp src/Layout.cpp src/ToolButton.cpp src/ToolBar.cpp src/TabWidget.cpp
    src/ContextMenu.cpp src/ScrollWidget.cpp src/ListWidget.cpp src/TreeWidget.cpp src/TableWidget.cpp src/OpenGLWidget.cpp
    src/EasingCurve.cpp src/StyleProperty.cpp src/Animation.cpp src/AnimationSystem.cpp
)
add_library(EStudioUI::EStudioUI ALIAS EStudioUI)
target_compile_features(EStudioUI PUBLIC cxx_std_20)
target_include_directories(EStudioUI PUBLIC include)
target_link_libraries(EStudioUI PRIVATE EStudioUIGl glfw Freetype::Freetype harfbuzz::harfbuzz ${ESTUDIOUI_UTF8PROC_TARGET})
if(MSVC)
    target_compile_options(EStudioUI PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(EStudioUI PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

if(ESTUDIOUI_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(EStudioUI PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(EStudioUI PUBLIC -fsanitize=address,undefined)
endif()

if(ESTUDIOUI_BUILD_DEMO)
    add_executable(EStudioUIDemo demo/main.cpp demo/WidgetGallery.cpp demo/DemoScene.cpp demo/AnimationGallery.cpp)
    target_link_libraries(EStudioUIDemo PRIVATE EStudioUI::EStudioUI EStudioUIGl glfw)
    if(MSVC)
        target_compile_options(EStudioUIDemo PRIVATE /utf-8)
    endif()
    add_custom_command(TARGET EStudioUIDemo POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:EStudioUIDemo>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/NotoSans.ttf" "$<TARGET_FILE_DIR:EStudioUIDemo>/assets/NotoSans.ttf"
        COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md" "$<TARGET_FILE_DIR:EStudioUIDemo>/THIRD_PARTY_NOTICES.md"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/licenses" "$<TARGET_FILE_DIR:EStudioUIDemo>/licenses"
        COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" "$<TARGET_FILE_DIR:EStudioUIDemo>/licenses/EStudioUI-MIT.txt"
        VERBATIM
    )
    if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        foreach(port glfw3 freetype harfbuzz utf8proc)
            set(copyright "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/${port}/copyright")
            if(EXISTS "${copyright}")
                add_custom_command(TARGET EStudioUIDemo POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy "${copyright}"
                    "$<TARGET_FILE_DIR:EStudioUIDemo>/licenses/${port}-vcpkg-copyright.txt"
                    VERBATIM
                )
            endif()
        endforeach()
    endif()
endif()

if(ESTUDIOUI_BUILD_TESTS)
    enable_testing()
    add_executable(EStudioUICoreTests tests/CoreTests.cpp)
    target_link_libraries(EStudioUICoreTests PRIVATE EStudioUI::EStudioUI)
    target_compile_definitions(EStudioUICoreTests PRIVATE ESTUDIOUI_FONT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/NotoSans.ttf")
    if(MSVC)
        target_compile_options(EStudioUICoreTests PRIVATE /utf-8)
    endif()
    add_test(NAME Core COMMAND EStudioUICoreTests)
    add_executable(EStudioUIWidgetTests tests/WidgetTests.cpp)
    target_link_libraries(EStudioUIWidgetTests PRIVATE EStudioUI::EStudioUI)
    target_compile_definitions(EStudioUIWidgetTests PRIVATE
        ESTUDIOUI_FONT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/NotoSans.ttf"
        ESTUDIOUI_ASSETS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/assets")
    if(MSVC)
        target_compile_options(EStudioUIWidgetTests PRIVATE /utf-8)
    endif()
    add_test(NAME WidgetsAndLayout COMMAND EStudioUIWidgetTests)

    add_executable(EStudioUIAnimationTests tests/AnimationTests.cpp)
    target_link_libraries(EStudioUIAnimationTests PRIVATE EStudioUI::EStudioUI)
    target_compile_definitions(EStudioUIAnimationTests PRIVATE
        ESTUDIOUI_FONT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/NotoSans.ttf")
    if(MSVC)
        target_compile_options(EStudioUIAnimationTests PRIVATE /utf-8)
    endif()
    add_test(NAME Animation COMMAND EStudioUIAnimationTests)

    if(ESTUDIOUI_GPU_TESTS AND TARGET EStudioUIDemo AND UNIX AND NOT APPLE)
        add_test(NAME GpuIntegration COMMAND EStudioUIDemo --headless --self-test --output "${CMAKE_CURRENT_BINARY_DIR}/verification")
        set_tests_properties(GpuIntegration PROPERTIES TIMEOUT 60)
    endif()
endif()
