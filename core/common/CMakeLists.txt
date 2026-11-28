# cloudmus-common: the few pieces that need Qt Core/Gui alone — the language
# (I18n), the About data, the cover art (reading, fitting, the aura) and QR
# codes (Qr). Linked by cloudmus-core, and by the mobile front's UI process on
# its own, which has none of the core.
# Expects from the including project: C++20, AUTOMOC, the Qt6 Core and Gui
# targets.
add_library(cloudmus-common STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/I18n/Languages.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/I18n/Languages.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/I18n/Translator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/I18n/Translator.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/Covers/CoverPicture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/Covers/CoverPicture.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/Covers/MeshAura.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/Covers/MeshAura.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/About/People.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/Qr/QrPicture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/Qr/QrPicture.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/qrcodegen/qrcodegen.cpp
)
target_include_directories(cloudmus-common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/I18n
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/Covers
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/About
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/Qr
)
target_include_directories(cloudmus-common PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/qrcodegen)
target_link_libraries(cloudmus-common PUBLIC Qt6::Core Qt6::Gui)
set_target_properties(cloudmus-common PROPERTIES POSITION_INDEPENDENT_CODE ON)

