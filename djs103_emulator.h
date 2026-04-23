/****************************************************************************
**
** Copyright (C) 2024
** DJS-103 Computer Emulator - Core Engine
**
** 103机 (DJS-103) 模拟器核心引擎
**   - 31位字长，最高位符号位，位1-30为小数部分（定点小数）
**   - 1024字磁鼓内存
**   - 两地址指令格式: [符号][6位操作码XY][12位地址1][12位地址2]
**
** 操作码XY解码:
**   X = 修饰符（第一个八进制数字），决定操作数来源和结果去向
**   Y = 操作码（第二个八进制数字），决定运算类型
**
** Y操作:
**   0=加法, 1=减法, 2=除法, 3=乘法, 6=逻辑与
**   4=控制命令, 5=传送命令, 7=输入/停机
**
** X修饰符 (Y=0,1,2,3,6时):
**   0: b,r = a○b        1: r = a○b
**   2: b,r = r○a        3: r = r○a
**   4: b,r = a○b,打印   5: r = |a|○|b|
**   6: b,r = r○a,打印   7: r = |r|○|b|
**
****************************************************************************/

#ifndef DJS103_EMULATOR_H
#define DJS103_EMULATOR_H

#include <array>
#include <vector>
#include <functional>
#include <cstdint>
#include <string>

class DJS103Emulator
{
public:
    static const int MEMORY_SIZE = 1024;
    static const int32_t WORD_MASK = 0x7FFFFFFF;      // 31-bit mask
    static const int32_t MAGNITUDE_MASK = 0x3FFFFFFF;  // 30-bit magnitude
    static const int32_t SIGN_BIT = 0x40000000;        // Bit 30 = sign

    DJS103Emulator();

    void reset();
    void loadProgram(const std::vector<int32_t> &program, int startAddress = 0);
    void step();
    void stop();

    // State queries
    double getAccumulatorValue() const;
    int32_t getAccumulator() const { return m_accumulator; }
    int32_t getProgramCounter() const { return m_programCounter; }
    int32_t getMemory(int addr) const;
    const std::array<int32_t, MEMORY_SIZE> &getMemory() const { return m_memory; }
    bool isRunning() const { return m_running; }
    bool isHalted() const { return m_halted; }
    int32_t getLastInstruction() const { return m_lastInstruction; }

    // Instruction decode helpers
    int getLastOpcode() const;
    int getLastX() const;
    int getLastY() const;
    int getLastAddr1() const;
    int getLastAddr2() const;
    std::string getLastInstructionMnemonic() const;

    // Set individual memory word
    void setMemory(int addr, int32_t value);

    // Set accumulator and PC
    void setAccumulator(int32_t value) { m_accumulator = value & WORD_MASK; }
    void setAccumulatorDouble(double value);
    void setProgramCounter(int32_t value) { m_programCounter = value % MEMORY_SIZE; }

    // Convert between internal format and double
    static double wordToDouble(int32_t word);
    static int32_t doubleToWord(double value);

    // Callbacks
    void setOutputCallback(std::function<void(const std::string &)> cb) { m_outputCallback = cb; }
    void setInputCallback(std::function<double()> cb) { m_inputCallback = cb; }

private:
    void executeInstruction(int32_t instruction);
    void executeArithmetic(int X, int Y, int addr1, int addr2);
    void executeControl(int X, int addr1, int addr2);
    void executeMove(int X, int addr1, int addr2);
    void executeInputHalt(int X, int addr1, int addr2);
    void storeAccumulator(int addr);
    void printValue(double value);

    std::array<int32_t, MEMORY_SIZE> m_memory;
    int32_t m_accumulator;   // Sign-magnitude format (same as memory)
    int32_t m_programCounter;
    bool m_running;
    bool m_halted;
    int32_t m_lastInstruction;

    std::function<void(const std::string &)> m_outputCallback;
    std::function<double()> m_inputCallback;
};

#endif // DJS103_EMULATOR_H
