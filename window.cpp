#include <QtWidgets>

#include "window.h"
#include "fft.h"
#include "tictactoe.h"
#include "tcpbwwidget.h"
#include "djs103widget.h"

/**
 * @brief 窗口构造函数
 *
 * 初始化主窗口，创建各功能Tab页面
 */
Window::Window()
{
    // 创建Tab控件
    tabWidget = new QTabWidget;
    createDJS103Tab();       // 创建103机模拟器Tab
    createTicTacToeTab();    // 创建井字棋RL演示Tab
    createFFTTab();          // 创建FFT演示Tab
    createTcpBwTab();        // 创建TCP带宽测试Tab

    // 设置主布局
    QHBoxLayout *layout = new QHBoxLayout;
    layout->addWidget(tabWidget);
    setLayout(layout);

    setWindowTitle(tr("荟萃"));
    resize(1100, 900);
}

/**
 * @brief 创建TCP带宽测试Tab页面
 */
void Window::createTcpBwTab()
{
    m_tcpBwWidget = new TcpBwWidget;
    tabWidget->addTab(m_tcpBwWidget, tr("网络测试"));
}

/**
 * @brief 创建103机模拟器Tab页面
 */
void Window::createDJS103Tab()
{
    m_djs103Widget = new DJS103Widget;
    tabWidget->addTab(m_djs103Widget, tr("103机模拟器"));
}

/**
 * @brief 创建傅里叶变换Tab页面
 */
void Window::createFFTTab()
{
    m_fftWidget = new FFTWidget;
    tabWidget->addTab(m_fftWidget, tr("傅里叶变换"));
}

/**
 * @brief 创建井字棋强化学习Tab页面
 */
void Window::createTicTacToeTab()
{
    m_gameWidget = new GameWidget;
    tabWidget->addTab(m_gameWidget, tr("井字棋"));
}
