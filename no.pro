TARGET = no
TEMPLATE = app
CONFIG += c++11
#CONFIG += debug
CONFIG += release
QT += widgets
CXXFLAGS += -Wall
SOURCES += main.cpp
INCLUDEPATH += /home/m5/sw/utility/CurrentWork/include
QMAKE_POST_LINK = "./$$TARGET --test"
