/****************************************************************************
**
** Copyright (C) 2024
** FFT module extracted from window.cpp
**
****************************************************************************/

#include <QtWidgets>
#include <cmath>
#include <complex>
#include <thread>
#include <vector>
#include <atomic>

#include "fft.h"

// ============ FFTSpectrumWidget 实现 ============

// 10 条曲线的配色方案（高区分度）
static const QColor s_signalColors[] = {
    QColor(30, 90, 200),    // 蓝
    QColor(220, 50, 50),    // 红
    QColor(30, 160, 60),    // 绿
    QColor(220, 150, 0),    // 橙
    QColor(140, 40, 180),   // 紫
    QColor(0, 170, 170),    // 青
    QColor(200, 80, 130),   // 粉
    QColor(100, 100, 100),  // 灰
    QColor(80, 50, 20),     // 棕
    QColor(0, 80, 130),     // 深蓝
};

FFTSpectrumWidget::FFTSpectrumWidget(QWidget *parent)
    : QWidget(parent)
    , m_startIdx(0)
    , m_endIdx(100)
    , m_maxVisibleSignals(10)
    , m_maxValue(0)
{
    setMinimumSize(400, 300);
}

void FFTSpectrumWidget::setSpectrums(const QVector<QVector<double>> &spectrums,
                                     const QStringList &labels)
{
    m_spectrums = spectrums;
    m_labels = labels;
    update();
}

void FFTSpectrumWidget::setDisplayRange(int startIdx, int endIdx)
{
    m_startIdx = qMax(0, startIdx);
    m_endIdx = endIdx;
    update();
}

void FFTSpectrumWidget::setMaxVisibleSignals(int count)
{
    m_maxVisibleSignals = count;
    update();
}

QColor FFTSpectrumWidget::signalColor(int index)
{
    return s_signalColors[index % 10];
}

void FFTSpectrumWidget::computeOptimalRange(int &startIdx, int &endIdx) const
{
    startIdx = 0;
    endIdx = 100;

    if (m_spectrums.isEmpty() || m_spectrums[0].isEmpty())
        return;

    int visibleCount = qMin(m_maxVisibleSignals, m_spectrums.size());
    int totalSize = m_spectrums[0].size();

    // 策略：找到所有可见信号中，最后一个显著频率分量的位置
    // "显著"定义为 > 全局最大值的 0.5%
    double globalMax = 0;
    for (int s = 0; s < visibleCount; ++s) {
        if (m_spectrums[s].isEmpty())
            continue;
        for (int i = 0; i < m_spectrums[s].size(); ++i) {
            if (m_spectrums[s][i] > globalMax)
                globalMax = m_spectrums[s][i];
        }
    }

    if (globalMax <= 0)
        return;

    double threshold = globalMax * 0.005;  // 0.5% 阈值
    int lastSignificant = 0;

    for (int s = 0; s < visibleCount; ++s) {
        if (m_spectrums[s].isEmpty())
            continue;
        for (int i = m_spectrums[s].size() - 1; i > lastSignificant; --i) {
            if (m_spectrums[s][i] > threshold) {
                lastSignificant = i;
                break;
            }
        }
    }

    // 在最后一个显著频率位置基础上留20%余量
    endIdx = qMin(totalSize - 1, (int)(lastSignificant * 1.2));
    if (endIdx <= startIdx)
        endIdx = startIdx + 100;
}

void FFTSpectrumWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // 背景
    painter.fillRect(rect(), Qt::white);

    // 绘图区域边距（右侧留出图例空间）
    const int marginLeft = 60;
    const int marginRight = 130;
    const int marginTop = 30;
    const int marginBottom = 45;

    QRect plotArea(marginLeft, marginTop,
                   width() - marginLeft - marginRight,
                   height() - marginTop - marginBottom);

    if (plotArea.width() <= 0 || plotArea.height() <= 0)
        return;

    // 绘图区域边框
    painter.setPen(QPen(Qt::darkGray, 1));
    painter.drawRect(plotArea);

    // 标题
    painter.setPen(Qt::black);
    QFont titleFont = painter.font();
    titleFont.setBold(true);
    titleFont.setPointSize(10);
    painter.setFont(titleFont);
    painter.drawText(QRect(0, 2, width(), marginTop - 2),
                     Qt::AlignCenter, tr("频域幅值图谱（前%1个信号）")
                     .arg(qMin(m_maxVisibleSignals, m_spectrums.size())));

    if (m_spectrums.isEmpty()) {
        painter.setPen(Qt::gray);
        QFont normalFont;
        normalFont.setPointSize(9);
        painter.setFont(normalFont);
        painter.drawText(plotArea, Qt::AlignCenter, tr("等待FFT计算..."));
        return;
    }

    // 计算显示范围（基于第一条频谱的长度）
    int rangeStart = m_startIdx;
    int rangeEnd = m_endIdx;
    if (!m_spectrums[0].isEmpty()) {
        rangeEnd = qMin(m_endIdx, m_spectrums[0].size() - 1);
    }
    if (rangeEnd <= rangeStart)
        rangeEnd = rangeStart + 1;

    // 计算所有可见信号的最大值（用于统一Y轴缩放）
    m_maxValue = 0;
    int visibleCount = qMin(m_maxVisibleSignals, m_spectrums.size());
    for (int s = 0; s < visibleCount; ++s) {
        if (m_spectrums[s].isEmpty())
            continue;
        int end = qMin(rangeEnd, m_spectrums[s].size() - 1);
        for (int i = rangeStart; i <= end; ++i) {
            if (m_spectrums[s][i] > m_maxValue)
                m_maxValue = m_spectrums[s][i];
        }
    }
    if (m_maxValue <= 0)
        m_maxValue = 1.0;

    drawGrid(painter, plotArea);
    drawCurves(painter, plotArea);
    drawLegend(painter, plotArea);
}

void FFTSpectrumWidget::drawGrid(QPainter &painter, const QRect &plotArea)
{
    painter.setPen(QPen(QColor(200, 200, 200), 1, Qt::DashLine));
    QFont smallFont;
    smallFont.setPointSize(7);
    painter.setFont(smallFont);

    int rangeStart = m_startIdx;
    int rangeEnd = m_endIdx;
    if (!m_spectrums.isEmpty() && !m_spectrums[0].isEmpty())
        rangeEnd = qMin(m_endIdx, m_spectrums[0].size() - 1);
    int rangeLen = rangeEnd - rangeStart;

    // 水平网格线（Y轴刻度）- 5 条
    int yTicks = 5;
    for (int i = 0; i <= yTicks; ++i) {
        int y = plotArea.bottom() - (i * plotArea.height()) / yTicks;
        painter.drawLine(plotArea.left(), y, plotArea.right(), y);
        double val = (m_maxValue * i) / yTicks;
        painter.setPen(Qt::darkGray);
        painter.drawText(QRect(0, y - 10, plotArea.left() - 5, 20),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(val, 'f', 1));
        painter.setPen(QPen(QColor(200, 200, 200), 1, Qt::DashLine));
    }

    // 垂直网格线（X轴刻度）- 约 10 条
    int xTicks = qMin(10, rangeLen);
    for (int i = 0; i <= xTicks; ++i) {
        int x = plotArea.left() + (i * plotArea.width()) / xTicks;
        painter.drawLine(x, plotArea.top(), x, plotArea.bottom());
        int freqIdx = rangeStart + (i * rangeLen) / xTicks;
        painter.setPen(Qt::darkGray);
        painter.drawText(QRect(x - 30, plotArea.bottom() + 3, 60, 20),
                         Qt::AlignCenter, QString::number(freqIdx));
        painter.setPen(QPen(QColor(200, 200, 200), 1, Qt::DashLine));
    }

    // 轴标签
    painter.setPen(Qt::black);
    QFont labelFont;
    labelFont.setPointSize(8);
    painter.setFont(labelFont);
    painter.drawText(QRect(plotArea.left(), plotArea.bottom() + 22,
                           plotArea.width(), 20),
                     Qt::AlignCenter, tr("频率索引"));
}

void FFTSpectrumWidget::drawCurves(QPainter &painter, const QRect &plotArea)
{
    int rangeStart = m_startIdx;
    int rangeEnd = m_endIdx;
    if (!m_spectrums.isEmpty() && !m_spectrums[0].isEmpty())
        rangeEnd = qMin(m_endIdx, m_spectrums[0].size() - 1);
    int rangeLen = rangeEnd - rangeStart;
    if (rangeLen <= 0)
        return;

    int visibleCount = qMin(m_maxVisibleSignals, m_spectrums.size());

    for (int s = 0; s < visibleCount; ++s) {
        if (m_spectrums[s].isEmpty())
            continue;

        int end = qMin(rangeEnd, m_spectrums[s].size() - 1);
        QColor color = signalColor(s);

        // 半透明填充
        QPainterPath fillPath;
        fillPath.moveTo(plotArea.left(), plotArea.bottom());
        for (int i = rangeStart; i <= end; ++i) {
            double x = plotArea.left() + ((double)(i - rangeStart) / rangeLen) * plotArea.width();
            double y = plotArea.bottom() - (m_spectrums[s][i] / m_maxValue) * plotArea.height();
            fillPath.lineTo(x, y);
        }
        fillPath.lineTo(plotArea.left() + ((double)(end - rangeStart) / rangeLen) * plotArea.width(),
                        plotArea.bottom());
        fillPath.closeSubpath();
        painter.fillPath(fillPath, QColor(color.red(), color.green(), color.blue(), 20));

        // 曲线
        QPainterPath curvePath;
        bool first = true;
        for (int i = rangeStart; i <= end; ++i) {
            double x = plotArea.left() + ((double)(i - rangeStart) / rangeLen) * plotArea.width();
            double y = plotArea.bottom() - (m_spectrums[s][i] / m_maxValue) * plotArea.height();
            if (first) {
                curvePath.moveTo(x, y);
                first = false;
            } else {
                curvePath.lineTo(x, y);
            }
        }
        painter.setPen(QPen(color, 1.2));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(curvePath);
    }
}

void FFTSpectrumWidget::drawLegend(QPainter &painter, const QRect &plotArea)
{
    int visibleCount = qMin(m_maxVisibleSignals, m_spectrums.size());
    if (visibleCount == 0)
        return;

    // 图例区域：绘图区右侧
    int legendX = plotArea.right() + 10;
    int legendY = plotArea.top() + 5;
    int lineH = 18;

    QFont legendFont;
    legendFont.setPointSize(8);
    painter.setFont(legendFont);

    // 图例背景
    int legendH = visibleCount * lineH + 10;
    painter.fillRect(QRect(legendX - 2, legendY - 2,
                           120, legendH), QColor(250, 250, 250));
    painter.setPen(QPen(Qt::lightGray, 1));
    painter.drawRect(QRect(legendX - 2, legendY - 2,
                           120, legendH));

    for (int s = 0; s < visibleCount; ++s) {
        int y = legendY + s * lineH + 8;
        QColor color = signalColor(s);

        // 色条
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawRect(legendX + 2, y - 5, 20, 8);

        // 标签
        painter.setPen(Qt::black);
        QString label = (s < m_labels.size()) ? m_labels[s]
                       : tr("信号 %1").arg(s + 1);
        painter.drawText(legendX + 26, y, label);
    }
}

// ============ FFT 算法实现 ============

/**
 * @brief 原地快速傅里叶变换（迭代式Cooley-Tukey算法）
 * @param data 输入输出数组，复数形式
 * @param n 数组长度，必须为2的幂
 *
 * 使用迭代方式实现，避免递归带来的栈开销
 * 算法复杂度：O(n log n)
 */
static void fftInPlace(std::vector<std::complex<double>> &data, int n)
{
    // 第一步：位逆序置换（bit-reversal permutation）
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(data[i], data[j]);
        }
    }

    // 第二步：蝶形运算
    for (int len = 2; len <= n; len <<= 1) {
        double angle = -M_PI * 2.0 / len;
        std::complex<double> wn(cos(angle), sin(angle));

        for (int i = 0; i < n; i += len) {
            std::complex<double> w(1);
            for (int j = 0; j < len / 2; ++j) {
                std::complex<double> u = data[i + j];
                std::complex<double> t = w * data[i + j + len / 2];
                data[i + j] = u + t;
                data[i + j + len / 2] = u - t;
                w *= wn;
            }
        }
    }
}

// ============ FFTTask 实现 ============

/**
 * @brief FFT任务构造函数
 * @param data 输入信号数据
 * @param threadCount 使用的线程数
 * @param parent 父对象
 */
FFTTask::FFTTask(const QVector<double> &data, int threadCount, QObject *parent)
    : QThread(parent), m_data(data), m_threadCount(threadCount)
{
    m_result.resize(data.size());
    m_elapsed = 0;
}

/**
 * @brief FFT任务的主执行函数
 *
 * 执行步骤：
 * 1. 准备数据：将QVector<double>转换为std::vector<std::complex<double>>
 * 2. 分配任务：根据线程数将信号分配给各线程
 * 3. 执行FFT：对每个信号进行快速傅里叶变换
 * 4. 收集结果：将计算结果写回m_result
 *
 * 多线程策略：每个线程处理多个独立的信号
 * 使用atomic计数器跟踪完成进度，确保进度条正确更新
 */
void FFTTask::run()
{
    // 开始计时
    QElapsedTimer timer;
    timer.start();

    // 立即发送初始进度，让UI立即响应
    emit progressChanged(0);
    QCoreApplication::processEvents();

    // 每个信号的长度（65536点）
    int signalSize = 65536;
    // 计算信号数量
    int signalCount = m_data.size() / signalSize;

    // 将输入数据转换为复数形式，方便FFT计算
    std::vector<std::vector<std::complex<double>>> allSignals(signalCount);
    for (int s = 0; s < signalCount; ++s) {
        allSignals[s].resize(signalSize);
        for (int i = 0; i < signalSize; ++i) {
            allSignals[s][i] = std::complex<double>(m_data[s * signalSize + i], 0);
        }
    }

    // 根据线程数选择执行策略
    if (m_threadCount == 1) {
        // 单线程模式：顺序处理所有信号
        for (int s = 0; s < signalCount; ++s) {
            fftInPlace(allSignals[s], signalSize);
            // 更新进度：(s+1)/signalCount * 100%
            emit progressChanged(((s + 1) * 100) / signalCount);
        }
    } else {
        // 多线程模式：并行处理信号
        std::vector<std::thread> threads;
        // 每个线程处理的信号数量
        int signalsPerThread = signalCount / m_threadCount;
        // 使用原子操作跟踪已完成信号总数
        std::atomic<int> completedSignals(0);

        // 工作函数：线程执行的计算任务
        auto worker = [this, &allSignals, &completedSignals, signalSize, signalCount, signalsPerThread](int threadId) {
            // 计算当前线程的信号范围
            int start = threadId * signalsPerThread;
            int end = (threadId == m_threadCount - 1) ? signalCount : start + signalsPerThread;

            // 对分配给该线程的每个信号执行FFT
            for (int s = start; s < end; ++s) {
                fftInPlace(allSignals[s], signalSize);
                // 原子递增计数器，获取已完成总数
                int done = completedSignals.fetch_add(1) + 1;
                emit progressChanged((done * 100) / signalCount);
            }
        };

        // 启动所有工作线程
        for (int t = 0; t < m_threadCount; ++t) {
            threads.emplace_back(worker, t);
        }

        // 等待所有线程完成
        for (auto &th : threads) {
            th.join();
        }
    }

    // 收集计算结果（取幅值）
    for (int s = 0; s < signalCount; ++s) {
        for (int i = 0; i < signalSize; ++i) {
            m_result[s * signalSize + i] = std::abs(allSignals[s][i]);
        }
    }

    // 记录耗时
    m_elapsed = timer.elapsed();
    emit progressChanged(100);
}

// ============ FFTWidget 实现 ============

/**
 * @brief FFT控件构造函数
 */
FFTWidget::FFTWidget(QWidget *parent)
    : QWidget(parent)
    , m_fftTask(nullptr)
    , m_signalCount(0)
    , m_signalSize(0)
{
    createUI();
}

void FFTWidget::createUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout;

    // 第一行：线程数 + 开始按钮
    QHBoxLayout *controlsLayout = new QHBoxLayout;
    controlsLayout->addWidget(new QLabel(tr("线程数：")));
    m_threadCountSpinBox = new QSpinBox;
    m_threadCountSpinBox->setRange(1, QThread::idealThreadCount());
    m_threadCountSpinBox->setValue(QThread::idealThreadCount());
    controlsLayout->addWidget(m_threadCountSpinBox);

    m_startButton = new QPushButton(tr("开始计算"));
    controlsLayout->addWidget(m_startButton);
    controlsLayout->addStretch();

    // 第二行：频率范围
    QHBoxLayout *viewControlsLayout = new QHBoxLayout;
    viewControlsLayout->addWidget(new QLabel(tr("频率范围：")));
    m_freqRangeStart = new QSpinBox;
    m_freqRangeStart->setRange(0, 32767);
    m_freqRangeStart->setValue(0);
    m_freqRangeStart->setEnabled(false);
    viewControlsLayout->addWidget(m_freqRangeStart);
    viewControlsLayout->addWidget(new QLabel(tr("-")));
    m_freqRangeEnd = new QSpinBox;
    m_freqRangeEnd->setRange(1, 32768);
    m_freqRangeEnd->setValue(100);
    m_freqRangeEnd->setEnabled(false);
    viewControlsLayout->addWidget(m_freqRangeEnd);
    viewControlsLayout->addStretch();

    // 进度条
    m_progressBar = new QProgressBar;
    m_progressBar->setValue(0);

    // 频域图谱
    m_spectrumWidget = new FFTSpectrumWidget;
    m_spectrumWidget->setMaxVisibleSignals(10);

    // 结果显示区域
    m_resultTextEdit = new QTextEdit;
    m_resultTextEdit->setReadOnly(true);
    m_resultTextEdit->setMaximumHeight(120);
    m_resultTextEdit->setPlainText(tr("点击\"开始计算\"运行多线程FFT\n\n计算 1024 个独立的 65536 点信号\n图谱将同时显示前10个信号的频域曲线"));

    // 添加到布局
    mainLayout->addLayout(controlsLayout);
    mainLayout->addLayout(viewControlsLayout);
    mainLayout->addWidget(m_progressBar);
    mainLayout->addWidget(m_spectrumWidget, 1);
    mainLayout->addWidget(m_resultTextEdit);
    setLayout(mainLayout);

    // 连接信号
    connect(m_startButton, SIGNAL(clicked()), this, SLOT(startFFT()));
    connect(m_freqRangeStart, SIGNAL(valueChanged(int)), this, SLOT(onFreqRangeChanged()));
    connect(m_freqRangeEnd, SIGNAL(valueChanged(int)), this, SLOT(onFreqRangeChanged()));
}

/**
 * @brief 开始FFT计算
 *
 * 准备测试数据，创建FFT任务线程并启动
 */
void FFTWidget::startFFT()
{
    // 禁用按钮，防止重复点击
    m_startButton->setEnabled(false);
    m_progressBar->setValue(0);

    // 设置参数：1024个信号，每个65536点
    const int signalSize = 65536;
    const int signalCount = 1024;
    const int N = signalSize * signalCount;

    // 生成测试信号数据
    // 每个信号使用不同频率的正弦波叠加，模拟实际信号
    QVector<double> data(N);
    for (int i = 0; i < N; ++i) {
        int signalIdx = i / signalSize;  // 信号编号
        int idxInSignal = i % signalSize;  // 信号内索引
        // 混合多个正弦波 + 随机噪声
        data[i] = sin(idxInSignal * 0.01 * (signalIdx + 1)) * cos(idxInSignal * 0.005) + (rand() % 100) / 500.0;
    }

    // 创建FFT任务
    m_fftTask = new FFTTask(data, m_threadCountSpinBox->value());
    // 连接进度更新信号
    connect(m_fftTask, SIGNAL(progressChanged(int)), m_progressBar, SLOT(setValue(int)));
    // 连接完成信号
    connect(m_fftTask, SIGNAL(finished()), this, SLOT(fftFinished()));
    // 启动线程
    m_fftTask->start();
}

/**
 * @brief FFT计算完成后的回调函数
 *
 * 显示计算结果：线程数、数据规模、耗时、频谱数据
 */
void FFTWidget::fftFinished()
{
    // 保存完整结果
    m_fftFullResult = m_fftTask->result();
    m_signalSize = 65536;
    m_signalCount = m_fftFullResult.size() / m_signalSize;

    QString result;
    result += tr("计算完成！\n");
    result += tr("线程数: %1\n").arg(m_fftTask->threadCount());
    result += tr("数据规模: %1 个 %2 点信号\n").arg(m_signalCount).arg(m_signalSize);
    result += tr("总数据量: %1 点\n").arg(m_signalCount * m_signalSize);
    result += tr("耗时: %1 ms\n").arg(m_fftTask->elapsedTime());

    // 提取前10个信号的频谱（只取前半部分，因为FFT结果是对称的）
    int halfSize = m_signalSize / 2;
    int numToShow = qMin(10, m_signalCount);
    QVector<QVector<double>> spectrums(numToShow);
    QStringList labels;
    for (int s = 0; s < numToShow; ++s) {
        spectrums[s].resize(halfSize);
        for (int i = 0; i < halfSize; ++i) {
            spectrums[s][i] = m_fftFullResult[s * m_signalSize + i];
        }
        labels << tr("信号 %1").arg(s + 1);
    }

    m_spectrumWidget->setSpectrums(spectrums, labels);

    // 自动计算最佳频率显示范围
    int optStart = 0, optEnd = 0;
    m_spectrumWidget->computeOptimalRange(optStart, optEnd);

    // 设置频率范围控件
    m_freqRangeStart->setMaximum(halfSize - 1);
    m_freqRangeEnd->setMaximum(halfSize);
    m_freqRangeStart->setValue(optStart);
    m_freqRangeEnd->setValue(optEnd);
    m_freqRangeStart->setEnabled(true);
    m_freqRangeEnd->setEnabled(true);

    m_spectrumWidget->setDisplayRange(optStart, optEnd);

    // 打印每个信号的详细信息
    result += tr("\n--- 前%1个信号详细信息 ---\n").arg(numToShow);
    for (int s = 0; s < numToShow; ++s) {
        // 找峰值
        int peakIdx = 0;
        double peakVal = 0;
        double sumVal = 0;
        for (int i = 0; i < halfSize; ++i) {
            double v = spectrums[s][i];
            sumVal += v;
            if (v > peakVal) {
                peakVal = v;
                peakIdx = i;
            }
        }
        double avgVal = sumVal / halfSize;

        // 找前3大峰值
        QVector<QPair<double, int>> topPeaks;
        for (int i = 1; i < halfSize - 1; ++i) {
            if (spectrums[s][i] > spectrums[s][i - 1] &&
                spectrums[s][i] > spectrums[s][i + 1] &&
                spectrums[s][i] > avgVal * 3) {
                topPeaks.append(qMakePair(spectrums[s][i], i));
            }
        }
        std::sort(topPeaks.begin(), topPeaks.end(),
                  [](const QPair<double, int> &a, const QPair<double, int> &b) {
                      return a.first > b.first;
                  });

        result += tr("\n信号 %1:\n").arg(s + 1);
        result += tr("  最大幅值: %1 @ 频率索引 %2\n")
                  .arg(peakVal, 0, 'f', 2).arg(peakIdx);
        result += tr("  平均幅值: %1\n").arg(avgVal, 0, 'f', 2);
        result += tr("  信噪比: %1 dB\n")
                  .arg(peakVal > 0 && avgVal > 0 ? 20.0 * log10(peakVal / avgVal) : 0, 0, 'f', 1);

        int peakCount = qMin(3, topPeaks.size());
        if (peakCount > 0) {
            result += tr("  前%1大峰值: ").arg(peakCount);
            for (int p = 0; p < peakCount; ++p) {
                if (p > 0) result += tr(", ");
                result += tr("%1@%2").arg(topPeaks[p].first, 0, 'f', 2)
                          .arg(topPeaks[p].second);
            }
            result += tr("\n");
        } else {
            result += tr("  无显著峰值\n");
        }
    }

    result += tr("\n频率显示范围: %1 - %2 (自动调整)\n").arg(optStart).arg(optEnd);

    m_resultTextEdit->setPlainText(result);

    // 重新启用按钮
    m_startButton->setEnabled(true);
    m_fftTask->deleteLater();
}

void FFTWidget::onFreqRangeChanged()
{
    int start = m_freqRangeStart->value();
    int end = m_freqRangeEnd->value();
    if (end <= start)
        return;
    m_spectrumWidget->setDisplayRange(start, end);
}
