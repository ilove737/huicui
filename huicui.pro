QT += widgets network
requires(qtConfig(combobox))
TARGET = huicui

HEADERS     = window.h \
              tictactoe.h \
              fft.h \
              tcpbwwidget.h \
              djs103_emulator.h \
              djs103widget.h
SOURCES     = main.cpp \
              window.cpp \
              tictactoe.cpp \
              fft.cpp \
              tcpbwwidget.cpp \
              djs103_emulator.cpp \
              djs103widget.cpp

# install
target.path = $$[QT_INSTALL_EXAMPLES]/widgets/widgets/huicui
INSTALLS += target
