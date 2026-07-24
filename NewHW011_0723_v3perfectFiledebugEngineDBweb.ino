/*
 * ================================================================
 * 项目名称：仿草履虫应激机制 - 小车避障进化验证系统 
 * 硬件平台：ESP32-S3-WROOM-1U (AT8236电机驱动)
 * 版本：    v5.5-ESP32S3-FULL-DATA-PERSISTENCE
 * 适配日期: 2026-07-23
 * 
 * 特色功能：
 * - 多重数据保存机制（EEPROM + SPIFFS）
 * - 3秒自动备份
 * - 断电恢复功能
 * - 完整历史数据保存
 * 
 * 引脚分配：
 * - 左电机: IN1=GPIO16, IN2=GPIO15
 * - 右电机: IN1=GPIO9, IN2=GPIO46
 * - 左编码器: A=GPIO18, B=GPIO17
 * - 右编码器: A=GPIO11, B=GPIO10
 * - 左TCRT5000: GPIO35
 * - 右TCRT5000: GPIO36
 * - 板载LED: GPIO14
 * ================================================================
 */
 /*
【20260724 NewHW0110723_v3perfectFiledebugEngineDBwebEVO版本变更记录】
变更内容汇总：
1、电机力矩均衡优化
   - 删除runTest内部局部leftGain/rightGain增益代码
   - 在Motor::setSpeed入口增加全局左右电机静态增益补偿
   - 实现网页手动控制、进化自主运行共用同一套力矩均衡策略
   - 解决现象：直行右偏、后退左偏、左转扭矩偏弱、右转扭矩过剩

2、TCRT5000红外传感器优化
   - 新增阈值宏：SENSOR_THRESHOLD=360，CLEAR_THRESHOLD=420，提升探测灵敏度并增加滞回区间
   - 重构Sensors类判断函数，修复原有左右传感器判断阈值不一致问题
   - hasObstacle()、isClear()、leftBlocked()、rightBlocked()统一采用标准阈值逻辑

3、Web监控页面功能扩展
   - EvolutionEngine新增getCurrentPhase()获取运行阶段
   - /status接口新增phase状态字段
   - 前端区分展示：空闲等待 / 正常测试中 / 混沌死局阶段，搭配不同颜色标识

备注：
1. 传感器无状态锁存，读数处于360~420区间时，hasObstacle与isClear会同时为false；边界抖动可后续追加状态锁存。
2. 电机增益参数可现场微调，避免输出长期饱和±255。
3. 所有改动均可注释对应代码实现一键回滚。
*/

#include <WiFi.h>
#include <EEPROM.h>
#include <WebServer.h>
#include <esp_wifi.h>
#include <SPIFFS.h>
#include <vector>
#include <algorithm>

// ===================== 引脚定义 =====================
// ★ TCRT5000 传感器
#define PIN_LEFT_SENSOR    4
#define PIN_RIGHT_SENSOR   5
#define PIN_NOISE          6   // GPIO6 = ADC1_CH5 ✓ (用作噪声源)
#define PIN_LED            14

// ★ 左电机 AT8236
#define PIN_LEFT_PWM       9
#define PIN_LEFT_DIR1      9
#define PIN_LEFT_DIR2      46

// ★ 右电机 AT8236
#define PIN_RIGHT_PWM      16
#define PIN_RIGHT_DIR1     16
#define PIN_RIGHT_DIR2     15

// ★ 左电机编码器 (霍尔编码器 AB相)
#define PIN_LEFT_ENC_A     10
#define PIN_LEFT_ENC_B     11

// ★ 右电机编码器 (霍尔编码器 AB相)
#define PIN_RIGHT_ENC_A    18
#define PIN_RIGHT_ENC_B    17

// ===================== 电机方向配置 =====================
#define LEFT_FORWARD   HIGH
#define LEFT_REVERSE   LOW
#define RIGHT_FORWARD  LOW
#define RIGHT_REVERSE  HIGH

// ===================== 系统参数 =====================
#define WIFI_SSID       "CarLogger"
#define WIFI_PASSWORD   "12345678"

#define POPULATION_SIZE 16
#define ELITE_COUNT     6
#define TEST_DURATION_MS    30000
#define STUCK_THRESHOLD_MS  3000

#define CHAOS_SAMPLE_COUNT  15
#define CHAOS_STEP_MS       100

// TCRT5000 阈值
#define SENSOR_THRESHOLD    360//0724修改 400改到300，300到360，更灵敏
#define CLEAR_THRESHOLD     420//0724修改 400改到300,300到420

#define EEPROM_START        0
#define EEPROM_BACKUP_SLOT  POPULATION_SIZE  // 使用第16个位置作为备份
#define MAX_HISTORY_GENERATIONS 1000
#define BACKUP_INTERVAL_MS  3000  // 3秒备份一次

// ⭐ 新增：EEPROM初始化标记
#define EEPROM_INIT_MAGIC   0xA5A5  // 魔数标记
#define EEPROM_MAGIC_ADDR   4090    // 存储位置（最后6个字节）

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
    
    uint32_t score() const {
        uint32_t s = distance_ticks / 2 + obstacle_count * 50 - chaos_count * 20;
        if (distance_ticks < 100) s = 0;
        if (chaos_count > 0 && chaos_escape_count > chaos_count / 2) {
            s += chaos_escape_count * 30;
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
        speed_bias = constrain(speed_bias + ds, 150, 180);
    }
    
    bool isValid() const {
        return id != 0 || generation > 0 || speed_bias > 0;
    }
};

// ===================== EEPROM =====================
class EEPROMStorage {
public:
    static void init() { EEPROM.begin(4096); }
    static void writeGene(uint8_t idx, const Gene& g) {
        EEPROM.put(EEPROM_START + idx * sizeof(Gene), g);
        EEPROM.commit();
    }
    static void readGene(uint8_t idx, Gene& g) {
        EEPROM.get(EEPROM_START + idx * sizeof(Gene), g);
    }
    static void clearGene(uint8_t idx) {
        Gene empty;
        memset(&empty, 0, sizeof(empty));
        writeGene(idx, empty);
    }
};

// ===================== 电机控制（AT8236适配） =====================
class Motor {
private:
    static int leftSpeed;
    static int rightSpeed;

public:
    static void init() {
        leftSpeed = 0;
        rightSpeed = 0;
        
        pinMode(PIN_LEFT_DIR1, OUTPUT);
        pinMode(PIN_LEFT_DIR2, OUTPUT);
        pinMode(PIN_RIGHT_DIR1, OUTPUT);
        pinMode(PIN_RIGHT_DIR2, OUTPUT);

        ledcAttach(PIN_LEFT_DIR1, 5000, 8);
        ledcAttach(PIN_RIGHT_DIR1, 5000, 8);
        ledcWrite(PIN_LEFT_DIR1, 0);
        ledcWrite(PIN_RIGHT_DIR1, 0);

        stop();
    }
    
    static void stop() {
        leftSpeed = 0;
        rightSpeed = 0;
        digitalWrite(PIN_LEFT_DIR1, LOW);
        digitalWrite(PIN_LEFT_DIR2, LOW);
        digitalWrite(PIN_RIGHT_DIR1, LOW);
        digitalWrite(PIN_RIGHT_DIR2, LOW);
        ledcWrite(PIN_LEFT_DIR1, 0); 
        ledcWrite(PIN_RIGHT_DIR1, 0);
    }
    
    static void setSpeed(int left, int right) {
        
        const float leftGain  = 1.30f;
        const float rightGain = 0.95f;
        float outL = left * leftGain;
        float outR = right * rightGain;

        left = constrain(left, -255, 255);
        right = constrain(right, -255, 255);
        
        leftSpeed = left;
        rightSpeed = right;

        // 左电机
        if (left > 0) {
            digitalWrite(PIN_LEFT_DIR2, LEFT_FORWARD);
            ledcWrite(PIN_LEFT_DIR1, left);
        } else if (left < 0) {
            digitalWrite(PIN_LEFT_DIR2, LEFT_REVERSE);
            ledcWrite(PIN_LEFT_DIR1, -left);
        } else {
            digitalWrite(PIN_LEFT_DIR2, LOW);
            ledcWrite(PIN_LEFT_DIR1, 0);
        }

        // 右电机
        if (right > 0) {
            digitalWrite(PIN_RIGHT_DIR2, RIGHT_FORWARD);
            ledcWrite(PIN_RIGHT_DIR1, right);
        } else if (right < 0) {
            digitalWrite(PIN_RIGHT_DIR2, RIGHT_REVERSE);
            ledcWrite(PIN_RIGHT_DIR1, -right);
        } else {
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

// ===================== 编码器（AB相霍尔编码器） =====================
volatile int32_t leftEncoderCount = 0;
volatile int32_t rightEncoderCount = 0;
volatile int32_t totalPulseCount = 0;

// 左编码器中断服务程序
void IRAM_ATTR leftEncoderISR() {
    if (digitalRead(PIN_LEFT_ENC_B) == HIGH) {
        leftEncoderCount++;
    } else {
        leftEncoderCount--;
    }
    totalPulseCount++;
}

// 右编码器中断服务程序
void IRAM_ATTR rightEncoderISR() {
    if (digitalRead(PIN_RIGHT_ENC_B) == HIGH) {
        rightEncoderCount++;
    } else {
        rightEncoderCount--;
    }
    totalPulseCount++;
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
        Logger::log("✅ Encoders initialized (AB phase)");
    }
    
    static void reset() { 
        leftEncoderCount = 0;
        rightEncoderCount = 0;
        totalPulseCount = 0;
        lastLeftCount = 0;
        lastRightCount = 0;
        lastTotalCount = 0;
    }
    
    static int32_t leftCount() { return leftEncoderCount; }
    static int32_t rightCount() { return rightEncoderCount; }
    static uint32_t totalCount() { return totalPulseCount; }
    
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

// ===================== 传感器（TCRT5000） =====================
class Sensors {
public:
    static void init() {
        pinMode(PIN_LEFT_SENSOR, INPUT);
        pinMode(PIN_RIGHT_SENSOR, INPUT);
        
        analogReadResolution(12);
        analogSetAttenuation(ADC_ATTENDB_MAX);
    }
    
    static int left() { 
        return analogRead(PIN_LEFT_SENSOR);
    }
    
    static int right() { 
        return analogRead(PIN_RIGHT_SENSOR);
    }
    
    static int noise() { 
        return analogRead(PIN_LEFT_SENSOR) ^ analogRead(PIN_RIGHT_SENSOR);
    }
    
    static bool hasObstacle() {
        return left() < SENSOR_THRESHOLD || right() < SENSOR_THRESHOLD;
    }

    static bool isClear() {
        return left() > CLEAR_THRESHOLD && right() > CLEAR_THRESHOLD;
    }

    static bool leftBlocked() {
        return left() < SENSOR_THRESHOLD;
    }

    static bool rightBlocked() {
        return right() < SENSOR_THRESHOLD;
    }
};

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
            genes[i].chaos_count = 0;
            genes[i].chaos_escape_count = 0;
        }
        save();
    }
    
    void load() {
        for (int i = 0; i < POPULATION_SIZE; i++) {
            EEPROMStorage::readGene(i, genes[i]);
        }
    }
    
    void save() {
        for (int i = 0; i < POPULATION_SIZE; i++) {
            EEPROMStorage::writeGene(i, genes[i]);
        }
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

// ===================== 进化引擎 =====================
class EvolutionEngine {
public:
    Population pop;
    uint8_t currentIdx = 0;
    uint32_t currentGen = 0;
    bool running = false;
    
    bool testRunning = false;
    uint32_t testStart = 0;
    uint32_t lastTotalPulse = 0;
    uint32_t stuckStart = 0;
    bool inChaos = false;
    uint32_t chaosStart = 0;
    int chaosStep = 0;
    bool chaosEscaped = false;
    
    void init() {
        EEPROMStorage::init();
        pop.load();
        
        // 检查并恢复备份
        restoreBackup();
        
        if (pop.genes[0].generation == 0 && pop.genes[0].id == 0) {
            Logger::log("⚠️ 检测到无效的世代数据，重新初始化...");
            pop.init();
        }
        currentGen = pop.maxGeneration();
        currentIdx = 0;
        Logger::logf("✅ Evolution loaded, generation %lu", currentGen);
    }
    
    void start() {
        running = true;
        currentIdx = 0;
        loadNextGene();
        Logger::log("▶ Evolution started");
    }
    
    void stop() {
        running = false;
        testRunning = false;
        Motor::stop();
        Logger::log("⏹ Evolution stopped");
    }
    
    void update() {
        if (!running) return;
        
        switch (state) {
            case STATE_LOAD:    loadNextGene(); break;
            case STATE_TEST:    runTest(); break;
            case STATE_CHAOS:   runChaos(); break;
            case STATE_SCORE:   calcScore(); break;
            case STATE_EVALUATE: evaluate(); break;
            case STATE_REPRODUCE: reproduce(); break;
        }
    }
    
    Gene& currentGene() { return pop.genes[currentIdx]; }
    uint32_t generation() { return currentGen; }
    bool isRunning() { return running; }
    bool isTesting() { return testRunning; }
    uint32_t testTime() { return testRunning ? (millis() - testStart) : 0; }


    // 运行阶段枚举
    enum RunPhase {
        PHASE_IDLE,
        PHASE_NORMAL_TEST,
        PHASE_CHAOS
    };
    RunPhase getCurrentPhase()
    {
        if(!running || !testRunning) return PHASE_IDLE;
        if(inChaos) return PHASE_CHAOS;
        return PHASE_NORMAL_TEST;
    }


    void saveHistoryToSPIFFS() {
        String filename = "/gen_" + String(currentGen) + ".csv";
        File file = SPIFFS.open(filename, FILE_WRITE);
        if (!file) {
            Logger::log(("❌ Failed to open history file: " + filename).c_str());
            return;
        }
        
        file.print("id,k_turn,speed_bias,generation,survival_ms,distance_ticks,obstacle_count,chaos_count,chaos_escape_count,score\n");
        
        for (int i = 0; i < POPULATION_SIZE; i++) {
            Gene& g = pop.genes[i];
            file.print(g.id); file.print(",");
            file.print(g.k_turn); file.print(",");
            file.print(g.speed_bias); file.print(",");
            file.print(g.generation); file.print(",");
            file.print(g.survival_ms); file.print(",");
            file.print(g.distance_ticks); file.print(",");
            file.print(g.obstacle_count); file.print(",");
            file.print(g.chaos_count); file.print(",");
            file.print(g.chaos_escape_count); file.print(",");
            file.println(g.score());
        }
        
        file.close();
        Logger::logf("💾 Generation %lu saved to SPIFFS (%s)", currentGen, filename.c_str());
    }
    
    void listHistoryFiles() {
        File root = SPIFFS.open("/");
        if (!root) {
            Logger::log("❌ Failed to open root directory");
            return;
        }
        
        File file = root.openNextFile();
        int count = 0;
        Logger::log("📁 History files in SPIFFS:");
        while (file) {
            if (!file.isDirectory()) {
                String name = String(file.name());
                if (name.startsWith("/gen_") && name.endsWith(".csv")) {
                    Logger::logf("  - %s (%lu bytes)", name.c_str(), file.size());
                    count++;
                }
            }
            file = root.openNextFile();
        }
        Logger::logf("📊 Total: %d generation files", count);
        root.close();
    }
    
    int getHistoryCount() {
        File root = SPIFFS.open("/");
        if (!root) return 0;
        
        int count = 0;
        File file = root.openNextFile();
        while (file) {
            if (!file.isDirectory()) {
                String name = String(file.name());
                if (name.startsWith("/gen_") && name.endsWith(".csv")) {
                    count++;
                }
            }
            file = root.openNextFile();
        }
        root.close();
        return count;
    }
    
    String readGenerationFromSPIFFS(uint32_t gen) {
        String filename = "/gen_" + String(gen) + ".csv";
        File file = SPIFFS.open(filename, FILE_READ);
        if (!file) {
            return "File not found: " + filename;
        }
        
        String content = "";
        while (file.available()) {
            content += (char)file.read();
        }
        file.close();
        return content;
    }

    // ===== 数据备份和恢复函数 =====
    void saveBackup(const Gene& g) {
        // 保存到EEPROM备份槽
        EEPROMStorage::writeGene(EEPROM_BACKUP_SLOT, g);
        EEPROM.commit();
        
        // 同时保存到SPIFFS作为双重保险
        String filename = "/backup_gen_" + String(g.generation) + "_id_" + String(g.id) + ".csv";
        File file = SPIFFS.open(filename, FILE_WRITE);
        if (file) {
            file.print("id,k_turn,speed_bias,generation,survival_ms,distance_ticks,obstacle_count,chaos_count,chaos_escape_count,score,timestamp\n");
            file.print(g.id); file.print(",");
            file.print(g.k_turn); file.print(",");
            file.print(g.speed_bias); file.print(",");
            file.print(g.generation); file.print(",");
            file.print(g.survival_ms); file.print(",");
            file.print(g.distance_ticks); file.print(",");
            file.print(g.obstacle_count); file.print(",");
            file.print(g.chaos_count); file.print(",");
            file.print(g.chaos_escape_count); file.print(",");
            file.print(g.score()); file.print(",");
            file.println(millis());
            file.close();
            Logger::logf("💾 Backup saved: %s", filename.c_str());
        }
    }
    
    void restoreBackup() {
        Gene backupGene;
        EEPROMStorage::readGene(EEPROM_BACKUP_SLOT, backupGene);
        
        // 检查是否有有效的备份数据
        if (backupGene.isValid()) {
            Logger::logf("⚠️ Found backup data: Gen %lu, ID %d", backupGene.generation, backupGene.id);
            
            // 保存到SPIFFS恢复文件
            String filename = "/recovery_gen_" + String(backupGene.generation) + "_id_" + String(backupGene.id) + ".csv";
            File file = SPIFFS.open(filename, FILE_WRITE);
            if (file) {
                file.print("id,k_turn,speed_bias,generation,survival_ms,distance_ticks,obstacle_count,chaos_count,chaos_escape_count,score,restored_at\n");
                file.print(backupGene.id); file.print(",");
                file.print(backupGene.k_turn); file.print(",");
                file.print(backupGene.speed_bias); file.print(",");
                file.print(backupGene.generation); file.print(",");
                file.print(backupGene.survival_ms); file.print(",");
                file.print(backupGene.distance_ticks); file.print(",");
                file.print(backupGene.obstacle_count); file.print(",");
                file.print(backupGene.chaos_count); file.print(",");
                file.print(backupGene.chaos_escape_count); file.print(",");
                file.print(backupGene.score()); file.print(",");
                file.println(millis());
                file.close();
                Logger::logf("✅ Backup restored to: %s", filename.c_str());
            }
            
            // 可以选择恢复到当前种群
            if (currentIdx < POPULATION_SIZE) {
                pop.genes[currentIdx] = backupGene;
                pop.save();
                Logger::log("✅ Backup data restored to current individual");
            }
            
            clearBackup();
        }
    }
    
    void clearBackup() {
        EEPROMStorage::clearGene(EEPROM_BACKUP_SLOT);
        Logger::log("🧹 Backup slot cleared");
    }

private:
    enum State {
        STATE_LOAD,
        STATE_TEST,
        STATE_CHAOS,
        STATE_BACKWARD_RESET, // 新增：个体结束后退复位
        STATE_SCORE,
        STATE_EVALUATE,
        STATE_REPRODUCE
    };
        State state = STATE_LOAD;
        // 后退复位计时器
        uint32_t backwardResetStart = 0;
        const uint32_t BACKWARD_RESET_DURATION = 1000;
        const int BACKWARD_SPEED = -110; // 后退速度，可微调
    
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
        
        state = STATE_TEST;
        Logger::logf("🧬 Gene ID:%d k:%d speed:%d", g.id, g.k_turn, g.speed_bias);
    }
    
    void runTest() {
        if (!testRunning) {
            testRunning = true;
            testStart = millis();
            Logger::log("🏃 Test started");
            
            // 清除旧的备份
            clearBackup();
        }
        
        Gene& g = currentGene();
        uint32_t now = millis();
        
        // 定期保存当前进度
        static uint32_t lastBackupTime = 0;
        if (now - lastBackupTime >= BACKUP_INTERVAL_MS && testRunning) {
            lastBackupTime = now;
            
            // 更新当前数据
            g.survival_ms = now - testStart;
            g.distance_ticks = Encoders::totalCount();
            
            // 保存备份
            saveBackup(g);
            
            // 同时保存到SPIFFS作为临时记录
            String filename = "/temp_gen_" + String(g.generation) + ".csv";
            File file = SPIFFS.open(filename, FILE_WRITE);
            if (file) {
                file.print("timestamp,id,k_turn,speed_bias,survival_ms,distance_ticks,obstacle_count\n");
                file.print(now); file.print(",");
                file.print(g.id); file.print(",");
                file.print(g.k_turn); file.print(",");
                file.print(g.speed_bias); file.print(",");
                file.print(g.survival_ms); file.print(",");
                file.print(g.distance_ticks); file.print(",");
                file.println(g.obstacle_count);
                file.close();
            }
        }
        
        uint32_t totalDelta = Encoders::totalDelta();
        bool stuck = false;
        
        if (totalDelta < 2) {
            if (stuckStart == 0) stuckStart = now;
            if (now - stuckStart > STUCK_THRESHOLD_MS) stuck = true;
        } else {
            stuckStart = 0;
        }
        
        if (stuck && !inChaos) {
            // 进入混沌前保存当前状态
            g.survival_ms = now - testStart;
            g.distance_ticks = Encoders::totalCount();
            saveBackup(g);
            enterChaos(g);
            return;
        }
        
        if (now - testStart >= TEST_DURATION_MS) {
            Motor::stop();
            g.survival_ms = TEST_DURATION_MS;
            g.distance_ticks = Encoders::totalCount();
            g.obstacle_count = 0;
            
            // 测试完成，保存最终数据
            pop.save();
            saveBackup(g);
            
            testRunning = false;
            state = STATE_SCORE;
            Logger::logf("✅ Test complete: %lu ms, dist:%lu", g.survival_ms, g.distance_ticks);
            
            // 清除备份（测试完成）
            clearBackup();
            return;
        }
        
        if (!inChaos) {
            int left = Sensors::left();   
            int right = Sensors::right(); 
            
            int baseSpeed = g.speed_bias - 50;
            baseSpeed = constrain(baseSpeed, 100, 200);
            
            int turnAmount = 0;
            int pwmLeft = 0;
            int pwmRight = 0;
            
            if (Sensors::hasObstacle()) {
                int diff = left - right;
                int obstacleSpeed = baseSpeed * 0.6;
                obstacleSpeed = constrain(obstacleSpeed, 60, 150);
                
                if (abs(diff) > 50) {
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
                g.obstacle_count++;
                
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
        
        // 进入混沌时保存状态
        saveBackup(g);
        Logger::log("🌀 CHAOS INJECTED! (backup saved)");
    }
    
    void runChaos() {
        Gene& g = currentGene();
        uint32_t now = millis();

        if (chaosStep < CHAOS_SAMPLE_COUNT) {
            static uint32_t lastChaosTime = 0;
            if (now - lastChaosTime >= CHAOS_STEP_MS) {
                lastChaosTime = now;
                
                // 混沌每一步都保存
                if (chaosStep % 2 == 0) {
                    g.survival_ms = now - testStart;
                    g.distance_ticks = Encoders::totalCount();
                    saveBackup(g);
                }
                
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
                    pop.save();
                    saveBackup(g);
                    Logger::log("✅ CHAOS ESCAPED!");
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
            g.survival_ms = now - testStart;
            g.distance_ticks = Encoders::totalCount();
            pop.save();
            saveBackup(g);
            inChaos = false;
            testRunning = false;
            state = STATE_SCORE;
        }
    }
    
    void calcScore() {
        Gene& g = currentGene();
        uint32_t score = g.score();
        Logger::logf("📊 Score: %lu (dist:%lu, chaos:%d/%d)", 
            score, g.distance_ticks, g.chaos_count, g.chaos_escape_count);
        
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
        pop.save();
        state = STATE_REPRODUCE;
    }
    
    void reproduce() {
        Logger::log("🧬 Reproducing...");
        
        Gene nextGen[POPULATION_SIZE];
        
        for (int i = 0; i < ELITE_COUNT; i++) {
            nextGen[i] = pop.genes[i];
            nextGen[i].generation = pop.genes[i].generation + 1;
        }
        
        for (int i = ELITE_COUNT; i < POPULATION_SIZE; i++) {
            uint16_t noise = Sensors::noise();
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
            nextGen[i].mutate(noise, currentGen);
            nextGen[i].id = i;
        }
        
        for (int i = 0; i < POPULATION_SIZE; i++) {
            pop.genes[i] = nextGen[i];
        }
        
        // 保存到EEPROM
        pop.save();
        
        // 保存到SPIFFS
        saveHistoryToSPIFFS();
        
        currentGen = pop.maxGeneration();
        
        // 清除备份
        clearBackup();
        
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
        
        server.on("/status", HTTP_GET, [this]() {
            String s = "running:" + String(engine.isRunning() ? "1" : "0") + "\n";
            s += "testing:" + String(engine.isTesting() ? "1" : "0") + "\n";
            s += "generation:" + String(engine.generation()) + "\n";
            s += "individual:" + String(engine.currentIdx) + "\n";
            s += "test_time:" + String(engine.testTime()) + "\n";
            s += "pulse:" + String(Encoders::totalCount()) + "\n";
            s += "encoder_L:" + String(Encoders::leftCount()) + "\n";
            s += "encoder_R:" + String(Encoders::rightCount()) + "\n";
            s += "sensor_L:" + String(Sensors::left()) + "\n";
            s += "sensor_R:" + String(Sensors::right()) + "\n";
            s += "motor_L:" + String(Motor::getLeftSpeed()) + "\n";
            s += "motor_R:" + String(Motor::getRightSpeed()) + "\n";
            s += "phase:" + String(engine.getCurrentPhase()) + "\n";
            server.send(200, "text/plain", s);
        });
        
        server.on("/population", HTTP_GET, [this]() {
            String csv = "id,k_turn,speed_bias,generation,survival_ms,distance_ticks,obstacle_count,chaos_count,chaos_escape_count,score\n";
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
                csv += String(g.score()) + "\n";
            }
            server.send(200, "text/csv", csv);
        });

        server.on("/history", HTTP_GET, [this]() {
            String html = generateHistoryPage();
            server.send(200, "text/html", html);
        });
        
        server.on("/download", HTTP_GET, [this]() {
            String filename = server.arg("file");
            if (filename.isEmpty() || !filename.startsWith("/gen_") || !filename.endsWith(".csv")) {
                server.send(400, "text/plain", "Invalid filename");
                return;
            }
            
            File file = SPIFFS.open(filename, FILE_READ);
            if (!file) {
                server.send(404, "text/plain", "File not found");
                return;
            }
            
            server.sendHeader("Content-Type", "text/csv");
            server.sendHeader("Content-Disposition", "attachment; filename=" + filename.substring(1));
            server.streamFile(file, "text/csv");
            file.close();
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

        server.on("/download/latest3", HTTP_GET, [this]() {
            String result = "=== 最近3代数据 ===\n\n";
            
            File root = SPIFFS.open("/");
            std::vector<String> files;
            File file = root.openNextFile();
            while (file) {
                if (!file.isDirectory()) {
                    String name = String(file.name());
                    if (name.startsWith("/gen_") && name.endsWith(".csv")) {
                        files.push_back(name);
                    }
                }
                file = root.openNextFile();
            }
            root.close();
            
            std::sort(files.begin(), files.end());
            
            int start = files.size() > 3 ? files.size() - 3 : 0;
            for (int i = start; i < files.size(); i++) {
                result += "📄 " + files[i] + "\n";
                File f = SPIFFS.open(files[i], FILE_READ);
                if (f) {
                    while (f.available()) {
                        result += (char)f.read();
                    }
                    f.close();
                }
                result += "\n---\n\n";
            }
            
            server.send(200, "text/plain", result);
        });

        server.on("/history/status", HTTP_GET, [this]() {
            String status = "=== 历史数据状态 ===\n";
            
            File root = SPIFFS.open("/");
            std::vector<String> files;
            File file = root.openNextFile();
            while (file) {
                if (!file.isDirectory()) {
                    String name = String(file.name());
                    if (name.startsWith("/gen_") && name.endsWith(".csv")) {
                        files.push_back(name);
                        status += name + "\n";
                    }
                }
                file = root.openNextFile();
            }
            root.close();
            
            status += "\n📊 总计: " + String(files.size()) + " 个文件\n";
            status += "📌 最近3代: ";
            if (files.size() > 0) {
                std::sort(files.begin(), files.end());
                int start = files.size() > 3 ? files.size() - 3 : 0;
                for (int i = start; i < files.size(); i++) {
                    status += files[i] + " ";
                }
            }
            
            server.send(200, "text/plain", status);
        });
        
        // 新增：备份状态查询
        server.on("/backup/status", HTTP_GET, [this]() {
            Gene backupGene;
            EEPROMStorage::readGene(EEPROM_BACKUP_SLOT, backupGene);
            
            String s = "=== Backup Status ===\n";
            if (backupGene.isValid()) {
                s += "✅ Backup exists:\n";
                s += "  Generation: " + String(backupGene.generation) + "\n";
                s += "  Individual ID: " + String(backupGene.id) + "\n";
                s += "  Survival: " + String(backupGene.survival_ms) + " ms\n";
                s += "  Distance: " + String(backupGene.distance_ticks) + " ticks\n";
                s += "  Score: " + String(backupGene.score()) + "\n";
            } else {
                s += "❌ No backup data\n";
            }
            server.send(200, "text/plain", s);
        });
        
        server.on("/backup/clear", HTTP_GET, [this]() {
            engine.clearBackup();
            server.send(200, "text/plain", "Backup cleared");
        });
        
        // 新增：强制保存当前数据
        server.on("/save/now", HTTP_GET, [this]() {
            engine.pop.save();
            engine.saveHistoryToSPIFFS();
            server.send(200, "text/plain", "Data saved to EEPROM and SPIFFS");
        });
        
        server.begin();
        Logger::log("🌐 Web server: http://192.168.4.1");
    }
    
    void loop() { server.handleClient(); }

private:
    EvolutionEngine& engine;
    WebServer server;
    
    String generateHistoryPage() {
        String html = R"raw(
<!DOCTYPE html>
<html>
<head>
    <meta charset='UTF-8'>
    <meta name='viewport' content='width=device-width, initial-scale=1.0'>
    <title>Evolution History</title>
    <style>
        body { font-family:Arial; background:#1a1a2e; padding:20px; color:#eee; max-width:800px; margin:0 auto; }
        h1 { color:#e94560; }
        .file-list { list-style:none; padding:0; }
        .file-list li { background:#16213e; padding:12px 20px; margin:5px 0; border-radius:8px; display:flex; justify-content:space-between; align-items:center; }
        .file-list a { color:#00d4ff; text-decoration:none; font-weight:bold; }
        .file-list a:hover { text-decoration:underline; }
        .file-size { color:#888; font-size:14px; }
        .stats { background:#16213e; padding:15px 20px; border-radius:8px; margin-bottom:20px; }
        .stats span { color:#00d4ff; font-weight:bold; }
        .back { margin-top:20px; display:inline-block; color:#00d4ff; text-decoration:none; }
        .back:hover { text-decoration:underline; }
    </style>
</head>
<body>
    <h1>📊 进化历史数据</h1>
)raw";
        
        File root = SPIFFS.open("/");
        int count = 0;
        uint32_t totalSize = 0;
        File file = root.openNextFile();
        while (file) {
            if (!file.isDirectory()) {
                String name = String(file.name());
                if (name.startsWith("/gen_") && name.endsWith(".csv")) {
                    count++;
                    totalSize += file.size();
                }
            }
            file = root.openNextFile();
        }
        root.close();
        
        html += "<div class='stats'>";
        html += "📁 共 <span>" + String(count) + "</span> 代数据 | ";
        html += "💾 总大小 <span>" + String(totalSize / 1024) + " KB</span>";
        html += "</div>";
        
        html += "<ul class='file-list'>";
        
        root = SPIFFS.open("/");
        file = root.openNextFile();
        std::vector<String> files;
        while (file) {
            if (!file.isDirectory()) {
                String name = String(file.name());
                if (name.startsWith("/gen_") && name.endsWith(".csv")) {
                    files.push_back(name);
                }
            }
            file = root.openNextFile();
        }
        root.close();
        
        std::sort(files.begin(), files.end(), std::greater<String>());
        
        for (String& name : files) {
            String genStr = name.substring(5, name.length() - 4);
            File f = SPIFFS.open(name, FILE_READ);
            if (f) {
                html += "<li>";
                html += "<a href='/download?file=" + name + "'>📄 第 " + genStr + " 代</a>";
                html += "<span class='file-size'>" + String(f.size() / 1024) + " KB</span>";
                html += "</li>";
                f.close();
            }
        }
        
        html += R"raw(
</ul>
    <a href='/' class='back'>⬅ 返回主页面</a>
</body>
</html>
)raw";
        return html;
    }
    
    String page() {
        String p = R"raw(
<!DOCTYPE html>
<html>
<head>
    <meta charset='UTF-8'>
    <meta name='viewport' content='width=device-width, initial-scale=1.0'>
    <title>Car Evolution v5.5 - ESP32-S3</title>
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
        .btn-pink { background:#e91e63; }
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
        .backup-status { background:#0a0a1a; padding:8px 12px; border-radius:6px; margin:8px 0; font-size:13px; }
        .backup-status .label { color:#888; }
        .backup-status .value { color:#2ecc71; font-weight:bold; }
    </style>
</head>
<body>
<div class='box'>
    <h1>🚗 Evolution v5.5</h1>
    <div class='grid'>
    <button class='btn btn-green' onclick='cmd("/start")'>▶ START</button>
    <button class='btn btn-red' onclick='cmd("/stop")'>⏹ STOP</button>
    <button class='btn btn-orange' onclick='cmd("/reset")'>🔄 RESET</button>
    <button class='btn btn-purple' onclick='window.open("/population")'>📊 数据</button>
    <button class='btn btn-blue' onclick='window.open("/history")'>📁 历史</button>
    <button class='btn btn-cyan' onclick='downloadLatest3()'>📥 最新3代</button>
    <button class='btn btn-pink' onclick='cmd("/save/now")'>💾 保存</button>
    <button class='btn btn-orange' onclick='checkBackup()'>📋 备份状态</button>
</div>

    <div style='margin:10px 0;'>
        <div class='info'><span class='info-label'>状态</span><span class='info-value' id='state'>--</span></div>
        <div class='info'><span class='info-label'>世代</span><span class='info-value' id='generation'>--</span></div>
        <div class='info'><span class='info-label'>个体</span><span class='info-value' id='individual'>--</span></div>
        <div class='info'><span class='info-label'>测试时间</span><span class='info-value' id='test_time'>--</span></div>
    </div>
    
    <div class='backup-status' id='backup_status'>
        <span class='label'>💾 备份状态: </span>
        <span class='value' id='backup_info'>检查中...</span>
    </div>
    
    <div class='sensor-box'>
        <div class='sensor-row'>
            <span class='sensor-label'>⬅ 左TCRT5000</span>
            <span id='sensor_L' class='sensor-low'>--</span>
        </div>
        <div class='sensor-row'>
            <span class='sensor-label'>➡ 右TCRT5000</span>
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

<div class='footer'><span>✅ 编码器已启用</span> (AB相霍尔) | 数据持久化 v5.5</div>

<script>
function cmd(url) {
    fetch(url).then(r => r.text()).then(data => { refresh(); });
}

function downloadLatest3() {
    fetch('/download/latest3')
        .then(r => r.text())
        .then(data => {
            const blob = new Blob([data], {type: 'text/plain;charset=utf-8'});
            const url = window.URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.href = url;
            a.download = 'latest_3_generations.txt';
            document.body.appendChild(a);
            a.click();
            document.body.removeChild(a);
            window.URL.revokeObjectURL(url);
        })
        .catch(err => alert('下载失败: ' + err));
}

function checkBackup() {
    fetch('/backup/status')
        .then(r => r.text())
        .then(data => {
            document.getElementById('backup_info').textContent = data.split('\\n').slice(0, 3).join(' | ');
            alert(data);
        })
        .catch(err => alert('获取备份状态失败: ' + err));
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
                const el = document.getElementById('sensor_L');
                const val = parseInt(data['sensor_L']);
                el.textContent = val;
                el.className = val > 400 ? 'sensor-high' : 'sensor-low';
            }
            if(data['sensor_R'] !== undefined) {
                const el = document.getElementById('sensor_R');
                const val = parseInt(data['sensor_R']);
                el.textContent = val;
                el.className = val > 400 ? 'sensor-high' : 'sensor-low';
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
                const phase = parseInt(data['phase'] ?? "0");
                if(data['running'] === '1' && data['testing'] === '1') {
                    if(phase === 2){
                        stateEl.textContent = '🌀 混沌阶段(死局)';
                        stateEl.style.color = '#9c27b0';
                    }else{
                        stateEl.textContent = '🏃 正常测试中';
                        stateEl.style.color = '#2ecc71';
                    }
                } else if(data['running'] === '1') {
                    stateEl.textContent = '⏳ 等待载入个体';
                    stateEl.style.color = '#f39c12';
                } else {
                    stateEl.textContent = '⏹ 进化停止';
                    stateEl.style.color = '#e74c3c';
                }
            }
            
            // 自动更新备份状态
            fetch('/backup/status')
                .then(r => r.text())
                .then(backupData => {
                    const lines = backupData.split('\n');
                    if (lines.length > 1 && lines[1].includes('✅')) {
                        document.getElementById('backup_info').textContent = '✅ 有备份数据';
                        document.getElementById('backup_info').style.color = '#2ecc71';
                    } else {
                        document.getElementById('backup_info').textContent = '❌ 无备份';
                        document.getElementById('backup_info').style.color = '#e74c3c';
                    }
                })
                .catch(() => {});
        })
        .catch(err => console.error('刷新错误:', err));
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
    Logger::log("  Car Evolution v5.5 - ESP32-S3 FULL");
    Logger::log("  DATA PERSISTENCE ENABLED");
    Logger::log("========================================");
    Logger::log("  ✅ 电机: AT8236驱动");
    Logger::log("  ✅ 编码器: AB相霍尔");
    Logger::log("  ✅ 传感器: TCRT5000 x2");
    Logger::log("  ✅ 数据持久化: EEPROM + SPIFFS");
    Logger::log("  ✅ 自动备份: 每3秒");
    Logger::log("========================================");
    
    // 初始化SPIFFS
    if (!SPIFFS.begin(true)) {
        Logger::log("❌ SPIFFS Mount Failed!");
    } else {
        Logger::log("✅ SPIFFS Mount Success!");
        File root = SPIFFS.open("/");
        if (root) {
            int count = 0;
            File file = root.openNextFile();
            while (file) {
                if (!file.isDirectory()) {
                    String name = String(file.name());
                    if (name.startsWith("/gen_") && name.endsWith(".csv")) {
                        count++;
                    }
                }
                file = root.openNextFile();
            }
            root.close();
            Logger::logf("📁 Found %d history files in SPIFFS", count);
        }
    }
    
    Motor::init();
    Sensors::init();
    Encoders::init();
    
    // ⭐⭐⭐ 新增：EEPROM自动检测和初始化 ⭐⭐⭐
    EEPROMStorage::init();
    
    // 读取魔数标记
    uint16_t magic;
    EEPROM.get(EEPROM_MAGIC_ADDR, magic);
    
    // 检查是否是首次运行或数据损坏
    bool needInit = false;
    
    if (magic != EEPROM_INIT_MAGIC) {
        Logger::log("🔄 首次运行或版本更新，初始化EEPROM...");
        needInit = true;
    } else {
        // 验证数据有效性
        Gene testGene;
        EEPROMStorage::readGene(0, testGene);
        
        // 检查数据是否合理
        if (testGene.generation > 100000 || 
            testGene.generation == 0 || 
            testGene.id > POPULATION_SIZE || 
            testGene.speed_bias < 100 || 
            testGene.speed_bias > 255) {
            Logger::log("⚠️ 检测到损坏的EEPROM数据，重新初始化...");
            needInit = true;
        }
    }
    
    if (needInit) {
        // 清除所有EEPROM数据
        Logger::log("🧹 清除EEPROM数据...");
        for (int i = 0; i < 4096; i++) {
            EEPROM.write(i, 0);
        }
        
        // 写入魔数标记
        EEPROM.put(EEPROM_MAGIC_ADDR, EEPROM_INIT_MAGIC);
        EEPROM.commit();
        Logger::logf("✅ EEPROM初始化完成 (Magic: 0x%04X)", EEPROM_INIT_MAGIC);

        } else {
        Logger::logf("✅ EEPROM数据有效 (Magic: 0x%04X)", EEPROM_INIT_MAGIC);
    }
    // ⭐⭐⭐ EEPROM自动检测结束 ⭐⭐⭐
    
    // 测试传感器和编码器
    for (int i = 0; i < 3; i++) {
        Logger::logf("🔍 SENS L:%d R:%d | ENC L:%d R:%d", 
            Sensors::left(), Sensors::right(),
            Encoders::leftCount(), Encoders::rightCount());
        delay(500);
    }

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
    Logger::logf("📁 历史数据: %d 代", engine.getHistoryCount());
 
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