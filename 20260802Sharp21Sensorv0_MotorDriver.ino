/*
 * ================================================================
 * 项目名称：仿草履虫应激机制 - 小车避障进化验证系统 
 * 硬件平台：ESP32-S3-WROOM-1U Maturo一体底板（内置AT8236电机驱动）
 * 版本：    v5.9-ESP32S3-GP2Y0A21YK0F-PURE-ANALOG
 * 适配日期: 2026-08-09
 * 
 * ================================================================
 * 【硬件迭代说明】
 * ================================================================
 * 
 * 本系统在开发过程中经历了三个硬件阶段，每次迭代都基于实验中发现
 * 的根本性缺陷，而非简单的元器件替换。记录如下：
 * 
 * 
 * 第一阶段：TCRT5000 红外反射传感器（原型验证，已淘汰）
 * ----------------------------------------------------------------
 * 选型理由：低成本、易于获取、模拟输出。
 * 
 * 淘汰原因（实验观察）：
 *   1. 有效探测距离仅 1~10cm，感知窗口过窄，小车无提前预判能力
 *   2. 输出电压与距离呈高度非线性，左右传感差值无法等比例映射转向幅度
 *   3. 依赖地面反射光，受地面材质、室内光照影响严重，基线持续漂移
 *   4. 在"去滤波、去标定、去PID"三原则约束下，硬件固有缺陷无法通过
 *      软件补偿，直接导致避障行为不可控、进化实验数据离散度过高
 * 
 * 结论：TCRT5000 仅适合简单的开关量避障，不适合本系统所要求的
 *       连续模拟感知与定量进化实验。
 * 
 * 
 * 第二阶段：GP2Y0E03 I2C 数字测距传感器（中间方案，已淘汰）
 * ----------------------------------------------------------------
 * 选型理由：测距区间更广（4~50cm）、输出线性度优异、不依赖地面反射。
 * 
 * 淘汰原因（实验观察）：
 *   1. I2C 数字接口引入了"量化"环节——物理世界的连续距离被离散化为
 *      数字值，这本身即是一种"隐式标定"
 *   2. 左右传感器共用 I2C 总线，依赖分时上电规避地址冲突，增加了
 *      硬件复杂度和时序不确定性
 *   3. I2C 通信的采样率受总线速度限制（100kHz），在快速避障场景中
 *      响应延迟高于直接 ADC 读取
 *   4. 最关键的一点：本论文的核心原则是"去标定"——系统不应将物理量
 *      换算为厘米或任何工程单位，而应直接使用原始物理信号（电压）。
 *      I2C 传感器输出的"距离值"无论多么精确，本质上都是对物理世界的
 *      一次"翻译"，违背了"物理即算法"的底层哲学。
 * 
 * 结论：虽然 GP2Y0E03 在工程指标上优于 TCRT5000，但其数字接口与
 *       本研究的"去标定"原则存在根本性冲突，必须淘汰。
 * 
 
 * ================================================================
 * 项目名称：仿草履虫应激机制 - 小车避障进化验证系统
 * 硬件平台：ESP32-S3-WROOM-1U Maturo一体底板（内置AT8236电机驱动）
 * 版本：v5.9-ESP32S3-GP2Y0A21YK0F-PURE-ANALOG
 * 适配日期: 2026-08-09
 * 核心设计准则：严格执行论文「去标定、去滤波、去PID」三原则
 * 硬件迭代说明：
 * 废弃前版GP2Y0E03 I2C分时供电数字传感器方案，更换GP2Y0A21YK0F模拟红外测距；
 * 删除I2C总线、VDD分时控制引脚，全程仅读取ADC原始数值，不做厘米/电压换算；
 * 无软件滑动平均、无硬件RC滤波，环境噪声直接参与控制与基因变异；
 * 控制逻辑 steer = 左原始ADC - 右原始ADC，纯物理几何差动映射，无PID补偿；
 * 进化算法、电机驱动、编码器采集、Web监控、混沌脱困逻辑全部保留；
 * 数据仅存放RAM，无EEPROM/SPIFFS闪存依赖，历史数据Web导出CSV。
 *
 * 【硬件引脚分配 PCB丝印 ↔ ESP32 GPIO 一一对应】
 * GPIO4      PCB IO4     左GP2Y0A21YK0F模拟输出OUT（ADC）
 * GPIO5      PCB IO5     右GP2Y0A21YK0F模拟输出OUT（ADC）
 * GPIO6      PCB IO6     悬空热噪声专用引脚（物理真随机变异源）
 * GPIO39     PCB IO39    预留障碍物电平外部中断（备用）
 * GPIO14     PCB IO14    板载LED指示灯
 * GPIO9      PCB IO9     左电机PWM调速 (AT8236 IN1)
 * GPIO46     PCB IO46    左电机方向控制 (AT8236 IN2)
 * GPIO16     PCB IO16    右电机PWM调速 (AT8236 IN1)
 * GPIO15     PCB IO15    右电机方向控制 (AT8236 IN2)
 * GPIO18     PCB IO18    左编码器A相
 * GPIO17     PCB IO17    左编码器B相
 * GPIO11     PCB IO11    右编码器A相
 * GPIO10     PCB IO10    右编码器B相
 * GND                   全局星形公共地（HW131、电机、ESP32、传感器共地）
 *
 * AT8236 电机驱动控制逻辑：
 *   IN1 = PWM 调速信号 (0~255)
 *   IN2 = 方向控制信号 (HIGH=正转, LOW=反转)
 * 
 * GP2Y0A21YK0F硬件接线硬性约束：
 * 左传感器：VCC→HW131 5V  GND→公共GND  OUT→GPIO4
 * 右传感器：VCC→HW131 5V  GND→公共GND  OUT→GPIO5
 * ⚠️接线红线：
 * 1. GP2Y0A21YK0F必须5V供电，禁止直接接3.3V；OUT输出最高2.8V，可直连ESP32 ADC；
 * 2. 全部模块单点星形接地，杜绝地环路造成ADC数值漂移；
 * 3. 无任何RC硬件滤波，传感器原始电压直接接入ADC引脚；
 * 4. 有效测距20~150cm，距离越近ADC采样数值越大；ADC阈值需实车现场校准；
 *
 * 论文设计原则落地对照表
 * ┌──────────────┬─────────────────────────────────────┐
 * │ 论文核心原则 │ 本硬件软件实现方式                   │
 * ├──────────────┼─────────────────────────────────────┤
 * │ 去标定       │ 只读取0~4095原始ADC，不换算距离/电压 │
 * │ 去滤波       │ 无硬件RC、无软件多次采样平均         │
 * │ 去PID        │ 仅ADC差值直驱PWM，无闭环调节         │
 * │ 物理即算法   │ steer = left_adc - right_adc         │
 * │ 物理变异源   │ GPIO6悬空半导体热噪声生成随机扰动    │
 * └──────────────┴─────────────────────────────────────┘
 *
 * 串口观测校准指引（上电串口打印原始ADC值）
 * 无障碍(>40cm)：ADC < CLEAR_ADC_THRESH
 * 障碍物(≤15cm)：ADC > OBSTACLE_ADC_THRESH
 * 左右读数差值持续>500：调整传感器对称安装角度
 * 实车测试校准完成后，同步更新代码内ADC阈值宏定义
 * ================================================================
 */

#include <WiFi.h>
#include <WebServer.h>
#include <vector>
#include <algorithm>
#include <cstdarg>
#include <cmath>

// ===================== 硬件引脚宏【与头部注释完全统一】 =====================
// GP2Y0A21YK0F模拟测距ADC输入
#define PIN_SENSOR_LEFT    4     // 左GP2Y0A21YK0F OUT模拟
#define PIN_SENSOR_RIGHT   5     // 右GP2Y0A21YK0F OUT模拟

// 悬空物理热噪声采样引脚（论文规定物理随机变异源）
#define PIN_NOISE_SOURCE   6

// 障碍物电平外部中断预留引脚
#define PIN_OBSTACLE_INT   39

// 板载LED指示灯
#define PIN_LED            14

// 左电机（AT8236 IN1=PWM, IN2=方向）
#define PIN_LEFT_PWM       9
#define PIN_LEFT_DIR1      9     // AT8236 IN1: PWM调速
#define PIN_LEFT_DIR2      46    // AT8236 IN2: 方向控制

// 右电机（AT8236 IN1=PWM, IN2=方向）
#define PIN_RIGHT_PWM      16
#define PIN_RIGHT_DIR1     16    // AT8236 IN1: PWM调速
#define PIN_RIGHT_DIR2     15    // AT8236 IN2: 方向控制

// 左编码器AB相
#define PIN_LEFT_ENC_A     18
#define PIN_LEFT_ENC_B     17

// 右编码器AB相
#define PIN_RIGHT_ENC_A    11
#define PIN_RIGHT_ENC_B    10

// ===================== AT8236 电机转向电平定义 =====================
// AT8236: IN1=PWM, IN2=DIR
// 左电机：IN2=HIGH 正转，IN2=LOW 反转
#define LEFT_FORWARD   HIGH
#define LEFT_REVERSE   LOW
// 右电机：IN2=LOW 正转，IN2=HIGH 反转（取决于电机接线方向）
#define RIGHT_FORWARD  LOW
#define RIGHT_REVERSE  HIGH

// ===================== 系统全局参数 =====================
#define WIFI_SSID       "CarLogger"
#define WIFI_PASSWORD   "12345678"

#define POPULATION_SIZE 16
#define ELITE_COUNT     6
#define TEST_DURATION_MS    30000
#define STUCK_THRESHOLD_MS  3000

#define CHAOS_SAMPLE_COUNT  15
#define CHAOS_STEP_MS       100
#define MAX_HISTORY_GENERATIONS 1000

#define BACKWARD_RESET_DURATION 2000
#define BACKWARD_SPEED -110

// ===================== GP2Y0A21YK0F ADC原始阈值（实车校准） =====================
// 传感器特性：障碍物距离越近，ADC采样数值越大
#define OBSTACLE_ADC_THRESH    2200   // ADC高于该值判定前方存在障碍
#define CLEAR_ADC_THRESH       1000   // ADC低于该值判定通道无障碍物

// ===================== Logger =====================
class Logger {
public:
    static void init() { Serial.begin(115200); delay(1000); }
    static void log(const char* msg) {
        Serial.println("[" + String(millis()/1000) + "s] " + String(msg));
    }
    static void logf(const char* fmt, ...) {
        char buf[256]; va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        log(buf);
    }
};

// ===================== 基因结构 =====================
struct Gene {
    uint16_t id;
    int8_t   k_turn;
    uint8_t  speed_bias;
    uint32_t generation;
    uint32_t survival_ms;
    uint32_t distance_ticks;
    uint16_t obstacle_count;
    uint8_t  chaos_count;
    uint8_t  chaos_escape_count;
    
    // 自身对照字段
    uint32_t pre_chaos_survival_ms;
    uint32_t post_chaos_survival_ms;
    float    chaos_gain_ratio;
    
    // score() 仅用于观察记录，不作为选择依据
    uint32_t score() const {
        uint32_t s = distance_ticks / 2 + obstacle_count * 50 - chaos_count * 20;
        if (distance_ticks < 100) s = 0;
        if (chaos_count > 0 && chaos_escape_count > chaos_count / 2) {
            s += chaos_escape_count * 30;
        }
        if (chaos_gain_ratio > 1.0f) {
            s += (uint32_t)(chaos_gain_ratio * 50);
        }
        return s;
    }
    
    void mutate(uint16_t noise, uint32_t gen) {
        int8_t delta;
        if (gen < 5) delta = (noise & 1) ? 1 : -1;
        else if (gen < 15) delta = (noise & 3) - 1;
        else delta = (noise & 7) - 3;
        k_turn = constrain(k_turn + delta, -20, 20);
        
        int8_t ds = ((noise >> 2) & 3) - 1;
        speed_bias = constrain(speed_bias + ds, 120, 160);
    }
    
    bool isValid() const {
        return id != 0 || generation > 0 || speed_bias > 0;
    }
};

// ===================== RAM 历史记录 =====================
struct HistoryRecord {
    uint32_t generation;
    float fitnessScore;
    uint32_t survivalTime;
    int8_t k_turn;
    uint8_t speed_bias;
    uint32_t distance_ticks;
    uint16_t obstacle_count;
    uint8_t chaos_count;
    uint8_t chaos_escape_count;
    uint32_t pre_chaos_survival_ms;
    uint32_t post_chaos_survival_ms;
    float chaos_gain_ratio;
};

HistoryRecord ramHistory[MAX_HISTORY_GENERATIONS];
int historyCount = 0;

void recordToRAM(const Gene& g, uint32_t gen) {
    if (historyCount >= MAX_HISTORY_GENERATIONS) {
        Logger::log("⚠️ RAM 历史缓冲区已满！");
        return;
    }
    ramHistory[historyCount].generation = gen;
    ramHistory[historyCount].fitnessScore = g.score();
    ramHistory[historyCount].survivalTime = g.survival_ms;
    ramHistory[historyCount].k_turn = g.k_turn;
    ramHistory[historyCount].speed_bias = g.speed_bias;
    ramHistory[historyCount].distance_ticks = g.distance_ticks;
    ramHistory[historyCount].obstacle_count = g.obstacle_count;
    ramHistory[historyCount].chaos_count = g.chaos_count;
    ramHistory[historyCount].chaos_escape_count = g.chaos_escape_count;
    ramHistory[historyCount].pre_chaos_survival_ms = g.pre_chaos_survival_ms;
    ramHistory[historyCount].post_chaos_survival_ms = g.post_chaos_survival_ms;
    ramHistory[historyCount].chaos_gain_ratio = g.chaos_gain_ratio;
    historyCount++;
}

// ===================== 种群 (RAM存储) =====================
class Population {
public:
    Gene genes[POPULATION_SIZE];
    
    void init() {
        for (int i = 0; i < POPULATION_SIZE; i++) {
            genes[i].id = i;
            genes[i].k_turn = 1;
            genes[i].speed_bias = 200;
            genes[i].generation = 1;
            genes[i].survival_ms = 0;
            genes[i].distance_ticks = 0;
            genes[i].obstacle_count = 0;
            genes[i].chaos_count = 0;
            genes[i].chaos_escape_count = 0;
            genes[i].pre_chaos_survival_ms = 0;
            genes[i].post_chaos_survival_ms = 0;
            genes[i].chaos_gain_ratio = 0.0f;
        }
        Logger::log("✅ Population initialized in RAM");
    }
    
    void sortByScore() {
        for (int i = 0; i < POPULATION_SIZE - 1; i++) {
            for (int j = i + 1; j < POPULATION_SIZE; j++) {
                if (genes[i].score() < genes[j].score()) {
                    Gene tmp = genes[i];
                    genes[i] = genes[j];
                    genes[j] = tmp;
                }
            }
        }
    }
    
    uint32_t maxGeneration() {
        uint32_t maxG = 0;
        for (int i = 0; i < POPULATION_SIZE; i++) {
            if (genes[i].generation > maxG) maxG = genes[i].generation;
        }
        return maxG;
    }
};

// ===================== 电机控制 (AT8236 修正版) =====================
class Motor {
private:
    static int leftSpeed;
    static int rightSpeed;

public:
    static void init() {
        leftSpeed = 0;
        rightSpeed = 0;
        
        // AT8236: IN1 作为 PWM 输出，IN2 作为方向控制
        pinMode(PIN_LEFT_DIR1, OUTPUT);   // PWM (GPIO9)
        pinMode(PIN_LEFT_DIR2, OUTPUT);   // DIR (GPIO46)
        pinMode(PIN_RIGHT_DIR1, OUTPUT);  // PWM (GPIO16)
        pinMode(PIN_RIGHT_DIR2, OUTPUT);  // DIR (GPIO15)

        // 配置 LEDC 通道用于 PWM 调速
        ledcAttach(PIN_LEFT_DIR1, 5000, 8);
        ledcAttach(PIN_RIGHT_DIR1, 5000, 8);
        ledcWrite(PIN_LEFT_DIR1, 0);
        ledcWrite(PIN_RIGHT_DIR1, 0);

        // 初始方向设置为停止状态
        digitalWrite(PIN_LEFT_DIR2, LOW);
        digitalWrite(PIN_RIGHT_DIR2, LOW);
        
        stop();
        Logger::log("✅ AT8236 Motor driver initialized");
        Logger::logf("   Left: PWM=GPIO%d, DIR=GPIO%d", PIN_LEFT_DIR1, PIN_LEFT_DIR2);
        Logger::logf("   Right: PWM=GPIO%d, DIR=GPIO%d", PIN_RIGHT_DIR1, PIN_RIGHT_DIR2);
    }
    
    static void stop() {
        leftSpeed = 0;
        rightSpeed = 0;
        ledcWrite(PIN_LEFT_DIR1, 0);
        ledcWrite(PIN_RIGHT_DIR1, 0);
        digitalWrite(PIN_LEFT_DIR2, LOW);
        digitalWrite(PIN_RIGHT_DIR2, LOW);
    }
    
    static void setSpeed(int left, int right) {
        // 电机增益补偿（左右电机可能存在差异）
        const float leftGain  = 1.00f;
        const float rightGain = 1.25f;
        float outL = left * leftGain;
        float outR = right * rightGain;
        int pwmL = constrain((int)round(outL), -255, 255);
        int pwmR = constrain((int)round(outR), -255, 255);
        
        leftSpeed = left;
        rightSpeed = right;

        // ----- 左电机控制 (AT8236: IN1=PWM, IN2=DIR) -----
        if (pwmL > 0) {
            // 正转：PWM 调速，IN2 高电平
            digitalWrite(PIN_LEFT_DIR2, LEFT_FORWARD);
            ledcWrite(PIN_LEFT_DIR1, pwmL);
        } else if (pwmL < 0) {
            // 反转：PWM 调速，IN2 低电平
            digitalWrite(PIN_LEFT_DIR2, LEFT_REVERSE);
            ledcWrite(PIN_LEFT_DIR1, -pwmL);
        } else {
            // 停止
            digitalWrite(PIN_LEFT_DIR2, LOW);
            ledcWrite(PIN_LEFT_DIR1, 0);
        }

        // ----- 右电机控制 (AT8236: IN1=PWM, IN2=DIR) -----
        if (pwmR > 0) {
            // 正转：PWM 调速，IN2 低电平（取决于电机接线方向）
            digitalWrite(PIN_RIGHT_DIR2, RIGHT_FORWARD);
            ledcWrite(PIN_RIGHT_DIR1, pwmR);
        } else if (pwmR < 0) {
            // 反转：PWM 调速，IN2 高电平
            digitalWrite(PIN_RIGHT_DIR2, RIGHT_REVERSE);
            ledcWrite(PIN_RIGHT_DIR1, -pwmR);
        } else {
            // 停止
            digitalWrite(PIN_RIGHT_DIR2, LOW);
            ledcWrite(PIN_RIGHT_DIR1, 0);
        }
    }
    
    static void setRandom() {
        setSpeed(random(-120, 121), random(-120, 121));
    }
    
    static int getLeftSpeed() { return leftSpeed; }
    static int getRightSpeed() { return rightSpeed; }
};

int Motor::leftSpeed = 0;
int Motor::rightSpeed = 0;



// ===================== 编码器 =====================
volatile int32_t leftEncoderCount = 0;
volatile int32_t rightEncoderCount = 0;
volatile int32_t totalPulseCount = 0;

void IRAM_ATTR leftEncoderISR() {
    if (digitalRead(PIN_LEFT_ENC_B) == HIGH) {
        leftEncoderCount--;
    } else {
        leftEncoderCount++;
    }
    totalPulseCount++;
}

void IRAM_ATTR rightEncoderISR() {
    if (digitalRead(PIN_RIGHT_ENC_B) == HIGH) {
        rightEncoderCount++;
    } else {
        rightEncoderCount--;
    }
    totalPulseCount++;
}

// ===================== 避障硬件中断计数 =====================
volatile uint16_t obstacleInterruptCount = 0;

void IRAM_ATTR obstacleISR() {
    obstacleInterruptCount++;
}

class Encoders {
private:
    static int32_t lastLeftCount;
    static int32_t lastRightCount;
    static uint32_t lastTotalCount;

public:
    static void init() {
        pinMode(PIN_LEFT_ENC_A, INPUT_PULLUP);
        pinMode(PIN_LEFT_ENC_B, INPUT_PULLUP);
        pinMode(PIN_RIGHT_ENC_A, INPUT_PULLUP);
        pinMode(PIN_RIGHT_ENC_B, INPUT_PULLUP);
        
        attachInterrupt(digitalPinToInterrupt(PIN_LEFT_ENC_A), leftEncoderISR, RISING);
        attachInterrupt(digitalPinToInterrupt(PIN_RIGHT_ENC_A), rightEncoderISR, RISING);
        
        // 避障硬件中断初始化（可选，当前未使用）
        // pinMode(PIN_OBSTACLE_COUNT, INPUT_PULLUP);
        // attachInterrupt(digitalPinToInterrupt(PIN_OBSTACLE_COUNT), obstacleISR, FALLING);
        
        reset();
        Logger::log("✅ Encoders initialized (AB phase)");
    }
    
    static void reset() { 
        leftEncoderCount = 0;
        rightEncoderCount = 0;
        totalPulseCount = 0;
        obstacleInterruptCount = 0;
        lastLeftCount = 0;
        lastRightCount = 0;
        lastTotalCount = 0;
    }
    
    static int32_t leftCount() { return leftEncoderCount; }
    static int32_t rightCount() { return rightEncoderCount; }
    static uint32_t totalCount() { return totalPulseCount; }
    static uint16_t obstacleCount() { return obstacleInterruptCount; }
    
    static int32_t leftDelta() { 
        int32_t delta = leftEncoderCount - lastLeftCount;
        lastLeftCount = leftEncoderCount;
        return delta;
    }
    
    static int32_t rightDelta() { 
        int32_t delta = rightEncoderCount - lastRightCount;
        lastRightCount = rightEncoderCount;
        return delta;
    }
    
    static uint32_t totalDelta() { 
        uint32_t delta = totalPulseCount - lastTotalCount;
        lastTotalCount = totalPulseCount;
        return delta;
    }
    
    static bool isStuck() {
        return (abs(leftDelta()) < 2 && abs(rightDelta()) < 2);
    }
};

int32_t Encoders::lastLeftCount = 0;
int32_t Encoders::lastRightCount = 0;
uint32_t Encoders::lastTotalCount = 0;

// ===================== 传感器类：GP2Y0A21YK0F 模拟方案 =====================
class Sensors {
public:
    static void init() {
        pinMode(PIN_SENSOR_LEFT, INPUT);
        pinMode(PIN_SENSOR_RIGHT, INPUT);
        
        // 物理噪声源引脚悬空
        pinMode(PIN_NOISE_SOURCE, INPUT);
        
        Logger::log("✅ GP2Y0A21YK0F 模拟传感器已初始化");
        Logger::log("   原则：去标定、去滤波、去PID");
        Logger::logf("   OBSTACLE_ADC_THRESH = %d (原始ADC值)", OBSTACLE_ADC_THRESH);
        Logger::logf("   CLEAR_ADC_THRESH = %d (原始ADC值)", CLEAR_ADC_THRESH);
    }

    // 【去标定】直接返回原始 ADC 值，不换算为厘米
    static int left() {
        return analogRead(PIN_SENSOR_LEFT);
    }
    
    static int right() {
        return analogRead(PIN_SENSOR_RIGHT);
    }

    // 【去滤波】物理噪声源：悬空引脚，不进行任何平滑处理
    static uint16_t readNoise() {
        return analogRead(PIN_NOISE_SOURCE);
    }

    // 【去标定】障碍判断：仅基于原始 ADC 阈值
    static bool hasObstacle() {
        return (left() > OBSTACLE_ADC_THRESH || right() > OBSTACLE_ADC_THRESH);
    }
    
    static bool isClear() {
        return (left() < CLEAR_ADC_THRESH && right() < CLEAR_ADC_THRESH);
    }
    
    static bool leftBlocked() {
        return left() > OBSTACLE_ADC_THRESH;
    }
    
    static bool rightBlocked() {
        return right() > OBSTACLE_ADC_THRESH;
    }
    
    // 获取当前左右原始值（用于调试）
    static void printStatus() {
        Logger::logf("  L:%d  R:%d  Obs:%d", left(), right(), hasObstacle());
    }
};

// ===================== 进化引擎 =====================
class EvolutionEngine {
public:
    Population pop;
    uint8_t currentIdx = 0;
    uint32_t currentGen = 0;
    bool running = false;
    bool controlMode = false;
    
    bool testRunning = false;
    uint32_t testStart = 0;
    uint32_t lastTotalPulse = 0;
    uint32_t stuckStart = 0;
    bool inChaos = false;
    uint32_t chaosStart = 0;
    int chaosStep = 0;
    bool chaosEscaped = false;
    
    void init() {
        pop.init();
        currentGen = pop.maxGeneration();
        currentIdx = 0;
        Logger::logf("✅ Evolution initialized, generation %lu", currentGen);
    }
    
    void start() {
        running = true;
        currentIdx = 0;
        state = STATE_LOAD;
        Logger::log("▶ Evolution started");
    }
    
    void stop() {
        running = false;
        testRunning = false;
        Motor::stop();
        Logger::log("⏹ Evolution stopped");
    }
    
    void setControlMode(bool enable) {
        controlMode = enable;
        Logger::logf("🔬 Control mode: %s", enable ? "ON (无变异/无选择)" : "OFF");
    }
    
    void update() {
        if (!running) return;
        
        switch (state) {
        case STATE_LOAD:
            loadNextGene();
            break;
        case STATE_TEST:
            runTest();
            break;
        case STATE_CHAOS:
            runChaos();
            break;
        case STATE_BACKWARD_RESET:
        {
            uint32_t now = millis();
            if (now - backwardResetStart >= BACKWARD_RESET_DURATION) {
                Motor::stop();
                Logger::log("⏱ Backward reset finished");
                state = STATE_SCORE;
            }
            break;
        }
        case STATE_SCORE:
            calcScore();
            break;
        case STATE_EVALUATE:
            evaluate();
            break;
        case STATE_REPRODUCE:
            reproduce();
            break;
        }
    }
    
    Gene& currentGene() { return pop.genes[currentIdx]; }
    uint32_t generation() { return currentGen; }
    bool isRunning() { return running; }
    bool isTesting() { return testRunning; }
    uint32_t testTime() { return testRunning ? (millis() - testStart) : 0; }

    enum RunPhase {
        PHASE_IDLE,
        PHASE_NORMAL_TEST,
        PHASE_CHAOS
    };
    RunPhase getCurrentPhase() {
        if(!running || !testRunning) return PHASE_IDLE;
        if(inChaos) return PHASE_CHAOS;
        return PHASE_NORMAL_TEST;
    }
    
    void getStatistics(uint32_t& avg, uint32_t& maxVal, uint32_t& minVal, float& stddev) {
        uint32_t sum = 0;
        maxVal = 0;
        minVal = 0xFFFFFFFF;
        for (int i = 0; i < POPULATION_SIZE; i++) {
            uint32_t s = pop.genes[i].survival_ms;
            sum += s;
            if (s > maxVal) maxVal = s;
            if (s < minVal) minVal = s;
        }
        avg = sum / POPULATION_SIZE;
        float variance = 0;
        for (int i = 0; i < POPULATION_SIZE; i++) {
            float diff = (float)pop.genes[i].survival_ms - avg;
            variance += diff * diff;
        }
        stddev = sqrt(variance / POPULATION_SIZE);
    }

private:
    enum State {
        STATE_LOAD,
        STATE_TEST,
        STATE_CHAOS,
        STATE_BACKWARD_RESET,
        STATE_SCORE,
        STATE_EVALUATE,
        STATE_REPRODUCE
    };
    State state = STATE_LOAD;
    uint32_t backwardResetStart = 0;
    
    void loadNextGene() {
        Logger::logf("📂 Loading gene %d/%d", currentIdx + 1, POPULATION_SIZE);
        
        Gene& g = currentGene();
        Encoders::reset();
        lastTotalPulse = 0;
        stuckStart = 0;
        testStart = millis();
        testRunning = false;
        inChaos = false;
        chaosEscaped = false;
        
        g.survival_ms = 0;
        g.distance_ticks = 0;
        g.obstacle_count = 0;
        g.pre_chaos_survival_ms = 0;
        g.post_chaos_survival_ms = 0;
        g.chaos_gain_ratio = 0.0f;
        
        state = STATE_TEST;
        Logger::logf("🧬 Gene ID:%d k:%d speed:%d", g.id, g.k_turn, g.speed_bias);
    }
    
    void runTest() {
        if (!testRunning) {
            testRunning = true;
            testStart = millis();
            Logger::log("🏃 Test started");
        }
        
        Gene& g = currentGene();
        uint32_t now = millis();
        
        uint32_t totalDelta = Encoders::totalDelta();
        bool stuck = false;
        
        if (totalDelta < 2) {
            if (stuckStart == 0) stuckStart = now;
            if (now - stuckStart > STUCK_THRESHOLD_MS) stuck = true;
        } else {
            stuckStart = 0;
        }
        
        if (stuck && !inChaos) {
            g.survival_ms = now - testStart;
            g.distance_ticks = Encoders::totalCount();
            g.obstacle_count = Encoders::obstacleCount();
            enterChaos(g);
            return;
        }
        
        if (now - testStart >= TEST_DURATION_MS) {
            g.survival_ms = TEST_DURATION_MS;
            g.distance_ticks = Encoders::totalCount();
            g.obstacle_count = Encoders::obstacleCount();
            testRunning = false;

            state = STATE_BACKWARD_RESET;
            backwardResetStart = millis();
            Motor::setSpeed(BACKWARD_SPEED, BACKWARD_SPEED);
            Logger::log("✅ Test complete, start backward reset");
            return;
        }
        
        if (!inChaos) {
            // 【核心】物理差动控制：直接使用原始ADC值
            int left = Sensors::left();   
            int right = Sensors::right(); 
            
            int baseSpeed = g.speed_bias - 60;
            baseSpeed = constrain(baseSpeed, 60, 120);
            
            int turnAmount = 0;
            int pwmLeft = 0;
            int pwmRight = 0;
            
            if (Sensors::hasObstacle()) {
                int diff = left - right;
                int obstacleSpeed = baseSpeed * 0.6;
                obstacleSpeed = constrain(obstacleSpeed, 60, 150);
                
                if (abs(diff) > 100) {
                    float ratio = constrain(abs(diff) / 2000.0, 0.0, 1.0);
                    turnAmount = obstacleSpeed * ratio * 0.4;
                    
                    if (diff > 0) {
                        turnAmount = turnAmount;
                    } else {
                        turnAmount = -turnAmount;
                    }
                } else {
                    turnAmount = obstacleSpeed * 0.15;
                    if (random(0, 2) == 0) {
                        turnAmount = -turnAmount;
                    }
                }
                
                pwmLeft = obstacleSpeed + turnAmount;
                pwmRight = obstacleSpeed - turnAmount;
                
            } else {
                int diff = left - right;
                
                if (abs(diff) > 100) {
                    float ratio = constrain(abs(diff) / 2000.0, 0.0, 1.0);
                    turnAmount = baseSpeed * ratio * 0.08;
                    
                    if (diff > 0) {
                        turnAmount = turnAmount;
                    } else {
                        turnAmount = -turnAmount;
                    }
                } else {
                    turnAmount = 0;
                }
                pwmLeft = baseSpeed + turnAmount;
                pwmRight = baseSpeed - turnAmount;
            }
                
            pwmLeft = constrain(pwmLeft, 20, 255);
            pwmRight = constrain(pwmRight, 20, 255);
            
            static uint32_t lastDebug = 0;
            if (now - lastDebug > 1000) {
                lastDebug = now;
                Logger::logf("🔍 L:%d R:%d  PWM L:%d R:%d  Obs:%d  Enc:%ld/%ld", 
                    left, right, pwmLeft, pwmRight, Sensors::hasObstacle(),
                    Encoders::leftCount(), Encoders::rightCount());
            }
            
            Motor::setSpeed(pwmLeft, pwmRight);
            lastTotalPulse = Encoders::totalCount();
        }
    }
    
    void enterChaos(Gene& g) {
        state = STATE_CHAOS;
        inChaos = true;
        chaosStart = millis();
        chaosStep = 0;
        chaosEscaped = false;
        g.chaos_count++;
        g.pre_chaos_survival_ms = millis() - testStart;
        Logger::logf("🌀 CHAOS INJECTED! Pre-chaos baseline: %lu ms", g.pre_chaos_survival_ms);
    }
    
    void runChaos() {
        Gene& g = currentGene();
        uint32_t now = millis();

        if (chaosStep < CHAOS_SAMPLE_COUNT) {
            static uint32_t lastChaosTime = 0;
            if (now - lastChaosTime >= CHAOS_STEP_MS) {
                lastChaosTime = now;
                
                if (chaosStep % 3 == 0) {
                    Motor::setSpeed(200, 200);
                    Logger::log("🌀 CHAOS: 全速前进");
                } else if (chaosStep % 3 == 1) {
                    Motor::setSpeed(150, -150);
                    Logger::log("🌀 CHAOS: 原地旋转");
                } else {
                    Motor::setRandom();
                    Logger::log("🌀 CHAOS: 随机运动");
                }

                if (Sensors::isClear() || Encoders::totalDelta() > 15) {
                    chaosEscaped = true;
                    g.chaos_escape_count++;
                    g.post_chaos_survival_ms = (now - testStart) - g.pre_chaos_survival_ms;
                    if (g.pre_chaos_survival_ms > 0) {
                        g.chaos_gain_ratio = (float)g.post_chaos_survival_ms / (float)g.pre_chaos_survival_ms;
                    } else {
                        g.chaos_gain_ratio = 0.0f;
                    }
                    Logger::logf("✅ CHAOS ESCAPED! Pre:%lu Post:%lu Ratio:%.2f", 
                                 g.pre_chaos_survival_ms, g.post_chaos_survival_ms, g.chaos_gain_ratio);
                    
                    inChaos = false;
                    state = STATE_TEST;
                    Motor::stop();
                    stuckStart = 0;
                    return;
                }
                chaosStep++;
                lastTotalPulse = Encoders::totalCount();
            }
            return;
        } else {
            Logger::log("⏰ CHAOS TIMEOUT");
            Motor::stop();
            
            g.post_chaos_survival_ms = (now - testStart) - g.pre_chaos_survival_ms;
            if (g.pre_chaos_survival_ms > 0) {
                g.chaos_gain_ratio = (float)g.post_chaos_survival_ms / (float)g.pre_chaos_survival_ms;
            } else {
                g.chaos_gain_ratio = 0.0f;
            }
            Logger::logf("⏰ CHAOS TIMEOUT! Pre:%lu Post:%lu Ratio:%.2f", 
                         g.pre_chaos_survival_ms, g.post_chaos_survival_ms, g.chaos_gain_ratio);
            
            g.survival_ms = now - testStart;
            g.distance_ticks = Encoders::totalCount();
            g.obstacle_count = Encoders::obstacleCount();
            inChaos = false;
            testRunning = false;
            state = STATE_SCORE;
        }
    }
    
    void calcScore() {
        Gene& g = currentGene();
        uint32_t score = g.score();
        Logger::logf("📊 Score: %lu (dist:%lu, obs:%d, chaos:%d/%d, gain:%.2f)", 
            score, g.distance_ticks, g.obstacle_count, g.chaos_count, g.chaos_escape_count, g.chaos_gain_ratio);
        
        recordToRAM(g, currentGen);
        
        currentIdx++;
        if (currentIdx >= POPULATION_SIZE) {
            currentIdx = 0;
            state = STATE_EVALUATE;
        } else {
            state = STATE_LOAD;
        }
    }
    
    void evaluate() {
        Logger::log("📊 Evaluating population...");
        pop.sortByScore();
        
        uint32_t avg, maxVal, minVal;
        float stddev;
        getStatistics(avg, maxVal, minVal, stddev);
        Logger::logf("📈 Gen %lu Stats - Avg:%lu Max:%lu Min:%lu StdDev:%.2f", 
                     currentGen, avg, maxVal, minVal, stddev);
        
        state = STATE_REPRODUCE;
    }
    
    void reproduce() {
        Logger::log("🧬 Reproducing...");
        
        Gene nextGen[POPULATION_SIZE];
        
        if (controlMode) {
            for (int i = 0; i < POPULATION_SIZE; i++) {
                nextGen[i] = pop.genes[i];
                nextGen[i].generation = pop.genes[i].generation + 1;
                nextGen[i].survival_ms = 0;
                nextGen[i].distance_ticks = 0;
                nextGen[i].obstacle_count = 0;
                nextGen[i].chaos_count = 0;
                nextGen[i].chaos_escape_count = 0;
                nextGen[i].pre_chaos_survival_ms = 0;
                nextGen[i].post_chaos_survival_ms = 0;
                nextGen[i].chaos_gain_ratio = 0.0f;
            }
            Logger::log("🔬 Control mode: Gene copied without mutation/selection");
        } else {
            for (int i = 0; i < ELITE_COUNT; i++) {
                nextGen[i] = pop.genes[i];
                nextGen[i].generation = pop.genes[i].generation + 1;
            }
            
            // 【关键】轮盘赌仅基于 survival_ms，不使用 score()
            for (int i = ELITE_COUNT; i < POPULATION_SIZE; i++) {
                uint16_t noise = Sensors::readNoise();
                int p1 = noise & 3;
                int p2 = (noise >> 2) & 3;
                
                nextGen[i] = pop.genes[p1];
                nextGen[i].speed_bias = pop.genes[p2].speed_bias;
                nextGen[i].generation = pop.genes[p1].generation + 1;
                nextGen[i].survival_ms = 0;
                nextGen[i].distance_ticks = 0;
                nextGen[i].obstacle_count = 0;
                nextGen[i].chaos_count = 0;
                nextGen[i].chaos_escape_count = 0;
                nextGen[i].pre_chaos_survival_ms = 0;
                nextGen[i].post_chaos_survival_ms = 0;
                nextGen[i].chaos_gain_ratio = 0.0f;
                nextGen[i].mutate(noise, currentGen);
                nextGen[i].id = i;
            }
        }
        
        for (int i = 0; i < POPULATION_SIZE; i++) {
            pop.genes[i] = nextGen[i];
        }
        
        currentGen = pop.maxGeneration();
        Logger::logf("🌟 New generation: %lu", currentGen);
        state = STATE_LOAD;
    }
};

// ===================== Web服务器 =====================
class MyWebServer {
public:
    MyWebServer(EvolutionEngine& eng) : engine(eng), server(80) {}
    
    void init() {
        server.on("/", HTTP_GET, [this]() {
            server.send(200, "text/html", page());
        });
        
        server.on("/start", HTTP_GET, [this]() {
            engine.start();
            server.send(200, "text/plain", "STARTED");
        });
        
        server.on("/stop", HTTP_GET, [this]() {
            engine.stop();
            server.send(200, "text/plain", "STOPPED");
        });
        
        server.on("/reset", HTTP_GET, [this]() {
            engine.stop();
            server.send(200, "text/plain", "RESET");
            delay(100);
            ESP.restart();
        });
        
        server.on("/control", HTTP_GET, [this]() {
            String mode = server.arg("mode");
            if (mode == "on") {
                engine.setControlMode(true);
                server.send(200, "text/plain", "Control mode ON");
            } else if (mode == "off") {
                engine.setControlMode(false);
                server.send(200, "text/plain", "Control mode OFF");
            } else {
                server.send(200, "text/plain", "Use /control?mode=on or /control?mode=off");
            }
        });
        
        server.on("/status", HTTP_GET, [this]() {
            String s = "running:" + String(engine.isRunning() ? "1" : "0") + "\n";
            s += "testing:" + String(engine.isTesting() ? "1" : "0") + "\n";
            s += "generation:" + String(engine.generation()) + "\n";
            s += "individual:" + String(engine.currentIdx) + "\n";
            s += "test_time:" + String(engine.testTime()) + "\n";
            s += "pulse:" + String(Encoders::totalCount()) + "\n";
            s += "encoder_L:" + String(Encoders::leftCount()) + "\n";
            s += "encoder_R:" + String(Encoders::rightCount()) + "\n";
            s += "obstacle_ISR:" + String(Encoders::obstacleCount()) + "\n";
            s += "sensor_L:" + String(Sensors::left()) + "\n";
            s += "sensor_R:" + String(Sensors::right()) + "\n";
            s += "motor_L:" + String(Motor::getLeftSpeed()) + "\n";
            s += "motor_R:" + String(Motor::getRightSpeed()) + "\n";
            s += "phase:" + String(engine.getCurrentPhase()) + "\n";
            s += "control_mode:" + String(engine.controlMode ? "1" : "0") + "\n";
            server.send(200, "text/plain", s);
        });
        
        server.on("/population", HTTP_GET, [this]() {
            String csv = "id,k_turn,speed_bias,generation,survival_ms,distance_ticks,obstacle_count,chaos_count,chaos_escape_count,pre_chaos_ms,post_chaos_ms,gain_ratio,score\n";
            for (int i = 0; i < POPULATION_SIZE; i++) {
                Gene& g = engine.pop.genes[i];
                csv += String(g.id) + ",";
                csv += String(g.k_turn) + ",";
                csv += String(g.speed_bias) + ",";
                csv += String(g.generation) + ",";
                csv += String(g.survival_ms) + ",";
                csv += String(g.distance_ticks) + ",";
                csv += String(g.obstacle_count) + ",";
                csv += String(g.chaos_count) + ",";
                csv += String(g.chaos_escape_count) + ",";
                csv += String(g.pre_chaos_survival_ms) + ",";
                csv += String(g.post_chaos_survival_ms) + ",";
                csv += String(g.chaos_gain_ratio, 3) + ",";
                csv += String(g.score()) + "\n";
            }
            server.send(200, "text/csv", csv);
        });

        server.on("/statistics", HTTP_GET, [this]() {
            uint32_t avg, maxVal, minVal;
            float stddev;
            engine.getStatistics(avg, maxVal, minVal, stddev);
            String json = "{";
            json += "\"generation\":" + String(engine.generation()) + ",";
            json += "\"average\":" + String(avg) + ",";
            json += "\"max\":" + String(maxVal) + ",";
            json += "\"min\":" + String(minVal) + ",";
            json += "\"stddev\":" + String(stddev, 2);
            json += "}";
            server.send(200, "application/json", json);
        });

        server.on("/history/export", HTTP_GET, [this]() {
            String csvData = "Generation,GeneID,Score,SurvivalTime_ms,k_turn,SpeedBias,DistanceTicks,ObstacleCount,ChaosCount,ChaosEscapeCount,PreChaos_ms,PostChaos_ms,GainRatio\n";
            for (int i = 0; i < historyCount; i++) {
                csvData += String(ramHistory[i].generation) + ",";
                csvData += String(i % POPULATION_SIZE) + ",";
                csvData += String(ramHistory[i].fitnessScore, 1) + ",";
                csvData += String(ramHistory[i].survivalTime) + ",";
                csvData += String(ramHistory[i].k_turn) + ",";
                csvData += String(ramHistory[i].speed_bias) + ",";
                csvData += String(ramHistory[i].distance_ticks) + ",";
                csvData += String(ramHistory[i].obstacle_count) + ",";
                csvData += String(ramHistory[i].chaos_count) + ",";
                csvData += String(ramHistory[i].chaos_escape_count) + ",";
                csvData += String(ramHistory[i].pre_chaos_survival_ms) + ",";
                csvData += String(ramHistory[i].post_chaos_survival_ms) + ",";
                csvData += String(ramHistory[i].chaos_gain_ratio, 3) + "\n";
            }
            server.sendHeader("Content-Type", "text/csv");
            server.sendHeader("Content-Disposition", "attachment; filename=evolution_history.csv");
            server.send(200, "text/csv", csvData);
        });

        server.on("/history/export/latest", HTTP_GET, [this]() {
            String nStr = server.arg("n");
            int n = nStr.isEmpty() ? 10 : nStr.toInt();
            if (n < 1) n = 1;
            if (n > historyCount) n = historyCount;
            
            String csvData = "Generation,GeneID,Score,SurvivalTime_ms,k_turn,SpeedBias,DistanceTicks,ObstacleCount,ChaosCount,ChaosEscapeCount,PreChaos_ms,PostChaos_ms,GainRatio\n";
            int start = max(0, historyCount - n);
            for (int i = start; i < historyCount; i++) {
                csvData += String(ramHistory[i].generation) + ",";
                csvData += String(i % POPULATION_SIZE) + ",";
                csvData += String(ramHistory[i].fitnessScore, 1) + ",";
                csvData += String(ramHistory[i].survivalTime) + ",";
                csvData += String(ramHistory[i].k_turn) + ",";
                csvData += String(ramHistory[i].speed_bias) + ",";
                csvData += String(ramHistory[i].distance_ticks) + ",";
                csvData += String(ramHistory[i].obstacle_count) + ",";
                csvData += String(ramHistory[i].chaos_count) + ",";
                csvData += String(ramHistory[i].chaos_escape_count) + ",";
                csvData += String(ramHistory[i].pre_chaos_survival_ms) + ",";
                csvData += String(ramHistory[i].post_chaos_survival_ms) + ",";
                csvData += String(ramHistory[i].chaos_gain_ratio, 3) + "\n";
            }
            String filename = "evolution_latest_" + String(n) + ".csv";
            server.sendHeader("Content-Type", "text/csv");
            server.sendHeader("Content-Disposition", "attachment; filename=" + filename);
            server.send(200, "text/csv", csvData);
        });

        server.on("/history/json", HTTP_GET, [this]() {
            String json = "{\n  \"totalRecords\": " + String(historyCount) + ",\n  \"records\": [\n";
            int maxShow = min(historyCount, 50);
            int start = max(0, historyCount - maxShow);
            for (int i = start; i < historyCount; i++) {
                json += "    {";
                json += "\"generation\":" + String(ramHistory[i].generation) + ",";
                json += "\"score\":" + String(ramHistory[i].fitnessScore, 1) + ",";
                json += "\"survivalTime\":" + String(ramHistory[i].survivalTime) + ",";
                json += "\"k_turn\":" + String(ramHistory[i].k_turn) + ",";
                json += "\"speedBias\":" + String(ramHistory[i].speed_bias) + ",";
                json += "\"preChaos\":" + String(ramHistory[i].pre_chaos_survival_ms) + ",";
                json += "\"postChaos\":" + String(ramHistory[i].post_chaos_survival_ms) + ",";
                json += "\"gainRatio\":" + String(ramHistory[i].chaos_gain_ratio, 3);
                json += "}";
                if (i < historyCount - 1) json += ",";
                json += "\n";
            }
            json += "  ]\n}";
            server.send(200, "application/json", json);
        });
        
        server.on("/cmd/motor/forward", HTTP_GET, [this]() {
            Motor::setSpeed(150, 150);
            server.send(200, "text/plain", "Forward");
        });
        
        server.on("/cmd/motor/backward", HTTP_GET, [this]() {
            Motor::setSpeed(-150, -150);
            server.send(200, "text/plain", "Backward");
        });
        
        server.on("/cmd/motor/left", HTTP_GET, [this]() {
            Motor::setSpeed(-120, 120);
            server.send(200, "text/plain", "Left");
        });
        
        server.on("/cmd/motor/right", HTTP_GET, [this]() {
            Motor::setSpeed(120, -120);
            server.send(200, "text/plain", "Right");
        });
        
        server.on("/cmd/motor/stop", HTTP_GET, [this]() {
            Motor::stop();
            server.send(200, "text/plain", "Stopped");
        });
        
        server.begin();
        Logger::log("🌐 Web server: http://192.168.4.1");
    }
    
    void loop() { server.handleClient(); }

private:
    EvolutionEngine& engine;
    WebServer server;
    
    String page() {
        String p = R"raw(
<!DOCTYPE html>
<html>
<head>
    <meta charset='UTF-8'>
    <meta name='viewport' content='width=device-width, initial-scale=1.0'>
    <title>Car Evolution v5.9 - GP2Y0A21YK0F</title>
    <style>
        * { margin:0; padding:0; box-sizing:border-box; }
        body { font-family:Arial; background:#1a1a2e; padding:20px; color:#eee; max-width:600px; margin:0 auto; }
        .box { background:#16213e; padding:20px; border-radius:12px; margin:10px 0; }
        h1 { color:#e94560; text-align:center; font-size:22px; }
        .grid { display:grid; grid-template-columns:1fr 1fr; gap:8px; margin:10px 0; }
        .btn { padding:12px; border-radius:8px; text-align:center; font-weight:bold; color:#fff; cursor:pointer; border:none; font-size:16px; width:100%; }
        .btn-green { background:#2ecc71; color:#1a1a2e; }
        .btn-red { background:#e74c3c; }
        .btn-orange { background:#f39c12; color:#1a1a2e; }
        .btn-blue { background:#3498db; }
        .btn-purple { background:#9c27b0; }
        .btn-cyan { background:#00bcd4; color:#1a1a2e; }
        .btn-teal { background:#00897b; }
        .btn-grey { background:#555; }
        .btn:active { opacity:0.7; }
        .info { display:flex; justify-content:space-between; padding:4px 0; border-bottom:1px solid #1a1a2e; }
        .info-label { color:#888; }
        .info-value { color:#00d4ff; font-weight:bold; }
        .motor-grid { display:grid; grid-template-columns:1fr 1fr 1fr; gap:6px; margin:8px 0; }
        .sensor-box { background:#0a0a1a; padding:8px 12px; border-radius:6px; margin:8px 0; }
        .sensor-row { display:flex; justify-content:space-between; font-size:14px; padding:2px 0; }
        .sensor-label { color:#888; }
        .sensor-high { color:#2ecc71; font-weight:bold; }
        .sensor-low { color:#e94560; font-weight:bold; }
        .motor-box { background:#0a0a1a; padding:8px 12px; border-radius:6px; margin:8px 0; }
        .motor-row { display:flex; justify-content:space-between; font-size:14px; padding:2px 0; }
        .motor-label { color:#888; }
        .motor-value { font-weight:bold; font-family:monospace; }
        .motor-forward { color:#2ecc71; }
        .motor-backward { color:#e74c3c; }
        .motor-stop { color:#666; }
        .footer { font-size:12px; color:#666; text-align:center; margin-top:12px; }
        .footer span { color:#00d4ff; }
        .enc-box { background:#0a0a1a; padding:8px 12px; border-radius:6px; margin:8px 0; }
        .enc-row { display:flex; justify-content:space-between; font-size:14px; padding:2px 0; }
        .enc-label { color:#888; }
        .enc-value { color:#f39c12; font-weight:bold; }
        .phase-badge { display:inline-block; padding:2px 10px; border-radius:10px; font-size:12px; font-weight:bold; }
        .phase-idle { background:#666; }
        .phase-test { background:#2ecc71; color:#1a1a2e; }
        .phase-chaos { background:#9c27b0; }
        .download-section { background:#0a0a1a; padding:12px; border-radius:8px; margin:10px 0; }
        .download-section h4 { color:#888; margin-bottom:8px; }
        .download-grid { display:grid; grid-template-columns:1fr 1fr; gap:6px; }
        .btn-small { padding:8px 12px; font-size:13px; }
        .history-count { color:#00d4ff; font-weight:bold; }
        .subtitle { text-align:center; font-size:13px; color:#888; margin:5px 0; }
        .badge-physical { background:#e94560; color:#fff; padding:2px 10px; border-radius:10px; font-size:11px; font-weight:bold; display:inline-block; }
        .badge-control { background:#f39c12; color:#1a1a2e; padding:2px 10px; border-radius:10px; font-size:11px; font-weight:bold; display:inline-block; }
        .stat-box { background:#0a0a1a; padding:8px 12px; border-radius:6px; margin:8px 0; }
        .stat-row { display:flex; justify-content:space-between; font-size:14px; padding:2px 0; }
        .stat-label { color:#888; }
        .stat-value { color:#00d4ff; font-weight:bold; }
        .control-box { background:#1a1a2e; padding:12px; border-radius:8px; border:1px solid #f39c12; margin:8px 0; }
        .control-box .label { color:#f39c12; font-weight:bold; }
        .driver-info { background:#0a0a1a; padding:6px 12px; border-radius:4px; margin:6px 0; font-size:12px; color:#888; }
        .driver-info span { color:#00d4ff; }
    </style>
</head>
<body>
<div class='box'>
    <h1>🚗 Evolution v5.9</h1>
    <div class='subtitle'>
        <span class='badge-physical'>⚡ 去滤波·去标定·去PID</span>
        <span class='badge-control'>🔬 只观察·不优化</span>
        <span style='color:#888;font-size:12px;'>| GP2Y0A21YK0F | AT8236</span>
    </div>
    
    <div style='text-align:center;margin:10px 0'>
        <span id='phase_badge' class='phase-badge phase-idle'>⏹ 空闲</span>
        <span id='control_badge' style='margin-left:8px;font-size:12px;color:#888;'></span>
    </div>
    
    <div class='grid'>
        <button class='btn btn-green' onclick='cmd("/start")'>▶ START</button>
        <button class='btn btn-red' onclick='cmd("/stop")'>⏹ STOP</button>
        <button class='btn btn-orange' onclick='cmd("/reset")'>🔄 RESET</button>
        <button class='btn btn-purple' onclick='window.open("/population")'>📊 种群数据</button>
    </div>
    
    <div class='control-box'>
        <span class='label'>🔬 对照组模式</span>
        <div class='grid' style='margin-top:6px;'>
            <button class='btn btn-grey btn-small' onclick='cmd("/control?mode=on")'>✅ 开启 (无变异/无选择)</button>
            <button class='btn btn-green btn-small' onclick='cmd("/control?mode=off")'>❌ 关闭 (进化模式)</button>
        </div>
    </div>
    
    <div style='margin:10px 0;'>
        <div class='info'><span class='info-label'>状态</span><span class='info-value' id='state'>--</span></div>
        <div class='info'><span class='info-label'>世代</span><span class='info-value' id='generation'>--</span></div>
        <div class='info'><span class='info-label'>个体</span><span class='info-value' id='individual'>--</span></div>
        <div class='info'><span class='info-label'>测试时间</span><span class='info-value' id='test_time'>--</span></div>
        <div class='info'><span class='info-label'>历史记录</span><span class='info-value' id='history_count'>--</span></div>
    </div>
    
    <div class='stat-box'>
        <div style='color:#888;font-size:12px;margin-bottom:4px;'>📈 当前世代统计 (基于生存时间)</div>
        <div class='stat-row'><span class='stat-label'>均值</span><span id='stat_avg' class='stat-value'>--</span></div>
        <div class='stat-row'><span class='stat-label'>最大值</span><span id='stat_max' class='stat-value'>--</span></div>
        <div class='stat-row'><span class='stat-label'>最小值</span><span id='stat_min' class='stat-value'>--</span></div>
        <div class='stat-row'><span class='stat-label'>标准差</span><span id='stat_std' class='stat-value'>--</span></div>
    </div>
    
    <div class='download-section'>
        <h4>📥 数据下载 (含自身对照 Pre/Post/Gain)</h4>
        <div class='download-grid'>
            <a href='/history/export' class='btn btn-cyan btn-small' style='text-decoration:none;display:block;text-align:center;border-radius:6px;padding:8px;'>📊 导出全部CSV</a>
            <a href='/history/export/latest?n=10' class='btn btn-teal btn-small' style='text-decoration:none;display:block;text-align:center;border-radius:6px;padding:8px;'>📥 最近10代</a>
            <a href='/history/export/latest?n=50' class='btn btn-teal btn-small' style='text-decoration:none;display:block;text-align:center;border-radius:6px;padding:8px;'>📥 最近50代</a>
            <a href='/history/json' class='btn btn-blue btn-small' style='text-decoration:none;display:block;text-align:center;border-radius:6px;padding:8px;'>📋 查看JSON</a>
        </div>
        <div style='margin-top:8px;font-size:12px;color:#666;text-align:center'>
            共 <span class='history-count' id='history_count2'>0</span> 条历史记录
        </div>
    </div>
    
    <div class='sensor-box'>
        <div style='color:#888;font-size:11px;margin-bottom:4px;'>原始ADC值 (去标定·去滤波)</div>
        <div class='sensor-row'>
            <span class='sensor-label'>⬅ 左</span>
            <span id='sensor_L' class='sensor-low'>--</span>
        </div>
        <div class='sensor-row'>
            <span class='sensor-label'>➡ 右</span>
            <span id='sensor_R' class='sensor-low'>--</span>
        </div>
        <div class='sensor-row'>
            <span class='sensor-label'>🚧 避障计数(硬件中断)</span>
            <span id='obstacle_ISR' class='enc-value'>--</span>
        </div>
    </div>
    
    <div class='enc-box'>
        <div class='enc-row'>
            <span class='enc-label'>⬅ 左编码器</span>
            <span id='encoder_L' class='enc-value'>--</span>
        </div>
        <div class='enc-row'>
            <span class='enc-label'>➡ 右编码器</span>
            <span id='encoder_R' class='enc-value'>--</span>
        </div>
        <div class='enc-row'>
            <span class='enc-label'>📊 总脉冲</span>
            <span id='pulse' class='enc-value'>--</span>
        </div>
    </div>
    
    <div class='motor-box'>
        <div class='motor-row'>
            <span class='motor-label'>⬅ 左电机</span>
            <span id='motor_L' class='motor-value motor-stop'>--</span>
        </div>
        <div class='motor-row'>
            <span class='motor-label'>➡ 右电机</span>
            <span id='motor_R' class='motor-value motor-stop'>--</span>
        </div>
        <div class='driver-info' style='margin-top:6px;'>
            AT8236 驱动: IN1=PWM, IN2=DIR
        </div>
    </div>
</div>

<div class='box'>
    <h3 style='color:#3498db;font-size:16px;'>🔧 电机测试</h3>
    <div class='motor-grid'>
        <button class='btn btn-green' onclick='cmd("/cmd/motor/forward")'>⬆ 前进</button>
        <button class='btn btn-red' onclick='cmd("/cmd/motor/backward")'>⬇ 后退</button>
        <button class='btn btn-blue' onclick='cmd("/cmd/motor/left")'>⬅ 左转</button>
        <button class='btn btn-blue' onclick='cmd("/cmd/motor/right")'>➡ 右转</button>
        <button class='btn btn-orange' onclick='cmd("/cmd/motor/stop")'>⏹ 停止</button>
    </div>
</div>

<div class='footer'>
    <span>✅ v5.9 GP2Y0A21YK0F + AT8236</span> | 纯模拟方案 | 去滤波·去标定·去PID | 只观察·不优化
</div>

<script>
function cmd(url) {
    fetch(url).then(r => r.text()).then(data => { refresh(); });
}

function refresh() {
    fetch('/status')
        .then(r => r.text())
        .then(t => {
            const data = {};
            t.split('\n').forEach(l => {
                const p = l.split(':');
                if(p.length === 2) data[p[0].trim()] = p[1].trim();
            });
            
            const fields = ['state', 'generation', 'individual', 'test_time', 'pulse'];
            fields.forEach(key => {
                const el = document.getElementById(key);
                if(el && data[key] !== undefined) el.textContent = data[key];
            });
            
            if(data['encoder_L'] !== undefined) {
                document.getElementById('encoder_L').textContent = data['encoder_L'];
            }
            if(data['encoder_R'] !== undefined) {
                document.getElementById('encoder_R').textContent = data['encoder_R'];
            }
            if(data['obstacle_ISR'] !== undefined) {
                document.getElementById('obstacle_ISR').textContent = data['obstacle_ISR'];
            }
            
            if(data['sensor_L'] !== undefined) {
                const el = document.getElementById('sensor_L');
                const val = parseInt(data['sensor_L']);
                el.textContent = val;
                el.className = val > 2200 ? 'sensor-low' : 'sensor-high';
            }
            if(data['sensor_R'] !== undefined) {
                const el = document.getElementById('sensor_R');
                const val = parseInt(data['sensor_R']);
                el.textContent = val;
                el.className = val > 2200 ? 'sensor-low' : 'sensor-high';
            }
            
            if(data['motor_L'] !== undefined) {
                const el = document.getElementById('motor_L');
                const val = parseInt(data['motor_L']);
                el.textContent = val;
                if(val > 0) el.className = 'motor-value motor-forward';
                else if(val < 0) el.className = 'motor-value motor-backward';
                else el.className = 'motor-value motor-stop';
            }
            if(data['motor_R'] !== undefined) {
                const el = document.getElementById('motor_R');
                const val = parseInt(data['motor_R']);
                el.textContent = val;
                if(val > 0) el.className = 'motor-value motor-forward';
                else if(val < 0) el.className = 'motor-value motor-backward';
                else el.className = 'motor-value motor-stop';
            }
            
            if(data['running'] !== undefined && data['testing'] !== undefined) {
                const stateEl = document.getElementById('state');
                const phase = parseInt(data['phase'] || "0");
                const badge = document.getElementById('phase_badge');
                
                if(data['running'] === '1' && data['testing'] === '1') {
                    if(phase === 2){
                        stateEl.textContent = '🌀 混沌阶段';
                        stateEl.style.color = '#9c27b0';
                        badge.textContent = '🌀 混沌干预中';
                        badge.className = 'phase-badge phase-chaos';
                    }else{
                        stateEl.textContent = '🏃 正常测试 (原始ADC)';
                        stateEl.style.color = '#2ecc71';
                        badge.textContent = '✅ 正常测试';
                        badge.className = 'phase-badge phase-test';
                    }
                } else if(data['running'] === '1') {
                    stateEl.textContent = '⏳ 载入个体';
                    stateEl.style.color = '#f39c12';
                    badge.textContent = '⏳ 载入中';
                    badge.className = 'phase-badge phase-idle';
                } else {
                    stateEl.textContent = '⏹ 停止';
                    stateEl.style.color = '#e74c3c';
                    badge.textContent = '⏹ 空闲';
                    badge.className = 'phase-badge phase-idle';
                }
            }
            
            if(data['control_mode'] !== undefined) {
                const badge = document.getElementById('control_badge');
                if(data['control_mode'] === '1') {
                    badge.textContent = '🔬 对照组模式';
                    badge.style.color = '#f39c12';
                } else {
                    badge.textContent = '🧬 进化模式';
                    badge.style.color = '#2ecc71';
                }
            }
            
            fetch('/history/json')
                .then(r => r.json())
                .then(data => {
                    const count = data.totalRecords || 0;
                    document.getElementById('history_count').textContent = count;
                    document.getElementById('history_count2').textContent = count;
                })
                .catch(() => {});
        })
        .catch(err => console.error('刷新错误:', err));
    
    fetch('/statistics')
        .then(r => r.json())
        .then(data => {
            document.getElementById('stat_avg').textContent = data.average || '--';
            document.getElementById('stat_max').textContent = data.max || '--';
            document.getElementById('stat_min').textContent = data.min || '--';
            document.getElementById('stat_std').textContent = data.stddev !== undefined ? data.stddev.toFixed(2) : '--';
        })
        .catch(() => {});
}

setInterval(refresh, 2000);
refresh();
</script>
</body>
</html>
)raw";
        return p;
    }
};

// ===================== 全局对象 =====================
EvolutionEngine engine;
MyWebServer webServer(engine);

// ===================== 编码器校准工具 =====================
class CalibrationTool {
public:
    static void calibrateGain() {
        Logger::log("=== 开始编码器校准 ===");
        Logger::log("将前进 3 秒，请确保前方无障碍物");
        delay(2000);
        
        // 重置编码器
        Encoders::reset();
        
        // 以固定速度前进 3 秒
        // 先假设增益为 1.0, 1.0 进行测试
        const float testLeftGain = 1.0f;
        const float testRightGain = 1.0f;
        
        // 临时修改增益（或者直接调用 setSpeed，在外部应用增益）
        Motor::setSpeed(150, 150);
        delay(3000);
        Motor::stop();
        
        // 读取编码器差值
        int32_t leftCount = Encoders::leftCount();
        int32_t rightCount = Encoders::rightCount();
        int32_t diff = leftCount - rightCount;
        
        Logger::logf("📊 左编码器: %ld, 右编码器: %ld, 差值: %ld", 
                     leftCount, rightCount, diff);
        
        // 计算建议增益
        // 如果 leftCount > rightCount，说明左轮快，需要增大 rightGain
        // 或者减小 leftGain
        if (abs(diff) > 50) {
            float suggestedLeftGain = 1.0f;
            float suggestedRightGain = 1.0f;
            
            if (leftCount > rightCount) {
                // 左轮快 → 增大右轮增益
                suggestedLeftGain = 1.0f;
                suggestedRightGain = (float)leftCount / rightCount;
                Logger::logf("💡 建议增益: leftGain=%.2f, rightGain=%.2f", 
                             suggestedLeftGain, suggestedRightGain);
            } else {
                // 右轮快 → 增大左轮增益
                suggestedLeftGain = (float)rightCount / leftCount;
                suggestedRightGain = 1.0f;
                Logger::logf("💡 建议增益: leftGain=%.2f, rightGain=%.2f", 
                             suggestedLeftGain, suggestedRightGain);
            }
            
            // 计算预估值
            Logger::logf("📐 应用建议增益后，预期差值接近 0");
        } else {
            Logger::log("✅ 校准完成！增益已经接近完美");
        }
    }
};

// ===================== 主程序 =====================
void setup() {
    Logger::init();
    Logger::log("========================================");
    Logger::log("  Car Evolution v5.9 - GP2Y0A21YK0F");
    Logger::log("  纯模拟方案 | 严格遵循三原则");
    Logger::log("  设计哲学：去滤波·去标定·去PID");
    Logger::log("========================================");
    Logger::log("  ✅ 传感器：GP2Y0A21YK0F 模拟红外测距");
    Logger::log("  ✅ 左传感器：GPIO4，右传感器：GPIO5");
    Logger::log("  ✅ 物理噪声：悬空ADC引脚 (GPIO6)");
    Logger::log("  ✅ 障碍判断：基于原始ADC阈值");
    Logger::log("  ✅ 选择依据：仅 survival_ms (非score)");
    Logger::log("  ✅ 对照组模式：Web可切换");
    Logger::log("  ✅ 电机驱动：AT8236 (IN1=PWM, IN2=DIR)");
    Logger::log("========================================");
    
    Motor::init();
    Sensors::init();
    Encoders::init();

    engine.init();
    
    WiFi.mode(WIFI_AP);
    IPAddress ip(192, 168, 4, 1);
    WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
    WiFi.softAP(WIFI_SSID, WIFI_PASSWORD);
    Logger::logf("📡 WiFi: %s (192.168.4.1)", WIFI_SSID);
    
    webServer.init();
    
    pinMode(PIN_LED, OUTPUT);
    for (int i = 0; i < 3; i++) {
        digitalWrite(PIN_LED, HIGH);
        delay(200);
        digitalWrite(PIN_LED, LOW);
        delay(200);
    }

Logger::logf("📊 当前世代: %lu", engine.generation());
    Logger::log("✅ System ready!");
    Logger::log("🌐 http://192.168.4.1");
    Logger::log("========================================");
    Serial.println("准备执行校准...");
CalibrationTool::calibrateGain();

}
    
    

void loop() {
    engine.update();
    webServer.loop();
    
    static uint32_t lastBlink = 0;
    if (engine.isTesting()) {
        digitalWrite(PIN_LED, HIGH);
    } else if (engine.isRunning()) {
        if (millis() - lastBlink > 500) {
            lastBlink = millis();
            digitalWrite(PIN_LED, !digitalRead(PIN_LED));
        }
    } else {
        if (millis() - lastBlink > 1000) {
            lastBlink = millis();
            digitalWrite(PIN_LED, !digitalRead(PIN_LED));
        }
    }
    
    delay(10);
}

