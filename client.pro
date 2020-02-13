TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += \
        main.c

HEADERS += \
        client.h


win32:CONFIG += static
win32-msvc:QMAKE_CFLAGS += /utf-8
win32-gcc:QMAKE_LFLAGS += -static -static-libgcc -static-libstdc++
win32-msvc:DEFINES += _CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS

win32:LIBS += -lws2_32 -lwsock32
win32:LIBS += "../chat-client/resources/icon.res" "../chat-client/resources/versioninfo.res"

unix:LIBS += -lpthread
