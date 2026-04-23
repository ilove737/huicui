/****************************************************************************
**
** Copyright (C) 2024
** DJS-103 Emulator Widget - Qt GUI Implementation
**
** 完整的 DJS-103 (M-3) 模拟器界面
** 支持 M3 文件格式: :AAAA(地址), =xxx(十进制小数), @AAAA(起始执行地址)
** 支持交互式输入对话框
**
****************************************************************************/

#include <QtWidgets>
#include <QTimer>
#include <QHeaderView>
#include <QFont>
#include <QSplitter>
#include <QMessageBox>
#include <QInputDialog>
#include <QRegularExpression>
#include <sstream>
#include <iomanip>

#include "djs103widget.h"

// ============ DJS103Widget Implementation ============

DJS103Widget::DJS103Widget(QWidget *parent)
    : QWidget(parent)
    , m_isRunning(false)
    , m_runSpeed(1)
    , m_updatingMemory(false)
{
    m_emulator.setOutputCallback([this](const std::string &s) {
        appendOutput(s);
    });

    m_emulator.setInputCallback([this]() -> double {
        double val = 0.0;
        // This is called from the emulator thread context (timer tick),
        // so we need to use a synchronous dialog
        QMetaObject::invokeMethod(this, [this, &val]() {
            bool ok = false;
            val = QInputDialog::getDouble(this, tr("103机输入"),
                                          tr("请输入一个定点小数 (|x|<1):"),
                                          0.0, -1.0, 1.0, 10, &ok);
            if (!ok) val = 0.0;
        }, Qt::BlockingQueuedConnection);
        return val;
    });

    createUI();
}

void DJS103Widget::createUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout;

    // === Top: Register Display ===
    QGroupBox *regGroup = new QGroupBox(tr("寄存器状态"));
    QGridLayout *regLayout = new QGridLayout;

    QFont monoFont("Monospace", 10);
    QFont boldFont("Monospace", 10, QFont::Bold);
    QFont memFont("Monospace", 11, QFont::Bold);

    // === Accumulator LED display (above accLabel) ===
    static const int accLedGroups[] = {1, 30};
    static const int accNumGroups = 2;
    static const char *accGroupNames[] = {"符号", "数值"};

    QVBoxLayout *accLedVLayout = new QVBoxLayout;

    QHBoxLayout *accLedOuterLayout = new QHBoxLayout;
    QHBoxLayout *accLedRowLayout = new QHBoxLayout;
    accLedRowLayout->setSpacing(30);

    int accBitIndex = 30;
    for (int g = 0; g < accNumGroups; ++g) {
        int groupSize = accLedGroups[g];

        QVBoxLayout *groupVLayout = new QVBoxLayout;
        groupVLayout->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        groupVLayout->setSpacing(4);

        QLabel *groupLabel = new QLabel(tr(accGroupNames[g]));
        groupLabel->setFont(QFont("Monospace", 7));
        groupLabel->setAlignment(Qt::AlignCenter);
        groupVLayout->addWidget(groupLabel);

        QHBoxLayout *groupLedLayout = new QHBoxLayout;
        groupLedLayout->setSpacing(15);

        int remaining = groupSize;
        while (remaining > 0) {
            int subSize = (remaining >= 3) ? 3 : remaining;

            QVBoxLayout *subVLayout = new QVBoxLayout;
            subVLayout->setSpacing(0);

            QHBoxLayout *subLedLayout = new QHBoxLayout;
            subLedLayout->setSpacing(1);
            for (int j = 0; j < subSize; ++j) {
                int i = accBitIndex - j;
                m_accLeds[i] = new QLabel;
                m_accLeds[i]->setFixedSize(14, 14);
                m_accLeds[i]->setAlignment(Qt::AlignCenter);
                m_accLeds[i]->setMargin(0);
                m_accLeds[i]->setToolTip(tr("位 %1").arg(i));
                subLedLayout->addWidget(m_accLeds[i]);
            }
            subVLayout->addLayout(subLedLayout);

            QHBoxLayout *subLabelLayout = new QHBoxLayout;
            subLabelLayout->setSpacing(1);
            static const char *weightLabels[] = {"4", "2", "1"};
            for (int j = 0; j < subSize; ++j) {
                QLabel *wLabel = new QLabel(tr(weightLabels[2 - (subSize - 1 - j)]));
                wLabel->setFixedSize(14, 10);
                wLabel->setAlignment(Qt::AlignCenter);
                wLabel->setFont(QFont("Monospace", 6));
                subLabelLayout->addWidget(wLabel);
            }
            subVLayout->addLayout(subLabelLayout);

            groupLedLayout->addLayout(subVLayout);

            accBitIndex -= subSize;
            remaining -= subSize;
        }

        groupVLayout->addLayout(groupLedLayout);
        accLedRowLayout->addLayout(groupVLayout);
    }

    accLedOuterLayout->addLayout(accLedRowLayout);
    accLedOuterLayout->addStretch(1);
    accLedVLayout->addLayout(accLedOuterLayout);

    regLayout->addLayout(accLedVLayout, 0, 0, 1, 2);

    // LED display for 31-bit last instruction
    // Groups: 1 (sign bit30), 6 (opcode bit24-29), 12 (addr1 bit12-23), 12 (addr2 bit0-11)
    // Sub-groups of 3 LEDs labeled 1,2,4 (weight within each octal digit)
    static const int ledGroups[] = {1, 6, 12, 12};
    static const int numGroups = 4;
    static const char *groupNames[] = {"符号", "操作码", "地址A", "地址B"};

    // Register text labels
    m_accLabel = new QLabel(tr("累加器 r: 00000000000 (八进制)"));
    m_accValueLabel = new QLabel(tr("= 0.0000000000 (十进制小数)"));
    m_pcLabel = new QLabel(tr("程序计数器 PC: 0000 (八进制) = 0 (十进制)"));
    m_statusLabel = new QLabel(tr("状态: 停机"));
    m_instLabel = new QLabel(tr("上次指令: 无"));

    m_accLabel->setFont(monoFont);
    m_accValueLabel->setFont(boldFont);
    m_pcLabel->setFont(monoFont);
    m_statusLabel->setFont(monoFont);
    m_instLabel->setFont(monoFont);

    regLayout->addWidget(m_accLabel, 1, 0);
    regLayout->addWidget(m_accValueLabel, 1, 1);
    regLayout->addWidget(m_pcLabel, 2, 0);
    regLayout->addWidget(m_statusLabel, 2, 1);

    QVBoxLayout *ledVLayout = new QVBoxLayout;

    QHBoxLayout *ledOuterLayout = new QHBoxLayout;
    QHBoxLayout *ledRowLayout = new QHBoxLayout;
    ledRowLayout->setSpacing(30);

    int bitIndex = 30; // start from highest bit
    for (int g = 0; g < numGroups; ++g) {
        int groupSize = ledGroups[g];

        QVBoxLayout *groupVLayout = new QVBoxLayout;
        groupVLayout->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        groupVLayout->setSpacing(4);

        // Group label
        QLabel *groupLabel = new QLabel(tr(groupNames[g]));
        groupLabel->setFont(QFont("Monospace", 7));
        groupLabel->setAlignment(Qt::AlignCenter);
        groupVLayout->addWidget(groupLabel);

        // LED row: split into sub-groups of 3, labeled 4,2,1
        QHBoxLayout *groupLedLayout = new QHBoxLayout;
        groupLedLayout->setSpacing(10); // spacing between sub-groups

        int remaining = groupSize;
        while (remaining > 0) {
            int subSize = (remaining >= 3) ? 3 : remaining;

            QVBoxLayout *subVLayout = new QVBoxLayout;
            subVLayout->setSpacing(0);

            // Sub-group LED row
            QHBoxLayout *subLedLayout = new QHBoxLayout;
            subLedLayout->setSpacing(1);
            for (int j = 0; j < subSize; ++j) {
                int i = bitIndex - j;
                m_leds[i] = new QLabel;
                m_leds[i]->setFixedSize(14, 14);
                m_leds[i]->setAlignment(Qt::AlignCenter);
                m_leds[i]->setMargin(0);
                m_leds[i]->setToolTip(tr("位 %1").arg(i));
                subLedLayout->addWidget(m_leds[i]);
            }
            subVLayout->addLayout(subLedLayout);

            // Sub-group weight labels: 4,2,1 (from high to low within each octal digit)
            QHBoxLayout *subLabelLayout = new QHBoxLayout;
            subLabelLayout->setSpacing(1);
            static const char *weightLabels[] = {"4", "2", "1"};
            for (int j = 0; j < subSize; ++j) {
                QLabel *wLabel = new QLabel(tr(weightLabels[2 - (subSize - 1 - j)]));
                wLabel->setFixedSize(14, 10);
                wLabel->setAlignment(Qt::AlignCenter);
                wLabel->setFont(QFont("Monospace", 6));
                subLabelLayout->addWidget(wLabel);
            }
            subVLayout->addLayout(subLabelLayout);

            groupLedLayout->addLayout(subVLayout);

            bitIndex -= subSize;
            remaining -= subSize;
        }

        groupVLayout->addLayout(groupLedLayout);

        ledRowLayout->addLayout(groupVLayout);
    }

    ledOuterLayout->addLayout(ledRowLayout);
    ledOuterLayout->addStretch(1);
    ledVLayout->addLayout(ledOuterLayout);

    regLayout->addLayout(ledVLayout, 4, 0, 1, 2);

    regLayout->addWidget(m_instLabel, 5, 0, 1, 2);

    regGroup->setLayout(regLayout);

    // Settings & Help group
    QGroupBox *settingsGroup = new QGroupBox(tr("设置与帮助"));
    QVBoxLayout *settingsLayout = new QVBoxLayout;

    QHBoxLayout *speedLayout = new QHBoxLayout;
    speedLayout->addWidget(new QLabel(tr("速度:")));
    QComboBox *speedCombo = new QComboBox;
    speedCombo->addItem(tr("磁鼓存储器(30次/秒)"), 3);
    speedCombo->addItem(tr("磁芯存储器(1800次/秒)"), 180);
    speedCombo->setCurrentIndex(0);
    m_runSpeed = 3;
    speedLayout->addWidget(speedCombo);
    speedLayout->addStretch();
    settingsLayout->addLayout(speedLayout);

    m_helpButton = new QPushButton(tr("帮助"));
    settingsLayout->addWidget(m_helpButton);
    settingsLayout->addStretch();

    settingsGroup->setLayout(settingsLayout);

    QHBoxLayout *topLayout = new QHBoxLayout;
    topLayout->addWidget(regGroup, 1);
    topLayout->addWidget(settingsGroup);
    mainLayout->addLayout(topLayout);

    // === Middle: Code Editor (left) + Control & Output (right) ===
    QSplitter *midSplitter = new QSplitter(Qt::Horizontal);

    // -- Left: Code Editor --
    QGroupBox *codeGroup = new QGroupBox(tr("程序代码"));
    QVBoxLayout *codeLayout = new QVBoxLayout;

    m_codeEdit = new QPlainTextEdit;
    m_codeEdit->setFont(monoFont);
    m_codeEdit->setPlaceholderText(tr("在此输入103机程序可载入执行"));
    codeLayout->addWidget(m_codeEdit);

    // Example buttons
    QHBoxLayout *exampleLayout = new QHBoxLayout;
    m_exampleButton = new QPushButton(tr("示例: 加法"));
    m_example2Button = new QPushButton(tr("示例: 乘法"));
    m_example3Button = new QPushButton(tr("示例: sin(x)"));
    m_example4Button = new QPushButton(tr("示例: 鸡兔同笼"));
    exampleLayout->addWidget(m_exampleButton);
    exampleLayout->addWidget(m_example2Button);
    exampleLayout->addWidget(m_example3Button);
    exampleLayout->addWidget(m_example4Button);
    exampleLayout->addStretch();
    codeLayout->addLayout(exampleLayout);

    codeGroup->setLayout(codeLayout);
    midSplitter->addWidget(codeGroup);

    // -- Right: Control + Output stacked vertically --
    QWidget *rightPanel = new QWidget;
    QVBoxLayout *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    // Output Console
    QGroupBox *outGroup = new QGroupBox(tr("输出控制台"));
    QVBoxLayout *outLayout = new QVBoxLayout;

    m_outputEdit = new QPlainTextEdit;
    m_outputEdit->setFont(monoFont);
    m_outputEdit->setReadOnly(true);
    outLayout->addWidget(m_outputEdit);

    outGroup->setLayout(outLayout);
    rightLayout->addWidget(outGroup, 1);

    // Control Buttons
    QGroupBox *ctrlGroup = new QGroupBox(tr("控制"));
    QVBoxLayout *ctrlLayout = new QVBoxLayout;

    // Row 1: main buttons
    QHBoxLayout *ctrlRow1 = new QHBoxLayout;
    m_loadButton = new QPushButton(tr("装入程序"));
    m_stepButton = new QPushButton(tr("单步执行"));
    m_runButton = new QPushButton(tr("连续运行"));
    m_stopButton = new QPushButton(tr("停止"));
    m_resetButton = new QPushButton(tr("复位"));

    m_stopButton->setEnabled(false);

    ctrlRow1->addWidget(m_loadButton);
    ctrlRow1->addWidget(m_stepButton);
    ctrlRow1->addWidget(m_runButton);
    ctrlRow1->addWidget(m_stopButton);
    ctrlRow1->addWidget(m_resetButton);
    ctrlRow1->addStretch();
    ctrlLayout->addLayout(ctrlRow1);

    ctrlGroup->setLayout(ctrlLayout);
    rightLayout->addWidget(ctrlGroup);

    midSplitter->addWidget(rightPanel);
    midSplitter->setSizes(QList<int>() << 500 << 350);
    mainLayout->addWidget(midSplitter, 1);

    // === Bottom: Memory View ===
    QGroupBox *memGroup = new QGroupBox(tr("内存视图 (1024字)"));
    QVBoxLayout *memLayout = new QVBoxLayout;

    m_memTable = new QTableWidget(128, 9);
    m_memTable->setFont(memFont);
    m_memTable->horizontalHeader()->setDefaultSectionSize(120);
    m_memTable->setColumnWidth(0, 60);
    m_memTable->verticalHeader()->setDefaultSectionSize(20);

    // Set headers: column 0 = address, columns 1-8 = octal words
    QStringList headers;
    headers << tr("地址");
    for (int i = 0; i < 8; ++i)
        headers << QString("+%1").arg(i);
    m_memTable->setHorizontalHeaderLabels(headers);
    m_memTable->verticalHeader()->hide();
    m_memTable->horizontalHeader()->setStretchLastSection(true);
    m_memTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);

    memLayout->addWidget(m_memTable);
    memGroup->setLayout(memLayout);
    mainLayout->addWidget(memGroup, 1);

    setLayout(mainLayout);

    // === Connections ===
    connect(m_loadButton, &QPushButton::clicked, this, &DJS103Widget::onLoadProgram);
    connect(m_stepButton, &QPushButton::clicked, this, &DJS103Widget::onStep);
    connect(m_runButton, &QPushButton::clicked, this, &DJS103Widget::onRun);
    connect(m_stopButton, &QPushButton::clicked, this, &DJS103Widget::onStop);
    connect(m_resetButton, &QPushButton::clicked, this, &DJS103Widget::onReset);
    connect(m_exampleButton, &QPushButton::clicked, this, &DJS103Widget::onLoadExample);
    connect(m_example2Button, &QPushButton::clicked, this, &DJS103Widget::onLoadExample2);
    connect(m_example3Button, &QPushButton::clicked, this, &DJS103Widget::onLoadExample3);
    connect(m_example4Button, &QPushButton::clicked, this, &DJS103Widget::onLoadExample4);
    connect(m_helpButton, &QPushButton::clicked, this, &DJS103Widget::onHelp);
    connect(m_memTable, &QTableWidget::cellChanged, this, &DJS103Widget::onMemoryCellChanged);
    connect(speedCombo, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, [this, speedCombo]() {
        m_runSpeed = speedCombo->currentData().toInt();
    });

    // Run timer
    m_runTimer = new QTimer(this);
    connect(m_runTimer, &QTimer::timeout, this, &DJS103Widget::onRunTick);

    // Initialize display
    updateRegisterDisplay();
    updateMemoryDisplay();
}

void DJS103Widget::onLoadProgram()
{
    QString text = m_codeEdit->toPlainText().trimmed();
    if (text.isEmpty()) {
        appendOutput(tr("ERROR: 代码为空"));
        return;
    }

    m_emulator.reset();
    loadM3Program(text);
    updateRegisterDisplay();
    updateMemoryDisplay();
    appendOutput(tr("程序已装入, PC=%1 (八进制)")
                 .arg(m_emulator.getProgramCounter(), 4, 8, QChar('0')));
}

void DJS103Widget::onStep()
{
    if (m_emulator.isHalted()) {
        appendOutput(tr("已停机，请装入程序或复位"));
        return;
    }
    m_emulator.step();
    updateRegisterDisplay();
    updateMemoryDisplay();
}

void DJS103Widget::onRun()
{
    if (m_emulator.isHalted()) {
        appendOutput(tr("已停机，请装入程序或复位"));
        return;
    }

    m_isRunning = true;
    m_runButton->setEnabled(false);
    m_stepButton->setEnabled(false);
    m_loadButton->setEnabled(false);
    m_stopButton->setEnabled(true);
    m_resetButton->setEnabled(false);

    m_runTimer->start(100);
}

void DJS103Widget::onStop()
{
    m_runTimer->stop();
    m_isRunning = false;
    m_emulator.stop();

    m_runButton->setEnabled(true);
    m_stepButton->setEnabled(true);
    m_loadButton->setEnabled(true);
    m_stopButton->setEnabled(false);
    m_resetButton->setEnabled(true);

    updateRegisterDisplay();
    updateMemoryDisplay();
}

void DJS103Widget::onReset()
{
    m_runTimer->stop();
    m_isRunning = false;
    m_emulator.reset();

    m_runButton->setEnabled(true);
    m_stepButton->setEnabled(true);
    m_loadButton->setEnabled(true);
    m_stopButton->setEnabled(false);
    m_resetButton->setEnabled(true);

    updateRegisterDisplay();
    updateMemoryDisplay();
    appendOutput(tr("--- 复位 ---"));
}

void DJS103Widget::onLoadExample()
{
    // 示例: 计算 0.3 + 0.5 并打印结果
    //
    // 103机汇编手册格式: ±操作码 地址1 地址2
    // 操作码为两位八进制XY: X=修饰符, Y=操作(0加,1减,2除,3乘)
    //
    // +05: 传送 (addr1)->(addr2), r<-数据
    // +00: 加法 b,r=a+b  (X=0,Y=0: 结果存addr2和r)
    // +45: 传送并打印
    // +04: 停机
    //
    // 单元分配:
    // 0000-0003: 程序
    // 0004: =0.3
    // 0005: =0 (临时)
    // 0006: =0.5

    QString program =
        "; 示例: 计算 0.3 + 0.5 并打印结果\n"
        "; 汇编格式: ±操作码(八进制) 地址1 地址2\n"
        ";\n"
        "0000\n"
        "+05  0004  0005   ; 传送 [4]->[5]\n"
        "+00  0004  0006   ; 加 [4]+[6]->[6]\n"
        "+45  0006  0006   ; 打印 [6]\n"
        "+04  0000  0000   ; 停机\n"
        ";\n"
        "0004\n"
        "=0.3               ; 常数\n"
        "=0                 ; 临时\n"
        "=0.5               ; 常数\n"
        "@0000\n";

    m_codeEdit->setPlainText(program);
    appendOutput(tr("示例程序已载入: 加法 0.3 + 0.5\n点击\"装入程序\"后执行"));
}

void DJS103Widget::onLoadExample2()
{
    // 示例: 计算 0.5 * 0.5 = 0.25 并打印
    // +03: X=0,Y=3 乘法 b,r=a*b
    //
    // 单元分配:
    // 0000-0003: 程序
    // 0004: =0.5
    // 0005: =0 (结果)

    QString program =
        "; 示例: 计算 0.5 * 0.5 = 0.25 并打印结果\n"
        ";\n"
        "0000\n"
        "+05  0004  0005   ; 传送 [4]->[5]\n"
        "+03  0004  0005   ; 乘 [4]*[5]->[5]\n"
        "+45  0005  0005   ; 打印 [5]\n"
        "+04  0000  0000   ; 停机\n"
        ";\n"
        "0004\n"
        "=0.5               ; 常数\n"
        "=0                 ; 结果\n"
        "@0000\n";

    m_codeEdit->setPlainText(program);
    appendOutput(tr("示例程序已载入: 乘法 0.5 * 0.5\n点击\"装入程序\"后执行"));
}

void DJS103Widget::onLoadExample3()
{
    // 示例: 计算 sin x (来自103机汇编手册)
    //
    // 计算公式: 1/2 * sin(pi/2 * x) = (((((c11*x^2+c9)*x^2+c7)*x^2+c5)*x^2+c3)*x^2+c1)*x
    //
    // 单元分配:
    // 0001: 自变量 x
    // 0002: 函数结果
    // 0010--0035: 子程序(20条), 0030-0035为常数
    //
    // 常数:
    // c1 = +0.62 20 77 32 50
    // c3 = -0.24 52 73 63 50
    // c5 = +0.02 43 15 36 67
    // c7 = -0.00 11 45 53 25
    // c9 = +0.00 00 25 03 43
    // c11= -0.00 00 00 36 04

    QString program =
        "; 示例: 计算 1/2*sin(pi/2*x) (来自103机汇编手册)\n"
        "; 公式: (((((c11*x^2+c9)*x^2+c7)*x^2+c5)*x^2+c3)*x^2+c1)*x\n"
        ";\n"
        "; --- 数据区 (单元分配) ---\n"
        ":0001\n"
        "=0.8               ; 自变量 x\n"
        ";\n"
        ":0002\n"
        "=0                 ; 结果存放单元\n"
        ";\n"
        "; --- 子程序 ---\n"
        ":0010\n"
        "+13  0001  0001     ; 乘 [1]*[1]->r = x^2\n"
        "+24  0012  0002     ; 跳转 PC<-[12],r->[02]\n"
        "+33  0030  0000     ; 乘 r*[30]->r\n"
        "+30  0031  0000     ; 加 r+[31]->r\n"
        "+33  0002  0000     ; 乘 r*[02]->r\n"
        "+30  0032  0000     ; 加 r+[32]->r\n"
        "+33  0002  0000     ; 乘 r*[02]->r\n"
        "+30  0033  0000     ; 加 r+[33]->r\n"
        "+33  0002  0000     ; 乘 r*[02]->r\n"
        "+30  0034  0000     ; 加 r+[34]->r\n"
        "+33  0002  0000     ; 乘 r*[02]->r\n"
        "+30  0035  0000     ; 加 r+[35]->r\n"
        "+63  0001  0002     ; 乘 r*[01]->[02],r  打印结果\n"
        "+04  0000  0000     ; 停机\n"
        "; --- 常数区 ---\n"
        ":0030\n"
        "-00  0000  3604     ; c11\n"
        "+00  0025  0343     ; c9\n"
        "-00  1145  5325     ; c7\n"
        "+02  4315  3667     ; c5\n"
        "-24  5273  6350     ; c3\n"
        "+62  2077  3250     ; c1\n"
        "@0010               ; 程序入口\n\n";

    m_codeEdit->setPlainText(program);
    appendOutput(tr("示例程序已载入: sin(x) 子程序 (来自103机汇编手册)\n点击\"装入程序\"后执行"));
}

void DJS103Widget::onLoadExample4()
{
    // 今有雉兔同笼，上有三十五头，下有九十四足，问雉兔各几何？
    // 鸡兔同笼问题程序: 已知总头数h、总脚数f，求鸡x、兔y
    // 存储单元分配:
    // 0020: 常数 0.5 (用于计算 f/2)
    // 0021: 总头数 h (输入值，需除以100，如35→0.35)
    // 0022: 总脚数 f (输入值，需除以100，如94→0.94)
    // 0023: 临时单元 - 存储 f/2
    // 0024: 兔的数量 y (结果，除以100后的值)
    // 0025: 鸡的数量 x (结果，除以100后的值)

    QString program =
        "; 今有雉兔同笼，上有三十五头，下有九十四足，问雉兔各几何？\n"
        "; 鸡兔同笼问题程序: 已知总头数h、总脚数f，求鸡x、兔y\n"
        ";\n"
        ":0000                  ; 程序起始地址\n"
        "+05  0022  0023       ; 传送: f → 0023，同时 r ← f\n"
        "+03  0020  0023       ; 乘法: r(f) × 0020(0.5) → 0023和r (现在r=f/2)\n"
        "+21  0021  0024       ; 减法: r(f/2) - 0021(h) → 0024和r (现在r=y=f/2-h)\n"
        "+05  0021  0000       ; 传送: h → 0000(仅用于加载r)，r ← h\n"
        "+21  0024  0025       ; 减法: r(h) - 0024(y) → 0025和r   (现在r=x=h-y)\n"
        "+45  0025  0025       ; 打印: 输出鸡的数量 (除以100后的值)\n"
        "+45  0024  0024       ; 打印: 输出兔的数量 (除以100后的值)\n"
        "+04  0000  0000       ; 停机\n"
        ";\n"
        "; 常数区\n"
        ":0020\n"
        "=0.5                  ; 常数 0.5 (用于除以2)\n"
        "=0.35                 ; h 总头数 (输入值，需除以100，如35→0.35)\n"
        "=0.94                 ; f 总脚数 (输入值，需除以100，如94→0.94)\n"
        "=0                    ; 临时单元\n"
        "=0                    ; y 兔的数量 (结果，除以100后的值)\n"
        "=0                    ; x 鸡的数量 (结果，除以100后的值)\n"
        "@0000                 ; 程序入口地址\n";

    m_codeEdit->setPlainText(program);
    appendOutput(tr("示例程序已载入: 鸡兔同笼问题\n点击\"装入程序\"后执行"));
}

void DJS103Widget::onMemoryCellChanged(int row, int col)
{
    if (m_updatingMemory || col == 0)
        return;

    QTableWidgetItem *item = m_memTable->item(row, col);
    if (!item)
        return;

    QString text = item->text().trimmed();
    int32_t value;

    if (text.startsWith("=")) {
        // Decimal fraction input
        bool ok = false;
        double dval = text.mid(1).toDouble(&ok);
        if (!ok) return;
        value = DJS103Emulator::doubleToWord(dval);
    } else {
        bool ok = false;
        value = text.toLong(&ok, 8);
        if (!ok) {
            // Try decimal
            value = text.toInt(&ok);
            if (!ok) return;
        }
    }

    int addr = row * 8 + (col - 1);
    if (addr >= 0 && addr < DJS103Emulator::MEMORY_SIZE) {
        m_emulator.setMemory(addr, value);
    }
}

void DJS103Widget::onRunTick()
{
    for (int i = 0; i < m_runSpeed; ++i) {
        if (m_emulator.isHalted()) {
            onStop();
            return;
        }
        m_emulator.step();
    }
    updateRegisterDisplay();
    updateMemoryDisplay();
}

void DJS103Widget::updateRegisterDisplay()
{
    int32_t acc = m_emulator.getAccumulator();
    int32_t pc = m_emulator.getProgramCounter();
    double accValue = m_emulator.getAccumulatorValue();

    m_accLabel->setText(tr("累加器 r: %1 (八进制)")
                        .arg(acc & DJS103Emulator::WORD_MASK, 11, 8, QChar('0')));
    // 智能精度显示：找到最短的有效表示，避免暴露定点量化误差
    QString accStr;
    double absVal = fabs(accValue);
    if (absVal == 0.0) {
        accStr = "0";
    } else {
        for (int prec = 1; prec <= 10; ++prec) {
            QString test = QString::number(accValue, 'f', prec);
            if (fabs(test.toDouble() - accValue) < 1e-12) {
                accStr = test;
                break;
            }
        }
        if (accStr.isEmpty())
            accStr = QString::number(accValue, 'f', 10);
    }
    m_accValueLabel->setText(tr("= %1 (十进制小数)").arg(accStr));
    m_pcLabel->setText(tr("程序计数器 PC: %1 (八进制) = %2 (十进制)")
                       .arg(pc, 4, 8, QChar('0'))
                       .arg(pc));

    if (m_emulator.isHalted()) {
        m_statusLabel->setText(tr("状态: 停机"));
        m_statusLabel->setStyleSheet("color: red;");
    } else if (m_isRunning) {
        m_statusLabel->setText(tr("状态: 运行中"));
        m_statusLabel->setStyleSheet("color: green;");
    } else {
        m_statusLabel->setText(tr("状态: 就绪"));
        m_statusLabel->setStyleSheet("color: blue;");
    }

    // Decode last instruction
    int32_t lastInst = m_emulator.getLastInstruction();
    if (lastInst != 0) {
        int opcode = m_emulator.getLastOpcode();
        int addr1 = m_emulator.getLastAddr1();
        int addr2 = m_emulator.getLastAddr2();
        std::string mnemonic = m_emulator.getLastInstructionMnemonic();
        m_instLabel->setText(tr("上次指令: %1  XY=%2 A=%3 B=%4  %5")
                             .arg(lastInst & DJS103Emulator::WORD_MASK, 11, 8, QChar('0'))
                             .arg(opcode, 2, 8, QChar('0'))
                             .arg(addr1, 4, 8, QChar('0'))
                             .arg(addr2, 4, 8, QChar('0'))
                             .arg(QString::fromStdString(mnemonic)));
    } else {
        m_instLabel->setText(tr("上次指令: 无"));
    }

    // Update accumulator LED display
    int32_t accLedBits = acc & DJS103Emulator::WORD_MASK;
    for (int i = 0; i < NUM_LEDS; ++i) {
        bool bitOn = (accLedBits >> i) & 1;
        if (bitOn) {
            m_accLeds[i]->setStyleSheet(
                "QLabel { background-color: #ff3030; border: 1px solid #cc0000; "
                "border-radius: 7px; }");
        } else {
            m_accLeds[i]->setStyleSheet(
                "QLabel { background-color: #3a3a3a; border: 1px solid #555555; "
                "border-radius: 7px; }");
        }
    }

    // Update LED display for 31-bit last instruction
    int32_t accBits = lastInst & DJS103Emulator::WORD_MASK;
    for (int i = 0; i < NUM_LEDS; ++i) {
        bool bitOn = (accBits >> i) & 1;
        if (bitOn) {
            // LED on: bright colored circle
            m_leds[i]->setStyleSheet(
                "QLabel { background-color: #ff3030; border: 1px solid #cc0000; "
                "border-radius: 7px; }");
        } else {
            // LED off: dark circle
            m_leds[i]->setStyleSheet(
                "QLabel { background-color: #3a3a3a; border: 1px solid #555555; "
                "border-radius: 7px; }");
        }
    }

    highlightCurrentLine();
}

void DJS103Widget::updateMemoryDisplay()
{
    m_updatingMemory = true;

    int pc = m_emulator.getProgramCounter();
    for (int row = 0; row < 128; ++row) {
        int baseAddr = row * 8;

        // Address column
        QTableWidgetItem *addrItem = m_memTable->item(row, 0);
        if (!addrItem) {
            addrItem = new QTableWidgetItem;
            m_memTable->setItem(row, 0, addrItem);
        }
        addrItem->setText(QString("%1").arg(baseAddr, 4, 8, QChar('0')));
        addrItem->setFlags(addrItem->flags() & ~Qt::ItemIsEditable);
        addrItem->setBackground(QColor(230, 230, 230));

        // Highlight current PC row
        QFont f = addrItem->font();
        bool isPcRow = (pc >= baseAddr && pc < baseAddr + 8);
        f.setBold(isPcRow);
        addrItem->setFont(f);

        // Memory data columns
        for (int col = 1; col <= 8; ++col) {
            int addr = baseAddr + (col - 1);
            QTableWidgetItem *item = m_memTable->item(row, col);
            if (!item) {
                item = new QTableWidgetItem;
                m_memTable->setItem(row, col, item);
            }

            int32_t val = m_emulator.getMemory(addr);
            item->setText(QString("%1").arg(val & DJS103Emulator::WORD_MASK, 11, 8, QChar('0')));

            // Highlight PC address
            QFont cf = item->font();
            cf.setBold(addr == pc);
            item->setFont(cf);

            if (addr == pc) {
                item->setBackground(QColor(200, 255, 200));
            } else if (val != 0) {
                item->setBackground(QColor(255, 255, 230));
                item->setForeground(QColor(180, 0, 0));
            } else {
                item->setBackground(Qt::white);
                item->setForeground(Qt::black);
            }

            // Tooltip showing decimal value (smart precision)
            double dval = DJS103Emulator::wordToDouble(val);
            QString dvalStr;
            double absDval = fabs(dval);
            if (absDval == 0.0) {
                dvalStr = "0";
            } else {
                for (int prec = 1; prec <= 10; ++prec) {
                    QString test = QString::number(dval, 'f', prec);
                    if (fabs(test.toDouble() - dval) < 1e-12) {
                        dvalStr = test;
                        break;
                    }
                }
                if (dvalStr.isEmpty())
                    dvalStr = QString::number(dval, 'f', 10);
            }
            item->setToolTip(tr("八进制: %1\n十进制小数: %2")
                             .arg(val & DJS103Emulator::WORD_MASK, 11, 8, QChar('0'))
                             .arg(dvalStr));
        }
    }

    m_updatingMemory = false;
}

void DJS103Widget::highlightCurrentLine()
{
    int pc = m_emulator.getProgramCounter();
    QTextCursor cursor = m_codeEdit->textCursor();
    QTextCharFormat defaultFmt;

    // Clear all previous highlighting
    QTextDocument *doc = m_codeEdit->document();
    QList<QTextEdit::ExtraSelection> selections;

    // Highlight the line corresponding to current PC
    if (m_addrToLine.contains(pc)) {
        int lineNum = m_addrToLine[pc];
        QTextBlock block = doc->findBlockByNumber(lineNum);
        if (block.isValid()) {
            QTextEdit::ExtraSelection sel;
            sel.cursor = QTextCursor(block);
            sel.cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            sel.format.setBackground(QColor(255, 220, 80));  // 黄色高亮
            sel.format.setForeground(Qt::black);
            selections.append(sel);

            // Scroll to make the highlighted line visible
            m_codeEdit->setTextCursor(sel.cursor);
        }
    }

    // Also highlight any other lines that map to the same PC (shouldn't happen normally,
    // but handle edge case where multiple source lines produce same address)
    m_codeEdit->setExtraSelections(selections);
}

void DJS103Widget::appendOutput(const QString &text)
{
    m_outputEdit->appendPlainText(text);
}

void DJS103Widget::appendOutput(const std::string &text)
{
    appendOutput(QString::fromStdString(text));
}

void DJS103Widget::loadM3Program(const QString &text)
{
    QStringList lines = text.split('\n');
    int currentAddr = 0;
    int startAddr = 0;
    int execAddr = -1;
    std::vector<int32_t> program(DJS103Emulator::MEMORY_SIZE, 0);
    std::vector<bool> loaded(DJS103Emulator::MEMORY_SIZE, false);
    m_addrToLine.clear();

    for (int lineIdx = 0; lineIdx < lines.size(); ++lineIdx) {
        const QString &line = lines[lineIdx];
        QString trimmed = line.trimmed();

        // Skip empty lines and comments
        if (trimmed.isEmpty() || trimmed.startsWith(';'))
            continue;

        // :AAAA - set current address
        if (trimmed.startsWith(':')) {
            QString addrStr = trimmed.mid(1);
            int semiPos = addrStr.indexOf(';');
            if (semiPos >= 0)
                addrStr = addrStr.left(semiPos).trimmed();
            bool ok = false;
            int addr = addrStr.toInt(&ok, 8);
            if (ok) {
                currentAddr = addr % DJS103Emulator::MEMORY_SIZE;
                if (startAddr == 0 && execAddr < 0)
                    startAddr = currentAddr;
            }
            continue;
        }

        // @AAAA - set execution start address
        if (trimmed.startsWith('@')) {
            QString addrStr = trimmed.mid(1);
            int semiPos = addrStr.indexOf(';');
            if (semiPos >= 0)
                addrStr = addrStr.left(semiPos).trimmed();
            bool ok = false;
            int addr = addrStr.toInt(&ok, 8);
            if (ok)
                execAddr = addr % DJS103Emulator::MEMORY_SIZE;
            continue;
        }

        // =xxx - decimal fraction value
        if (trimmed.startsWith('=')) {
            // Remove inline comments before parsing the number
            QString valueStr = trimmed.mid(1);
            int semiPos = valueStr.indexOf(';');
            if (semiPos >= 0)
                valueStr = valueStr.left(semiPos).trimmed();
            bool ok = false;
            double val = valueStr.toDouble(&ok);
            if (ok) {
                int32_t word = DJS103Emulator::doubleToWord(val);
                if (currentAddr < DJS103Emulator::MEMORY_SIZE) {
                    program[currentAddr] = word;
                    loaded[currentAddr] = true;
                    m_addrToLine[currentAddr] = lineIdx;
                    currentAddr++;
                }
            }
            continue;
        }

        // Try to parse instruction
        // Supported formats:
        //   Manual format (from assembly handbook): "+05 0004 0005" or "-05 0004 0005"
        //   M3 format: "S OP A1 A2" (4 space-separated, S=0/1)
        //   Compact format: "地址 指令(八进制)"
        //   Sequential: "指令(八进制)"
        //   Address-only line: "0010" (sets current address, like :AAAA)

        // Remove inline comments first
        int semiPos = trimmed.indexOf(';');
        if (semiPos >= 0)
            trimmed = trimmed.left(semiPos).trimmed();

        if (trimmed.isEmpty())
            continue;

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
        QStringList parts = trimmed.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
#else
        QStringList parts = trimmed.split(QRegularExpression("\\s+"), QString::SkipEmptyParts);
#endif

        // Manual format: "±YY AAAA BBBB" (starts with + or -)
        if (parts.size() == 3 && (trimmed.startsWith('+') || trimmed.startsWith('-'))) {
            bool opOk, a1Ok, a2Ok;
            int sign = (trimmed.startsWith('+')) ? 0 : 1;  // + -> sign bit 0, - -> sign bit 1
            int op = parts[0].mid(1).toInt(&opOk, 8);      // skip the +/- prefix
            int a1 = parts[1].toInt(&a1Ok, 8);
            int a2 = parts[2].toInt(&a2Ok, 8);

            if (opOk && a1Ok && a2Ok && op >= 0 && op < 64) {
                int32_t instruction = (sign << 30) | (op << 24) | (a1 << 12) | a2;
                instruction &= DJS103Emulator::WORD_MASK;
                if (currentAddr < DJS103Emulator::MEMORY_SIZE) {
                    program[currentAddr] = instruction;
                    loaded[currentAddr] = true;
                    m_addrToLine[currentAddr] = lineIdx;
                    currentAddr++;
                }
                continue;
            }
        }

        // Address-only line: "0010" (pure octal address, sets currentAddr)
        if (parts.size() == 1) {
            bool addrOk = false;
            int addr = parts[0].toInt(&addrOk, 8);
            if (addrOk && addr >= 0 && addr < DJS103Emulator::MEMORY_SIZE) {
                currentAddr = addr;
                if (startAddr == 0 && execAddr < 0)
                    startAddr = currentAddr;
                continue;
            }

            // Sequential instruction: pure octal instruction word
            QString noSpace = parts[0];
            noSpace.remove(' ');
            bool instrOk = false;
            int32_t instr = noSpace.toLong(&instrOk, 8);
            if (instrOk && noSpace.length() >= 6) {
                if (currentAddr < DJS103Emulator::MEMORY_SIZE) {
                    program[currentAddr] = instr & DJS103Emulator::WORD_MASK;
                    loaded[currentAddr] = true;
                    m_addrToLine[currentAddr] = lineIdx;
                    currentAddr++;
                }
                continue;
            }
        }

        // Try "地址 指令" format (M3 compact: "AAAA 0500040005")
        if (parts.size() == 2) {
            bool addrOk = false;
            int parsedAddr = parts[0].toInt(&addrOk, 8);
            if (addrOk && parsedAddr >= 0 && parsedAddr < DJS103Emulator::MEMORY_SIZE) {
                // Try to parse second part as octal instruction
                QString instrStr = parts[1];
                instrStr.remove(' ');
                bool instrOk = false;
                int32_t instr = instrStr.toLong(&instrOk, 8);
                if (instrOk) {
                    program[parsedAddr] = instr & DJS103Emulator::WORD_MASK;
                    loaded[parsedAddr] = true;
                    m_addrToLine[parsedAddr] = lineIdx;
                    currentAddr = parsedAddr + 1;
                    if (startAddr == 0 && execAddr < 0)
                        startAddr = parsedAddr;
                    continue;
                }
            }
        }

        // M3 format: "S OP A1 A2" (4 space-separated octal values)
        if (parts.size() >= 4) {
            bool sOk, opOk, a1Ok, a2Ok;
            int s = parts[0].toInt(&sOk, 8);
            int op = parts[1].toInt(&opOk, 8);
            int a1 = parts[2].toInt(&a1Ok, 8);
            int a2 = parts[3].toInt(&a2Ok, 8);

            if (sOk && opOk && a1Ok && a2Ok && s <= 1 && op >= 0 && op < 64) {
                int32_t instruction = (s << 30) | (op << 24) | (a1 << 12) | a2;
                instruction &= DJS103Emulator::WORD_MASK;
                if (currentAddr < DJS103Emulator::MEMORY_SIZE) {
                    program[currentAddr] = instruction;
                    loaded[currentAddr] = true;
                    m_addrToLine[currentAddr] = lineIdx;
                    currentAddr++;
                }
                continue;
            }
        }

        // Simple format: just instruction (no address)
        QString noSpace = trimmed;
        noSpace.remove(' ');
        bool ok = false;
        int32_t instr = noSpace.toLong(&ok, 8);
        if (ok && noSpace.length() >= 6) {
            if (currentAddr < DJS103Emulator::MEMORY_SIZE) {
                program[currentAddr] = instr & DJS103Emulator::WORD_MASK;
                loaded[currentAddr] = true;
                m_addrToLine[currentAddr] = lineIdx;
                currentAddr++;
            }
        }
    }

    // Load all program words into emulator
    m_emulator.reset();
    for (int i = 0; i < DJS103Emulator::MEMORY_SIZE; ++i) {
        if (loaded[i])
            m_emulator.setMemory(i, program[i]);
    }

    // Set execution address
    if (execAddr >= 0)
        m_emulator.setProgramCounter(execAddr);
    else
        m_emulator.setProgramCounter(startAddr);

    m_emulator.setAccumulator(0);
}

void DJS103Widget::onHelp()
{
    QString css =
        "<style>"
        "  body { font-family: 'Microsoft YaHei', 'PingFang SC', sans-serif; "
        "         color: #2c3e50; padding: 8px; }"
        "  h3 { color: #1a5276; font-size: 20px; border-bottom: 3px solid #2980b9; "
        "       padding-bottom: 8px; margin-bottom: 16px; }"
        "  h4 { color: #2471a3; font-size: 15px; margin-top: 18px; margin-bottom: 8px; "
        "       padding-left: 8px; border-left: 4px solid #3498db; }"
        "  p { line-height: 1.6; margin: 6px 0; }"
        "  code { color: #1a5276; padding: 2px 6px; font-size: 13px; }"
        "  table { border-collapse: collapse; width: 100%; margin: 8px 0 12px 0; "
        "          font-size: 13px; }"
        "  th { background: #2980b9; color: #ffffff; padding: 7px 10px; "
        "       text-align: left; font-weight: 600; }"
        "  td { padding: 6px 10px; border: 1px solid #d5dbdb; }"
        "  hr { border: none; border-top: 1px solid #d5dbdb; margin: 14px 0; }"
        "  ul, ol { line-height: 1.8; padding-left: 20px; }"
        "  li { margin-bottom: 4px; }"
        "  pre { color: #1a5276; padding: 10px 14px; "
        "        border: 1px solid #d5dbdb; font-size: 12px; line-height: 1.6; "
        "        white-space: pre-wrap; margin: 6px 0; }"
        "  i { color: #5d6d7e; }"
        "  b { color: #1a5276; }"
        "</style>";

    QString helpText = css + tr(
        "<h3>DJS-103 机（八一型）指令系统详解</h3>"

        "<p>103机是中国第一台通用数字电子计算机，采用<b>两地址指令格式</b>，"
        "每条指令占一个存储单元。基于苏联M-3机设计，略有本土修改。"
        "机器为异步设计（无统一时钟），一条指令执行分为八拍，"
        "运算器中有一个隐含的<b>累加寄存器</b>（r，用于暂存中间结果）。</p>"

        // ── 1. 基本技术规格 ──
        "<h4>1. 基本技术规格</h4>"
        "<table>"
        "<tr><th>项目</th><th>规格</th></tr>"
        "<tr><td>字长</td><td>31位（1位符号 + 30位二进制定点数）</td></tr>"
        "<tr><td>存储容量</td><td>初期磁鼓1024字，后期磁芯2048字</td></tr>"
        "<tr><td>地址长度</td><td>指令中12位地址字段（支持扩展）</td></tr>"
        "<tr><td>指令表示</td><td>八进制形式，如 <code>+12 3456 7012</code></td></tr>"
        "<tr><td>输入输出</td><td>五单位穿孔纸带 + 电传打字机</td></tr>"
        "<tr><td>执行速度</td><td>磁鼓~30次/秒，磁芯1800–2300次/秒</td></tr>"
        "</table>"

        "<p><b>指令格式（位分配）</b>：</p>"
        "<table>"
        "<tr><th>位范围</th><th>字段</th><th>说明</th></tr>"
        "<tr><td>位0</td><td>符号位</td><td>通常0（写为 +）</td></tr>"
        "<tr><td>位1–6</td><td>操作码 XY</td><td>6位 = 两位八进制数</td></tr>"
        "<tr><td>位7–18</td><td>第一地址 A</td><td>12位</td></tr>"
        "<tr><td>位19–30</td><td>第二地址 B</td><td>12位（结果地址）</td></tr>"
        "</table>"

        "<p>每条指令本质上是「对A和B（或上次结果）进行操作，结果放B（或不存）」。</p>"

        // ── 2. 数据表示 ──
        "<h4>2. 数据表示</h4>"
        "<p>DJS-103 采用<b>定点小数</b>表示，所有数值满足 |x| &lt; 1 。<br>"
        "内部格式：1位符号 + 29位尾数，值 = 尾数 / 2<sup>30</sup></p>"

        // ���─ 3. 操作码结构 ──
        "<h4>3. 操作码结构（XY，两位八进制）</h4>"

        "<p><b>Y（第二位八进制）</b>：基本操作种类（5种算术/逻辑运算）</p>"
        "<table>"
        "<tr><th>Y</th><th>操作</th><th>符号</th></tr>"
        "<tr><td>0</td><td>加法</td><td>+</td></tr>"
        "<tr><td>1</td><td>减法</td><td>−</td></tr>"
        "<tr><td>2</td><td>除法</td><td>÷</td></tr>"
        "<tr><td>3</td><td>乘法</td><td>×</td></tr>"
        "<tr><td>6</td><td>逻辑乘法（按位与）</td><td>AND</td></tr>"
        "</table>"

        "<p><b>X（第一位八进制）</b>：操作修饰符（决定操作数来源、结果写回、打印、绝对值等）</p>"
        "<table>"
        "<tr><th>X</th><th>操作数</th><th>结果存放</th><th>含义</th></tr>"
        "<tr><td>0</td><td>a○b</td><td>b和r</td><td>a与b运算，写回b，存r</td></tr>"
        "<tr><td>1</td><td>a○b</td><td>r</td><td>a与b运算，仅存r</td></tr>"
        "<tr><td>2</td><td>r○a</td><td>b和r</td><td>r与a运算，写回b，存r</td></tr>"
        "<tr><td>3</td><td>r○a</td><td>r</td><td>r与a运算，仅存r</td></tr>"
        "<tr><td>4</td><td>a○b</td><td>b和r</td><td>a与b运算，写回b，存r，并打印</td></tr>"
        "<tr><td>5</td><td>|a|○|b|</td><td>r</td><td>a与b绝对值运算，仅存r</td></tr>"
        "<tr><td>6</td><td>r○a</td><td>b和r</td><td>r与a运算，写回b，存r，并打印</td></tr>"
        "<tr><td>7</td><td>|r|○|b|</td><td>r</td><td>r与b绝对值运算，仅存r</td></tr>"
        "</table>"
        "<p><i>注：a=第一地址A的值，b=第二地址B的值，r=累加器（运算器寄存器）的值。<br>"
        "以上5种Y × 8种X共形成 <b>40条算术/逻辑指令</b>。所有运算为定点，结果可能溢出需程序处理。</i></p>"

        // ── 4. 控制传送指令 (Y=5) ──
        "<h4>4. 控制传送指令 (Y=5)</h4>"
        "<table>"
        "<tr><th>操作码</th><th>操作</th><th>功能</th></tr>"
        "<tr><td>+05</td><td>传送</td><td>A→B，r←A</td></tr>"
        "<tr><td>+15</td><td>传送</td><td>A→B，r←A</td></tr>"
        "<tr><td>+45</td><td>传输并打印</td><td>A→B，打印</td></tr>"
        "<tr><td>+55</td><td>传输并打印</td><td>A→B，打印</td></tr>"
        "</table>"

        // ── 5. 控制转移指令 (Y=4) ──
        "<h4>5. 控制转移指令 (Y=4)</h4>"
        "<table>"
        "<tr><th>操作码</th><th>操作</th><th>功能</th></tr>"
        "<tr><td>+24</td><td>无条件跳转</td><td>PC←A，r→B</td></tr>"
        "<tr><td>+34</td><td>条件分支</td><td>r≥0则PC←B，r&lt;0则PC←A</td></tr>"
        "<tr><td>+64</td><td>无条件跳转</td><td>PC←A，r→B，打印</td></tr>"
        "<tr><td>+74</td><td>无条件跳转</td><td>PC←B，|r|→r</td></tr>"
        "</table>"

        // ── 6. 输入/停机指令 (Y=7) ──
        "<h4>6. 输入/停机指令 (Y=7)</h4>"
        "<table>"
        "<tr><th>操作码</th><th>操作</th><th>功能</th></tr>"
        "<tr><td>+07, +27</td><td>输入</td><td>从穿孔带输入一个数字，写入第二地址，不保存在寄存器中</td></tr>"
        "<tr><td>+04, +14, +44, +54, +17, +37, +57, +77</td><td>停机</td><td>机器停止</td></tr>"
        "</table>"

        // ── 7. 指令执行流程 ──
        "<h4>7. 指令执行流程</h4>"
        "<ol>"
        "<li>取指令：从当前PC地址读出31位字</li>"
        "<li>译码操作码XY</li>"
        "<li>根据X/Y取A、B操作数（或r中上次结果）</li>"
        "<li>执行运算（加/减/乘/除/AND）</li>"
        "<li>根据X决定是否写回B、是否打印、是否更新r</li>"
        "<li>更新程序计数器（PC），或跳转</li>"
        "</ol>"

        // ── 8. M3 文件格式 ──
        "<h4>8. M3 文件格式</h4>"
        "<table>"
        "<tr><th>格式</th><th>示例</th><th>说明</th></tr>"
        "<tr><td>:AAAA</td><td>:0010</td><td>设置当前地址（八进制）</td></tr>"
        "<tr><td>=X.XXX</td><td>=0.5</td><td>十进制小数常量（存入当前地址）</td></tr>"
        "<tr><td>@AAAA</td><td>@0020</td><td>设置起始执行地址</td></tr>"
        "<tr><td>+XY A B</td><td>+05 0050 0060</td><td>指令（八进制）</td></tr>"
        "</table>"

        // ── 9. 编程示例 ──
        "<h4>9. 编程示例</h4>"
        "<p>典型短程序片段（演示乘法 + 转移 + 打印 + 停机）：</p>"
        "<pre>"
        "; 示例: 计算 0.5 * 0.5 = 0.25 并打印结果\n"
        ":0000\n"
        "+05  0004  0005   ; MOV [4]-&gt;[5]\n"
        "+03  0004  0005   ; MUL [4]*[5]-&gt;[5]\n"
        "+45  0005  0005   ; PRN [5]\n"
        "+04  0000  0000   ; HLT\n"
        ";\n"
        ":0004\n"
        "=0.5              ; 常数\n"
        "=0                ; 结果\n"
        "@0000             ; 程序入口\n"
        "</pre>"
        "<p><i>实际编程需用《103电子计算机程序汇编》（1961年科学出版社）中的子程序库。</i></p>"

        // ── 10. 常用编程技巧 ──
        "<h4>10. 常用编程技巧</h4>"
        "<ul>"
        "<li><b>减法存入</b>：DJS-103无直接减法存入指令，可用「取反再加」模式：<br>"
        "&nbsp;&nbsp;<code>+01 0052 0063</code> (r = 0 - [0063]，取反)<br>"
        "&nbsp;&nbsp;<code>+00 0063 0061</code> ([0061] = [0061] + r，完成减法)</li>"
        "<li><b>除以整数</b>：103机不支持直接除以整数&gt;1，<br>"
        "需转为乘以倒数：x/3 → x×(1/3)，将1/3存为常量</li>"
        "<li><b>循环</b>：用 <code>+34</code> 条件转移实现，注意退出条件（r≥0走B，r&lt;0走A）</li>"
        "<li><b>累加器利用</b>：连续运算时善用X=2/3（以r为操作数），可减少中间存储</li>"
        "</ul>"

        // ── 历史意义 ──
        "<h4>历史意义</h4>"
        "<p>103机指令系统简单实用，体现了早期计算机「先仿制后自主」的特点：<br>"
        "全部指令用8拍异步完成，无现代流水线/中断。<br>"
        "程序主要靠手编八进制机器码或简单汇编，配合纸带输入。<br>"
        "正是这套系统，让中国从零起步，培养了第一批计算机人才，<br>"
        "并为后续104机、109机等奠基。</p>"
    );

    QDialog dlg(this);
    dlg.setWindowTitle(tr("DJS-103 汇编帮助"));
    dlg.resize(960, 800);

    QVBoxLayout *layout = new QVBoxLayout(&dlg);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    QTextBrowser *browser = new QTextBrowser(&dlg);
    browser->setHtml(helpText);
    browser->setOpenExternalLinks(false);
    browser->setStyleSheet(
        "QTextBrowser { border: none; padding: 4px; }"
    );
    layout->addWidget(browser);

    QFrame *sep = new QFrame(&dlg);
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("color: #d5dbdb;");
    layout->addWidget(sep);

    QHBoxLayout *btnLayout = new QHBoxLayout;
    btnLayout->setContentsMargins(12, 8, 12, 8);
    btnLayout->addStretch();
    QPushButton *closeBtn = new QPushButton(tr("关闭"), &dlg);
    closeBtn->setDefault(true);
    closeBtn->setFixedSize(90, 32);
    closeBtn->setStyleSheet(
        "QPushButton { background: #2980b9; color: white; border: none; "
        "              border-radius: 4px; font-size: 14px; }"
        "QPushButton:hover { background: #2471a3; }"
        "QPushButton:pressed { background: #1a5276; }"
    );
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    btnLayout->addWidget(closeBtn);
    layout->addLayout(btnLayout);

    dlg.exec();
}
