cmake_minimum_required(VERSION 3.24)
project(EStudioUI VERSION 0.14.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ESTUDIOUI_BUILD_TESTS "Build core and GPU integration checks" ON)
option(ESTUDIOUI_BUILD_DEMO "Build the interactive widget gallery" ON)
option(ESTUDIOUI_GPU_TESTS "Enable Linux OSMesa integration checks in CTest" ON)
option(ESTUDIOUI_ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
option(ESTUDIOUI_BUILD_THREAD_TESTS "Build only isolated CPU ThreadSanitizer checks, without UI dependencies" OFF)

# A separate build directory exercises the actual concurrency sources without
# loading GLFW, font services, GL, or uninstrumented third-party dependencies.
if(ESTUDIOUI_BUILD_THREAD_TESTS)
    if(ESTUDIOUI_ENABLE_SANITIZERS)
        message(FATAL_ERROR "ThreadSanitizer and address/undefined sanitizers require separate build directories.")
    endif()
    if(WIN32 OR NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$" OR CMAKE_SIZEOF_VOID_P LESS 8)
        message(FATAL_ERROR "Isolated ThreadSanitizer checks require a supported 64-bit GCC/Clang platform.")
    endif()
    find_package(Threads REQUIRED)
    include(CheckCXXSourceCompiles)
    include(CMakePushCheckState)
    cmake_push_check_state(RESET)
    set(CMAKE_REQUIRED_FLAGS "-fsanitize=thread -fno-omit-frame-pointer")
    set(CMAKE_REQUIRED_LINK_OPTIONS -fsanitize=thread)
    set(CMAKE_REQUIRED_LIBRARIES Threads::Threads)
    check_cxx_source_compiles(
        "#include <thread>\nint main() { std::thread([] {}).join(); return 0; }"
        ESTUDIOUI_HAS_THREAD_SANITIZER)
    cmake_pop_check_state()
    if(NOT ESTUDIOUI_HAS_THREAD_SANITIZER)
        message(FATAL_ERROR "The selected compiler cannot compile and link ThreadSanitizer executables.")
    endif()

    add_library(EStudioUIThreadRuntime STATIC
        src/DispatchReceipt.cpp src/UiDispatcher.cpp src/UiHandle.cpp src/ThreadAffinity.cpp
        src/Task.cpp src/BackgroundTasks.cpp src/StateStore.cpp src/State.cpp
        src/Action.cpp src/Shortcut.cpp src/InputEvent.cpp
        src/ModelValue.cpp src/ModelSnapshot.cpp src/RecordModel.cpp src/ModelBuilder.cpp
        src/ItemModel.cpp src/SelectionModel.cpp src/SortFilterModel.cpp src/TreeRowMap.cpp)
    target_include_directories(EStudioUIThreadRuntime PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_link_libraries(EStudioUIThreadRuntime PUBLIC Threads::Threads)
    target_compile_options(EStudioUIThreadRuntime PUBLIC -fsanitize=thread -fno-omit-frame-pointer -O1 -g1)
    target_link_options(EStudioUIThreadRuntime PUBLIC -fsanitize=thread)
    target_compile_options(EStudioUIThreadRuntime PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)

    enable_testing()
    foreach(suite Dispatch QueuedSignal Task State Model ActionDomain)
        add_executable(EStudioUIThread${suite}Tests tests/${suite}Tests.cpp)
        target_link_libraries(EStudioUIThread${suite}Tests PRIVATE EStudioUIThreadRuntime)
        add_test(NAME Thread${suite} COMMAND EStudioUIThread${suite}Tests)
        set_tests_properties(Thread${suite} PROPERTIES
            LABELS ThreadSanitizer TIMEOUT 120 ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1")
    endforeach()
    return()
endif()

# Runtime libraries are supplied by the pinned vcpkg manifest. Optional package
# features are disabled there; the generated GL loader remains a small source file.
find_package(glfw3 3.4 CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(Freetype 2.13 REQUIRED)
find_package(harfbuzz CONFIG REQUIRED)
find_path(ESTUDIOUI_KIWI_INCLUDE_DIR kiwi/kiwi.h REQUIRED)

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
    src/InputEvent.cpp src/Shortcut.cpp src/Action.cpp src/PopupSession.cpp src/UiInput.cpp
    src/Measure.cpp src/LayoutStrategy.cpp src/BoxLayout.cpp src/GridLayout.cpp src/FormLayout.cpp src/FlowLayout.cpp
    src/ConstraintExpression.cpp src/Constraint.cpp src/ConstraintItem.cpp src/ConstraintLayout.cpp src/NativeResizePump.cpp
    src/ContextMenu.cpp src/ScrollWidget.cpp src/ListWidget.cpp src/TreeWidget.cpp src/TableWidget.cpp src/OpenGLWidget.cpp
    src/FramePacer.cpp src/VisualStyle.cpp src/CssMotion.cpp src/EasingCurve.cpp src/StyleProperty.cpp src/Animation.cpp src/AnimationSystem.cpp
    src/CanvasScene.cpp src/CanvasGeometry.cpp src/CanvasWidget.cpp src/SketchDocument.cpp src/SketchWidget.cpp
    src/InputValidator.cpp src/NumericInput.cpp src/Slider.cpp src/SplitToolButton.cpp
    src/Painter.cpp src/PainterPath.cpp src/PaintWidget.cpp
    src/ThreadAffinity.cpp src/DispatchReceipt.cpp src/UiDispatcher.cpp src/UiHandle.cpp
    src/Task.cpp src/BackgroundTasks.cpp src/Application.cpp
    src/StateStore.cpp src/State.cpp src/Bindings.cpp src/Invalidation.cpp
    src/ModelValue.cpp src/ModelSnapshot.cpp src/RecordModel.cpp src/ModelBuilder.cpp src/Models.cpp
    src/ItemModel.cpp src/SelectionModel.cpp src/SortFilterModel.cpp src/CollectionView.cpp src/ItemDelegate.cpp src/TreeRowMap.cpp
)
add_library(EStudioUI::EStudioUI ALIAS EStudioUI)
target_compile_features(EStudioUI PUBLIC cxx_std_20)
target_include_directories(EStudioUI PUBLIC include)
target_include_directories(EStudioUI SYSTEM PRIVATE "${ESTUDIOUI_KIWI_INCLUDE_DIR}")
if(WIN32)
    target_link_libraries(EStudioUI PRIVATE comctl32)
endif()
target_link_libraries(EStudioUI PRIVATE EStudioUIGl glfw Freetype::Freetype harfbuzz::harfbuzz ${ESTUDIOUI_UTF8PROC_TARGET})
target_link_libraries(EStudioUI PUBLIC Threads::Threads)
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
    add_executable(EStudioUIDemo demo/main.cpp demo/WidgetGallery.cpp demo/DemoScene.cpp demo/AnimationGallery.cpp demo/RichCssGallery.cpp demo/GpuWorkload.cpp
        demo/SketchGallery.cpp demo/CanvasPrimitiveProbe.cpp demo/CanvasProbe.cpp demo/CanvasCurvesProbe.cpp
        demo/CustomWidgetGallery.cpp demo/PainterProbe.cpp demo/InputControlsProbe.cpp
        demo/WidgetStylesGallery.cpp demo/WidgetStylesProbe.cpp demo/ApplicationProbe.cpp demo/FoundationGallery.cpp
        demo/StateGallery.cpp demo/ModelGallery.cpp demo/LayoutGallery.cpp demo/ConstraintGallery.cpp demo/InputGallery.cpp)
    target_link_libraries(EStudioUIDemo PRIVATE EStudioUI::EStudioUI EStudioUIGl glfw)
    add_executable(EStudioUIConstraintExample examples/ConstraintLayout.cpp)
    target_link_libraries(EStudioUIConstraintExample PRIVATE EStudioUI::EStudioUI)
    add_executable(EStudioUIInputExample examples/InputActions.cpp)
    target_link_libraries(EStudioUIInputExample PRIVATE EStudioUI::EStudioUI)
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
        foreach(port glfw3 freetype harfbuzz utf8proc kiwi)
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

    add_executable(EStudioUIRichCssTests tests/RichCssTests.cpp)
    target_link_libraries(EStudioUIRichCssTests PRIVATE EStudioUI::EStudioUI)
    target_compile_definitions(EStudioUIRichCssTests PRIVATE
        ESTUDIOUI_FONT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/NotoSans.ttf")
    if(MSVC)
        target_compile_options(EStudioUIRichCssTests PRIVATE /utf-8)
    endif()
    add_test(NAME RichCss COMMAND EStudioUIRichCssTests)

    foreach(suite CanvasScene CanvasGeometry SketchDocument CanvasWidget InputControls SplitToolButton Painter
                  StyleParts StyledCollections StyledScroll CompositeStyle
                  Dispatch ThreadAffinity Task QueuedSignal ControlAffinity ServiceAffinity
                  State Binding Invalidation ControlUpdate Model ModelView Layout ConstraintLayout InputFoundation ActionDomain)
        add_executable(EStudioUI${suite}Tests tests/${suite}Tests.cpp)
        target_link_libraries(EStudioUI${suite}Tests PRIVATE EStudioUI::EStudioUI)
        target_compile_definitions(EStudioUI${suite}Tests PRIVATE
            ESTUDIOUI_FONT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/NotoSans.ttf")
        if(MSVC)
            target_compile_options(EStudioUI${suite}Tests PRIVATE /utf-8)
        endif()
        add_test(NAME ${suite} COMMAND EStudioUI${suite}Tests)
    endforeach()

    if(WIN32)
        add_executable(EStudioUINativeResizeTests tests/NativeResizeTests.cpp src/NativeResizePump.cpp)
        target_include_directories(EStudioUINativeResizeTests PRIVATE src)
        target_link_libraries(EStudioUINativeResizeTests PRIVATE comctl32)
        add_test(NAME NativeResize COMMAND EStudioUINativeResizeTests)
        set_tests_properties(NativeResize PROPERTIES TIMEOUT 20)
    endif()

    if(ESTUDIOUI_GPU_TESTS AND TARGET EStudioUIDemo AND UNIX AND NOT APPLE)
        add_test(NAME GpuIntegration COMMAND EStudioUIDemo --headless --self-test --output "${CMAKE_CURRENT_BINARY_DIR}/verification")
        set_tests_properties(GpuIntegration PROPERTIES TIMEOUT 120)
    endif()
endif()
