/****************************************************************************
**
** Copyright (C) 2024
** TCP bandwidth test widget - self-contained implementation
**
** Protocol:
**   Client -> Server handshake:
**     1. Client connects to Server
**     2. Client sends 8 bytes: magic "BWTEST01"
**     3. Client sends 4 bytes: test duration (big-endian uint32)
**     4. Client sends 1 byte: direction (0=upload, 1=download)
**     5. Server validates magic, reads params
**     6. If download mode: Server sends data to Client
**        If upload mode: Client sends data to Server
**     7. After duration expires, sender closes connection
**
****************************************************************************/

#include <QtWidgets>
#include <QTcpSocket>
#include <QTcpServer>
#include <QElapsedTimer>
#include <QHostAddress>

#include "tcpbwwidget.h"

// Qt 5.14 引入 loadRelaxed()，旧版本用 load()
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
#define ATOMIC_LOAD(x) (x).loadRelaxed()
#else
#define ATOMIC_LOAD(x) (x).load()
#endif

// ============ BandwidthChart 实现 ============

BandwidthChart::BandwidthChart(QWidget *parent)
    : QWidget(parent)
    , m_maxBandwidth(0)
{
    setMinimumSize(400, 200);
}

void BandwidthChart::addDataPoint(int timeSec, double bandwidthMbps)
{
    m_data.append(qMakePair(timeSec, bandwidthMbps));
    if (bandwidthMbps > m_maxBandwidth)
        m_maxBandwidth = bandwidthMbps;
    update();
}

void BandwidthChart::clearData()
{
    m_data.clear();
    m_maxBandwidth = 0;
    update();
}

void BandwidthChart::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const int marginLeft = 70;
    const int marginRight = 20;
    const int marginTop = 30;
    const int marginBottom = 40;

    QRect plotArea(marginLeft, marginTop,
                   width() - marginLeft - marginRight,
                   height() - marginTop - marginBottom);

    painter.fillRect(rect(), Qt::white);
    painter.setPen(QPen(Qt::darkGray, 1));
    painter.drawRect(plotArea);

    painter.setPen(Qt::black);
    QFont titleFont = painter.font();
    titleFont.setBold(true);
    titleFont.setPointSize(10);
    painter.setFont(titleFont);
    painter.drawText(QRect(0, 2, width(), marginTop - 2),
                     Qt::AlignCenter, tr("实时带宽"));

    if (m_data.isEmpty()) {
        painter.setPen(Qt::gray);
        QFont normalFont;
        normalFont.setPointSize(9);
        painter.setFont(normalFont);
        painter.drawText(plotArea, Qt::AlignCenter, tr("等待测试数据..."));
        return;
    }

    int minTime = m_data.first().first;
    int maxTime = m_data.last().first;
    if (maxTime <= minTime)
        maxTime = minTime + 1;

    double maxBw = m_maxBandwidth * 1.15;
    if (maxBw <= 0)
        maxBw = 1.0;

    // Grid
    painter.setPen(QPen(QColor(200, 200, 200), 1, Qt::DashLine));
    QFont smallFont;
    smallFont.setPointSize(7);
    painter.setFont(smallFont);

    int yTicks = 5;
    for (int i = 0; i <= yTicks; ++i) {
        int y = plotArea.bottom() - (i * plotArea.height()) / yTicks;
        painter.drawLine(plotArea.left(), y, plotArea.right(), y);
        double val = (maxBw * i) / yTicks;
        painter.setPen(Qt::darkGray);
        painter.drawText(QRect(0, y - 10, plotArea.left() - 5, 20),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(val, 'f', 1));
        painter.setPen(QPen(QColor(200, 200, 200), 1, Qt::DashLine));
    }

    int xTicks = qMin(10, maxTime - minTime);
    if (xTicks < 1) xTicks = 1;
    for (int i = 0; i <= xTicks; ++i) {
        int x = plotArea.left() + (i * plotArea.width()) / xTicks;
        painter.drawLine(x, plotArea.top(), x, plotArea.bottom());
        int t = minTime + (i * (maxTime - minTime)) / xTicks;
        painter.setPen(Qt::darkGray);
        painter.drawText(QRect(x - 25, plotArea.bottom() + 3, 50, 20),
                         Qt::AlignCenter, QString::number(t) + tr("s"));
        painter.setPen(QPen(QColor(200, 200, 200), 1, Qt::DashLine));
    }

    painter.setPen(Qt::black);
    QFont labelFont;
    labelFont.setPointSize(8);
    painter.setFont(labelFont);
    painter.drawText(QRect(plotArea.left(), plotArea.bottom() + 22,
                           plotArea.width(), 20),
                     Qt::AlignCenter, tr("时间 (秒)"));

    // Filled area
    QPainterPath fillPath;
    fillPath.moveTo(plotArea.left(), plotArea.bottom());
    for (int i = 0; i < m_data.size(); ++i) {
        double x = plotArea.left() +
                   ((double)(m_data[i].first - minTime) / (maxTime - minTime)) * plotArea.width();
        double y = plotArea.bottom() -
                   (m_data[i].second / maxBw) * plotArea.height();
        fillPath.lineTo(x, y);
    }
    fillPath.lineTo(plotArea.left() +
                    ((double)(m_data.last().first - minTime) / (maxTime - minTime)) * plotArea.width(),
                    plotArea.bottom());
    fillPath.closeSubpath();
    painter.fillPath(fillPath, QColor(30, 90, 200, 30));

    // Curve
    QPainterPath curvePath;
    bool first = true;
    for (int i = 0; i < m_data.size(); ++i) {
        double x = plotArea.left() +
                   ((double)(m_data[i].first - minTime) / (maxTime - minTime)) * plotArea.width();
        double y = plotArea.bottom() -
                   (m_data[i].second / maxBw) * plotArea.height();
        if (first) {
            curvePath.moveTo(x, y);
            first = false;
        } else {
            curvePath.lineTo(x, y);
        }
    }
    painter.setPen(QPen(QColor(30, 90, 200), 1.5));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(curvePath);

    // Data points
    for (int i = 0; i < m_data.size(); ++i) {
        double x = plotArea.left() +
                   ((double)(m_data[i].first - minTime) / (maxTime - minTime)) * plotArea.width();
        double y = plotArea.bottom() -
                   (m_data[i].second / maxBw) * plotArea.height();
        painter.setBrush(QColor(30, 90, 200));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(x, y), 3, 3);
    }
}

// ============ TcpBwWorker 实现 ============

static const char s_magic[] = "BWTEST01";

TcpBwWorker::TcpBwWorker(QObject *parent)
    : QThread(parent)
    , m_mode(Client)
    , m_port(52737)
    , m_duration(10)
    , m_direction(Upload)
    , m_stop(0)
{
}

void TcpBwWorker::run()
{
    if (m_mode == Server)
        runServer();
    else
        runClient();
}

void TcpBwWorker::runServer()
{
    QTcpServer server;
    if (!server.listen(QHostAddress::Any, m_port)) {
        emit testFinished(false, tr("服务端监听失败: %1").arg(server.errorString()));
        return;
    }

    emit logMessage(tr("服务端已启动，监听端口 %1，等待连接...").arg(m_port));

    // Wait for a client connection (with stop check)
    while (!server.waitForNewConnection(500)) {
        if (ATOMIC_LOAD(m_stop)) {
            server.close();
            emit testFinished(false, tr("服务端已手动停止"));
            return;
        }
        if (server.serverError() != QTcpSocket::SocketTimeoutError) {
            emit testFinished(false, tr("监听错误: %1").arg(server.errorString()));
            return;
        }
    }

    QTcpSocket *client = server.nextPendingConnection();
    emit logMessage(tr("客户端已连接: %1:%2")
                    .arg(client->peerAddress().toString())
                    .arg(client->peerPort()));

    // Read handshake: magic(8) + duration(4) + direction(1)
    client->waitForReadyRead(5000);
    if (client->bytesAvailable() < 13) {
        client->waitForReadyRead(3000);
    }

    QByteArray handshake = client->read(13);
    if (handshake.size() < 13 || handshake.left(8) != s_magic) {
        emit testFinished(false, tr("握手失败: 无效的协议标识"));
        client->close();
        delete client;
        server.close();
        return;
    }

    quint32 clientDuration = (quint8)handshake[8] << 24 |
                             (quint8)handshake[9] << 16 |
                             (quint8)handshake[10] << 8 |
                             (quint8)handshake[11];
    quint8 clientDir = (quint8)handshake[12];

    emit logMessage(tr("握手成功: 时长=%1秒, 方向=%2")
                    .arg(clientDuration)
                    .arg(clientDir == 0 ? tr("客户端上传") : tr("客户端下载")));

    int testDuration = clientDuration;
    bool serverSends = (clientDir == 1);  // Download: server sends data

    // Send buffer
    QByteArray sendBuf(128 * 1024, 0);
    for (int i = 0; i < sendBuf.size(); ++i)
        sendBuf[i] = (char)(i & 0xFF);

    QElapsedTimer timer;
    timer.start();

    qint64 totalBytes = 0;
    qint64 intervalBytes = 0;
    int lastSec = 0;

    while (!ATOMIC_LOAD(m_stop)) {
        int elapsed = (int)(timer.elapsed() / 1000);
        if (elapsed >= testDuration)
            break;

        if (serverSends) {
            // Server sends data to client
            qint64 written = client->write(sendBuf);
            if (written < 0) {
                emit logMessage(tr("发送错误: %1").arg(client->errorString()));
                break;
            }
            totalBytes += written;
            intervalBytes += written;

            // Flush periodically to avoid excessive buffering
            if (totalBytes % (1024 * 1024) == 0 || timer.elapsed() % 200 < 10)
                client->flush();
        } else {
            // Server receives data from client
            client->waitForReadyRead(100);
            qint64 avail = client->bytesAvailable();
            if (avail > 0) {
                QByteArray data = client->read(avail);
                totalBytes += data.size();
                intervalBytes += data.size();
            }
        }

        // Report per-second interval
        int curSec = elapsed;
        if (curSec > lastSec) {
            double intervalSec = curSec - lastSec;
            double mbps = (intervalBytes * 8.0) / (intervalSec * 1000000.0);
            emit intervalResult(curSec, mbps, intervalBytes);
            intervalBytes = 0;
            lastSec = curSec;
        }
    }

    // Report final partial interval
    double finalSec = timer.elapsed() / 1000.0 - lastSec;
    if (finalSec > 0.1 && intervalBytes > 0) {
        double mbps = (intervalBytes * 8.0) / (finalSec * 1000000.0);
        emit intervalResult((int)(timer.elapsed() / 1000), mbps, intervalBytes);
    }

    client->waitForBytesWritten(3000);
    client->close();
    delete client;
    server.close();

    double totalMbps = (totalBytes * 8.0) / (timer.elapsed() / 1000.0 * 1000000.0);
    emit logMessage(tr("服务端测试完成: 总传输 %1 MB, 平均带宽 %2 Mbps")
                    .arg(totalBytes / (1024.0 * 1024.0), 0, 'f', 2)
                    .arg(totalMbps, 0, 'f', 2));
    emit testFinished(true, tr("服务端测试完成"));
}

void TcpBwWorker::runClient()
{
    QTcpSocket socket;

    emit logMessage(tr("正在连接 %1:%2...").arg(m_host).arg(m_port));

    socket.connectToHost(m_host, m_port);
    if (!socket.waitForConnected(5000)) {
        emit testFinished(false, tr("连接失败: %1").arg(socket.errorString()));
        return;
    }

    emit logMessage(tr("已连接服务端 %1:%2").arg(m_host).arg(m_port));

    // Send handshake: magic(8) + duration(4 BE) + direction(1)
    QByteArray handshake(s_magic, 8);
    handshake.append((char)((m_duration >> 24) & 0xFF));
    handshake.append((char)((m_duration >> 16) & 0xFF));
    handshake.append((char)((m_duration >> 8) & 0xFF));
    handshake.append((char)(m_duration & 0xFF));
    handshake.append((char)(m_direction == Download ? 1 : 0));

    socket.write(handshake);
    socket.flush();
    socket.waitForBytesWritten(3000);

    bool clientSends = (m_direction == Upload);  // Upload: client sends data

    // Send buffer
    QByteArray sendBuf(128 * 1024, 0);
    for (int i = 0; i < sendBuf.size(); ++i)
        sendBuf[i] = (char)(i & 0xFF);

    QElapsedTimer timer;
    timer.start();

    qint64 totalBytes = 0;
    qint64 intervalBytes = 0;
    int lastSec = 0;

    while (!ATOMIC_LOAD(m_stop)) {
        int elapsed = (int)(timer.elapsed() / 1000);
        if (elapsed >= m_duration)
            break;

        if (clientSends) {
            // Client uploads data
            qint64 written = socket.write(sendBuf);
            if (written < 0) {
                emit logMessage(tr("发送错误: %1").arg(socket.errorString()));
                break;
            }
            totalBytes += written;
            intervalBytes += written;

            if (totalBytes % (1024 * 1024) == 0 || timer.elapsed() % 200 < 10)
                socket.flush();
        } else {
            // Client downloads data
            socket.waitForReadyRead(100);
            qint64 avail = socket.bytesAvailable();
            if (avail > 0) {
                QByteArray data = socket.read(avail);
                totalBytes += data.size();
                intervalBytes += data.size();
            }
        }

        // Report per-second interval
        int curSec = elapsed;
        if (curSec > lastSec) {
            double intervalSec = curSec - lastSec;
            double mbps = (intervalBytes * 8.0) / (intervalSec * 1000000.0);
            emit intervalResult(curSec, mbps, intervalBytes);
            intervalBytes = 0;
            lastSec = curSec;
        }
    }

    // Report final partial interval
    double finalSec = timer.elapsed() / 1000.0 - lastSec;
    if (finalSec > 0.1 && intervalBytes > 0) {
        double mbps = (intervalBytes * 8.0) / (finalSec * 1000000.0);
        emit intervalResult((int)(timer.elapsed() / 1000), mbps, intervalBytes);
    }

    socket.waitForBytesWritten(3000);
    socket.close();

    double totalMbps = (totalBytes * 8.0) / (timer.elapsed() / 1000.0 * 1000000.0);
    emit logMessage(tr("客户端测试完成: 总传输 %1 MB, 平均带宽 %2 Mbps, 耗时 %3 ms")
                    .arg(totalBytes / (1024.0 * 1024.0), 0, 'f', 2)
                    .arg(totalMbps, 0, 'f', 2)
                    .arg(timer.elapsed()));
    emit testFinished(true, tr("客户端测试完成"));
}

// ============ TcpBwWidget 实现 ============

TcpBwWidget::TcpBwWidget(QWidget *parent)
    : QWidget(parent)
    , m_worker(nullptr)
    , m_serverWorker(nullptr)
{
    createUI();
}

void TcpBwWidget::createUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout;

    // === Parameters group ===
    QGroupBox *paramGroup = new QGroupBox(tr("测试参数"));
    QGridLayout *paramLayout = new QGridLayout;

    // Mode
    paramLayout->addWidget(new QLabel(tr("模式：")), 0, 0);
    m_modeCombo = new QComboBox;
    m_modeCombo->addItem(tr("客户端 (Client)"), TcpBwWorker::Client);
    m_modeCombo->addItem(tr("服务端 (Server)"), TcpBwWorker::Server);
    m_modeCombo->addItem(tr("本机回环 (Loopback)"), TcpBwWorker::Loopback);
    paramLayout->addWidget(m_modeCombo, 0, 1);

    // Host
    paramLayout->addWidget(new QLabel(tr("目标主机：")), 0, 2);
    m_hostEdit = new QLineEdit("127.0.0.1");
    paramLayout->addWidget(m_hostEdit, 0, 3);

    // Port
    paramLayout->addWidget(new QLabel(tr("端口：")), 1, 0);
    m_portSpinBox = new QSpinBox;
    m_portSpinBox->setRange(1, 65535);
    m_portSpinBox->setValue(52737);
    paramLayout->addWidget(m_portSpinBox, 1, 1);

    // Duration
    paramLayout->addWidget(new QLabel(tr("测试时长(秒)：")), 1, 2);
    m_durationSpinBox = new QSpinBox;
    m_durationSpinBox->setRange(1, 3600);
    m_durationSpinBox->setValue(10);
    paramLayout->addWidget(m_durationSpinBox, 1, 3);

    // Direction (client only)
    paramLayout->addWidget(new QLabel(tr("测试方向：")), 2, 0);
    m_directionCombo = new QComboBox;
    m_directionCombo->addItem(tr("上传 (Upload)"), TcpBwWorker::Upload);
    m_directionCombo->addItem(tr("下载 (Download)"), TcpBwWorker::Download);
    paramLayout->addWidget(m_directionCombo, 2, 1);

    paramGroup->setLayout(paramLayout);
    mainLayout->addWidget(paramGroup);

    // === Buttons ===
    QHBoxLayout *btnLayout = new QHBoxLayout;
    m_startButton = new QPushButton(tr("开始测试"));
    m_stopButton = new QPushButton(tr("停止测试"));
    m_stopButton->setEnabled(false);
    btnLayout->addWidget(m_startButton);
    btnLayout->addWidget(m_stopButton);
    btnLayout->addStretch();
    mainLayout->addLayout(btnLayout);

    // === Chart ===
    m_chart = new BandwidthChart;
    mainLayout->addWidget(m_chart, 1);

    // === Output ===
    m_outputEdit = new QTextEdit;
    m_outputEdit->setReadOnly(true);
    m_outputEdit->setMaximumHeight(150);
    m_outputEdit->setPlainText(
        tr("TCP 带宽测试工具 (内置，无需外部依赖)\n\n"
           "使用说明：\n"
           "1. 在一台机器上启动服务端模式\n"
           "2. 在另一台机器上选择客户端模式，输入服务端地址\n"
           "3. 也可选择本机回环模式测试本地带宽\n"
           "4. 设置测试参数后点击\"开始测试\"\n\n"
           "协议: TCP | 支持上传/下载双向测试"));
    mainLayout->addWidget(m_outputEdit);

    setLayout(mainLayout);

    // Connections
    connect(m_startButton, &QPushButton::clicked, this, &TcpBwWidget::startTest);
    connect(m_stopButton, &QPushButton::clicked, this, &TcpBwWidget::stopTest);
    connect(m_modeCombo, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &TcpBwWidget::onModeChanged);

    onModeChanged(m_modeCombo->currentIndex());
}

void TcpBwWidget::onModeChanged(int /*index*/)
{
    int mode = m_modeCombo->currentData().toInt();
    bool isClient = (mode == TcpBwWorker::Client);
    bool isLoopback = (mode == TcpBwWorker::Loopback);
    m_hostEdit->setEnabled(isClient);
    m_durationSpinBox->setEnabled(isClient || isLoopback);
    m_directionCombo->setEnabled(isClient || isLoopback);
}

void TcpBwWidget::startTest()
{
    if ((m_worker && m_worker->isRunning()) ||
        (m_serverWorker && m_serverWorker->isRunning())) {
        appendColoredText(tr("测试正在进行中，请先停止"), Qt::red);
        return;
    }

    m_chart->clearData();
    m_outputEdit->clear();

    int mode = m_modeCombo->currentData().toInt();

    if (mode == TcpBwWorker::Loopback) {
        // Loopback mode: start server first, then client
        m_serverWorker = new TcpBwWorker(this);
        m_serverWorker->setMode(TcpBwWorker::Server);
        m_serverWorker->setPort(m_portSpinBox->value());
        m_serverWorker->setDuration(m_durationSpinBox->value());
        m_serverWorker->setDirection(TcpBwWorker::Upload);

        // Only connect logMessage from server (don't double-report intervals)
        connect(m_serverWorker, &TcpBwWorker::logMessage,
                this, &TcpBwWidget::onLogMessage);
        connect(m_serverWorker, &TcpBwWorker::testFinished,
                this, [this](bool success, const QString &message) {
            appendColoredText(tr("[服务端] %1").arg(message),
                              success ? QColor(0, 100, 0) : Qt::red);
            // If server finishes first, stop client too
            if (m_worker && m_worker->isRunning())
                m_worker->stop();
        });

        m_serverWorker->start();

        // Wait a bit for server to start listening
        QThread::msleep(300);

        m_worker = new TcpBwWorker(this);
        m_worker->setMode(TcpBwWorker::Client);
        m_worker->setHost("127.0.0.1");
        m_worker->setPort(m_portSpinBox->value());
        m_worker->setDuration(m_durationSpinBox->value());
        m_worker->setDirection((TcpBwWorker::Direction)m_directionCombo->currentData().toInt());

        connect(m_worker, &TcpBwWorker::intervalResult,
                this, &TcpBwWidget::onIntervalResult);
        connect(m_worker, &TcpBwWorker::testFinished,
                this, &TcpBwWidget::onTestFinished);
        connect(m_worker, &TcpBwWorker::logMessage,
                this, &TcpBwWidget::onLogMessage);

        appendColoredText(tr("启动本机回环测试 - 端口:%1, 时长:%2秒, 方向:%3")
                          .arg(m_portSpinBox->value())
                          .arg(m_durationSpinBox->value())
                          .arg(m_directionCombo->currentText()),
                          QColor(0, 100, 0));

        m_worker->start();
    } else {
        m_worker = new TcpBwWorker(this);
        m_worker->setMode((TcpBwWorker::Mode)mode);
        m_worker->setHost(m_hostEdit->text().trimmed());
        m_worker->setPort(m_portSpinBox->value());
        m_worker->setDuration(m_durationSpinBox->value());
        m_worker->setDirection((TcpBwWorker::Direction)m_directionCombo->currentData().toInt());

        connect(m_worker, &TcpBwWorker::intervalResult,
                this, &TcpBwWidget::onIntervalResult);
        connect(m_worker, &TcpBwWorker::testFinished,
                this, &TcpBwWidget::onTestFinished);
        connect(m_worker, &TcpBwWorker::logMessage,
                this, &TcpBwWidget::onLogMessage);

        appendColoredText(tr("启动 %1 测试 - 端口:%2")
                          .arg(m_modeCombo->currentText())
                          .arg(m_portSpinBox->value()),
                          QColor(0, 100, 0));

        m_worker->start();
    }

    updateButtonState(true);
}

void TcpBwWidget::stopTest()
{
    if (m_worker && m_worker->isRunning()) {
        m_worker->stop();
        m_worker->wait(3000);
    }
    if (m_serverWorker && m_serverWorker->isRunning()) {
        m_serverWorker->stop();
        m_serverWorker->wait(3000);
    }
    if ((m_worker && m_worker->isRunning()) ||
        (m_serverWorker && m_serverWorker->isRunning())) {
        appendColoredText(tr("测试已手动停止"), Qt::red);
    }
}

void TcpBwWidget::onIntervalResult(int elapsedSec, double bandwidthMbps, qint64 bytesTransferred)
{
    m_chart->addDataPoint(elapsedSec, bandwidthMbps);

    // Format transfer size
    QString transferStr;
    if (bytesTransferred >= 1024 * 1024)
        transferStr = tr("%1 MB").arg(bytesTransferred / (1024.0 * 1024.0), 0, 'f', 2);
    else if (bytesTransferred >= 1024)
        transferStr = tr("%1 KB").arg(bytesTransferred / 1024.0, 0, 'f', 2);
    else
        transferStr = tr("%1 B").arg(bytesTransferred);

    // Format bandwidth
    QString bwStr;
    if (bandwidthMbps >= 1000)
        bwStr = tr("%1 Gbps").arg(bandwidthMbps / 1000.0, 0, 'f', 2);
    else if (bandwidthMbps >= 1)
        bwStr = tr("%1 Mbps").arg(bandwidthMbps, 0, 'f', 2);
    else
        bwStr = tr("%1 Kbps").arg(bandwidthMbps * 1000, 0, 'f', 0);

    m_outputEdit->append(tr("[%1s]  %2  %3")
                         .arg(elapsedSec)
                         .arg(transferStr)
                         .arg(bwStr));
}

void TcpBwWidget::onTestFinished(bool success, const QString &message)
{
    if (success)
        appendColoredText(message, QColor(0, 100, 0));
    else
        appendColoredText(message, Qt::red);

    // In loopback mode, wait for both workers to finish
    if (m_serverWorker && m_serverWorker->isRunning()) {
        // Client finished but server still running - stop server
        m_serverWorker->stop();
        return;
    }

    updateButtonState(false);

    if (m_worker) {
        m_worker->deleteLater();
        m_worker = nullptr;
    }
    if (m_serverWorker) {
        m_serverWorker->deleteLater();
        m_serverWorker = nullptr;
    }
}

void TcpBwWidget::onLogMessage(const QString &text)
{
    appendColoredText(text, QColor(0, 80, 160));
}

void TcpBwWidget::appendColoredText(const QString &text, const QColor &color)
{
    m_outputEdit->append(QString("<font color='%1'>%2</font>")
                         .arg(color.name())
                         .arg(text.toHtmlEscaped()));
}

void TcpBwWidget::updateButtonState(bool running)
{
    int mode = m_modeCombo->currentData().toInt();
    bool isClient = (mode == TcpBwWorker::Client);
    bool isLoopback = (mode == TcpBwWorker::Loopback);
    m_startButton->setEnabled(!running);
    m_stopButton->setEnabled(running);
    m_modeCombo->setEnabled(!running);
    m_hostEdit->setEnabled(!running && isClient);
    m_portSpinBox->setEnabled(!running);
    m_durationSpinBox->setEnabled(!running && (isClient || isLoopback));
    m_directionCombo->setEnabled(!running && (isClient || isLoopback));
}
