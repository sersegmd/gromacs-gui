cmake_minimum_required(VERSION 3.16)

project(MolecularStudio
    VERSION 0.3.0
    DESCRIPTION "Molecular Studio - a unified desktop workspace for GROMACS"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets)
find_package(OpenGL REQUIRED)

set(MOLECULAR_STUDIO_SOURCES
    src/main.cpp
    src/app/MainWindow.cpp
    src/gui/ProjectPanel.cpp
    src/gui/MdpEditorPanel.cpp
    src/gui/JobMonitorPanel.cpp
    src/gui/ConsolePanel.cpp
    src/gui/ViewerWidget.cpp
    src/gui/AnalysisDialog.cpp
    src/gui/SimulationWizard.cpp
    src/gui/ResourceEstimatorDialog.cpp
    src/core/CoreBridge.cpp
    src/core/JobManager.cpp
)

set(MOLECULAR_STUDIO_HEADERS
    src/app/MainWindow.h
    src/gui/ProjectPanel.h
    src/gui/MdpEditorPanel.h
    src/gui/JobMonitorPanel.h
    src/gui/ConsolePanel.h
    src/gui/ViewerWidget.h
    src/gui/AnalysisDialog.h
    src/gui/SimulationWizard.h
    src/gui/ResourceEstimatorDialog.h
    src/gui/MdpTemplates.h
    src/core/CoreBridge.h
    src/core/JobManager.h
    src/core/ErrorHints.h
)

add_executable(molecular-studio
    ${MOLECULAR_STUDIO_SOURCES}
    ${MOLECULAR_STUDIO_HEADERS}
)

target_include_directories(molecular-studio PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(molecular-studio PRIVATE
    Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
    OpenGL::GL
)

target_compile_definitions(molecular-studio PRIVATE
    MS_VERSION="${PROJECT_VERSION}"
    # When the Rust core library is built, point MS_CORE_LIB at its path
    # (e.g. -DMS_CORE_LIB="/path/to/libmolecular_core.so") to enable live
    # bindings. See core/rust/molecular_core.
    $<$<BOOL:${MS_CORE_LIB}>:MS_CORE_LIB="${MS_CORE_LIB}">
)

install(TARGETS molecular-studio
    RUNTIME DESTINATION bin
)
