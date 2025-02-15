#-------------------------------------------------
#   Project : Yet Another OpenSSL GUI
#   Author : Patrick Proy
#   Modifications : William Shine, 2025
#   Copyright (C) 2018-2020
#   Copyright (C) 2025, William Shine
#
#   Licence : http://www.gnu.org/licenses/gpl.txt
#
#-------------------------------------------------

QT       += core gui network widgets
QMAKE_MACOSX_DEPLOYMENT_TARGET = 15
TARGET = YetAnotherOpensslGui
TEMPLATE = app

SOURCES +=  src/app/main.cpp\
            #src/app/cx509extensions.cpp \
            src/app/dialogx509extensions.cpp \
            src/app/sslmainwindow.cpp \
            src/app/sslcertificates.cpp \
            src/app/dialoggeneratekey.cpp \
            src/app/dialogsslerrors.cpp \
            src/app/dialogcertdate.cpp \
            src/app/dialogx509v3extention.cpp \
            src/app/cdialogpkcs12.cpp \
            src/app/stackwindow.cpp

HEADERS  += src/app/sslmainwindow.h \
            #src/app/cx509extensions.h \
            src/app/dialogx509extensions.h \
            src/app/sslcertificates.h \
            src/app/dialoggeneratekey.h \
            src/app/dialogsslerrors.h \
            src/app/dialogcertdate.h \
            src/app/dialogx509v3extention.h \
            src/app/cdialogpkcs12.h \
            src/app/stackwindow.h

FORMS    += src/app/sslmainwindow.ui \
            src/app/dialoggeneratekey.ui \
            src/app/dialogsslerrors.ui \
            src/app/dialogcertdate.ui \
            src/app/dialogx509extensions.ui \
            src/app/dialogx509v3extention.ui \
            src/app/cdialogpkcs12.ui \
            src/app/stackwindow.ui

LIBS += -L"/opt/homebrew/lib"
LIBS += /opt/homebrew/lib/libssl.a  /opt/homebrew/lib/libcrypto.a 

INCLUDEPATH += "/opt/homebrew/include"

RESOURCES += \
    src/app/ressources.qrc
