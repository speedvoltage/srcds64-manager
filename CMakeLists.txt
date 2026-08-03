cmake_minimum_required(VERSION 3.21)

project(srcds64-manager VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Network)

add_library(srcds64_core
    src/core/account.cpp
    src/core/account.h
    src/core/depot_downloader_manager.cpp
    src/core/depot_downloader_manager.h
    src/core/elf_inspector.cpp
    src/core/elf_inspector.h
    src/core/game_catalog.cpp
    src/core/game_catalog.h
    src/core/installer.cpp
    src/core/installer.h
    src/core/process_runner.cpp
    src/core/process_runner.h
    src/core/steamcmd_manager.cpp
    src/core/steamcmd_manager.h
    src/core/verifier.cpp
    src/core/verifier.h
    resources/resources.qrc
)

target_include_directories(srcds64_core PUBLIC src)
target_link_libraries(srcds64_core PUBLIC Qt6::Core Qt6::Network)
target_compile_definitions(srcds64_core PUBLIC SRCDS64_MANAGER_VERSION="${PROJECT_VERSION}")
target_compile_options(srcds64_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)

add_executable(srcds64 src/cli/main.cpp)
target_link_libraries(srcds64 PRIVATE srcds64_core)
target_compile_options(srcds64 PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)

include(GNUInstallDirs)
install(TARGETS srcds64 RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES README.md LICENSE THIRD_PARTY_NOTICES.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(PROGRAMS scripts/install-dependencies.sh DESTINATION ${CMAKE_INSTALL_DOCDIR})

enable_testing()
add_test(NAME srcds64_version COMMAND srcds64 --version)
set_tests_properties(srcds64_version PROPERTIES PASS_REGULAR_EXPRESSION "srcds64 1\\.0\\.0")
add_test(NAME srcds64_games COMMAND srcds64 games)
set_tests_properties(srcds64_games PROPERTIES PASS_REGULAR_EXPRESSION "hl2dm.*232370.*Half-Life 2: Deathmatch")
