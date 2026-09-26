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
# Svg : les icônes du thème (vendor/lucide), rendues par QSvgRenderer.
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Svg)

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
    src/atelier/execution.cpp src/atelier/aide.cpp src/atelier/projet.cpp src/atelier/schema.cpp src/atelier/reecriture.cpp src/atelier/panneau.cpp src/atelier/lien.cpp src/atelier/vue_ecran.cpp src/atelier/onglet_ecrans.cpp src/atelier/theme.cpp)
target_link_libraries(grym-atelier PRIVATE grym_coeur Qt6::Widgets Qt6::Svg)
# La grammaire, embarquée : l'aide de l'atelier est toujours celle de sa version (docs/atelier.md, § 6.5).
qt_add_resources(grym-atelier "documentation" PREFIX "/documentation" BASE docs FILES docs/grammaire.md)
# Le thème (docs/atelier.md, § 5 ter) : jetons et modèle de feuille, polices (OFL 1.1), icônes (ISC et MIT), et leurs licences.
set(GRYM_THEME_FICHIERS src/atelier/theme/grymoir-jetons.json src/atelier/theme/grymoir.qss.modele)
set(GRYM_POLICES
    vendor/atkinson/AtkinsonHyperlegibleNext-Regular.ttf vendor/atkinson/AtkinsonHyperlegibleNext-Italic.ttf
    vendor/atkinson/AtkinsonHyperlegibleNext-SemiBold.ttf vendor/atkinson/AtkinsonHyperlegibleNext-Bold.ttf
    vendor/atkinson/AtkinsonHyperlegibleMono-Regular.ttf vendor/atkinson/AtkinsonHyperlegibleMono-Italic.ttf
    vendor/atkinson/AtkinsonHyperlegibleMono-SemiBold.ttf)
set(GRYM_ICONES vendor/lucide/chevron-down.svg vendor/lucide/check.svg)
set(GRYM_LICENCES vendor/atkinson/OFL-Next.txt vendor/atkinson/OFL-Mono.txt vendor/lucide/LICENSE)
function(grym_ressources_theme cible)
    qt_add_resources(${cible} "${cible}_theme" PREFIX "/theme" BASE src/atelier/theme FILES ${GRYM_THEME_FICHIERS})
    qt_add_resources(${cible} "${cible}_polices" PREFIX "/polices" BASE vendor/atkinson FILES ${GRYM_POLICES})
    qt_add_resources(${cible} "${cible}_icones" PREFIX "/icones" BASE vendor/lucide FILES ${GRYM_ICONES})
    qt_add_resources(${cible} "${cible}_licences" PREFIX "/licences" BASE vendor FILES ${GRYM_LICENCES})
endfunction()
grym_ressources_theme(grym-atelier)
set_target_properties(grym-atelier PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "GrymoiR"
    MACOSX_BUNDLE_GUI_IDENTIFIER "ch.grymoir.atelier")
if(NOT MSVC)
    target_compile_options(grym-atelier PRIVATE -Wall -Wextra -pedantic)
endif()

# Essais de l'atelier, sans fenêtre (QT_QPA_PLATFORM=offscreen) : ctest --test-dir construction
enable_testing()
add_executable(test_atelier tests/test_atelier.cpp src/atelier/coloration.cpp src/atelier/editeur.cpp
    src/atelier/execution.cpp src/atelier/aide.cpp src/atelier/projet.cpp src/atelier/schema.cpp src/atelier/reecriture.cpp src/atelier/panneau.cpp src/atelier/lien.cpp src/atelier/vue_ecran.cpp src/atelier/onglet_ecrans.cpp src/atelier/theme.cpp)
qt_add_resources(test_atelier "documentation_essais" PREFIX "/documentation" BASE docs FILES docs/grammaire.md)
target_include_directories(test_atelier PRIVATE src/atelier)
target_link_libraries(test_atelier PRIVATE grym_coeur Qt6::Widgets Qt6::Svg)
grym_ressources_theme(test_atelier)
add_test(NAME atelier COMMAND test_atelier)
set_tests_properties(atelier PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
