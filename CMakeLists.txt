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

# Le cœur, tel que le Makefile le compile (variable ANALYSEUR) : l'atelier l'appelle directement.
add_library(grym_coeur STATIC
    src/analyseur.c src/compact.c src/arbre.c src/imprimeur.c src/decimal.c
    src/lexeur.c src/date.c src/texte.c)
target_include_directories(grym_coeur PUBLIC src)
if(NOT MSVC)
    target_compile_options(grym_coeur PRIVATE -Wall -Wextra -pedantic)
endif()

add_executable(grym-atelier MACOSX_BUNDLE WIN32
    src/atelier/principal.cpp src/atelier/fenetre.cpp src/atelier/editeur.cpp src/atelier/coloration.cpp)
target_link_libraries(grym-atelier PRIVATE grym_coeur Qt6::Widgets)
set_target_properties(grym-atelier PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "GrymoiR"
    MACOSX_BUNDLE_GUI_IDENTIFIER "ch.grymoir.atelier")
if(NOT MSVC)
    target_compile_options(grym-atelier PRIVATE -Wall -Wextra -pedantic)
endif()

# Essais de l'atelier, sans fenêtre (QT_QPA_PLATFORM=offscreen) : ctest --test-dir construction
enable_testing()
add_executable(test_atelier tests/test_atelier.cpp src/atelier/coloration.cpp src/atelier/editeur.cpp)
target_include_directories(test_atelier PRIVATE src/atelier)
target_link_libraries(test_atelier PRIVATE grym_coeur Qt6::Widgets)
add_test(NAME atelier COMMAND test_atelier)
set_tests_properties(atelier PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
