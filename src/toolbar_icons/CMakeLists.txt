# The toolbar icon set (doc/design/ICONSET-DESIGN.md section 4): the SVGs in
# src/gui/icons/ compiled in as resources, and the loader that colours and
# renders them with QSvgRenderer. Shared by jnext_gui (Main toolbar) and
# jnext_debugger (Debug toolbar), hence a library of its own. QtSvg is linked
# for QSvgRenderer only; the QIcon SVG icon-engine plugin is not used, so no
# plugin has to be deployed. JNEXT_QT_MAJOR (5 or 6) comes from the root.
find_package(Qt${JNEXT_QT_MAJOR} REQUIRED COMPONENTS Widgets Svg)

add_library(jnext_toolbar_icons STATIC
    toolbar_icons.cpp toolbar_icons.h toolbar_icons.qrc)
set_target_properties(jnext_toolbar_icons PROPERTIES AUTORCC ON)
target_include_directories(jnext_toolbar_icons PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(jnext_toolbar_icons PUBLIC
    Qt${JNEXT_QT_MAJOR}::Widgets
    Qt${JNEXT_QT_MAJOR}::Svg)
