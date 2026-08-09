/*
 * ================================================================
 * 项目名称：仿草履虫应激机制 - 脱困能力进化系统
 * 版本：    v7.0 - 脱困优先版
 * 
 * ================================================================
 * 【v7.0 核心设计】
 * ================================================================
 * 1. 舍弃"生存时间"作为适应度指标
 * 2. 以"脱困能力"为唯一筛选标准
 * 3. 脱困率 + 脱困速度 → 综合评分
 * 4. 不能脱困的个体 → 淘汰
 * 5. 不需要"同等实验条件"的科学假设
 * 6. 从连续量进化转向离散量进化
 * ================================================================
 */

#include <WiFi.h>
#include <WebServer.h>
#include <vector>
#include <algorithm>
#include <cstdarg>
#include <cmath>

// ===================== 硬件引脚宏【丝印 GPIO 一一对应】 =====================
#define PIN_SENSOR_LEFT    4
#define PIN_SENSOR_RIGHT   5
#define PIN_NOISE_SOURCE   6
#define PIN_OBSTACLE_INT   39
#define PIN_LED            14
#define PIN_LEFT_PWM       9
#define PIN_LEFT_DIR2      46
#define PIN_RIGHT_PWM      16
#define PIN_RIGHT_DIR2     15
#define PIN_LEFT_ENC_A     18
#define PIN_LEFT_ENC_B     17
#define PIN_RIGHT_ENC_A    11
#define PIN_RIGHT_ENC_B    10

// ===================== 电机转向电平定义 =====================
#define LEFT_FORWARD   HIGH
#define LEFT_REVERSE   LOW
#define RIGHT_FORWARD  LOW
#define RIGHT_REVERSE  HIGH

// ===================== 系统全局参数 =====================
#define WIFI_SSID       "CarLogger"
#define WIFI_PASSWORD   "12345678"

#define POPULATION_SIZE 16
#define ELITE_COUNT     6
#define TEST_DURATION_MS    30000
#define STUCK_THRESHOLD_MS  2000

// ===================== 混沌触发参数 =====================
#define CHAOS_MIN_INTERVAL_MS   5000
#define COLLISION_THRESHOLD     8
#define SWING_THRESHOLD         6
#define EXPLORE_STALL_MS        3000
#define CHAOS_SAMPLE_COUNT      15
#define CHAOS_STEP_MS           100
#define MAX_HISTORY_GENERATIONS 1000
#define BACKWARD_RESET_DURATION 2000
#define BACKWARD_SPEED -110

// ===================== 困境类型枚举 =====================
enum DilemmaType {
    DILEMMA_NONE = 0,
    DILEMMA_STUCK,
    DILEMMA_COLLISION,
    DILEMMA_SWING,
    DILEMMA_EXPLORE_STALL
};

// ===================== GP2Y0A21YK0F ADC原始阈值 =====================
#define CLEAR_ADC_THRESH       600
#define OBSTACLE_ADC_THRESH    1400
#define DANGER_ADC_THRESH      2600

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

// ===================== 【v7.0】基因结构（新版） =====================
struct Gene {
    uint16_t id;
    int8_t   k_turn;
    uint8_t  speed_bias;
    uint32_t generation;
    
    // 【核心】脱困能力指标
    uint8_t  chaos_total;           // 总卡死次数
    uint8_t  chaos_escaped;         // 脱困成功次数
    float    escape_rate;           // 脱困率
    
    uint32_t avg_escape_time_ms;    // 平均脱困时间
    uint32_t last_escape_time_ms;   // 上次脱困时间
    uint32_t fastest_escape_ms;     // 最快脱困
    
    // 【修复】添加缺失字段
    uint32_t previous_chaos_start;  // 本次混沌开始时间
    
    // 脱困策略记录
    uint8_t  preferred_action;      // 偏好动作
    uint8_t  action_history[10];    // 最近10次动作
    
    // 辅助信息（仅供参考）
    uint32_t survival_ms;
    uint32_t distance_ticks;
    uint16_t obstacle_count;
    
    // 【v7.0核心】计算脱困适应度
    float getFitness() const {
        if (chaos_total == 0) return 0.0f;
        
        float rateScore = (float)chaos_escaped / chaos_total;
        
        float timeScore = 0.0f;
        if (avg_escape_time_ms > 0) {
            if (avg_escape_time_ms < 1000) timeScore = 1.0f;
            else if (avg_escape_time_ms < 2000) timeScore = 0.7f;
            else if (avg_escape_time_ms < 3000) timeScore = 0.4f;
            else timeScore = 0.1f;
        }
        
        return rateScore * 0.7f + timeScore * 0.3f;
    }
    
    // 【v7.0】获取适应度等级
    const char* getFitnessGrade() const {
        float f = getFitness();
        if (f >= 0.8f) return "优秀";
        if (f >= 0.6f) return "良好";
        if (f >= 0.4f) return "一般";
        if (f >= 0.2f) return "较差";
        return "失败";
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
    
    // 记录脱困动作
    void recordAction(uint8_t action) {
        for (int i = 9; i > 0; i--) {
            action_history[i] = action_history[i-1];
        }
        action_history[0] = action;
        
        // 统计偏好动作
        uint8_t counts[6] = {0,0,0,0,0,0};
        for (int i = 0; i < 10; i++) {
            if (action_history[i] < 6) counts[action_history[i]]++;
        }
        uint8_t maxCount = 0;
        for (int i = 0; i < 6; i++) {
            if (counts[i] > maxCount) {
                maxCount = counts[i];
                preferred_action = i;
            }
        }
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
    uint8_t chaos_total;
    uint8_t chaos_escaped;
    float escape_rate;
    uint32_t avg_escape_time_ms;
    uint32_t fastest_escape_ms;
    uint8_t preferred_action;
};

HistoryRecord ramHistory[MAX_HISTORY_GENERATIONS];
int historyCount = 0;

void recordToRAM(const Gene& g, uint32_t gen) {
    if (historyCount >= MAX_HISTORY_GENERATIONS) {
        Logger::log("⚠️ RAM 历史缓冲区已满！");
        return;
    }
    ramHistory[historyCount].generation = gen;
    ramHistory[historyCount].fitnessScore = g.getFitness();
    ramHistory[historyCount].survivalTime = g.survival_ms;
    ramHistory[historyCount].k_turn = g.k_turn;
    ramHistory[historyCount].speed_bias = g.speed_bias;
    ramHistory[historyCount].distance_ticks = g.distance_ticks;
    ramHistory[historyCount].obstacle_count = g.obstacle_count;
    ramHistory[historyCount].chaos_total = g.chaos_total;
    ramHistory[historyCount].chaos_escaped = g.chaos_escaped;
    ramHistory[historyCount].escape_rate = g.escape_rate;
    ramHistory[historyCount].avg_escape_time_ms = g.avg_escape_time_ms;
    ramHistory[historyCount].fastest_escape_ms = g.fastest_escape_ms;
    ramHistory[historyCount].preferred_action = g.preferred_action;
    historyCount++;
}

// ===================== 种群 =====================
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
            
            // 【v7.0】脱困指标初始化
            genes[i].chaos_total = 0;
            genes[i].chaos_escaped = 0;
            genes[i].escape_rate = 0.0f;
            genes[i].avg_escape_time_ms = 0;
            genes[i].last_escape_time_ms = 0;
            genes[i].fastest_escape_ms = 999999;
            genes[i].previous_chaos_start = 0;  // 【修复】初始化
            genes[i].preferred_action = 0;
            for (int j = 0; j < 10; j++) {
                genes[i].action_history[j] = 0;
            }
        }
        Logger::log("✅ Population initialized (v7.0 - Escape Priority)");
    }
    
    // 【v7.0】按适应度排序
    void sortByFitness() {
        for (int i = 0; i < POPULATION_SIZE - 1; i++) {
            for (int j = i + 1; j < POPULATION_SIZE; j++) {
                if (genes[i].getFitness() < genes[j].getFitness()) {
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

// ===================== 电机控制 =====================
class Motor {
private:
    static int leftSpeed;
    static int rightSpeed;

public:
    static void init() {
        leftSpeed = 0;
        rightSpeed = 0;
        
        pinMode(PIN_LEFT_PWM, OUTPUT);
        pinMode(PIN_LEFT_DIR2, OUTPUT);
        pinMode(PIN_RIGHT_PWM, OUTPUT);
        pinMode(PIN_RIGHT_DIR2, OUTPUT);

        ledcAttach(PIN_LEFT_PWM, 5000, 8);
        ledcAttach(PIN_RIGHT_PWM, 5000, 8);
        ledcWrite(PIN_LEFT_PWM, 0);
        ledcWrite(PIN_RIGHT_PWM, 0);

        stop();
    }
    
    static void stop() {
        leftSpeed = 0;
        rightSpeed = 0;
        digitalWrite(PIN_LEFT_PWM, LOW);
        digitalWrite(PIN_LEFT_DIR2, LOW);
        digitalWrite(PIN_RIGHT_PWM, LOW);
        digitalWrite(PIN_RIGHT_DIR2, LOW);
        ledcWrite(PIN_LEFT_PWM, 0); 
        ledcWrite(PIN_RIGHT_PWM, 0);
    }
    
    static void setSpeed(int left, int right) {
        const float leftGain  = 1.30f;
        const float rightGain = 1.50f;
        float outL = left * leftGain;
        float outR = right * rightGain;
        int pwmL = constrain((int)round(outL), -255, 255);
        int pwmR = constrain((int)round(outR), -255, 255);
        
        leftSpeed = left;
        rightSpeed = right;

        if (pwmL > 0) {
            digitalWrite(PIN_LEFT_DIR2, LEFT_FORWARD);
            ledcWrite(PIN_LEFT_PWM, pwmL);
        } else if (pwmL < 0) {
            digitalWrite(PIN_LEFT_DIR2, LEFT_REVERSE);
            ledcWrite(PIN_LEFT_PWM, -pwmL);
        } else {
            digitalWrite(PIN_LEFT_DIR2, LOW);
            ledcWrite(PIN_LEFT_PWM, 0);
        }

        if (pwmR > 0) {
            digitalWrite(PIN_RIGHT_DIR2, RIGHT_FORWARD);
            ledcWrite(PIN_RIGHT_PWM, pwmR);
        } else if (pwmR < 0) {
            digitalWrite(PIN_RIGHT_DIR2, RIGHT_REVERSE);
            ledcWrite(PIN_RIGHT_PWM, -pwmR);
        } else {
            digitalWrite(PIN_RIGHT_DIR2, LOW);
            ledcWrite(PIN_RIGHT_PWM, 0);
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
        leftEncoderCount++;
    } else {
        leftEncoderCount--;
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
        
        reset();
        Logger::log("✅ Encoders initialized");
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

// ===================== 传感器类 =====================
class Sensors {
public:
    static void init() {
        pinMode(PIN_SENSOR_LEFT, INPUT);
        pinMode(PIN_SENSOR_RIGHT, INPUT);
        pinMode(PIN_NOISE_SOURCE, INPUT);
        Logger::log("✅ GP2Y0A21YK0F 传感器已初始化");
    }

    static int left() { return analogRead(PIN_SENSOR_LEFT); }
    static int right() { return analogRead(PIN_SENSOR_RIGHT); }
    static uint16_t readNoise() { return analogRead(PIN_NOISE_SOURCE); }

    static int getObstacleLevel() {
        int l = left();
        int r = right();
        int maxVal = max(l, r);
        if (maxVal > DANGER_ADC_THRESH) return 3;
        if (maxVal > OBSTACLE_ADC_THRESH) return 2;
        if (maxVal > CLEAR_ADC_THRESH) return 1;
        return 0;
    }
    
    static bool hasObstacle() {
        return (left() > OBSTACLE_ADC_THRESH || right() > OBSTACLE_ADC_THRESH);
    }
    
    static bool isClear() {
        return (left() < CLEAR_ADC_THRESH && right() < CLEAR_ADC_THRESH);
    }
    
    static bool isDanger() {
        return (left() > DANGER_ADC_THRESH || right() > DANGER_ADC_THRESH);
    }
};

// ===================== 【v7.0】进化引擎 =====================
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
    uint32_t lastChaosTriggerTime = 0;
    DilemmaType currentDilemma = DILEMMA_NONE;
    uint8_t currentChaosStrategy = 0;
    
    // 【v7.0】困境检测计数器
    uint16_t collisionCounter = 0;
    uint16_t swingCounter = 0;
    uint32_t lastObstacleChange = 0;
    
    // 混沌策略统计
    struct StrategyStats {
        uint8_t strategy;
        uint32_t successCount;
        uint32_t totalAttempts;
        float successRate;
    };
    StrategyStats strategyStats[6];
    
    void init() {
        pop.init();
        currentGen = pop.maxGeneration();
        currentIdx = 0;
        
        for (int i = 0; i < 6; i++) {
            strategyStats[i].strategy = i;
            strategyStats[i].successCount = 0;
            strategyStats[i].totalAttempts = 0;
            strategyStats[i].successRate = 0.0f;
        }
        
        Logger::logf("✅ Evolution initialized (v7.0), generation %lu", currentGen);
    }
    
    void start() {
        running = true;
        currentIdx = 0;
        state = STATE_LOAD;
        Logger::log("▶ Evolution started (v7.0 - Escape Priority)");
    }
    
    void stop() {
        running = false;
        testRunning = false;
        Motor::stop();
        Logger::log("⏹ Evolution stopped");
    }
    
    void setControlMode(bool enable) {
        controlMode = enable;
        Logger::logf("🔬 Control mode: %s", enable ? "ON" : "OFF");
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
    
    const char* getDilemmaName() {
        switch(currentDilemma) {
            case DILEMMA_STUCK: return "卡死";
            case DILEMMA_COLLISION: return "持续碰撞";
            case DILEMMA_SWING: return "反复摇摆";
            case DILEMMA_EXPLORE_STALL: return "探索停滞";
            default: return "无";
        }
    }
    
    const char* getStrategyName(uint8_t strategy) {
        const char* names[] = {
            "全速直冲", "原地旋转", "随机运动",
            "强力转向", "后退冲刺", "脉冲震荡"
        };
        if (strategy < 6) return names[strategy];
        return "未知";
    }
    
    // 【v7.0】获取种群统计
    void getStatistics(float& avgFitness, float& maxFitness, float& minFitness, float& stddev) {
        float sum = 0;
        maxFitness = 0;
        minFitness = 999.0f;
        for (int i = 0; i < POPULATION_SIZE; i++) {
            float f = pop.genes[i].getFitness();
            sum += f;
            if (f > maxFitness) maxFitness = f;
            if (f < minFitness) minFitness = f;
        }
        avgFitness = sum / POPULATION_SIZE;
        float variance = 0;
        for (int i = 0; i < POPULATION_SIZE; i++) {
            float diff = pop.genes[i].getFitness() - avgFitness;
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
        
        collisionCounter = 0;
        swingCounter = 0;
        lastObstacleChange = 0;
        currentDilemma = DILEMMA_NONE;
        
        g.survival_ms = 0;
        g.distance_ticks = 0;
        g.obstacle_count = 0;
        g.previous_chaos_start = 0;  // 【修复】重置
        
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
        
        DilemmaType dilemma = detectDilemma();
        
        if (dilemma != DILEMMA_NONE && !inChaos && 
            (now - lastChaosTriggerTime > CHAOS_MIN_INTERVAL_MS || lastChaosTriggerTime == 0)) {
            
            g.survival_ms = now - testStart;
            g.distance_ticks = Encoders::totalCount();
            g.obstacle_count = Encoders::obstacleCount();
            enterChaos(g, dilemma);
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
            int left = Sensors::left();   
            int right = Sensors::right();
            int obstacleLevel = Sensors::getObstacleLevel();
            
            int baseSpeed;
            switch (obstacleLevel) {
                case 3: baseSpeed = 60; break;
                case 2: baseSpeed = 90; break;
                case 1: baseSpeed = 120; break;
                default: baseSpeed = 150; break;
            }
            
            int diff = left - right;
            int pwmLeft, pwmRight;
            
            if (obstacleLevel >= 2) {
                float ratio = constrain(abs(diff) / 2500.0, 0.0, 1.0);
                int turnAmount = baseSpeed * ratio * 0.6;
                
                if (diff > 50) {
                    pwmLeft = baseSpeed + turnAmount;
                    pwmRight = baseSpeed - turnAmount * 0.4;
                } else if (diff < -50) {
                    pwmLeft = baseSpeed - turnAmount * 0.4;
                    pwmRight = baseSpeed + turnAmount;
                } else {
                    if (random(0, 2) == 0) {
                        pwmLeft = baseSpeed * 0.7;
                        pwmRight = baseSpeed;
                    } else {
                        pwmLeft = baseSpeed;
                        pwmRight = baseSpeed * 0.7;
                    }
                }
            } else {
                float ratio = constrain(abs(diff) / 2000.0, 0.0, 1.0);
                int turnAmount = baseSpeed * ratio * 0.1;
                
                if (diff > 30) {
                    pwmLeft = baseSpeed + turnAmount;
                    pwmRight = baseSpeed - turnAmount;
                } else if (diff < -30) {
                    pwmLeft = baseSpeed - turnAmount;
                    pwmRight = baseSpeed + turnAmount;
                } else {
                    pwmLeft = baseSpeed;
                    pwmRight = baseSpeed;
                }
            }
            
            pwmLeft = constrain(pwmLeft, 30, 255);
            pwmRight = constrain(pwmRight, 30, 255);
            Motor::setSpeed(pwmLeft, pwmRight);
            
            static uint32_t lastDebug = 0;
            if (now - lastDebug > 2000) {
                lastDebug = now;
                Logger::logf("🔍 L:%d R:%d Level:%d PWM L:%d R:%d Enc:%ld", 
                    left, right, obstacleLevel, pwmLeft, pwmRight, Encoders::totalCount());
            }
            
            lastTotalPulse = Encoders::totalCount();
        }
    }
    
    DilemmaType detectDilemma() {
        uint32_t now = millis();
        int left = Sensors::left();
        int right = Sensors::right();
        bool hasObs = (left > OBSTACLE_ADC_THRESH || right > OBSTACLE_ADC_THRESH);
        bool isDanger = (left > DANGER_ADC_THRESH || right > DANGER_ADC_THRESH);
        uint32_t totalDelta = Encoders::totalDelta();
        
        if (totalDelta < 2) {
            if (stuckStart == 0) stuckStart = now;
            if (now - stuckStart > STUCK_THRESHOLD_MS) {
                return DILEMMA_STUCK;
            }
        } else {
            stuckStart = 0;
        }
        
        if (isDanger) {
            collisionCounter++;
            if (collisionCounter > COLLISION_THRESHOLD) {
                return DILEMMA_COLLISION;
            }
        } else {
            if (collisionCounter > 0) collisionCounter--;
        }
        
        static int lastLeftDir = 0, lastRightDir = 0;
        int leftSpeed = Motor::getLeftSpeed();
        int rightSpeed = Motor::getRightSpeed();
        int leftDir = (leftSpeed > 10) ? 1 : (leftSpeed < -10) ? -1 : 0;
        int rightDir = (rightSpeed > 10) ? 1 : (rightSpeed < -10) ? -1 : 0;
        
        if (leftDir != 0 && rightDir != 0) {
            if (leftDir != lastLeftDir || rightDir != lastRightDir) {
                swingCounter++;
                if (swingCounter > SWING_THRESHOLD * 2 && totalDelta < 10) {
                    return DILEMMA_SWING;
                }
            }
        } else {
            if (swingCounter > 0) swingCounter -= 2;
            if (swingCounter < 0) swingCounter = 0;
        }
        lastLeftDir = leftDir;
        lastRightDir = rightDir;
        
        if (hasObs && totalDelta > 20) {
            if (lastObstacleChange == 0) lastObstacleChange = now;
            static int lastLeftVal = 0, lastRightVal = 0;
            if (abs(left - lastLeftVal) > 50 || abs(right - lastRightVal) > 50) {
                lastObstacleChange = now;
                lastLeftVal = left;
                lastRightVal = right;
            }
            if (now - lastObstacleChange > EXPLORE_STALL_MS) {
                return DILEMMA_EXPLORE_STALL;
            }
        } else {
            lastObstacleChange = now;
        }
        
        return DILEMMA_NONE;
    }
    
    uint8_t selectChaosStrategy(DilemmaType dilemma, Gene& g) {
        uint16_t noise = Sensors::readNoise();
        
        if (g.preferred_action < 6 && strategyStats[g.preferred_action].successRate > 0.3f) {
            Logger::logf("📋 使用历史有效策略: %s", getStrategyName(g.preferred_action));
            return g.preferred_action;
        }
        
        uint8_t recommended = 0;
        switch(dilemma) {
            case DILEMMA_STUCK:
                recommended = (noise & 1) ? 3 : 4;
                break;
            case DILEMMA_COLLISION:
                recommended = (noise & 1) ? 4 : 0;
                break;
            case DILEMMA_SWING:
                recommended = (noise & 1) ? 5 : 2;
                break;
            case DILEMMA_EXPLORE_STALL:
                recommended = (noise & 1) ? 2 : 0;
                break;
            default:
                float totalRate = 0;
                for (int i = 0; i < 6; i++) {
                    totalRate += strategyStats[i].successRate + 0.1f;
                }
                float randVal = (noise % 1000) / 1000.0f;
                float cumulative = 0;
                for (int i = 0; i < 6; i++) {
                    cumulative += (strategyStats[i].successRate + 0.1f) / totalRate;
                    if (randVal <= cumulative) {
                        recommended = i;
                        break;
                    }
                }
                break;
        }
        
        if ((noise & 3) == 0) {
            recommended = (noise >> 2) & 7;
            if (recommended >= 6) recommended = 6 - recommended;
        }
        
        return recommended;
    }
    
    void executeChaosStrategy(uint8_t strategy, int intensity) {
        float scale = intensity / 100.0f;
        
        switch(strategy) {
            case 0:
                Motor::setSpeed(200 * scale, 200 * scale);
                break;
            case 1: {
                int rotSpeed = 180 * scale;
                int dir = (millis() / 500) % 2 ? 1 : -1;
                Motor::setSpeed(dir * rotSpeed, -dir * rotSpeed);
                break;
            }
            case 2: {
                int randL = random(-160 * scale, 161 * scale);
                int randR = random(-160 * scale, 161 * scale);
                Motor::setSpeed(randL, randR);
                break;
            }
            case 3: {
                int turnSpeed = 200 * scale;
                int dir = (millis() / 300) % 2 ? 1 : -1;
                Motor::setSpeed(dir * turnSpeed, -dir * turnSpeed * 0.8);
                break;
            }
            case 4: {
                int backSpeed = -180 * scale;
                Motor::setSpeed(backSpeed, backSpeed);
                break;
            }
            case 5: {
                int phase = (millis() / 200) % 4;
                int speed = 150 * scale;
                switch(phase) {
                    case 0: Motor::setSpeed(speed, speed); break;
                    case 1: Motor::setSpeed(-speed, speed); break;
                    case 2: Motor::setSpeed(-speed, -speed); break;
                    case 3: Motor::setSpeed(speed, -speed); break;
                }
                break;
            }
        }
        Logger::logf("🌀 CHAOS: %s (力度:%d%%)", getStrategyName(strategy), intensity);
    }
    
    void enterChaos(Gene& g, DilemmaType dilemma) {
        state = STATE_CHAOS;
        inChaos = true;
        chaosStart = millis();
        chaosStep = 0;
        chaosEscaped = false;
        currentDilemma = dilemma;
        g.chaos_total++;
        g.previous_chaos_start = chaosStart;  // 【修复】记录混沌开始时间
        g.last_escape_time_ms = 0;
        
        currentChaosStrategy = selectChaosStrategy(dilemma, g);
        strategyStats[currentChaosStrategy].totalAttempts++;
        
        Logger::logf("🌀 CHAOS #%d! Type:%s Strategy:%s", 
                     g.chaos_total, getDilemmaName(), getStrategyName(currentChaosStrategy));
    }
    
    void runChaos() {
        Gene& g = currentGene();
        uint32_t now = millis();

        if (chaosStep < CHAOS_SAMPLE_COUNT) {
            static uint32_t lastChaosTime = 0;
            if (now - lastChaosTime >= CHAOS_STEP_MS) {
                lastChaosTime = now;
                
                int intensity = 50 + (chaosStep * 50 / CHAOS_SAMPLE_COUNT);
                if (intensity > 100) intensity = 100;
                
                executeChaosStrategy(currentChaosStrategy, intensity);
                g.recordAction(currentChaosStrategy);
                
                int effectiveSteps = CHAOS_SAMPLE_COUNT;
                if (currentDilemma == DILEMMA_STUCK) effectiveSteps = CHAOS_SAMPLE_COUNT + 5;
                if (currentDilemma == DILEMMA_COLLISION) effectiveSteps = CHAOS_SAMPLE_COUNT + 3;
                
                chaosStep++;
                if (chaosStep >= effectiveSteps) {
                    Logger::log("⏰ CHAOS TIMEOUT");
                    Motor::stop();
                    
                    g.last_escape_time_ms = 0;
                    
                    // 更新策略统计（失败）
                    strategyStats[currentChaosStrategy].totalAttempts++;
                    updateStrategyStats();
                    
                    Logger::logf("⏰ CHAOS TIMEOUT! Total:%d Escaped:%d", 
                                 g.chaos_total, g.chaos_escaped);
                    
                    g.survival_ms = now - testStart;
                    g.distance_ticks = Encoders::totalCount();
                    g.obstacle_count = Encoders::obstacleCount();
                    inChaos = false;
                    testRunning = false;
                    state = STATE_SCORE;
                    return;
                }

                if (Sensors::isClear() || Encoders::totalDelta() > 30) {
                    chaosEscaped = true;
                    g.chaos_escaped++;
                    
                    // 【修复】使用 g.previous_chaos_start
                    uint32_t escapeTime = now - g.previous_chaos_start;
                    g.last_escape_time_ms = escapeTime;
                    
                    // 更新平均脱困时间
                    if (g.avg_escape_time_ms == 0) {
                        g.avg_escape_time_ms = escapeTime;
                    } else {
                        g.avg_escape_time_ms = (g.avg_escape_time_ms * (g.chaos_escaped - 1) + escapeTime) / g.chaos_escaped;
                    }
                    
                    // 更新最快脱困时间
                    if (escapeTime < g.fastest_escape_ms) {
                        g.fastest_escape_ms = escapeTime;
                    }
                    
                    // 更新脱困率
                    g.escape_rate = (float)g.chaos_escaped / g.chaos_total;
                    
                    strategyStats[currentChaosStrategy].successCount++;
                    g.preferred_action = currentChaosStrategy;
                    updateStrategyStats();
                    lastChaosTriggerTime = now;
                    
                    Logger::logf("✅ ESCAPED! Total:%d Escaped:%d Rate:%.2f Time:%lu ms", 
                                 g.chaos_total, g.chaos_escaped, g.escape_rate, escapeTime);
                    
                    inChaos = false;
                    state = STATE_TEST;
                    Motor::stop();
                    stuckStart = 0;
                    return;
                }
                lastTotalPulse = Encoders::totalCount();
            }
            return;
        }
    }
    
    void updateStrategyStats() {
        for (int i = 0; i < 6; i++) {
            if (strategyStats[i].totalAttempts > 0) {
                strategyStats[i].successRate = 
                    (float)strategyStats[i].successCount / (float)strategyStats[i].totalAttempts;
            }
        }
    }
    
    void calcScore() {
        Gene& g = currentGene();
        float fitness = g.getFitness();
        
        Logger::logf("📊 Fitness: %.3f (total:%d escaped:%d rate:%.2f avgTime:%lu ms)", 
            fitness, g.chaos_total, g.chaos_escaped, g.escape_rate, g.avg_escape_time_ms);
        
        recordToRAM(g, currentGen);
        
        currentIdx++;
        if (currentIdx >= POPULATION_SIZE) {
            currentIdx = 0;
            state = STATE_EVALUATE;
        } else {
            state = STATE_LOAD;
        }
    }
    
    // ==================== 【v7.0核心】新的评估逻辑 ====================
    void evaluate() {
        Logger::log("📊 Evaluating population by ESCAPE ABILITY...");
        
        // 计算每个个体的适应度
        for (int i = 0; i < POPULATION_SIZE; i++) {
            float fitness = pop.genes[i].getFitness();
            Logger::logf("   Gene %d: fitness=%.3f (%s)", 
                         pop.genes[i].id, fitness, pop.genes[i].getFitnessGrade());
        }
        
        // 按适应度排序
        pop.sortByFitness();
        
        // 【关键】统计脱困能力
        float avgFitness, maxFitness, minFitness, stddev;
        getStatistics(avgFitness, maxFitness, minFitness, stddev);
        
        Logger::logf("📈 Gen %lu Escape Stats - Avg:%.3f Max:%.3f Min:%.3f StdDev:%.3f", 
                     currentGen, avgFitness, maxFitness, minFitness, stddev);
        
        // 【关键】统计脱困能力达标的个体数量
        int survivalCount = 0;
        for (int i = 0; i < POPULATION_SIZE; i++) {
            if (pop.genes[i].getFitness() >= 0.5f) {
                survivalCount++;
            }
        }
        
        // 至少保留6个
        if (survivalCount < 6) survivalCount = 6;
        
        Logger::logf("📊 Survival count: %d (fitness >= 0.5)", survivalCount);
        
        // 【v7.0】打印策略统计
        Logger::log("📊 Chaos Strategy Stats:");
        for (int i = 0; i < 6; i++) {
            Logger::logf("   %s: %d/%d (%.1f%%)", 
                         getStrategyName(i),
                         strategyStats[i].successCount,
                         strategyStats[i].totalAttempts,
                         strategyStats[i].successRate * 100);
        }
        
        state = STATE_REPRODUCE;
    }
    
    // ==================== 【v7.0】繁殖逻辑（只保留高适应度个体） ====================
    void reproduce() {
        Logger::log("🧬 Reproducing (Escape Priority)...");
        
        Gene nextGen[POPULATION_SIZE];
        
        if (controlMode) {
            for (int i = 0; i < POPULATION_SIZE; i++) {
                nextGen[i] = pop.genes[i];
                nextGen[i].generation = pop.genes[i].generation + 1;
                resetGeneForNextGen(nextGen[i]);
            }
            Logger::log("🔬 Control mode: Gene copied without selection");
        } else {
            // 【v7.0】计算适应度阈值，只保留前survivalCount个
            int survivalCount = 0;
            for (int i = 0; i < POPULATION_SIZE; i++) {
                if (pop.genes[i].getFitness() >= 0.5f) {
                    survivalCount++;
                }
            }
            if (survivalCount < 6) survivalCount = 6;
            
            Logger::logf("🧬 Keeping top %d individuals for reproduction", survivalCount);
            
            // 精英保留：复制前survivalCount个个体
            for (int i = 0; i < survivalCount; i++) {
                nextGen[i] = pop.genes[i];
                nextGen[i].generation = pop.genes[i].generation + 1;
                resetGeneForNextGen(nextGen[i]);
            }
            
            // 从精英中杂交产生后代
            for (int i = survivalCount; i < POPULATION_SIZE; i++) {
                uint16_t noise = Sensors::readNoise();
                int p1 = noise & (survivalCount - 1);
                int p2 = (noise >> 2) & (survivalCount - 1);
                
                // 确保p1和p2有效
                if (p1 >= survivalCount) p1 = survivalCount - 1;
                if (p2 >= survivalCount) p2 = survivalCount - 1;
                
                nextGen[i] = pop.genes[p1];
                nextGen[i].speed_bias = pop.genes[p2].speed_bias;
                nextGen[i].generation = pop.genes[p1].generation + 1;
                nextGen[i].k_turn = (pop.genes[p1].k_turn + pop.genes[p2].k_turn) / 2;
                resetGeneForNextGen(nextGen[i]);
                nextGen[i].mutate(noise, currentGen);
                nextGen[i].id = i;
                
                // 继承父代的偏好动作
                if (pop.genes[p1].preferred_action < 6) {
                    nextGen[i].preferred_action = pop.genes[p1].preferred_action;
                }
            }
        }
        
        for (int i = 0; i < POPULATION_SIZE; i++) {
            pop.genes[i] = nextGen[i];
        }
        
        currentGen = pop.maxGeneration();
        Logger::logf("🌟 New generation: %lu", currentGen);
        state = STATE_LOAD;
    }
    
    // 【v7.0】重置基因的脱困相关字段
    void resetGeneForNextGen(Gene& g) {
        g.survival_ms = 0;
        g.distance_ticks = 0;
        g.obstacle_count = 0;
        g.chaos_total = 0;
        g.chaos_escaped = 0;
        g.escape_rate = 0.0f;
        g.avg_escape_time_ms = 0;
        g.last_escape_time_ms = 0;
        g.previous_chaos_start = 0;  // 【修复】重置
        // 保留fastest_escape_ms和preferred_action
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
            s += "dilemma:" + String(engine.getDilemmaName()) + "\n";
            s += "chaos_strategy:" + String(engine.getStrategyName(engine.currentChaosStrategy)) + "\n";
            server.send(200, "text/plain", s);
        });
        
        server.on("/population", HTTP_GET, [this]() {
            String csv = "id,k_turn,speed_bias,generation,chaos_total,chaos_escaped,escape_rate,avg_escape_ms,fastest_escape_ms,preferred_action,fitness\n";
            for (int i = 0; i < POPULATION_SIZE; i++) {
                Gene& g = engine.pop.genes[i];
                csv += String(g.id) + ",";
                csv += String(g.k_turn) + ",";
                csv += String(g.speed_bias) + ",";
                csv += String(g.generation) + ",";
                csv += String(g.chaos_total) + ",";
                csv += String(g.chaos_escaped) + ",";
                csv += String(g.escape_rate, 3) + ",";
                csv += String(g.avg_escape_time_ms) + ",";
                csv += String(g.fastest_escape_ms) + ",";
                csv += String(g.preferred_action) + ",";
                csv += String(g.getFitness(), 3) + "\n";
            }
            server.send(200, "text/csv", csv);
        });

        server.on("/statistics", HTTP_GET, [this]() {
            float avg, maxVal, minVal, stddev;
            engine.getStatistics(avg, maxVal, minVal, stddev);
            String json = "{";
            json += "\"generation\":" + String(engine.generation()) + ",";
            json += "\"average\":" + String(avg, 3) + ",";
            json += "\"max\":" + String(maxVal, 3) + ",";
            json += "\"min\":" + String(minVal, 3) + ",";
            json += "\"stddev\":" + String(stddev, 3);
            json += "}";
            server.send(200, "application/json", json);
        });

        server.on("/history/export", HTTP_GET, [this]() {
            String csvData = "Generation,GeneID,Fitness,k_turn,SpeedBias,ChaosTotal,ChaosEscaped,EscapeRate,AvgEscape_ms,FastestEscape_ms,PreferredAction\n";
            for (int i = 0; i < historyCount; i++) {
                csvData += String(ramHistory[i].generation) + ",";
                csvData += String(i % POPULATION_SIZE) + ",";
                csvData += String(ramHistory[i].fitnessScore, 3) + ",";
                csvData += String(ramHistory[i].k_turn) + ",";
                csvData += String(ramHistory[i].speed_bias) + ",";
                csvData += String(ramHistory[i].chaos_total) + ",";
                csvData += String(ramHistory[i].chaos_escaped) + ",";
                csvData += String(ramHistory[i].escape_rate, 3) + ",";
                csvData += String(ramHistory[i].avg_escape_time_ms) + ",";
                csvData += String(ramHistory[i].fastest_escape_ms) + ",";
                csvData += String(ramHistory[i].preferred_action) + "\n";
            }
            server.sendHeader("Content-Type", "text/csv");
            server.sendHeader("Content-Disposition", "attachment; filename=escape_history.csv");
            server.send(200, "text/csv", csvData);
        });

        server.on("/history/export/latest", HTTP_GET, [this]() {
            String nStr = server.arg("n");
            int n = nStr.isEmpty() ? 10 : nStr.toInt();
            if (n < 1) n = 1;
            if (n > historyCount) n = historyCount;
            
            String csvData = "Generation,GeneID,Fitness,k_turn,SpeedBias,ChaosTotal,ChaosEscaped,EscapeRate,AvgEscape_ms,FastestEscape_ms,PreferredAction\n";
            int start = max(0, historyCount - n);
            for (int i = start; i < historyCount; i++) {
                csvData += String(ramHistory[i].generation) + ",";
                csvData += String(i % POPULATION_SIZE) + ",";
                csvData += String(ramHistory[i].fitnessScore, 3) + ",";
                csvData += String(ramHistory[i].k_turn) + ",";
                csvData += String(ramHistory[i].speed_bias) + ",";
                csvData += String(ramHistory[i].chaos_total) + ",";
                csvData += String(ramHistory[i].chaos_escaped) + ",";
                csvData += String(ramHistory[i].escape_rate, 3) + ",";
                csvData += String(ramHistory[i].avg_escape_time_ms) + ",";
                csvData += String(ramHistory[i].fastest_escape_ms) + ",";
                csvData += String(ramHistory[i].preferred_action) + "\n";
            }
            String filename = "escape_latest_" + String(n) + ".csv";
            server.sendHeader("Content-Type", "text/csv");
            server.sendHeader("Content-Disposition", "attachment; filename=" + filename);
            server.send(200, "text/csv", csvData);
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
    <title>Car Evolution v7.0 - Escape Priority</title>
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
        .badge-escape { background:#e94560; color:#fff; padding:2px 10px; border-radius:10px; font-size:11px; font-weight:bold; display:inline-block; }
        .badge-control { background:#f39c12; color:#1a1a2e; padding:2px 10px; border-radius:10px; font-size:11px; font-weight:bold; display:inline-block; }
        .stat-box { background:#0a0a1a; padding:8px 12px; border-radius:6px; margin:8px 0; }
        .stat-row { display:flex; justify-content:space-between; font-size:14px; padding:2px 0; }
        .stat-label { color:#888; }
        .stat-value { color:#00d4ff; font-weight:bold; }
        .control-box { background:#1a1a2e; padding:12px; border-radius:8px; border:1px solid #f39c12; margin:8px 0; }
        .control-box .label { color:#f39c12; font-weight:bold; }
        .chaos-box { background:#0a0a1a; padding:8px 12px; border-radius:6px; margin:8px 0; border-left:3px solid #9c27b0; }
        .chaos-box .label { color:#9c27b0; font-weight:bold; }
        .chaos-box .val { color:#00d4ff; }
        .fitness-box { background:#0a0a1a; padding:8px 12px; border-radius:6px; margin:8px 0; border-left:3px solid #e94560; }
        .fitness-box .label { color:#e94560; font-weight:bold; }
        .fitness-box .val { color:#2ecc71; font-weight:bold; }
    </style>
</head>
<body>
<div class='box'>
    <h1>🚗 Evolution v7.0</h1>
    <div class='subtitle'>
        <span class='badge-escape'>🏆 脱困优先</span>
        <span class='badge-control'>📊 脱困率+速度</span>
        <span style='color:#888;font-size:12px;'>| 不能脱困 → 淘汰</span>
    </div>
    
    <div class='fitness-box'>
        <div class='label'>🎯 适应度评分 = 脱困率×0.7 + 速度×0.3</div>
        <div class='sensor-row'>
            <span class='sensor-label'>当前适应度</span>
            <span class='val' id='fitness_display'>--</span>
        </div>
        <div class='sensor-row'>
            <span class='sensor-label'>脱困率</span>
            <span class='val' id='escape_rate_display'>--</span>
        </div>
        <div class='sensor-row'>
            <span class='sensor-label'>平均脱困时间</span>
            <span class='val' id='avg_escape_display'>--</span>
        </div>
    </div>
    
    <div style='text-align:center;margin:10px 0'>
        <span id='phase_badge' class='phase-badge phase-idle'>⏹ 空闲</span>
        <span id='dilemma_badge' style='margin-left:8px;font-size:12px;color:#888;'></span>
        <span id='strategy_badge' style='margin-left:8px;font-size:12px;color:#9c27b0;'></span>
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
            <button class='btn btn-grey btn-small' onclick='cmd("/control?mode=on")'>✅ 开启</button>
            <button class='btn btn-green btn-small' onclick='cmd("/control?mode=off")'>❌ 关闭</button>
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
        <div style='color:#888;font-size:12px;margin-bottom:4px;'>📈 种群适应度统计</div>
        <div class='stat-row'><span class='stat-label'>均值</span><span id='stat_avg' class='stat-value'>--</span></div>
        <div class='stat-row'><span class='stat-label'>最大值</span><span id='stat_max' class='stat-value'>--</span></div>
        <div class='stat-row'><span class='stat-label'>最小值</span><span id='stat_min' class='stat-value'>--</span></div>
        <div class='stat-row'><span class='stat-label'>标准差</span><span id='stat_std' class='stat-value'>--</span></div>
    </div>
    
    <div class='download-section'>
        <h4>📥 数据下载 (脱困能力记录)</h4>
        <div class='download-grid'>
            <a href='/history/export' class='btn btn-cyan btn-small' style='text-decoration:none;display:block;text-align:center;border-radius:6px;padding:8px;'>📊 导出全部CSV</a>
            <a href='/history/export/latest?n=10' class='btn btn-teal btn-small' style='text-decoration:none;display:block;text-align:center;border-radius:6px;padding:8px;'>📥 最近10代</a>
            <a href='/history/export/latest?n=50' class='btn btn-teal btn-small' style='text-decoration:none;display:block;text-align:center;border-radius:6px;padding:8px;'>📥 最近50代</a>
        </div>
        <div style='margin-top:8px;font-size:12px;color:#666;text-align:center'>
            共 <span class='history-count' id='history_count2'>0</span> 条历史记录
        </div>
    </div>
    
    <div class='sensor-box'>
        <div style='color:#888;font-size:11px;margin-bottom:4px;'>原始ADC值</div>
        <div class='sensor-row'>
            <span class='sensor-label'>⬅ 左 (IO4)</span>
            <span id='sensor_L' class='sensor-low'>--</span>
        </div>
        <div class='sensor-row'>
            <span class='sensor-label'>➡ 右 (IO5)</span>
            <span id='sensor_R' class='sensor-low'>--</span>
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
    <span>✅ v7.0 脱困优先进化</span> | 脱困率+速度 | 不能脱困→淘汰
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
            
            if(data['sensor_L'] !== undefined) {
                document.getElementById('sensor_L').textContent = data['sensor_L'];
            }
            if(data['sensor_R'] !== undefined) {
                document.getElementById('sensor_R').textContent = data['sensor_R'];
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
            
            if(data['dilemma'] !== undefined) {
                document.getElementById('dilemma_display').textContent = data['dilemma'];
                document.getElementById('dilemma_badge').textContent = '⚠️ ' + data['dilemma'];
            }
            if(data['chaos_strategy'] !== undefined) {
                document.getElementById('strategy_display').textContent = data['chaos_strategy'];
                document.getElementById('strategy_badge').textContent = '🎯 ' + data['chaos_strategy'];
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
                        stateEl.textContent = '🏃 正常测试';
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
            document.getElementById('stat_std').textContent = data.stddev !== undefined ? data.stddev.toFixed(3) : '--';
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

// ===================== 主程序 =====================
void setup() {
    Logger::init();
    Logger::log("========================================");
    Logger::log("  Car Evolution v7.0 - Escape Priority");
    Logger::log("  脱困能力优先 | 不能脱困→淘汰");
    Logger::log("========================================");
    Logger::log("  ✅ 适应度 = 脱困率×0.7 + 速度×0.3");
    Logger::log("  ✅ 4种困境识别：卡死·碰撞·摇摆·停滞");
    Logger::log("  ✅ 6种混沌策略 + 策略学习");
    Logger::log("  ✅ 淘汰机制：fitness < 0.5 淘汰");
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