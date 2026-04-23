/****************************************************************************
**
** Copyright (C) 2024
** TCP bandwidth test widget - self-contained implementation
**
****************************************************************************/

#ifndef TCPBWWIDGET_H
#define TCPBWWIDGET_H

#include <QWidget>
#include <QThread>
#include <QVector>
#include <QAtomicInt>

QT_BEGIN_NAMESPACE
class QComboBox;
class QSpinBox;
class QLineEdit;
class QPushButton;
class QTextEdit;
class QLabel;
class QCheckBox;
QT_END_NAMESPACE

class BandwidthChart;

/**
 * @brief TCP带宽测试工作线程
 *
 * 支持服务端监听、客户端发送和本机回环三种模式。
 * 客户端：连接服务端，持续发送数据，每秒统计带宽。
 * 服务端：监听端口，接收数据，每秒统计带宽。
 * 使用自定义握手协议，可与其他运行本程序的实例互连。
 */
class TcpBwWorker : public QThread
{
    Q_OBJECT

public:
    enum Mode { Client = 0, Server = 1, Loopback = 2 };
    enum Direction { Upload = 0, Download = 1 };

    TcpBwWorker(QObject *parent = nullptr);

    void setMode(Mode mode) { m_mode = mode; }
    void setHost(const QString &host) { m_host = host; }
    void setPort(int port) { m_port = port; }
    void setDuration(int seconds) { m_duration = seconds; }
    void setDirection(Direction dir) { m_direction = dir; }
    void stop() { m_stop = 1; }

signals:
    void intervalResult(int elapsedSec, double bandwidthMbps, qint64 bytesTransferred);
    void testFinished(bool success, const QString &message);
    void logMessage(const QString &text);

protected:
    void run() override;

private:
    void runServer();
    void runClient();

    Mode m_mode;
    QString m_host;
    int m_port;
    int m_duration;
    Direction m_direction;
    QAtomicInt m_stop;
};

/**
 * @brief TCP带宽测试主控件
 */
class TcpBwWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TcpBwWidget(QWidget *parent = nullptr);

private slots:
    void startTest();
    void stopTest();
    void onModeChanged(int index);
    void onIntervalResult(int elapsedSec, double bandwidthMbps, qint64 bytesTransferred);
    void onTestFinished(bool success, const QString &message);
    void onLogMessage(const QString &text);

private:
    void createUI();
    void updateButtonState(bool running);
    void appendColoredText(const QString &text, const QColor &color);

    // Controls
    QComboBox *m_modeCombo;
    QLineEdit *m_hostEdit;
    QSpinBox *m_portSpinBox;
    QSpinBox *m_durationSpinBox;
    QComboBox *m_directionCombo;
    QPushButton *m_startButton;
    QPushButton *m_stopButton;

    // Output
    QTextEdit *m_outputEdit;
    BandwidthChart *m_chart;

    // Worker
    TcpBwWorker *m_worker;
    TcpBwWorker *m_serverWorker;  // Used in loopback mode
};

/**
 * @brief 实时带宽折线图控件
 *
 * X轴为时间（秒），Y轴为带宽（Mbps）
 */
class BandwidthChart : public QWidget
{
    Q_OBJECT

public:
    explicit BandwidthChart(QWidget *parent = nullptr);

    void addDataPoint(int timeSec, double bandwidthMbps);
    void clearData();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<QPair<int, double>> m_data;  // (time, bandwidth)
    double m_maxBandwidth;
};

#endif // TCPBWWIDGET_H
