QT += core gui widgets network printsupport concurrent testlib
CONFIG += c++17
QT.testlib.CONFIG -= console
TEMPLATE = app
TARGET = EasySnip
VERSION = 0.1.0
DEFINES += NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00 ZX_USE_UTF8
QMAKE_CXXFLAGS += -Wall -Wextra
win32:LIBS += -luser32 -lgdi32 -ldwmapi -lole32 -loleaut32 -loleacc -luuid -lshell32 -ladvapi32
INCLUDEPATH += src third_party/zxing/core/src
SOURCES += src/main.cpp src/settings.cpp src/appearance.cpp src/native.cpp src/document.cpp src/controls.cpp \
           src/capture.cpp src/editor.cpp src/library.cpp src/application.cpp src/share.cpp src/accessibility.cpp src/selftest.cpp
HEADERS += src/settings.h src/appearance.h src/native.h src/document.h src/controls.h src/capture.h \
           src/editor.h src/library.h src/application.h src/uiautomation_compat.h
include(third_party/zxing.pri)
RC_FILE = resources/EasySnip.rc
RESOURCES += resources/theme.qrc
DESTDIR = $$OUT_PWD/bin
OBJECTS_DIR = $$OUT_PWD/obj
MOC_DIR = $$OUT_PWD/moc
RCC_DIR = $$OUT_PWD/rcc
