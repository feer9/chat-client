TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += \
        main.c


win32:CONFIG += static
win32:QMAKE_LFLAGS += -static -static-libgcc -static-libstdc++

win32:LIBS += -lws2_32 -lwsock32

LIBS += -lpthread
