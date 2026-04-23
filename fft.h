/****************************************************************************
**
** Copyright (C) 2024
** FFT module - self-contained widget
**
****************************************************************************/

#ifndef FFT_H
#define FFT_H

#include <QWidget>
#include <QThread>
#include <QMutex>
#include <QVector>

QT_BEGIN_NAMESPACE
class QSpinBox;
class QPushButton;
class QProgressBar;
class QTextEdit;
class QLabel;
QT_END_NAMESPACE

/**
 * @brief 频域图谱绘制控件
 *
 * 支持同时显示多条频谱曲线，X轴为频率索引，Y轴为幅值
 */
class FFTSpectrumWidget : public QWidget
{
    Q_OBJECT

public:
    explicit FFTSpectrumWidget(QWidget *parent = nullptr);

    void setSpectrums(const QVector<QVector<double>> &spectrums,
                      const QStringList &labels = QStringList());
    void setDisplayRange(int startIdx, int endIdx);
    void setMaxVisibleSignals(int count);

    /**
     * @brief 自动计算最佳显示范围
     * @param startIdx 返回最佳起始频率索引
     * @param endIdx 返回最佳结束频率索引
     *
     * 策略：找到所有信号中最后一个显著峰值的频率位置，
     * 然后在此基础上留20%余量，确保所有峰值都可见
     */
    void computeOptimalRange(int &startIdx, int &endIdx) const;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<QVector<double>> m_spectrums;
    QStringList m_labels;
    int m_startIdx;
    int m_endIdx;
    int m_maxVisibleSignals;
    double m_maxValue;

    static QColor signalColor(int index);
    void drawGrid(QPainter &painter, const QRect &plotArea);
    void drawCurves(QPainter &painter, const QRect &plotArea);
    void drawLegend(QPainter &painter, const QRect &plotArea);
};

/**
 * @brief 多线程FFT计算任务
 *
 * 对多个独立信号执行快速傅里叶变换，支持单线程和多线程模式
 */
class FFTTask : public QThread
{
    Q_OBJECT

public:
    FFTTask(const QVector<double> &data, int threadCount, QObject *parent = nullptr);
    void run() override;
    QVector<double> result() const { return m_result; }
    int threadCount() const { return m_threadCount; }
    qint64 elapsedTime() const { return m_elapsed; }

signals:
    void progressChanged(int value);

private:
    QVector<double> m_data;
    QVector<double> m_result;
    int m_threadCount;
    QMutex m_mutex;
    qint64 m_elapsed;
};

/**
 * @brief FFT演示主控件
 *
 * 包含线程数选择、开始按钮、进度条、频域图谱和结果显示区域
 */
class FFTWidget : public QWidget
{
    Q_OBJECT

public:
    explicit FFTWidget(QWidget *parent = nullptr);

private slots:
    void startFFT();
    void fftFinished();
    void onFreqRangeChanged();

private:
    void createUI();

    QSpinBox *m_threadCountSpinBox;
    QPushButton *m_startButton;
    QProgressBar *m_progressBar;
    FFTSpectrumWidget *m_spectrumWidget;
    QTextEdit *m_resultTextEdit;
    QSpinBox *m_freqRangeStart;
    QSpinBox *m_freqRangeEnd;

    FFTTask *m_fftTask;
    QVector<double> m_fftFullResult;
    int m_signalCount;
    int m_signalSize;
};

#endif // FFT_H
