QT += core gui websockets
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17 qtquickcompiler no_keywords
TARGET = webrtc-demo



clang {
    message("* Using compiler: clang.")
    QMAKE_CXXFLAGS += -Wthread-safety
    QMAKE_CXXFLAGS += -Wno-unused-parameter
    message($$QMAKE_CXXFLAGS)
    QMAKE_CXXFLAGS_WARN_ON += -Wno-unused-parameter -Wnounused-lambda-capture
    QMAKE_CXXFLAGS_WARN_ON = ""
#    message($$QMAKE_CXXFLAGS_WARN_ON)

}
# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0


WEBRTC_SRC = /Users/heyu/code/webrtc/src
message($$WEBRTC_SRC)
LIBWEBSOCET = /Users/heyu/code/libwebsockets

#RESOURCES += \
#    src/ui/qml.qrc

FORMS += \
    src/ui/DemoPusher.ui \
    src/ui/devicesetting.ui

HEADERS += \
    include/IDeviceManager.h \
    include/IRTCLivePusher.h \
    src/ui/DemoPusher.h \
    src/ui/devicesetting.h \
    src/RTCLivePusher/CapturerTrackSource.h \
    src/RTCLivePusher/DeviceManager.h \
    src/RTCLivePusher/ExternalVideoFrameTrackSource.h \
    src/RTCLivePusher/MediaStream.h \
    src/RTCLivePusher/CapturerTrackSource.h \
    src/RTCLivePusher/CapturerTrackSource.h \
    src/RTCLivePusher/CapturerTrackSource.h \
    src/RTCLivePusher/RTCLivePusher.h \
    src/RTCLivePusher/RTCVideoSink.h \
    src/RTCLivePusher/RtcLogWrite.h \
    src/RTCLivePusher/SDLRenderer.h \
    src/RTCLivePusher/VcmCapturer.h \
    src/RTCLivePusher/VideoObserver.h \
    src/RTCLivePusher/VideoRenderer.h \
    src/RTCLivePusher/WebrtcBase.h \
    src/RTCLivePusher/peerconnectionStream.h \
    src/RTCLivePusher/websocketConnect.h

SOURCES += \
        src/RTCLivePusher/DeviceManager.cpp \
        src/RTCLivePusher/MediaStream.cpp \
        src/RTCLivePusher/RTCLivePusher.cpp \
        src/RTCLivePusher/RtcLogWrite.cpp \
        src/RTCLivePusher/RtcVideoSink.cpp \
        src/RTCLivePusher/SDLRenderer.cpp \
        src/RTCLivePusher/VideoObserver.cpp \
        src/RTCLivePusher/VideoRenderer.cpp \
        src/RTCLivePusher/peerconnectionStream.cpp \
        src/RTCLivePusher/websocketConnect.cpp \
        src/ui/main.cpp \
        src/ui/devicesetting.cpp \
        src/ui/DemoPusher.cpp


# Additional import path used to resolve QML modules in Qt Creator's code model
QML_IMPORT_PATH =

# Additional import path used to resolve QML modules just for Qt Quick Designer
QML_DESIGNER_IMPORT_PATH =


INCLUDEPATH += \
    $$LIBWEBSOCET/build/include \
    include \
    src \
    3rdparty \
    3rdparty/WebRTC/include \
    3rdparty/util \
    3rdparty/SDL2/include \
    3rdparty/openssl/include \
    3rdparty/TaskQueue \
    3rdparty/WebRTC/include/third_party \
    3rdparty/WebRTC/include/third_party/abseil-cpp \
    3rdparty/WebRTC/include/third_party/libyuv/include

LIBS += -L$$WEBRTC_SRC/out/mac/obj -lwebrtc
LIBS += -L$$LIBWEBSOCET/build/lib/ -lwebsockets
DEFINES += WEBRTC_MAC WEBRTC_POSIX WEBRTC_UNIX

qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
