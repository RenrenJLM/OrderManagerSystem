cmake_minimum_required(VERSION 3.19)
project(OrderManagerSystem LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Sql LinguistTools)

qt_standard_project_setup()

# ADDED: project documentation files
set(PROJECT_DOCS
    requirements.md
    plan.md
    AGENTS.md
    README.md
)

# ADDED: mark docs as non-compilable files, only for IDE display
set_source_files_properties(${PROJECT_DOCS} PROPERTIES HEADER_FILE_ONLY ON)

qt_add_executable(OrderManagerSystem
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    databasemanager.h databasemanager.cpp
    ${PROJECT_DOCS}
)

qt_add_translations(
    TARGETS OrderManagerSystem
    TS_FILES OrderManagerSystem_zh_CN.ts
)

target_link_libraries(OrderManagerSystem
    PRIVATE
        Qt::Core
        Qt::Sql
        Qt::Widgets
)

install(TARGETS OrderManagerSystem
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET OrderManagerSystem
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
