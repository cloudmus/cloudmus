# cloudmus-common: the few pieces that need Qt Core/Gui alone — the language
# (I18n), the About data and the cover aura. Linked by cloudmus-core, and by
# the mobile front's UI process on its own, which has none of the core.
# Expects from the including project: C++20, AUTOMOC, the Qt6 Core and Gui
# targets.
add_library(cloudmus-common STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/I18n/Languages.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/I18n/Languages.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/I18n/Translator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/I18n/Translator.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/Covers/MeshAura.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/Covers/MeshAura.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/About/People.h
)
target_include_directories(cloudmus-common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/I18n
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/Covers
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/About
)
target_link_libraries(cloudmus-common PUBLIC Qt6::Core Qt6::Gui)
set_target_properties(cloudmus-common PROPERTIES POSITION_INDEPENDENT_CODE ON)

