/****************************************************************************
**
** DJS-103 计算机模拟器 - 核心引擎实现
**
** DJS-103 (M-3) 指令集完整实现
**
** 指令格式: [符号1位][6位操作码XY][12位地址1][12位地址2]
**   操作码XY: X=修饰符, Y=操作类型
**
** Y=0,1,2,3,6 时为算术逻辑指令，X修饰符决定操作数组合:
**   X=0: b,r = a○b        X=1: r = a○b
**   X=2: b,r = r○a        X=3: r = r○a
**   X=4: b,r = a○b,打印   X=5: r = |a|○|b|
**   X=6: b,r = r○a,打印   X=7: r = |r|○|b|
**
** Y操作: 0=加, 1=减, 2=除, 3=乘, 6=逻辑与
**
** 控制命令 (Y=4,5,7):
**   05/15: 传送 (addr1)->(addr2), r<-数据
**   45/55: 传送并打印
**   07/27: 输入
**   24: 无条件跳转
**   34: 条件分支
**   64: 无条件跳转并打印
**   74: 无条件跳转(取绝对值)
**   04/14/44/54/17/37/57/77: 停机
**
****************************************************************************/

#include "djs103_emulator.h"
#include <sstream>
#include <iomanip>
#include <cmath>
#include <cstdlib>

// ============ 构造/复位 ============

DJS103Emulator::DJS103Emulator()
{
    reset();
}

void DJS103Emulator::reset()
{
    m_memory.fill(0);
    m_accumulator = 0;
    m_programCounter = 0;
    m_running = false;
    m_halted = false;
    m_lastInstruction = 0;
}

// ============ 程序加载 ============

void DJS103Emulator::loadProgram(const std::vector<int32_t> &program, int startAddress)
{
    for (size_t i = 0; i < program.size() && (startAddress + (int)i) < MEMORY_SIZE; ++i) {
        m_memory[startAddress + i] = program[i] & WORD_MASK;
    }
    m_programCounter = startAddress % MEMORY_SIZE;
    m_halted = false;
    m_running = false;
}

// ============ 执行控制 ============

void DJS103Emulator::step()
{
    if (m_halted)
        return;

    m_running = true;

    if (m_programCounter < 0 || m_programCounter >= MEMORY_SIZE) {
        m_running = false;
        m_halted = true;
        if (m_outputCallback)
            m_outputCallback("ERROR: PC out of range");
        return;
    }

    int32_t instruction = m_memory[m_programCounter];
    m_lastInstruction = instruction;
    m_programCounter = (m_programCounter + 1) % MEMORY_SIZE;

    executeInstruction(instruction);
}

void DJS103Emulator::stop()
{
    m_running = false;
}

// ============ 存储器访问 ============

int32_t DJS103Emulator::getMemory(int addr) const
{
    if (addr >= 0 && addr < MEMORY_SIZE)
        return m_memory[addr];
    return 0;
}

void DJS103Emulator::setMemory(int addr, int32_t value)
{
    if (addr >= 0 && addr < MEMORY_SIZE)
        m_memory[addr] = value & WORD_MASK;
}

// ============ 格式转换 ============

double DJS103Emulator::wordToDouble(int32_t word)
{
    // 103机定点小数: 符号位(bit30), 位0-29为小数绝对值
    // 值 = (-1)^sign * (magnitude / 2^30)
    word &= WORD_MASK;
    bool negative = (word & SIGN_BIT) != 0;
    int32_t magnitude = word & MAGNITUDE_MASK;
    double value = (double)magnitude / (double)(1 << 30);
    return negative ? -value : value;
}

int32_t DJS103Emulator::doubleToWord(double value)
{
    // 转换为103机内部格式
    // 103机采用符号-幅度定点小数 |x| < 1
    bool negative = (value < 0);
    if (negative) value = -value;
    // 限制到有效范围 [0, 1)
    // 溢出时饱和到最大可表示值 (0.999999999068...)
    if (value >= 1.0)
        value = (double)MAGNITUDE_MASK / (double)(1LL << 30);
    int32_t magnitude = (int32_t)(value * (1LL << 30)) & MAGNITUDE_MASK;
    if (negative)
        return SIGN_BIT | magnitude;
    return magnitude;
}

double DJS103Emulator::getAccumulatorValue() const
{
    return wordToDouble(m_accumulator);
}

void DJS103Emulator::setAccumulatorDouble(double value)
{
    m_accumulator = doubleToWord(value);
}

// ============ 指令译码辅助函数 ============

int DJS103Emulator::getLastOpcode() const
{
    return (m_lastInstruction >> 24) & 0x3F;
}

int DJS103Emulator::getLastX() const
{
    return (m_lastInstruction >> 27) & 0x7;
}

int DJS103Emulator::getLastY() const
{
    return getLastOpcode() & 0x7;
}

int DJS103Emulator::getLastAddr1() const
{
    return (m_lastInstruction >> 12) & 0xFFF;
}

int DJS103Emulator::getLastAddr2() const
{
    return m_lastInstruction & 0xFFF;
}

std::string DJS103Emulator::getLastInstructionMnemonic() const
{
    int op = getLastOpcode();
    int X = getLastX();
    int Y = getLastY();
    int a1 = getLastAddr1();
    int a2 = getLastAddr2();

    std::ostringstream ss;
    ss << std::oct;

    // 操作符映射
    static const char *opNames[] = {"加", "减", "除", "乘", "", "", "与"};

    // Y=5: 控制传送指令
    if (Y == 5) {
        switch (op) {
        case 5:    ss << "传送 [" << a1 << "]->[" << a2 << "], r<-[" << a1 << "]"; return ss.str();
        case 13:   ss << "传送 [" << a1 << "]->[" << a2 << "], r<-[" << a1 << "]"; return ss.str();
        case 37:   ss << "传送 [" << a1 << "]->[" << a2 << "], 打印"; return ss.str();
        case 45:   ss << "传送 [" << a1 << "]->[" << a2 << "], 打印"; return ss.str();
        }
    }

    // Y=4: 控制转移指令
    if (Y == 4) {
        switch (op) {
        case 20:   ss << "跳转 PC<-" << a1 << ", r->[" << a2 << "]"; return ss.str();  // +24
        case 28:   ss << "分支 r≥0?PC<-" << a2 << ":PC<-" << a1; return ss.str();       // +34
        case 52:   ss << "跳转 PC<-" << a1 << ", r->[" << a2 << "], 打印"; return ss.str();  // +64
        case 60:   ss << "跳转 PC<-" << a2 << ", |r|->r"; return ss.str();           // +74
        case 4: case 12: case 36: case 44:   // +04,+14,+44,+54
        case 15: case 31: case 47: case 63:  // +17,+37,+57,+77
            ss << "停机"; return ss.str();
        }
    }

    // Y=7: 输入/停机指令
    if (Y == 7) {
        switch (op) {
        case 7:    ss << "输入 ->[" << a2 << "]"; return ss.str();   // +07
        case 23:   ss << "输入 ->[" << a2 << "]"; return ss.str();  // +27
        case 15:   ss << "停机"; return ss.str();  // +17
        case 31:   ss << "停机"; return ss.str();  // +37
        case 47:   ss << "停机"; return ss.str();  // +57
        case 63:   ss << "停机"; return ss.str();  // +77
        }
    }

    // Y=0,1,2,3,6: 算术逻辑指令
    if (Y <= 6 && Y != 4 && Y != 5) {
        ss << opNames[Y];
        switch (X) {
        case 0: ss << " [" << a1 << "]○[" << a2 << "], ->[" << a2 << "],r"; break;
        case 1: ss << " [" << a1 << "]○[" << a2 << "], ->r"; break;
        case 2: ss << " r○[" << a1 << "], ->[" << a2 << "],r"; break;
        case 3: ss << " r○[" << a1 << "], ->r"; break;
        case 4: ss << " [" << a1 << "]○[" << a2 << "], ->[" << a2 << "],r,打印"; break;
        case 5: ss << " |[" << a1 << "]|○|[" << a2 << "]|, ->r"; break;
        case 6: ss << " r○[" << a1 << "], ->[" << a2 << "],r,打印"; break;
        case 7: ss << " |r|○|[" << a2 << "]|, ->r"; break;
        }
        return ss.str();
    }

    ss << "未知操作码 " << op;
    return ss.str();
}

// ============ 内部辅助函数 ============

void DJS103Emulator::storeAccumulator(int addr)
{
    if (addr >= 0 && addr < MEMORY_SIZE)
        m_memory[addr] = m_accumulator & WORD_MASK;
}

void DJS103Emulator::printValue(double value)
{
    if (m_outputCallback) {
        // 先量化为103机内部格式再转回，确保打印值与存储值一致
        int32_t word = doubleToWord(value);
        double stored = wordToDouble(word);
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(10) << stored;
        m_outputCallback(ss.str());
    }
}

// ============ 指令执行 ============

void DJS103Emulator::executeInstruction(int32_t instruction)
{
    int opcode = (instruction >> 24) & 0x3F;
    int X = (opcode >> 3) & 0x7;   // 第一个八进制位
    int Y = opcode & 0x7;           // 第二个八进制位
    int addr1 = (instruction >> 12) & 0xFFF;
    int addr2 = instruction & 0xFFF;

    addr1 %= MEMORY_SIZE;
    addr2 %= MEMORY_SIZE;

    // 根据Y分发
    switch (Y) {
    case 0: // 加法
    case 1: // 减法
    case 2: // 除法
    case 3: // 乘法
    case 6: // 逻辑与
        executeArithmetic(X, Y, addr1, addr2);
        break;
    case 4: // 控制命令
        executeControl(X, addr1, addr2);
        break;
    case 5: // 传送命令
        executeMove(X, addr1, addr2);
        break;
    case 7: // 输入/停机
        executeInputHalt(X, addr1, addr2);
        break;
    default:
        if (m_outputCallback) {
            std::ostringstream ss;
            ss << "Unknown opcode: " << std::oct << opcode;
            m_outputCallback(ss.str());
        }
        m_running = false;
        m_halted = true;
        break;
    }
}

void DJS103Emulator::executeArithmetic(int X, int Y, int addr1, int addr2)
{
    // 103机硬件级运算: 31位 = 1位符号(bit0) + 30位尾数(bit1-30)
    // 小数点约定在符号位和尾数之间，硬件只做整数运算

    // 根据X修饰符获取操作数(整数形式)
    int32_t wordA = m_memory[addr1] & WORD_MASK;
    int32_t wordB = m_memory[addr2] & WORD_MASK;
    int32_t wordR = m_accumulator & WORD_MASK;

    // 提取符号位和尾数
    bool signA = (wordA & SIGN_BIT) != 0;
    bool signB = (wordB & SIGN_BIT) != 0;
    bool signR = (wordR & SIGN_BIT) != 0;
    uint32_t magA = wordA & MAGNITUDE_MASK;  // 30位尾数
    uint32_t magB = wordB & MAGNITUDE_MASK;
    uint32_t magR = wordR & MAGNITUDE_MASK;

    // 根据X确定左操作数和右操作数
    uint32_t leftMag, rightMag;
    bool leftSign, rightSign;
    bool storeB = false;
    bool printResult = false;

    switch (X) {
    case 0: // 结果存B和累加器
        leftSign = signA; leftMag = magA;
        rightSign = signB; rightMag = magB;
        storeB = true;
        break;
    case 1: // 结果仅存累加器
        leftSign = signA; leftMag = magA;
        rightSign = signB; rightMag = magB;
        storeB = false;
        break;
    case 2: // r op A，结果存B和累加器
        leftSign = signR; leftMag = magR;
        rightSign = signA; rightMag = magA;
        storeB = true;
        break;
    case 3: // r op A，结果仅存累加器
        leftSign = signR; leftMag = magR;
        rightSign = signA; rightMag = magA;
        storeB = false;
        break;
    case 4: // 同case0，但打印结果
        leftSign = signA; leftMag = magA;
        rightSign = signB; rightMag = magB;
        storeB = true;
        printResult = true;
        break;
    case 5: // |A| op |B|，仅存累加器
        leftSign = false; leftMag = magA;
        rightSign = false; rightMag = magB;
        storeB = false;
        break;
    case 6: // 同case2，但打印结果
        leftSign = signR; leftMag = magR;
        rightSign = signA; rightMag = magA;
        storeB = true;
        printResult = true;
        break;
    case 7: // |r| op |B|，仅存累加器
        leftSign = false; leftMag = magR;
        rightSign = false; rightMag = magB;
        storeB = false;
        break;
    default:
        leftSign = signA; leftMag = magA;
        rightSign = signB; rightMag = magB;
        storeB = true;
        break;
    }

    // 硬件级整数运算
    uint32_t resultMag = 0;
    bool resultSign = false;

    switch (Y) {
    case 0: // 加法
    {
        // 硬件方法：两个操作数都转换为31位补码形式，然后直接二进制相加
        // 正数：31位补码 = 0x40000000 | 尾数（符号位0）
        // 负数：31位补码 = 取反 + 1

        // 转为31位有符号整数补码表示
        uint32_t leftTwosComplement = leftSign ?
            (SIGN_BIT | (((~leftMag) & MAGNITUDE_MASK) + 1)) : leftMag;
        uint32_t rightTwosComplement = rightSign ?
            (SIGN_BIT | (((~rightMag) & MAGNITUDE_MASK) + 1)) : rightMag;

        // 31位补码相加（按32位整数运算，自动丢弃溢出）
        uint32_t sum = leftTwosComplement + rightTwosComplement;

        // 提取结果：bit30是符号位，bit0-29是尾数
        resultSign = (sum & SIGN_BIT) != 0;
        if (resultSign) {
            // 负数：从补码还原幅度
            resultMag = ((~sum) & MAGNITUDE_MASK) + 1;
            // if (resultMag > MAGNITUDE_MASK) resultMag = 0; // 溢出保护（103机硬件无溢出处理）
        } else {
            resultMag = sum & MAGNITUDE_MASK;
        }
        break;
    }
    case 1: // 减法: left - right = left + (-right的补码)
    {
        // 右操作数取反: -right = 补码(right)
        uint32_t negRightMag = (MAGNITUDE_MASK + 1) - rightMag;
        uint64_t diff = (uint64_t)leftMag + negRightMag;
        resultMag = diff & MAGNITUDE_MASK;
        // 结果符号由硬件自动处理: 正数-正数可能得负
        // 通过检查是否产生借位(高位进位)来判断
        bool carry = (diff >> 30) & 1;
        if (!carry && !leftSign && !rightSign) {
            // 正 - 正 = 负
            resultSign = true;
        } else if (!carry && leftSign && rightSign) {
            // 负 - 负 = 正
            resultSign = false;
        } else if (!carry && !leftSign && rightSign) {
            // 正 - 负 = 正
            resultSign = false;
        } else if (!carry && leftSign && !rightSign) {
            // 负 - 正 = 负
            resultSign = true;
        } else {
            resultSign = leftSign;
        }
        break;
    }
    case 2: // 除法: 必须满足 |left| < |right|
    {
        // 硬件铁律: |被除数| < |除数|，否则溢出
        if (leftMag >= rightMag) {
            // 溢出: 商>=1，硬件输出错误结果(这里饱和到最大)
            resultMag = MAGNITUDE_MASK;
            resultSign = (leftSign != rightSign);
        } else {
            // 合法除法: 30位/30位 = 30位
            // 使用64位计算避免溢出
            uint64_t dividend = ((uint64_t)leftMag) << 30;  // 扩展到60位
            resultMag = (uint32_t)(dividend / rightMag);
            resultSign = (leftSign != rightSign);
        }
        break;
    }
    case 3: // 乘法: 30位×30位 = 60位，取高30位
    {
        // 符号位异或
        resultSign = (leftSign != rightSign);
        // 尾数整数乘法
        uint64_t product = (uint64_t)leftMag * (uint64_t)rightMag;
        // 取高30位(丢弃低30位)
        resultMag = (product >> 30) & MAGNITUDE_MASK;
        break;
    }
    case 6: // 逻辑与
    {
        resultMag = leftMag & rightMag;
        resultSign = false;  // 逻辑与结果为正
        break;
    }
    default:
        if (m_outputCallback) {
            std::ostringstream ss;
            ss << "错误: 未知的算术操作符 Y=" << Y;
            m_outputCallback(ss.str());
        }
        return;
    }

    // 组装结果: 符号位在bit30
    m_accumulator = resultSign ? (SIGN_BIT | resultMag) : resultMag;

    // 如果修饰符要求则存储到addr2
    if (storeB) {
        m_memory[addr2] = m_accumulator & WORD_MASK;
    }

    // 需要时打印
    if (printResult) {
        printValue(wordToDouble(m_accumulator));
    }
}

void DJS103Emulator::executeControl(int X, int addr1, int addr2)
{
// Y=4, X决定具体的控制命令
    // 操作码 = X*8 + 4
    int opcode = (X << 3) | 4;

    // 所有case值为八进制操作码的十进制等价
    switch (opcode) {
    case 20:   // 八进制24: 无条件跳转
        storeAccumulator(addr2);
        m_programCounter = addr1;
        break;

    case 28:   // 八进制34: 条件分支
        {
            bool negative = (m_accumulator & SIGN_BIT) != 0;
            if (!negative) {
                m_programCounter = addr2;
            } else {
                m_programCounter = addr1;
            }
        }
        break;

    case 36:   // 八进制44: 停机
        m_running = false;
        m_halted = true;
        if (m_outputCallback) {
            std::ostringstream ss;
            ss << "HALT at PC=" << std::oct << ((m_programCounter - 1 + MEMORY_SIZE) % MEMORY_SIZE);
            m_outputCallback(ss.str());
        }
        break;

    case 44:   // 八进制54: 停机
        m_running = false;
        m_halted = true;
        if (m_outputCallback) {
            std::ostringstream ss;
            ss << "HALT at PC=" << std::oct << ((m_programCounter - 1 + MEMORY_SIZE) % MEMORY_SIZE);
            m_outputCallback(ss.str());
        }
        break;

    case 52:   // 八进制64: 无条件跳转并打印
        storeAccumulator(addr2);
        printValue(getAccumulatorValue());
        m_programCounter = addr1;
        break;

    case 60:   // 八进制74: 无条件跳转(取绝对值)
    {
        double absVal = fabs(getAccumulatorValue());
        m_accumulator = doubleToWord(absVal);
        m_programCounter = addr2;
        break;
    }

    case 4:    // 八进制04: 停机
    case 12:   // 八进制14: 停机
        m_running = false;
        m_halted = true;
        if (m_outputCallback) {
            std::ostringstream ss;
            ss << "HALT at PC=" << std::oct << ((m_programCounter - 1 + MEMORY_SIZE) % MEMORY_SIZE);
            m_outputCallback(ss.str());
        }
        break;

    default:
        m_running = false;
        m_halted = true;
        if (m_outputCallback) {
            std::ostringstream ss;
            ss << "HALT (unknown control) at PC=" << std::oct
               << ((m_programCounter - 1 + MEMORY_SIZE) % MEMORY_SIZE);
            m_outputCallback(ss.str());
        }
        break;
    }
}

void DJS103Emulator::executeMove(int X, int addr1, int addr2)
{
    int opcode = (X << 3) | 5;

// 所有case值为八进制操作码的十进制等价

    switch (opcode) {
    case 5:    // 八进制05: 传送
        m_accumulator = m_memory[addr1] & WORD_MASK;
        m_memory[addr2] = m_accumulator;
        break;

    case 13:   // 八进制15: 传送变体
        m_accumulator = m_memory[addr1] & WORD_MASK;
        m_memory[addr2] = m_accumulator;
        break;

    case 37:   // 八进制45: 传送并打印
    {
        int32_t data = m_memory[addr1] & WORD_MASK;
        m_accumulator = data;
        m_memory[addr2] = data;
        double value = wordToDouble(data);
        printValue(value);
        break;
    }

    case 45:   // 八进制55: 传送并打印变体
    {
        int32_t data = m_memory[addr1] & WORD_MASK;
        m_accumulator = data;
        m_memory[addr2] = data;
        double value = wordToDouble(data);
        printValue(value);
        break;
    }

    default:
        m_accumulator = m_memory[addr1] & WORD_MASK;
        m_memory[addr2] = m_accumulator;
        break;
    }
}

void DJS103Emulator::executeInputHalt(int X, int addr1, int addr2)
{
    (void)addr1;  // 输入/停机命令中未使用
    // Y=7, X决定具体的命令
    int opcode = (X << 3) | 7;

    // 所有case值为八进制操作码的十进制等价
    switch (opcode) {
    case 7:    // 八进制07: 输入
    case 23:   // 八进制27: 输入变体
    {
        double inputValue = 0.0;
        if (m_inputCallback) {
            inputValue = m_inputCallback();
        }
        m_memory[addr2] = doubleToWord(inputValue);
        if (m_outputCallback) {
            std::ostringstream ss;
            ss << "INPUT -> [" << std::oct << addr2 << "] = "
               << std::dec << std::setprecision(10) << inputValue;
            m_outputCallback(ss.str());
        }
        break;
    }

    case 15:   // 八进制17: 停机
    case 31:   // 八进制37: 停机
    case 47:   // 八进制57: 停机
    case 63:   // 八进制77: 停机
        m_running = false;
        m_halted = true;
        if (m_outputCallback) {
            std::ostringstream ss;
            ss << "HALT at PC=" << std::oct << ((m_programCounter - 1 + MEMORY_SIZE) % MEMORY_SIZE);
            m_outputCallback(ss.str());
        }
        break;

    default:
        m_running = false;
        m_halted = true;
        if (m_outputCallback) {
            std::ostringstream ss;
            ss << "HALT at PC=" << std::oct << ((m_programCounter - 1 + MEMORY_SIZE) % MEMORY_SIZE);
            m_outputCallback(ss.str());
        }
        break;
    }
}
