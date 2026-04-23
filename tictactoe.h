/****************************************************************************
**
** Copyright (C) 2024
** Based on ttt.c by antirez (https://github.com/antirez/ttt-rl)
**
****************************************************************************/

#ifndef TICTACTOE_H
#define TICTACTOE_H

#include <QWidget>
#include <QThread>
#include <QMutex>
#include <QAtomicInt>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTextEdit>
#include <QProgressBar>
#include <QTimer>
#include <QVector>
#include <QObject>

/**
 * @brief 神经网络参数（与 ttt.c 一致）
 */
#define NN_INPUT_SIZE 18
#define NN_HIDDEN_SIZE 100
#define NN_OUTPUT_SIZE 9
#define LEARNING_RATE 0.1f

/**
 * @brief 井字棋游戏状态
 */
struct GameState {
    char board[9];
    int current_player;
};

/**
 * @brief 神经网络结构（与 ttt.c 一致）
 */
struct NeuralNetwork {
    float weights_ih[NN_INPUT_SIZE * NN_HIDDEN_SIZE];
    float weights_ho[NN_HIDDEN_SIZE * NN_OUTPUT_SIZE];
    float biases_h[NN_HIDDEN_SIZE];
    float biases_o[NN_OUTPUT_SIZE];

    float inputs[NN_INPUT_SIZE];
    float hidden[NN_HIDDEN_SIZE];
    float raw_logits[NN_OUTPUT_SIZE];
    float outputs[NN_OUTPUT_SIZE];
};

/**
 * @brief 神经网络封装类
 */
class TicTacToeNN
{
public:
    TicTacToeNN();
    ~TicTacToeNN();

    NeuralNetwork* getNN() { return &m_nn; }
    const NeuralNetwork* getNN() const { return &m_nn; }
    int getBestMove(const char *board);
    void getMoveProbs(const char *board, float *probs);
    int getParamCount() const { return NN_INPUT_SIZE * NN_HIDDEN_SIZE +
                                       NN_HIDDEN_SIZE * NN_OUTPUT_SIZE +
                                       NN_HIDDEN_SIZE + NN_OUTPUT_SIZE; }
    void learnFromGame(const int *moveHistory, int numMoves, char winner);

    // 多线程支持：权重复制与合并
    void copyWeightsFrom(const TicTacToeNN &other);
    void averageWeightsFrom(const QVector<TicTacToeNN *> &nns);

private:
    NeuralNetwork m_nn;
    void initNeuralNetwork();
    void boardToInputs(const char *board, float *inputs);
    void forwardPass(float *inputs);
    void backProp(const float *targetProbs, float rewardScaling);
    static float relu(float x);
    static float reluDerivative(float x);
    static void softmax(float *input, float *output, int size);
};

/**
 * @brief 单个训练工作线程
 */
class RLTrainingWorker : public QThread
{
    Q_OBJECT

public:
    RLTrainingWorker(int workerId, int numGames, int syncInterval,
                     QObject *parent = nullptr);
    ~RLTrainingWorker();

    TicTacToeNN* getLocalNN() { return m_localNN; }
    QMutex* getLocalNNMutex() { return &m_nnMutex; }
    void stopTraining();

    // 全局原子计数器（由 RLTrainingTask 设置）
    void setGlobalCounters(QAtomicInt *games, QAtomicInt *wins,
                           QAtomicInt *losses, QAtomicInt *ties) {
        m_globalGamesPlayed = games;
        m_globalWins = wins;
        m_globalLosses = losses;
        m_globalTies = ties;
    }

signals:
    void syncRequested(int workerId);

protected:
    void run() override;

private:
    int m_workerId;
    int m_numGames;
    int m_syncInterval;
    QAtomicInt m_stopped;
    TicTacToeNN *m_localNN;
    QMutex m_nnMutex;
    QAtomicInt *m_globalGamesPlayed;
    QAtomicInt *m_globalWins;
    QAtomicInt *m_globalLosses;
    QAtomicInt *m_globalTies;

    void initGame(GameState *state);
    int checkGameOver(GameState *state, char *winner);
    int getRandomMove(GameState *state);
    char playRandomGame();
};

/**
 * @brief 训练管理器：协调多线程训练
 */
class RLTrainingTask : public QObject
{
    Q_OBJECT

public:
    RLTrainingTask(TicTacToeNN *nn, QObject *parent = nullptr);
    ~RLTrainingTask();
    void setTrainingParams(int numGames, int threadCount = 1);
    void startTraining();
    void stopTraining();
    bool isRunning() const;

signals:
    void progressChanged(int value);
    void statsUpdated(int wins, int losses, int ties);
    void trainingComplete();

private slots:
    void onWorkerSyncRequested(int workerId);
    void onWorkerFinished();

private:
    TicTacToeNN *m_nn;
    int m_numGames;
    int m_threadCount;

    QVector<RLTrainingWorker *> m_workers;
    int m_activeWorkers;

    // 全局原子计数器
    QAtomicInt m_globalGamesPlayed;
    QAtomicInt m_globalWins;
    QAtomicInt m_globalLosses;
    QAtomicInt m_globalTies;

    int m_lastReportedProgress;

    QMutex m_syncMutex;

    void mergeWorkerWeights();
};

/**
 * @brief 井字棋游戏控件
 */
class GameWidget : public QWidget
{
    Q_OBJECT

public:
    GameWidget(QWidget *parent = nullptr);
    ~GameWidget();

public slots:
    void startTraining(int numGames);
    void stopTraining();
    void restartGame();
    void playerMove(int position);

private slots:
    void onTrainingStats(int wins, int losses, int ties);
    void onTrainingComplete();

private:
    void createUI();
    void updateBoard();
    void updateStats();
    int checkWinner();
    void aiMove();

    char m_board[9];
    int m_currentPlayer;
    bool m_gameOver;
    bool m_isTraining;
    int m_totalWins, m_totalLosses, m_totalTies;

    TicTacToeNN *m_nn;
    RLTrainingTask *m_trainingTask;

    QPushButton *m_cells[9];
    QLabel *m_statusLabel;
    QLabel *m_statsLabel;
    QSpinBox *m_gamesSpinBox;
    QSpinBox *m_threadCountSpinBox;
    QPushButton *m_startButton;
    QPushButton *m_restartButton;
    QProgressBar *m_progressBar;
    QTextEdit *m_infoEdit;
};

#endif // TICTACTOE_H
