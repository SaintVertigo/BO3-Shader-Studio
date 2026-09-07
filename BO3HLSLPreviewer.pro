QT += widgets network
CONFIG += c++17 release windows
CONFIG -= debug debug_and_release
TEMPLATE = app
TARGET = BO3HLSLPreviewer

SOURCES += src/main.cpp \
           src/github_update.cpp \
           src/model_import.cpp \
           src/bo3_install_history.cpp \
           src/hlsl_preview_mode.cpp \
           src/preview_package_session.cpp \
           src/live_window_capture.cpp \
           src/bo3_package.cpp \
           src/bo3_package_adapter.cpp \
           src/bo3_postfx_adapter.cpp \
           src/bo3_material_adapter.cpp \
           src/bo3_sky_adapter.cpp \
           src/bo3_package_regression.cpp \
           src/bo3_package_validation.cpp \
           src/bo3_shader_reflection.cpp \
           src/bo3_techset.cpp \
           src/bo3_techset_writer.cpp \
           src/shadertoy_project.cpp \
           third_party/tinyexr/miniz.c

INCLUDEPATH += $$PWD/third_party/tinyexr

RESOURCES += resources/learning.qrc
HEADERS += src/github_update.h \
           src/bo3_package.h \
           src/model_import.h \
           src/bo3_install_history.h \
           src/bo3_package_adapter.h \
           src/hlsl_preview_mode.h \
           src/preview_package_session.h \
           src/live_window_capture.h \
           src/bo3_package_regression.h \
           src/bo3_package_validation.h \
           src/bo3_shader_reflection.h \
           src/bo3_techset.h \
           src/bo3_techset_writer.h \
           src/shadertoy_project.h \
           third_party/tinyexr/tinyexr.h \
           third_party/tinyexr/miniz.h

DEFINES += UNICODE _UNICODE
QMAKE_CXXFLAGS += /utf-8 /W3

# Automatic tester builds use sccache as a compiler launcher. The normal local
# build and deliberate full/stable CI builds are unchanged. SCCACHE_PATH is
# supplied by mozilla-actions/sccache-action on GitHub; falling back to the
# command name also keeps this usable on developer machines that have sccache
# on PATH.
contains(CONFIG, bo3_sccache) {
    BO3_SCCACHE = $$(SCCACHE_PATH)
    isEmpty(BO3_SCCACHE): BO3_SCCACHE = sccache
    QMAKE_CC = $$quote($$BO3_SCCACHE) $$QMAKE_CC
    QMAKE_CXX = $$quote($$BO3_SCCACHE) $$QMAKE_CXX
}

LIBS += d3d11.lib dxgi.lib d3dcompiler.lib windowscodecs.lib ole32.lib user32.lib windowsapp.lib

DESTDIR = $$PWD/dist
OBJECTS_DIR = $$PWD/build_qt/obj
MOC_DIR = $$PWD/build_qt/moc
RCC_DIR = $$PWD/build_qt/rcc
UI_DIR = $$PWD/build_qt/ui
