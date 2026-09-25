# GrymoiR : construction de l'atelier (docs/atelier.md, § 4).
# Le cœur et grym se construisent avec le Makefile, sans dépendance ; seule la partie Qt passe par CMake.
#   cmake -B construction
#   cmake --build construction
cmake_minimum_required(VERSION 3.16)
project(grymoir_atelier LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Qt 6.4 au moins : la version des distributions Linux courantes (Ubuntu 24.04, Debian 12).
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)

# SQLite embarqué, avec les réglages du Makefile (vendor/sqlite, voir PROVENANCE.md).
add_library(grym_sqlite STATIC vendor/sqlite/sqlite3.c)
target_include_directories(grym_sqlite PUBLIC vendor/sqlite)
target_compile_definitions(grym_sqlite PRIVATE SQLITE_THREADSAFE=0 SQLITE_OMIT_LOAD_EXTENSION SQLITE_DQS=0
    SQLITE_DEFAULT_FOREIGN_KEYS=1 SQLITE_OMIT_DEPRECATED SQLITE_DEFAULT_MEMSTATUS=0)
if(UNIX AND NOT APPLE)
    target_link_libraries(grym_sqlite PUBLIC m)
endif()

# Le cœur, tel que le Makefile le compile (variables ANALYSEUR et EXECUTION) : l'atelier l'appelle directement.
add_library(grym_coeur STATIC
    src/chemins.c src/analyseur.c src/compact.c src/arbre.c src/imprimeur.c src/decimal.c
    src/lexeur.c src/date.c src/texte.c
    src/compilateur.c src/vm.c src/console.c src/base.c src/bytecode.c)
target_include_directories(grym_coeur PUBLIC src)
target_link_libraries(grym_coeur PUBLIC grym_sqlite)
if(NOT MSVC)
    target_compile_options(grym_coeur PRIVATE -Wall -Wextra -pedantic $<$<COMPILE_LANGUAGE:C>:-Werror=implicit-function-declaration>)
endif()

add_executable(grym-atelier MACOSX_BUNDLE WIN32
    src/atelier/principal.cpp src/atelier/fenetre.cpp src/atelier/editeur.cpp src/atelier/coloration.cpp
    src/atelier/execution.cpp src/atelier/aide.cpp)
target_link_libraries(grym-atelier PRIVATE grym_coeur Qt6::Widgets)
# La grammaire, embarquée : l'aide de l'atelier est toujours celle de sa version (docs/atelier.md, § 6.5).
qt_add_resources(grym-atelier "documentation" PREFIX "/documentation" BASE docs FILES docs/grammaire.md)
set_target_properties(grym-atelier PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "GrymoiR"
    MACOSX_BUNDLE_GUI_IDENTIFIER "ch.grymoir.atelier")
if(NOT MSVC)
    target_compile_options(grym-atelier PRIVATE -Wall -Wextra -pedantic)
endif()

# Essais de l'atelier, sans fenêtre (QT_QPA_PLATFORM=offscreen) : ctest --test-dir construction
enable_testing()
add_executable(test_atelier tests/test_atelier.cpp src/atelier/coloration.cpp src/atelier/editeur.cpp
    src/atelier/execution.cpp src/atelier/aide.cpp)
qt_add_resources(test_atelier "documentation_essais" PREFIX "/documentation" BASE docs FILES docs/grammaire.md)
target_include_directories(test_atelier PRIVATE src/atelier)
target_link_libraries(test_atelier PRIVATE grym_coeur Qt6::Widgets)
add_test(NAME atelier COMMAND test_atelier)
set_tests_properties(atelier PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
