TEMPLATE = app
CONFIG += debug_and_release
CONFIG += c++11

build_pass:CONFIG(debug, debug|release) {
TARGET = nodbg
DEFINES = IS_DEBUG_BUILD
} else {
TARGET = no
DEFINES = IS_RELEASE_BUILD
DEFINES += QT_NO_DEBUG_OUTPUT
}

QT += widgets
CXXFLAGS += -Wall
SOURCES += main.cpp
INCLUDEPATH += /home/m5/sw/utility/CurrentWork/include
QMAKE_POST_LINK = "./$$TARGET --test"
