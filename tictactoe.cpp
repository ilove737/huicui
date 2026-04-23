/****************************************************************************
**
** Copyright (C) 2024
** Based on ttt.c by antirez (https://github.com/antirez/ttt-rl)
**
****************************************************************************/

#include <QtWidgets>
#include <QtCore>
#include <QtGlobal>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <cstring>

#include "tictactoe.h"

// Qt 5.14 引入 loadRelaxed()，旧版本用 load()
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
#define ATOMIC_LOAD(x) (x).loadRelaxed()
#else
#define ATOMIC_LOAD(x) (x).load()
#endif

// ============ ttt.c 算法实现（直接移植）============

static float randomWeight()
{
    return ((float)rand() / RAND_MAX) - 0.5f;
}

void TicTacToeNN::initNeuralNetwork()
{
    for (int i = 0; i < NN_INPUT_SIZE * NN_HIDDEN_SIZE; ++i)
        m_nn.weights_ih[i] = randomWeight();
    for (int i = 0; i < NN_HIDDEN_SIZE * NN_OUTPUT_SIZE; ++i)
        m_nn.weights_ho[i] = randomWeight();
    for (int i = 0; i < NN_HIDDEN_SIZE; ++i)
        m_nn.biases_h[i] = randomWeight();
    for (int i = 0; i < NN_OUTPUT_SIZE; ++i)
        m_nn.biases_o[i] = randomWeight();
}

float TicTacToeNN::relu(float x)
{
    return x > 0 ? x : 0;
}

float TicTacToeNN::reluDerivative(float x)
{
    return x > 0 ? 1.0f : 0.0f;
}

void TicTacToeNN::softmax(float *input, float *output, int size)
{
    float maxVal = input[0];
    for (int i = 1; i < size; ++i)
        if (input[i] > maxVal) maxVal = input[i];

    float sum = 0.0f;
    for (int i = 0; i < size; ++i) {
        output[i] = expf(input[i] - maxVal);
        sum += output[i];
    }

    if (sum > 0) {
        for (int i = 0; i < size; ++i) output[i] /= sum;
    } else {
        for (int i = 0; i < size; ++i) output[i] = 1.0f / size;
    }
}

void TicTacToeNN::boardToInputs(const char *board, float *inputs)
{
    for (int i = 0; i < 9; ++i) {
        if (board[i] == '.') {
            inputs[i*2] = 0;
            inputs[i*2+1] = 0;
        } else if (board[i] == 'X') {
            inputs[i*2] = 1;
            inputs[i*2+1] = 0;
        } else {
            inputs[i*2] = 0;
            inputs[i*2+1] = 1;
        }
    }
}

void TicTacToeNN::forwardPass(float *inputs)
{
    memcpy(m_nn.inputs, inputs, NN_INPUT_SIZE * sizeof(float));

    for (int i = 0; i < NN_HIDDEN_SIZE; ++i) {
        float sum = m_nn.biases_h[i];
        for (int j = 0; j < NN_INPUT_SIZE; ++j)
            sum += inputs[j] * m_nn.weights_ih[j * NN_HIDDEN_SIZE + i];
        m_nn.hidden[i] = relu(sum);
    }

    for (int i = 0; i < NN_OUTPUT_SIZE; ++i) {
        m_nn.raw_logits[i] = m_nn.biases_o[i];
        for (int j = 0; j < NN_HIDDEN_SIZE; ++j)
            m_nn.raw_logits[i] += m_nn.hidden[j] * m_nn.weights_ho[j * NN_OUTPUT_SIZE + i];
    }

    softmax(m_nn.raw_logits, m_nn.outputs, NN_OUTPUT_SIZE);
}

void TicTacToeNN::backProp(const float *targetProbs, float rewardScaling)
{
    float outputDeltas[NN_OUTPUT_SIZE];
    float hiddenDeltas[NN_HIDDEN_SIZE];

    for (int i = 0; i < NN_OUTPUT_SIZE; ++i)
        outputDeltas[i] = (m_nn.outputs[i] - targetProbs[i]) * fabsf(rewardScaling);

    for (int i = 0; i < NN_HIDDEN_SIZE; ++i) {
        float error = 0;
        for (int j = 0; j < NN_OUTPUT_SIZE; ++j)
            error += outputDeltas[j] * m_nn.weights_ho[i * NN_OUTPUT_SIZE + j];
        hiddenDeltas[i] = error * reluDerivative(m_nn.hidden[i]);
    }

    for (int i = 0; i < NN_HIDDEN_SIZE; ++i)
        for (int j = 0; j < NN_OUTPUT_SIZE; ++j)
            m_nn.weights_ho[i * NN_OUTPUT_SIZE + j] -= LEARNING_RATE * outputDeltas[j] * m_nn.hidden[i];
    for (int j = 0; j < NN_OUTPUT_SIZE; ++j)
        m_nn.biases_o[j] -= LEARNING_RATE * outputDeltas[j];

    for (int i = 0; i < NN_INPUT_SIZE; ++i)
        for (int j = 0; j < NN_HIDDEN_SIZE; ++j)
            m_nn.weights_ih[i * NN_HIDDEN_SIZE + j] -= LEARNING_RATE * hiddenDeltas[j] * m_nn.inputs[i];
    for (int j = 0; j < NN_HIDDEN_SIZE; ++j)
        m_nn.biases_h[j] -= LEARNING_RATE * hiddenDeltas[j];
}

void TicTacToeNN::learnFromGame(const int *moveHistory, int numMoves, char winner)
{
    float reward;
    char nnSymbol = 'O';

    if (winner == 'T')
        reward = 0.2f;
    else if (winner == nnSymbol)
        reward = 1.0f;
    else
        reward = -1.0f;

    float targetProbs[NN_OUTPUT_SIZE];
    char board[9];
    float inputs[NN_INPUT_SIZE];

    for (int moveIdx = 0; moveIdx < numMoves; ++moveIdx) {
        if (moveIdx % 2 != 1) continue;

        memset(board, '.', 9);
        for (int i = 0; i < moveIdx; ++i) {
            char symbol = (i % 2 == 0) ? 'X' : 'O';
            board[moveHistory[i]] = symbol;
        }

        boardToInputs(board, inputs);
        forwardPass(inputs);

        int move = moveHistory[moveIdx];
        float moveImportance = 0.5f + 0.5f * (float)moveIdx / (float)numMoves;
        float scaledReward = reward * moveImportance;

        memset(targetProbs, 0, sizeof(targetProbs));
        if (scaledReward >= 0) {
            targetProbs[move] = 1.0f;
        } else {
            int validMoves = 0;
            for (int i = 0; i < 9; ++i)
                if (board[i] == '.' && i != move) validMoves++;
            if (validMoves > 0) {
                float otherProb = 1.0f / validMoves;
                for (int i = 0; i < 9; ++i)
                    if (board[i] == '.' && i != move) targetProbs[i] = otherProb;
            }
        }

        backProp(targetProbs, scaledReward);
    }
}

// ============ TicTacToeNN 封装类实现 ============

TicTacToeNN::TicTacToeNN()
{
    initNeuralNetwork();
}

TicTacToeNN::~TicTacToeNN()
{
}

int TicTacToeNN::getBestMove(const char *board)
{
    float inputs[NN_INPUT_SIZE];
    boardToInputs(board, inputs);
    forwardPass(inputs);

    int bestMove = -1;
    float bestProb = -1.0f;
    for (int i = 0; i < 9; ++i) {
        if (board[i] == '.' && m_nn.outputs[i] > bestProb) {
            bestMove = i;
            bestProb = m_nn.outputs[i];
        }
    }
    return bestMove;
}

void TicTacToeNN::getMoveProbs(const char *board, float *probs)
{
    float inputs[NN_INPUT_SIZE];
    boardToInputs(board, inputs);
    forwardPass(inputs);
    for (int i = 0; i < NN_OUTPUT_SIZE; ++i) probs[i] = m_nn.outputs[i];
}

void TicTacToeNN::copyWeightsFrom(const TicTacToeNN &other)
{
    memcpy(m_nn.weights_ih, other.m_nn.weights_ih, sizeof(m_nn.weights_ih));
    memcpy(m_nn.weights_ho, other.m_nn.weights_ho, sizeof(m_nn.weights_ho));
    memcpy(m_nn.biases_h, other.m_nn.biases_h, sizeof(m_nn.biases_h));
    memcpy(m_nn.biases_o, other.m_nn.biases_o, sizeof(m_nn.biases_o));
}

void TicTacToeNN::averageWeightsFrom(const QVector<TicTacToeNN *> &nns)
{
    int count = nns.size();
    if (count == 0) return;

    copyWeightsFrom(*nns[0]);
    if (count == 1) return;

    for (int k = 1; k < count; ++k) {
        const NeuralNetwork &other = *nns[k]->getNN();
        for (int i = 0; i < NN_INPUT_SIZE * NN_HIDDEN_SIZE; ++i)
            m_nn.weights_ih[i] += other.weights_ih[i];
        for (int i = 0; i < NN_HIDDEN_SIZE * NN_OUTPUT_SIZE; ++i)
            m_nn.weights_ho[i] += other.weights_ho[i];
        for (int i = 0; i < NN_HIDDEN_SIZE; ++i)
            m_nn.biases_h[i] += other.biases_h[i];
        for (int i = 0; i < NN_OUTPUT_SIZE; ++i)
            m_nn.biases_o[i] += other.biases_o[i];
    }

    float inv = 1.0f / count;
    for (int i = 0; i < NN_INPUT_SIZE * NN_HIDDEN_SIZE; ++i)
        m_nn.weights_ih[i] *= inv;
    for (int i = 0; i < NN_HIDDEN_SIZE * NN_OUTPUT_SIZE; ++i)
        m_nn.weights_ho[i] *= inv;
    for (int i = 0; i < NN_HIDDEN_SIZE; ++i)
        m_nn.biases_h[i] *= inv;
    for (int i = 0; i < NN_OUTPUT_SIZE; ++i)
        m_nn.biases_o[i] *= inv;
}

// ============ RLTrainingWorker 实现 ============

RLTrainingWorker::RLTrainingWorker(int workerId, int numGames, int syncInterval,
                                   QObject *parent)
    : QThread(parent)
    , m_workerId(workerId)
    , m_numGames(numGames)
    , m_syncInterval(syncInterval)
    , m_stopped(0)
    , m_localNN(new TicTacToeNN())
    , m_globalGamesPlayed(nullptr)
    , m_globalWins(nullptr)
    , m_globalLosses(nullptr)
    , m_globalTies(nullptr)
{
}

RLTrainingWorker::~RLTrainingWorker()
{
    stopTraining();
    wait();
    delete m_localNN;
}

void RLTrainingWorker::stopTraining()
{
    m_stopped = 1;
}

void RLTrainingWorker::initGame(GameState *state)
{
    memset(state->board, '.', 9);
    state->current_player = 0;
}

int RLTrainingWorker::checkGameOver(GameState *state, char *winner)
{
    for (int i = 0; i < 3; ++i) {
        if (state->board[i*3] != '.' && state->board[i*3] == state->board[i*3+1] &&
            state->board[i*3+1] == state->board[i*3+2]) {
            *winner = state->board[i*3];
            return 1;
        }
    }
    for (int i = 0; i < 3; ++i) {
        if (state->board[i] != '.' && state->board[i] == state->board[i+3] &&
            state->board[i+3] == state->board[i+6]) {
            *winner = state->board[i];
            return 1;
        }
    }
    if (state->board[0] != '.' && state->board[0] == state->board[4] && state->board[4] == state->board[8]) {
        *winner = state->board[0];
        return 1;
    }
    if (state->board[2] != '.' && state->board[2] == state->board[4] && state->board[4] == state->board[6]) {
        *winner = state->board[2];
        return 1;
    }
    for (int i = 0; i < 9; ++i)
        if (state->board[i] == '.') return 0;
    *winner = 'T';
    return 1;
}

int RLTrainingWorker::getRandomMove(GameState *state)
{
    int validMoves[9], count = 0;
    for (int i = 0; i < 9; ++i)
        if (state->board[i] == '.') validMoves[count++] = i;
    return validMoves[rand() % count];
}

char RLTrainingWorker::playRandomGame()
{
    GameState state;
    char winner = 0;
    int moveHistory[9];
    int numMoves = 0;

    initGame(&state);

    while (!checkGameOver(&state, &winner)) {
        int move;
        if (state.current_player == 0) {
            move = getRandomMove(&state);
        } else {
            move = m_localNN->getBestMove(state.board);
        }

        char symbol = (state.current_player == 0) ? 'X' : 'O';
        state.board[move] = symbol;
        moveHistory[numMoves++] = move;
        state.current_player = 1 - state.current_player;
    }

    m_localNN->learnFromGame(moveHistory, numMoves, winner);
    return winner;
}

void RLTrainingWorker::run()
{
    for (int i = 0; i < m_numGames && !m_stopped; ++i) {
        char winner = playRandomGame();

        // 原子更新全局计数器
        if (m_globalGamesPlayed) m_globalGamesPlayed->fetchAndAddRelaxed(1);
        if (winner == 'O') {
            if (m_globalWins) m_globalWins->fetchAndAddRelaxed(1);
        } else if (winner == 'X') {
            if (m_globalLosses) m_globalLosses->fetchAndAddRelaxed(1);
        } else {
            if (m_globalTies) m_globalTies->fetchAndAddRelaxed(1);
        }

        // 定期请求同步
        if ((i + 1) % m_syncInterval == 0) {
            emit syncRequested(m_workerId);
        }
    }
}

// ============ RLTrainingTask 实现 ============

RLTrainingTask::RLTrainingTask(TicTacToeNN *nn, QObject *parent)
    : QObject(parent)
    , m_nn(nn)
    , m_numGames(150000)
    , m_threadCount(1)
    , m_activeWorkers(0)
    , m_globalGamesPlayed(0)
    , m_globalWins(0)
    , m_globalLosses(0)
    , m_globalTies(0)
    , m_lastReportedProgress(0)
{
}

RLTrainingTask::~RLTrainingTask()
{
    stopTraining();
}

void RLTrainingTask::setTrainingParams(int numGames, int threadCount)
{
    m_numGames = numGames;
    m_threadCount = threadCount;
}

void RLTrainingTask::startTraining()
{
    m_globalGamesPlayed = 0;
    m_globalWins = 0;
    m_globalLosses = 0;
    m_globalTies = 0;
    m_lastReportedProgress = 0;

    qDeleteAll(m_workers);
    m_workers.clear();

    int gamesPerThread = m_numGames / m_threadCount;
    // 同步间隔：每 1000 局同步一次权重
    int syncInterval = 1000;

    m_activeWorkers = m_threadCount;

    for (int i = 0; i < m_threadCount; ++i) {
        int games = (i == m_threadCount - 1)
                    ? m_numGames - gamesPerThread * (m_threadCount - 1)
                    : gamesPerThread;

        RLTrainingWorker *worker = new RLTrainingWorker(i, games, syncInterval);

        // 初始化本地NN：从主NN复制权重
        {
            QMutexLocker locker(worker->getLocalNNMutex());
            worker->getLocalNN()->copyWeightsFrom(*m_nn);
        }

        // 设置全局原子计数器
        worker->setGlobalCounters(&m_globalGamesPlayed, &m_globalWins,
                                  &m_globalLosses, &m_globalTies);

        connect(worker, &RLTrainingWorker::syncRequested,
                this, &RLTrainingTask::onWorkerSyncRequested);
        connect(worker, &QThread::finished,
                this, &RLTrainingTask::onWorkerFinished);
        m_workers.append(worker);
    }

    for (auto *worker : m_workers)
        worker->start();
}

void RLTrainingTask::stopTraining()
{
    for (auto *worker : m_workers)
        worker->stopTraining();
    for (auto *worker : m_workers)
        worker->wait();
}

bool RLTrainingTask::isRunning() const
{
    for (auto *worker : m_workers)
        if (worker->isRunning()) return true;
    return false;
}

void RLTrainingTask::onWorkerSyncRequested(int workerId)
{
    Q_UNUSED(workerId);

    // 合并权重
    mergeWorkerWeights();

    // 更新进度和统计
    int gamesPlayed = ATOMIC_LOAD(m_globalGamesPlayed);
    int progress = qMin(99, gamesPlayed * 100 / m_numGames);

    if (progress > m_lastReportedProgress) {
        m_lastReportedProgress = progress;
        emit progressChanged(progress);
    }

    // 报告当前统计
    int wins = ATOMIC_LOAD(m_globalWins);
    int losses = ATOMIC_LOAD(m_globalLosses);
    int ties = ATOMIC_LOAD(m_globalTies);
    emit statsUpdated(wins, losses, ties);
}

void RLTrainingTask::onWorkerFinished()
{
    mergeWorkerWeights();

    m_activeWorkers--;
    if (m_activeWorkers <= 0) {
        // 最终统计
        int wins = ATOMIC_LOAD(m_globalWins);
        int losses = ATOMIC_LOAD(m_globalLosses);
        int ties = ATOMIC_LOAD(m_globalTies);

        emit statsUpdated(wins, losses, ties);
        emit progressChanged(100);
        emit trainingComplete();
    }
}

void RLTrainingTask::mergeWorkerWeights()
{
    QMutexLocker locker(&m_syncMutex);

    // 收集所有 worker 的本地NN指针
    QVector<TicTacToeNN *> localNNs;

    // 先将主NN作为第一个
    localNNs.append(m_nn);

    // 从每个 worker 的 localNN 复制到临时副本（加锁读取）
    QVector<TicTacToeNN *> workerCopies;
    for (auto *worker : m_workers) {
        TicTacToeNN *copy = new TicTacToeNN();
        QMutexLocker nnLocker(worker->getLocalNNMutex());
        copy->copyWeightsFrom(*worker->getLocalNN());
        workerCopies.append(copy);
        localNNs.append(copy);
    }

    // 计算平均权重
    TicTacToeNN averaged;
    averaged.averageWeightsFrom(localNNs);

    // 写回主NN
    m_nn->copyWeightsFrom(averaged);

    // 写回所有 worker 的 localNN（加锁写入）
    for (auto *worker : m_workers) {
        QMutexLocker nnLocker(worker->getLocalNNMutex());
        worker->getLocalNN()->copyWeightsFrom(averaged);
    }

    // 清理临时副本
    qDeleteAll(workerCopies);
}

// ============ GameWidget 实现 ============

GameWidget::GameWidget(QWidget *parent)
    : QWidget(parent)
    , m_currentPlayer(0)
    , m_gameOver(false)
    , m_isTraining(false)
    , m_totalWins(0), m_totalLosses(0), m_totalTies(0)
{
    memset(m_board, '.', 9);
    m_nn = new TicTacToeNN();
    m_trainingTask = nullptr;
    srand(time(nullptr));
    createUI();
    updateBoard();
}

GameWidget::~GameWidget()
{
    stopTraining();
    delete m_nn;
}

void GameWidget::createUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout;

    QHBoxLayout *controlLayout = new QHBoxLayout;
    controlLayout->addWidget(new QLabel(tr("训练局数：")));
    m_gamesSpinBox = new QSpinBox;
    m_gamesSpinBox->setRange(1000, 5000000);
    m_gamesSpinBox->setValue(150000);
    m_gamesSpinBox->setSingleStep(10000);
    controlLayout->addWidget(m_gamesSpinBox);

    controlLayout->addWidget(new QLabel(tr("线程数：")));
    m_threadCountSpinBox = new QSpinBox;
    m_threadCountSpinBox->setRange(1, QThread::idealThreadCount());
    m_threadCountSpinBox->setValue(1);
    controlLayout->addWidget(m_threadCountSpinBox);

    m_startButton = new QPushButton(tr("开始训练"));
    controlLayout->addWidget(m_startButton);
    m_restartButton = new QPushButton(tr("重新开始"));
    m_restartButton->setEnabled(false);
    controlLayout->addWidget(m_restartButton);
    controlLayout->addStretch();

    mainLayout->addLayout(controlLayout);

    m_progressBar = new QProgressBar;
    mainLayout->addWidget(m_progressBar);

    QGridLayout *boardLayout = new QGridLayout;
    boardLayout->setSpacing(2);
    boardLayout->setAlignment(Qt::AlignCenter);

    QWidget *boardContainer = new QWidget;
    boardContainer->setLayout(boardLayout);
    boardContainer->setFixedSize(300, 300);

    for (int i = 0; i < 9; ++i) {
        m_cells[i] = new QPushButton;
        m_cells[i]->setFixedSize(90, 90);
        m_cells[i]->setFont(QFont(QString(), 48, QFont::Bold));
        m_cells[i]->setText(QString());
        connect(m_cells[i], &QPushButton::clicked, this, [this, i]() { playerMove(i); });
        boardLayout->addWidget(m_cells[i], i / 3, i % 3);
    }

    mainLayout->addWidget(boardContainer, 0, Qt::AlignHCenter);

    m_statusLabel = new QLabel(tr("点击\"开始训练\"训练神经网络，或直接点击棋盘与AI对战"));
    mainLayout->addWidget(m_statusLabel);

    m_statsLabel = new QLabel(tr("胜: 0  负: 0  平: 0"));
    mainLayout->addWidget(m_statsLabel);

    m_infoEdit = new QTextEdit;
    m_infoEdit->setReadOnly(true);
    m_infoEdit->setMaximumHeight(120);
    m_infoEdit->setPlainText(tr("神经网络参数数量: %1\n").arg(m_nn->getParamCount()) +
                             tr("输入层: 18 神经元\n") +
                             tr("隐藏层: 100 神经元 (ReLU)\n") +
                             tr("输出层: 9 神经元 (Softmax)\n\n") +
                             tr("算法来源: ttt.c (antirez)\n") +
                             tr("学习率: 0.1\n") +
                             tr("奖励: 胜+1.0 平+0.2 负-1.0\n") +
                             tr("多线程: 每线程独立NN副本，定期权重平均"));
    mainLayout->addWidget(m_infoEdit);

    setLayout(mainLayout);

    connect(m_startButton, &QPushButton::clicked, this, [this]() {
        startTraining(m_gamesSpinBox->value());
    });
    connect(m_restartButton, &QPushButton::clicked, this, &GameWidget::restartGame);
}

void GameWidget::startTraining(int numGames)
{
    m_isTraining = true;
    m_startButton->setEnabled(false);
    m_gamesSpinBox->setEnabled(false);
    m_threadCountSpinBox->setEnabled(false);
    m_restartButton->setEnabled(true);

    m_totalWins = m_totalLosses = m_totalTies = 0;

    delete m_nn;
    m_nn = new TicTacToeNN();

    int threadCount = m_threadCountSpinBox->value();

    if (m_trainingTask) {
        m_trainingTask->stopTraining();
        m_trainingTask->deleteLater();
    }
    m_trainingTask = new RLTrainingTask(m_nn);
    m_trainingTask->setTrainingParams(numGames, threadCount);

    connect(m_trainingTask, &RLTrainingTask::progressChanged, m_progressBar, &QProgressBar::setValue);
    connect(m_trainingTask, &RLTrainingTask::statsUpdated, this, &GameWidget::onTrainingStats);
    connect(m_trainingTask, &RLTrainingTask::trainingComplete, this, &GameWidget::onTrainingComplete);

    m_statusLabel->setText(tr("正在训练 (%1 线程)...").arg(threadCount));
    m_progressBar->setValue(0);
    m_trainingTask->startTraining();
}

void GameWidget::stopTraining()
{
    if (m_trainingTask && m_trainingTask->isRunning()) {
        m_trainingTask->stopTraining();
    }
}

void GameWidget::restartGame()
{
    memset(m_board, '.', 9);
    m_currentPlayer = 0;
    m_gameOver = false;
    updateBoard();
    m_statusLabel->setText(tr("游戏开始！你执X，AI执O"));
}

void GameWidget::playerMove(int position)
{
    if (m_gameOver || m_isTraining) return;
    if (m_board[position] != '.') return;
    if (m_currentPlayer != 0) return;

    m_board[position] = 'X';
    updateBoard();

    int winner = checkWinner();
    if (winner == 'X') {
        m_statusLabel->setText(tr("恭喜！你赢了！"));
        m_totalWins++;
        m_gameOver = true;
    } else if (winner == 'T') {
        m_statusLabel->setText(tr("平局！"));
        m_totalTies++;
        m_gameOver = true;
    } else {
        m_currentPlayer = 1;
        m_statusLabel->setText(tr("AI思考中..."));
        QTimer::singleShot(300, this, &GameWidget::aiMove);
    }
    updateStats();
}

void GameWidget::aiMove()
{
    if (m_gameOver || m_currentPlayer != 1) return;

    int move = m_nn->getBestMove(m_board);
    if (move >= 0) {
        m_board[move] = 'O';
        updateBoard();

        int winner = checkWinner();
        if (winner == 'O') {
            m_statusLabel->setText(tr("AI赢了！"));
            m_totalLosses++;
            m_gameOver = true;
        } else if (winner == 'T') {
            m_statusLabel->setText(tr("平局！"));
            m_totalTies++;
            m_gameOver = true;
        } else {
            m_currentPlayer = 0;
            m_statusLabel->setText(tr("你的回合"));
        }
    }
    updateStats();
}

int GameWidget::checkWinner()
{
    for (int i = 0; i < 3; ++i)
        if (m_board[i*3] != '.' && m_board[i*3] == m_board[i*3+1] && m_board[i*3+1] == m_board[i*3+2])
            return m_board[i*3];
    for (int i = 0; i < 3; ++i)
        if (m_board[i] != '.' && m_board[i] == m_board[i+3] && m_board[i+3] == m_board[i+6])
            return m_board[i];
    if (m_board[0] != '.' && m_board[0] == m_board[4] && m_board[4] == m_board[8]) return m_board[0];
    if (m_board[2] != '.' && m_board[2] == m_board[4] && m_board[4] == m_board[6]) return m_board[2];
    for (int i = 0; i < 9; ++i) if (m_board[i] == '.') return 0;
    return 'T';
}

void GameWidget::updateBoard()
{
    for (int i = 0; i < 9; ++i) {
        if (m_board[i] == 'X') {
            m_cells[i]->setText("X");
            m_cells[i]->setStyleSheet("background-color: rgb(200, 220, 255);");
        } else if (m_board[i] == 'O') {
            m_cells[i]->setText("O");
            m_cells[i]->setStyleSheet("background-color: rgb(255, 220, 200);");
        } else {
            m_cells[i]->setText("");
            m_cells[i]->setStyleSheet("background-color: rgb(245, 245, 245);");
        }
    }
}

void GameWidget::updateStats()
{
    m_statsLabel->setText(tr("胜: %1  负: %2  平: %3").arg(m_totalWins).arg(m_totalLosses).arg(m_totalTies));
}

void GameWidget::onTrainingStats(int wins, int losses, int ties)
{
    float total = wins + losses + ties;
    if (total > 0) {
        QString info = m_infoEdit->toPlainText();
        info += QString("\n已训练 %1 局: 胜 %2 (%3%)  负 %4 (%5%)  平 %6 (%7%)")
            .arg((int)total)
            .arg(wins).arg(wins * 100.0f / total, 0, 'f', 1)
            .arg(losses).arg(losses * 100.0f / total, 0, 'f', 1)
            .arg(ties).arg(ties * 100.0f / total, 0, 'f', 1);
        m_infoEdit->setPlainText(info);
        QTextCursor cursor = m_infoEdit->textCursor();
        cursor.movePosition(QTextCursor::End);
        m_infoEdit->setTextCursor(cursor);
    }
}

void GameWidget::onTrainingComplete()
{
    m_isTraining = false;
    m_startButton->setEnabled(true);
    m_gamesSpinBox->setEnabled(true);
    m_threadCountSpinBox->setEnabled(true);
    m_restartButton->setEnabled(true);
    m_statusLabel->setText(tr("训练完成！现在可以与AI对战了"));
    m_progressBar->setValue(100);
    restartGame();
}
