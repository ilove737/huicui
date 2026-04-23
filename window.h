#ifndef WINDOW_H
#define WINDOW_H

#include <QWidget>

QT_BEGIN_NAMESPACE
class QTabWidget;
QT_END_NAMESPACE
class GameWidget;
class TcpBwWidget;
class DJS103Widget;
class FFTWidget;

class Window : public QWidget
{
    Q_OBJECT

public:
    Window();

private:
    void createTcpBwTab();
    void createDJS103Tab();
    void createFFTTab();
    void createTicTacToeTab();

    QTabWidget *tabWidget;

    GameWidget *m_gameWidget;
    TcpBwWidget *m_tcpBwWidget;
    DJS103Widget *m_djs103Widget;
    FFTWidget *m_fftWidget;
};

#endif
