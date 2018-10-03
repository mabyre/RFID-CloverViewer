#-------------------------------------------------
#
# Project created by QtCreator 2014-10-08T11:37:09
#
#-------------------------------------------------

QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = CloverViewer
TEMPLATE = app

DEFINES += QT_COMPILATION

SOURCES += main.cpp\
    cloverviewer.cpp \
    readerslist.cpp \
    Component/src/csl_component.c \
    UserControl/slineeditautocomplete.cpp

HEADERS  += cloverviewer.h \
    ASTrace/ASTrace.h \
    ASTrace/pmTrace.h \
    PMLite/inc/pmEnv.h \
    PMLite/inc/pmInterface.h \
    PMLite/inc/pmTrace.h \
    readerslist.h \
    Component/inc/cl_readers.h \
    Component/inc/csl_component.h \
    UserControl/slineeditautocomplete.h

FORMS    += cloverviewer.ui

INCLUDEPATH += $$PWD/Component/inc/

#------------- CSLLib

unix|win32: LIBS += -L$$PWD/../../../../Eclipse/WorkspaceCSL/CSLLib/Debug/ -lCSLLib

win32:CONFIG(release, debug|release): LIBS += -L$$PWD/../../../../Eclipse/WorkspaceCSL/CSLLib/release/ -lCSLLib
else:win32:CONFIG(debug, debug|release): LIBS += -L$$PWD/../../../../Eclipse/WorkspaceCSL/CSLLib/debug/ -lCSLLib

INCLUDEPATH += $$PWD/../../../../Eclipse/WorkspaceCSL/CSLLib
DEPENDPATH += $$PWD/../../../../Eclipse/WorkspaceCSL/CSLLib

#------------- PMLite

INCLUDEPATH += $$PWD/PMLite/inc/

unix|win32: LIBS += -L$$PWD/../../../../Eclipse/WorkspaceCSL/PMLite/Debug -lCSLLib

win32:CONFIG(release, debug|release): LIBS += -L$$PWD/../../../../Eclipse/WorkspaceCSL/PMLite/release/ -lPMLite
else:win32:CONFIG(debug, debug|release): LIBS += -L$$PWD/../../../../Eclipse/WorkspaceCSL/PMLite/debug/ -lPMLite

INCLUDEPATH += $$PWD/../../../../Eclipse/WorkspaceCSL/PMLite/inc
DEPENDPATH += $$PWD/../../../../Eclipse/WorkspaceCSL/PMLite/Debug

#---------------------------

unix|win32: LIBS += -lpthread
unix|win32: LIBS += -lws2_32
unix|win32: LIBS += -lDbgHelp

LIBS += -L$$PWD/ASTrace/ -lASTrace
LIBS += -L$$PWD/ASTrace/ASTrace.dll

INCLUDEPATH += $$PWD/ASTrace/inc/

unix|win32:RC_FILE = CloverViewer.rc

DISTFILES += \
    ASTrace/ASTrace.dll \
    ASTrace/ASTrace.lib

RESOURCES += \
    cloverviewer.qrc


