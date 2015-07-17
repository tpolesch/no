
TARGET = no
CONFIG += c++11
CONFIG += debug
QT += widgets
TEMPLATE = app
CXXFLAGS += -Wall
HEADERS += MainWindow.h
SOURCES += main.cpp
INCLUDEPATH += /home/m5/sw/utility/CurrentWork/include
QMAKE_POST_LINK = "./$$TARGET --test"
message("Start unit tests:")
