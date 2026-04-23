/****************************************************************************
**
** Copyright (C) 2024
** DJS-103 Emulator Widget - Qt GUI for the 103 computer emulator
**
****************************************************************************/

#ifndef DJS103WIDGET_H
#define DJS103WIDGET_H

#include <QWidget>
#include <QMap>
#include "djs103_emulator.h"

QT_BEGIN_NAMESPACE
class QPushButton;
class QPlainTextEdit;
class QTableWidget;
class QLabel;
class QSpinBox;
class QLineEdit;
class QSplitter;
class QComboBox;
QT_END_NAMESPACE

/**
 * @brief 103机模拟器界面控件
 *
 * 包含代码编辑器、控制面板、寄存器/内存视图和输出控制台
 * 支持 M3 文件格式和交互式输入
 */
class DJS103Widget : public QWidget
{
    Q_OBJECT

public:
    explicit DJS103Widget(QWidget *parent = nullptr);

private slots:
    void onLoadProgram();
    void onStep();
    void onRun();
    void onStop();
    void onReset();
    void onLoadExample();
    void onLoadExample2();
    void onLoadExample3();
    void onLoadExample4();
    void onMemoryCellChanged(int row, int col);
    void onRunTick();
    void onHelp();

private:
    void createUI();
    void updateRegisterDisplay();
    void updateMemoryDisplay();
    void appendOutput(const QString &text);
    void appendOutput(const std::string &text);
    void loadM3Program(const QString &text);

    // Emulator core
    DJS103Emulator m_emulator;

    // Controls
    QPushButton *m_loadButton;
    QPushButton *m_stepButton;
    QPushButton *m_runButton;
    QPushButton *m_stopButton;
    QPushButton *m_resetButton;
    QPushButton *m_exampleButton;
    QPushButton *m_example2Button;
    QPushButton *m_example3Button;
    QPushButton *m_example4Button;
    QPushButton *m_helpButton;

    // Register display
    QLabel *m_accLabel;
    QLabel *m_accValueLabel;
    QLabel *m_pcLabel;
    QLabel *m_statusLabel;
    QLabel *m_instLabel;

    // LED display for 31-bit register
    static const int NUM_LEDS = 31;
    QLabel *m_leds[NUM_LEDS];      // last instruction LEDs
    QLabel *m_accLeds[NUM_LEDS];   // accumulator LEDs

    // Code editor
    QPlainTextEdit *m_codeEdit;

    // Memory view
    QTableWidget *m_memTable;

    // Output console
    QPlainTextEdit *m_outputEdit;

    // Run timer
    QTimer *m_runTimer;
    bool m_isRunning;
    int m_runSpeed;

    // Memory update guard
    bool m_updatingMemory;

    // Source line tracking: memory address -> editor line number (0-based)
    QMap<int, int> m_addrToLine;

    void highlightCurrentLine();
};

#endif // DJS103WIDGET_H
