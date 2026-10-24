cmake_minimum_required(VERSION 3.24)
project(EStudioUI VERSION 0.26.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ESTUDIOUI_BUILD_TESTS "Build core and GPU integration checks" ON)
option(ESTUDIOUI_BUILD_CREATOR "Build the EStudioUICreator authoring application" ON)
option(ESTUDIOUI_BUILD_DATABASE_BROWSER "Build the SQLite database browser" ON)
option(ESTUDIOUI_BUILD_SQLITE "Build the reusable SQLite model adapter" ON)
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
        src/ItemModel.cpp src/ModelEditSession.cpp src/ModelQuery.cpp src/SelectionModel.cpp src/SortFilterModel.cpp src/TreeRowMap.cpp)
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
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Freetype 2.13 REQUIRED)
find_package(harfbuzz CONFIG REQUIRED)
find_package(SheenBidi CONFIG REQUIRED)
find_package(libunibreak CONFIG REQUIRED)
find_path(ESTUDIOUI_KIWI_INCLUDE_DIR kiwi/kiwi.h REQUIRED)
find_path(ESTUDIOUI_STB_INCLUDE_DIR stb_image.h REQUIRED)

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
    src/CommonControlBindings.cpp src/UiTimer.cpp src/CompletionPopup.cpp src/SearchBox.cpp src/CommandPalette.cpp
    src/ValidationMessage.cpp src/NotificationCenter.cpp src/LogSink.cpp src/LogView.cpp
    src/SegmentedControl.cpp src/MultiSelectComboBox.cpp src/TagEditor.cpp src/RangeSlider.cpp
    src/SelectableLabel.cpp src/LinkLabel.cpp src/FontEditor.cpp src/FontPicker.cpp
    src/TemporalValue.cpp src/Calendar.cpp src/DateEdit.cpp src/TimeEdit.cpp src/DateTimeEdit.cpp
    src/UndoHistory.cpp src/WindowCloseRequest.cpp src/DocumentWorkspace.cpp src/DocumentSaveRequest.cpp src/DocumentCloseSession.cpp
    src/Theme.cpp src/ThemeController.cpp
    src/UiDocument.cpp src/WidgetRegistry.cpp
    src/PropertyValue.cpp src/PropertyModel.cpp src/PropertyGrid.cpp src/CollapsiblePanel.cpp
    src/UnitEditor.cpp src/VectorEditor.cpp src/ColorEditor.cpp src/ColorPicker.cpp
    src/FileDialog.cpp src/FilePathEditor.cpp src/FolderPicker.cpp src/StatusBar.cpp
    src/MainWindow.cpp src/WindowTitleBar.cpp src/WindowCommandButton.cpp src/NativeWindowChrome.cpp src/UiReparent.cpp
    src/DockWidget.cpp src/DockManager.cpp
    src/Image.cpp src/ImageDecoder.cpp src/Icon.cpp src/ImageView.cpp
    src/Style.cpp src/TextBuffer.cpp src/FontFace.cpp src/FontResource.cpp src/TextLayout.cpp src/TextShaper.cpp src/Renderer.cpp
    src/Widget.cpp src/UiContext.cpp src/Label.cpp src/Button.cpp src/TextBox.cpp src/Window.cpp
    src/CheckBox.cpp src/RadioButton.cpp src/RadioGroup.cpp src/ComboBox.cpp
    src/ProgressBar.cpp src/BusyIndicator.cpp src/Frame.cpp src/Separator.cpp src/GroupBox.cpp
    src/ScrollArea.cpp src/ScrollBar.cpp src/ScrollGeometry.cpp src/StackedWidget.cpp src/Splitter.cpp src/ToolTip.cpp src/UiToolTip.cpp
    src/SizeRule.cpp src/Layout.cpp src/ToolButton.cpp src/ToolBar.cpp src/TabWidget.cpp
    src/InputEvent.cpp src/Shortcut.cpp src/Action.cpp src/PopupSession.cpp src/UiInput.cpp
    src/SemanticTypes.cpp src/SemanticTree.cpp src/NativeAccessibility.cpp src/NativeAccessibilityText.cpp
    src/TextInputClient.cpp src/NativeTextInput.cpp
    src/PlatformTypes.cpp src/ClipboardData.cpp src/FileDialogSession.cpp src/PlatformServices.cpp src/MemoryPlatformServices.cpp
    src/NativePlatformServices.cpp src/WindowsSupport.cpp src/WindowsClipboard.cpp src/WindowsFileDialog.cpp
    src/Measure.cpp src/LayoutStrategy.cpp src/BoxLayout.cpp src/GridLayout.cpp src/FormLayout.cpp src/FlowLayout.cpp
    src/ConstraintExpression.cpp src/Constraint.cpp src/ConstraintItem.cpp src/ConstraintLayout.cpp src/NativeResizePump.cpp
    src/Popup.cpp src/MenuItem.cpp src/MenuBar.cpp src/DialogButtonBox.cpp src/Dialog.cpp
    src/MessageBox.cpp src/InputDialog.cpp src/InputDialogSession.cpp
    src/ContextMenu.cpp src/ScrollWidget.cpp src/ListWidget.cpp src/TreeWidget.cpp src/TableWidget.cpp src/OpenGLWidget.cpp
    src/FramePacer.cpp src/VisualStyle.cpp src/CssMotion.cpp src/EasingCurve.cpp src/StyleProperty.cpp src/Animation.cpp src/AnimationSystem.cpp
    src/CanvasScene.cpp src/CanvasGeometry.cpp src/CanvasWidget.cpp src/SketchDocument.cpp src/SketchWidget.cpp
    src/InputValidator.cpp src/NumericInput.cpp src/Slider.cpp src/SplitToolButton.cpp
    src/Painter.cpp src/PainterPath.cpp src/PaintWidget.cpp
    src/ThreadAffinity.cpp src/DispatchReceipt.cpp src/UiDispatcher.cpp src/UiHandle.cpp
    src/Task.cpp src/BackgroundTasks.cpp src/Application.cpp
    src/StateStore.cpp src/State.cpp src/Bindings.cpp src/Invalidation.cpp
    src/ModelValue.cpp src/ModelSnapshot.cpp src/RecordModel.cpp src/ModelBuilder.cpp src/Models.cpp
    src/ModelQuery.cpp src/ModelAdapter.cpp src/ModelEditSession.cpp src/AdapterModel.cpp src/SnapshotModelAdapter.cpp
    src/CellEditor.cpp src/TreeTable.cpp src/ItemDragDrop.cpp
    src/ItemModel.cpp src/SelectionModel.cpp src/SortFilterModel.cpp src/CollectionView.cpp src/ItemDelegate.cpp src/TreeRowMap.cpp
)
add_library(EStudioUI::EStudioUI ALIAS EStudioUI)
target_compile_features(EStudioUI PUBLIC cxx_std_20)
target_include_directories(EStudioUI PUBLIC include)
target_include_directories(EStudioUI SYSTEM PRIVATE "${ESTUDIOUI_KIWI_INCLUDE_DIR}")
target_include_directories(EStudioUI SYSTEM PRIVATE "${ESTUDIOUI_STB_INCLUDE_DIR}")
if(WIN32)
    if(MSVC)
        target_link_libraries(EStudioUI PRIVATE windowsapp runtimeobject)
    endif()
    target_link_libraries(EStudioUI PRIVATE dwmapi comctl32 ole32 oleaut32 shell32 uuid uiautomationcore imm32)
endif()
target_link_libraries(EStudioUI PRIVATE nlohmann_json::nlohmann_json EStudioUIGl glfw Freetype::Freetype harfbuzz::harfbuzz SheenBidi::SheenBidi libunibreak::libunibreak ${ESTUDIOUI_UTF8PROC_TARGET})
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

if(ESTUDIOUI_BUILD_DEMO OR ESTUDIOUI_BUILD_CREATOR OR ESTUDIOUI_BUILD_DATABASE_BROWSER OR ESTUDIOUI_BUILD_TESTS)
    include(cmake/ControlFonts.cmake)
endif()

if(ESTUDIOUI_BUILD_SQLITE OR ESTUDIOUI_BUILD_DATABASE_BROWSER)
    find_package(unofficial-sqlite3 CONFIG REQUIRED)
    add_library(EStudioUISqlite STATIC src/SqliteAdapter.cpp)
    add_library(EStudioUI::Sqlite ALIAS EStudioUISqlite)
    target_link_libraries(EStudioUISqlite PUBLIC EStudioUI::EStudioUI PRIVATE unofficial::sqlite3::sqlite3)
    if(MSVC)
        target_compile_options(EStudioUISqlite PRIVATE /utf-8 /W4 /permissive-)
    else()
        target_compile_options(EStudioUISqlite PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    endif()
endif()

if(ESTUDIOUI_BUILD_DATABASE_BROWSER)
    add_library(EStudioUIDatabaseApp STATIC browser/DatabaseBrowserWindow.cpp browser/ExampleDatabase.cpp)
    target_include_directories(EStudioUIDatabaseApp PUBLIC browser)
    target_link_libraries(EStudioUIDatabaseApp PUBLIC EStudioUI::Sqlite PRIVATE unofficial::sqlite3::sqlite3)
    add_executable(EStudioUIDatabaseBrowser browser/main.cpp browser/Verification.cpp demo/GpuProbeWindow.cpp)
    target_link_libraries(EStudioUIDatabaseBrowser PRIVATE EStudioUIDatabaseApp EStudioUIGl glfw)
    if(MSVC)
        target_compile_options(EStudioUIDatabaseApp PRIVATE /utf-8 /W4 /permissive-)
        target_compile_options(EStudioUIDatabaseBrowser PRIVATE /utf-8 /W4)
    else()
        target_compile_options(EStudioUIDatabaseApp PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
        target_compile_options(EStudioUIDatabaseBrowser PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    endif()
    add_custom_command(TARGET EStudioUIDatabaseBrowser POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:EStudioUIDatabaseBrowser>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${ESTUDIOUI_CONTROL_FONT_DIR}" "$<TARGET_FILE_DIR:EStudioUIDatabaseBrowser>/assets/fonts"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/NotoSans.ttf" "$<TARGET_FILE_DIR:EStudioUIDatabaseBrowser>/assets/NotoSans.ttf"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/licenses" "$<TARGET_FILE_DIR:EStudioUIDatabaseBrowser>/licenses"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md" "$<TARGET_FILE_DIR:EStudioUIDatabaseBrowser>/THIRD_PARTY_NOTICES.md"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" "$<TARGET_FILE_DIR:EStudioUIDatabaseBrowser>/licenses/EStudioUI-MIT.txt"
        VERBATIM)
    if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        foreach(port glfw3 freetype harfbuzz utf8proc kiwi sheenbidi libunibreak stb nlohmann-json sqlite3)
            set(copyright "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/${port}/copyright")
            if(EXISTS "${copyright}")
                add_custom_command(TARGET EStudioUIDatabaseBrowser POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy "${copyright}"
                    "$<TARGET_FILE_DIR:EStudioUIDatabaseBrowser>/licenses/${port}-vcpkg-copyright.txt" VERBATIM)
            endif()
        endforeach()
    endif()
endif()

if(ESTUDIOUI_BUILD_CREATOR OR ESTUDIOUI_BUILD_TESTS)
    add_library(EStudioUICreatorApp STATIC creator/CreatorWindow.cpp creator/DesignerSurface.cpp creator/WidgetPalette.cpp creator/DesignSnap.cpp)
    target_include_directories(EStudioUICreatorApp PUBLIC creator)
    target_link_libraries(EStudioUICreatorApp PUBLIC EStudioUI::EStudioUI)
    if(MSVC)
        target_compile_options(EStudioUICreatorApp PRIVATE /utf-8 /W4 /permissive-)
    else()
        target_compile_options(EStudioUICreatorApp PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    endif()
endif()

if(ESTUDIOUI_BUILD_CREATOR)
    add_executable(EStudioUICreator creator/main.cpp demo/GpuProbeWindow.cpp)
    target_link_libraries(EStudioUICreator PRIVATE EStudioUICreatorApp glfw)
    if(MSVC)
        target_compile_options(EStudioUICreator PRIVATE /utf-8 /W4)
    endif()
    add_custom_command(TARGET EStudioUICreator POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:EStudioUICreator>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/NotoSans.ttf" "$<TARGET_FILE_DIR:EStudioUICreator>/assets/NotoSans.ttf"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/licenses" "$<TARGET_FILE_DIR:EStudioUICreator>/licenses"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md" "$<TARGET_FILE_DIR:EStudioUICreator>/THIRD_PARTY_NOTICES.md"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" "$<TARGET_FILE_DIR:EStudioUICreator>/licenses/EStudioUI-MIT.txt"
        VERBATIM)
    if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        foreach(port glfw3 freetype harfbuzz utf8proc kiwi sheenbidi libunibreak stb nlohmann-json)
            set(copyright "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/${port}/copyright")
            if(EXISTS "${copyright}")
                add_custom_command(TARGET EStudioUICreator POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy "${copyright}"
                    "$<TARGET_FILE_DIR:EStudioUICreator>/licenses/${port}-vcpkg-copyright.txt" VERBATIM)
            endif()
        endforeach()
    endif()
endif()

if(ESTUDIOUI_BUILD_DEMO)
    add_executable(EStudioUISearchExample examples/SearchFeedback.cpp)
    target_link_libraries(EStudioUISearchExample PRIVATE EStudioUI::EStudioUI)
    add_executable(EStudioUIPickersExample examples/CommonPickers.cpp)
    target_link_libraries(EStudioUIPickersExample PRIVATE EStudioUI::EStudioUI)
    add_executable(EStudioUIModelAdapterExample examples/ModelAdapter.cpp)
    target_link_libraries(EStudioUIModelAdapterExample PRIVATE EStudioUI::EStudioUI)
    add_executable(EStudioUIPropertyExample examples/PropertyEditing.cpp)
    target_link_libraries(EStudioUIPropertyExample PRIVATE EStudioUI::EStudioUI)
    add_executable(EStudioUIRuntimeExample examples/RuntimeUi.cpp)
    target_link_libraries(EStudioUIRuntimeExample PRIVATE EStudioUI::EStudioUI)
    add_executable(EStudioUIDemo demo/main.cpp demo/WidgetGallery.cpp demo/DemoScene.cpp demo/AnimationGallery.cpp demo/RichCssGallery.cpp demo/GpuWorkload.cpp
        demo/SketchGallery.cpp demo/CanvasPrimitiveProbe.cpp demo/CanvasProbe.cpp demo/CanvasCurvesProbe.cpp
        demo/CustomWidgetGallery.cpp demo/PainterProbe.cpp demo/InputControlsProbe.cpp
        demo/WidgetStylesGallery.cpp demo/WidgetStylesProbe.cpp demo/ApplicationProbe.cpp demo/FoundationGallery.cpp
        demo/StateGallery.cpp demo/ModelGallery.cpp demo/LayoutGallery.cpp demo/ConstraintGallery.cpp demo/InputGallery.cpp demo/PlatformGallery.cpp
        demo/PlatformSemanticProbe.cpp demo/TextGallery.cpp demo/EssentialGallery.cpp demo/CommandGallery.cpp
        demo/GpuProbeWindow.cpp demo/RenderingProfile.cpp demo/ImageGallery.cpp demo/ScrollStackGallery.cpp demo/WorkspaceGallery.cpp demo/DocumentGallery.cpp demo/ProductivityGallery.cpp)
    target_link_libraries(EStudioUIDemo PRIVATE EStudioUI::EStudioUI EStudioUIGl glfw)
    target_compile_definitions(EStudioUIDemo PRIVATE ESTUDIOUI_DEMO_BUILD_CONFIGURATION="$<CONFIG>")
    add_executable(EStudioUIDocumentWorkspaceExample examples/DocumentWorkspace.cpp)
    target_link_libraries(EStudioUIDocumentWorkspaceExample PRIVATE EStudioUI::EStudioUI)
    add_executable(EStudioUIWorkspaceExample examples/ApplicationWorkspace.cpp)
    target_link_libraries(EStudioUIWorkspaceExample PRIVATE EStudioUI::EStudioUI)
    add_executable(EStudioUIConstraintExample examples/ConstraintLayout.cpp)
    target_link_libraries(EStudioUIConstraintExample PRIVATE EStudioUI::EStudioUI)
    add_executable(EStudioUIInputExample examples/InputActions.cpp)
    target_link_libraries(EStudioUIInputExample PRIVATE EStudioUI::EStudioUI)
    add_executable(EStudioUICommandExample examples/CommandDialogs.cpp)
    target_link_libraries(EStudioUICommandExample PRIVATE EStudioUI::EStudioUI)
    add_executable(EStudioUIControlsExample examples/CommonControls.cpp)
    target_link_libraries(EStudioUIControlsExample PRIVATE EStudioUI::EStudioUI)
    add_executable(EStudioUIScrollStackExample examples/ScrollStack.cpp)
    target_link_libraries(EStudioUIScrollStackExample PRIVATE EStudioUI::EStudioUI)
    add_executable(EStudioUIImageExample examples/ImageResources.cpp)
    target_link_libraries(EStudioUIImageExample PRIVATE EStudioUI::EStudioUI)
    add_executable(EStudioUITextExample examples/ControlText.cpp)
    target_link_libraries(EStudioUITextExample PRIVATE EStudioUI::EStudioUI)
    if(MSVC)
        target_compile_options(EStudioUITextExample PRIVATE /utf-8)
    endif()
    add_executable(EStudioUIPlatformExample examples/PlatformServices.cpp)
    target_link_libraries(EStudioUIPlatformExample PRIVATE EStudioUI::EStudioUI)
    if(MSVC)
        target_compile_options(EStudioUIDemo PRIVATE /utf-8)
    endif()
    add_custom_command(TARGET EStudioUIDemo POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${ESTUDIOUI_CONTROL_FONT_DIR}" "$<TARGET_FILE_DIR:EStudioUIDemo>/assets/fonts"
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
        foreach(port glfw3 freetype harfbuzz utf8proc kiwi sheenbidi libunibreak stb nlohmann-json)
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
    foreach(suite SearchFeedback CommonControls PropertyEditing UiDocument Theme UndoHistory DocumentWorkspace AdapterModel DataViews)
        add_executable(EStudioUI${suite}Tests tests/${suite}Tests.cpp)
        target_link_libraries(EStudioUI${suite}Tests PRIVATE EStudioUI::EStudioUI)
        target_compile_definitions(EStudioUI${suite}Tests PRIVATE ESTUDIOUI_FONT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/NotoSans.ttf")
        if(MSVC)
            target_compile_options(EStudioUI${suite}Tests PRIVATE /utf-8 /W4)
        endif()
        add_test(NAME ${suite} COMMAND EStudioUI${suite}Tests)
    endforeach()
    if(TARGET EStudioUISqlite)
        add_executable(EStudioUISqliteAdapterTests tests/SqliteAdapterTests.cpp)
        target_link_libraries(EStudioUISqliteAdapterTests PRIVATE EStudioUI::Sqlite unofficial::sqlite3::sqlite3)
        if(MSVC)
            target_compile_options(EStudioUISqliteAdapterTests PRIVATE /utf-8 /W4)
        endif()
        add_test(NAME SqliteAdapter COMMAND EStudioUISqliteAdapterTests)
    endif()
    if(ESTUDIOUI_GPU_TESTS AND TARGET EStudioUIDatabaseBrowser AND UNIX AND NOT APPLE)
        add_test(NAME DatabaseBrowserGpuIntegration COMMAND EStudioUIDatabaseBrowser --headless --self-test --output "${CMAKE_CURRENT_BINARY_DIR}/database-verification")
        set_tests_properties(DatabaseBrowserGpuIntegration PROPERTIES TIMEOUT 120)
    endif()
    if(TARGET EStudioUICreatorApp)
        add_executable(EStudioUIDesignSnapTests tests/DesignSnapTests.cpp)
        target_link_libraries(EStudioUIDesignSnapTests PRIVATE EStudioUICreatorApp)
        add_test(NAME DesignSnap COMMAND EStudioUIDesignSnapTests)
    endif()
    if(NOT CMAKE_CROSSCOMPILING AND TARGET EStudioUICreatorApp)
        add_executable(EStudioUICreatorGenerator tests/CreatorGenerator.cpp)
        target_link_libraries(EStudioUICreatorGenerator PRIVATE EStudioUICreatorApp)
        set(generated_ui "${CMAKE_CURRENT_BINARY_DIR}/generated/CreatorGenerated.cpp")
        set(generated_json "${CMAKE_CURRENT_BINARY_DIR}/generated/creator-catalog.json")
        set(generated_class "${CMAKE_CURRENT_BINARY_DIR}/generated/GeneratedComposite.cpp")
        set(generated_header "${CMAKE_CURRENT_BINARY_DIR}/generated/GeneratedComposite.h")
        add_custom_command(OUTPUT "${generated_ui}" "${generated_json}" "${generated_class}" "${generated_header}"
            COMMAND EStudioUICreatorGenerator "${generated_ui}" "${generated_json}"
            DEPENDS EStudioUICreatorGenerator VERBATIM)
        add_executable(EStudioUICreatorGeneratedTests tests/CreatorGeneratedTests.cpp "${generated_ui}" "${generated_class}")
        target_include_directories(EStudioUICreatorGeneratedTests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
        target_link_libraries(EStudioUICreatorGeneratedTests PRIVATE EStudioUI::EStudioUI)
        target_compile_definitions(EStudioUICreatorGeneratedTests PRIVATE
            ESTUDIOUI_FONT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/NotoSans.ttf"
            ESTUDIOUI_GENERATED_JSON="${generated_json}")
        if(MSVC)
            target_compile_options(EStudioUICreatorGenerator PRIVATE /utf-8 /W4)
            target_compile_options(EStudioUICreatorGeneratedTests PRIVATE /utf-8 /W4)
        endif()
        add_test(NAME CreatorGenerated COMMAND EStudioUICreatorGeneratedTests)
    endif()
    if(ESTUDIOUI_GPU_TESTS AND TARGET EStudioUICreator AND UNIX AND NOT APPLE)
        add_test(NAME CreatorGpuIntegration COMMAND EStudioUICreator --headless --self-test --output "${CMAKE_CURRENT_BINARY_DIR}/creator-verification")
        set_tests_properties(CreatorGpuIntegration PROPERTIES TIMEOUT 120)
    endif()
    add_executable(EStudioUIWorkspaceCoreTests tests/WorkspaceCoreTests.cpp)
    target_link_libraries(EStudioUIWorkspaceCoreTests PRIVATE EStudioUI::EStudioUI)
    target_compile_definitions(EStudioUIWorkspaceCoreTests PRIVATE ESTUDIOUI_FONT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/NotoSans.ttf")
    if(MSVC)
        target_compile_options(EStudioUIWorkspaceCoreTests PRIVATE /utf-8)
    endif()
    add_test(NAME WorkspaceCore COMMAND EStudioUIWorkspaceCoreTests)
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
                  State Binding Invalidation ControlUpdate Model ModelView Layout ConstraintLayout InputFoundation ActionDomain Platform Semantic TextInput FontText EssentialWidgets CommandDialogs Image ScrollStack)
        add_executable(EStudioUI${suite}Tests tests/${suite}Tests.cpp)
        target_link_libraries(EStudioUI${suite}Tests PRIVATE EStudioUI::EStudioUI)
        target_compile_definitions(EStudioUI${suite}Tests PRIVATE
            ESTUDIOUI_FONT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/NotoSans.ttf")
        if(MSVC)
            target_compile_options(EStudioUI${suite}Tests PRIVATE /utf-8)
        endif()
        add_test(NAME ${suite} COMMAND EStudioUI${suite}Tests)
    endforeach()

    target_compile_definitions(EStudioUIImageTests PRIVATE ESTUDIOUI_IMAGE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/images")

    target_link_libraries(EStudioUIFontTextTests PRIVATE harfbuzz::harfbuzz)
    target_compile_definitions(EStudioUIFontTextTests PRIVATE ESTUDIOUI_TEST_FONT_DIR="${ESTUDIOUI_CONTROL_FONT_DIR}")

    if(WIN32)
        add_executable(EStudioUINativeTextInputTests tests/NativeTextInputTests.cpp)
        target_include_directories(EStudioUINativeTextInputTests PRIVATE src)
        target_link_libraries(EStudioUINativeTextInputTests PRIVATE EStudioUI::EStudioUI imm32)
        target_compile_definitions(EStudioUINativeTextInputTests PRIVATE
            ESTUDIOUI_FONT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/NotoSans.ttf")
        if(MSVC)
            target_compile_options(EStudioUINativeTextInputTests PRIVATE /utf-8)
        endif()
        add_test(NAME NativeTextInput COMMAND EStudioUINativeTextInputTests)
        set_tests_properties(NativeTextInput PROPERTIES TIMEOUT 30 LABELS "Native;Input")
        add_executable(EStudioUINativePlatformTests tests/NativePlatformTests.cpp)
        target_compile_definitions(EStudioUINativePlatformTests PRIVATE ESTUDIOUI_TEST_FONT_DIR="${ESTUDIOUI_CONTROL_FONT_DIR}")
        target_include_directories(EStudioUINativePlatformTests PRIVATE src)
        target_link_libraries(EStudioUINativePlatformTests PRIVATE EStudioUI::EStudioUI uiautomationcore ole32 oleaut32 uuid)
        target_compile_definitions(EStudioUINativePlatformTests PRIVATE
            ESTUDIOUI_FONT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/NotoSans.ttf")
        if(MSVC)
            target_compile_options(EStudioUINativePlatformTests PRIVATE /utf-8)
        endif()
        add_test(NAME NativePlatform COMMAND EStudioUINativePlatformTests)
        set_tests_properties(NativePlatform PROPERTIES TIMEOUT 90)
        add_executable(EStudioUINativeChromeTests tests/NativeWindowChromeTests.cpp src/NativeWindowChrome.cpp src/NativeResizePump.cpp)
        target_include_directories(EStudioUINativeChromeTests PRIVATE src include)
        target_link_libraries(EStudioUINativeChromeTests PRIVATE comctl32 dwmapi shell32)
        if(MSVC)
            target_compile_options(EStudioUINativeChromeTests PRIVATE /utf-8 /W4)
        endif()
        add_test(NAME NativeChrome COMMAND EStudioUINativeChromeTests)
        set_tests_properties(NativeChrome PROPERTIES TIMEOUT 20 LABELS "Native;Window")
        add_executable(EStudioUINativeResizeTests tests/NativeResizeTests.cpp src/NativeResizePump.cpp)
        target_include_directories(EStudioUINativeResizeTests PRIVATE src)
        target_link_libraries(EStudioUINativeResizeTests PRIVATE comctl32)
        add_test(NAME NativeResize COMMAND EStudioUINativeResizeTests)
        set_tests_properties(NativeResize PROPERTIES TIMEOUT 20)
    endif()

    if(ESTUDIOUI_GPU_TESTS AND TARGET EStudioUIDemo AND UNIX AND NOT APPLE)
        add_test(NAME GpuIntegration COMMAND EStudioUIDemo --headless --self-test --output "${CMAKE_CURRENT_BINARY_DIR}/verification")
        set_tests_properties(GpuIntegration PROPERTIES TIMEOUT 120)
        add_test(NAME RenderingProfile COMMAND EStudioUIDemo --headless --profile-rendering
            --profile-frames 12 --profile-warmup 4 --profile-rate 0
            --output "${CMAKE_CURRENT_BINARY_DIR}/profile-smoke")
        set_tests_properties(RenderingProfile PROPERTIES TIMEOUT 120)
    endif()
endif()
