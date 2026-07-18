/*
 * ================================================================
 * 项目名称：仿草履虫应激机制 - 小车避障进化验证系统
 * 版本：    v5.4 (修正传感器反逻辑 + 页面动态显示)
 * 最后更新: 2026-07-18
 * 
 * 修改说明：
 * 1. 传感器值反转 (未遮挡高值→低值，遮挡低值→高值)
 * 2. 适应"遮挡=低值"的传感器特性
 * 3. 修复速度计算逻辑，避免原地转圈
 * 4. 页面动态显示传感器和电机参数
 * 5. 每个个体生存时间改为30秒钟
 * ================================================================
 */

#include <WiFi.h>
#include <EEPROM.h>
#include <WebServer.h>
#include <esp_wifi.h>

// ===================== 引脚定义 =====================
#define PIN_LEFT_SENSOR    34
#define PIN_RIGHT_SENSOR   35
#define PIN_NOISE          36
#define PIN_LED            2

#define PIN_LEFT_PWM       21
#define PIN_LEFT_DIR1      12
#define PIN_LEFT_DIR2      14

#define PIN_RIGHT_PWM      22
#define PIN_RIGHT_DIR1     16
#define PIN_RIGHT_DIR2     17

#define PIN_LEFT_ENC       4
#define PIN_RIGHT_ENC      5

// ===================== 电机方向配置 =====================
#define LEFT_FORWARD   LOW
#define LEFT_REVERSE   HIGH
#define RIGHT_FORWARD  HIGH
#define RIGHT_REVERSE  LOW

// ===================== 系统参数 =====================
#define WIFI_SSID       "CarLogger"
#define WIFI_PASSWORD   "12345678"

#define POPULATION_SIZE 16
#define ELITE_COUNT     6
#define TEST_DURATION_MS    30000   // 每轮测试60秒
#define STUCK_THRESHOLD_MS  3000    // 3秒不动判定为卡死

#define CHAOS_SAMPLE_COUNT  15      // 混沌尝试次数
#define CHAOS_STEP_MS       100     // 混沌每步时间

#define SENSOR_THRESHOLD    2000    // 障碍物阈值 (反转后使用)
#define CLEAR_THRESHOLD     1500    // 空旷阈值 (反转后使用)

#define EEPROM_START        0

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
    int8_t   k_turn;          // 转向系数
    uint8_t  speed_bias;      // 速度基准 (150-180)
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
        pinMode(PIN_RIGHT_PWM, OUTPUT);
        pinMode(PIN_LEFT_DIR1, OUTPUT);
        pinMode(PIN_LEFT_DIR2, OUTPUT);
        pinMode(PIN_RIGHT_DIR1, OUTPUT);
        pinMode(PIN_RIGHT_DIR2, OUTPUT);
        ledcAttach(PIN_LEFT_PWM, 5000, 8);
        ledcAttach(PIN_RIGHT_PWM, 5000, 8);
        stop();
    }
    
    static void stop() {
        leftSpeed = 0;
        rightSpeed = 0;
        digitalWrite(PIN_LEFT_DIR1, LOW);
        digitalWrite(PIN_LEFT_DIR2, LOW);
        digitalWrite(PIN_RIGHT_DIR1, LOW);
        digitalWrite(PIN_RIGHT_DIR2, LOW);
        ledcWrite(PIN_LEFT_PWM, 0);
        ledcWrite(PIN_RIGHT_PWM, 0);
    }
    
    static void setSpeed(int left, int right) {
        left = constrain(left, -255, 255);
        right = constrain(right, -255, 255);
        
        leftSpeed = left;
        rightSpeed = right;
        
        // 左电机
        if (left > 0) {
            digitalWrite(PIN_LEFT_DIR1, LEFT_FORWARD);
            digitalWrite(PIN_LEFT_DIR2, LEFT_REVERSE);
            ledcWrite(PIN_LEFT_PWM, left);
        } else if (left < 0) {
            digitalWrite(PIN_LEFT_DIR1, LEFT_REVERSE);
            digitalWrite(PIN_LEFT_DIR2, LEFT_FORWARD);
            ledcWrite(PIN_LEFT_PWM, -left);
        } else {
            digitalWrite(PIN_LEFT_DIR1, LOW);
            digitalWrite(PIN_LEFT_DIR2, LOW);
            ledcWrite(PIN_LEFT_PWM, 0);
        }
        
        // 右电机
        if (right > 0) {
            digitalWrite(PIN_RIGHT_DIR1, RIGHT_FORWARD);
            digitalWrite(PIN_RIGHT_DIR2, RIGHT_REVERSE);
            ledcWrite(PIN_RIGHT_PWM, right);
        } else if (right < 0) {
            digitalWrite(PIN_RIGHT_DIR1, RIGHT_REVERSE);
            digitalWrite(PIN_RIGHT_DIR2, RIGHT_FORWARD);
            ledcWrite(PIN_RIGHT_PWM, -right);
        } else {
            digitalWrite(PIN_RIGHT_DIR1, LOW);
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

// 初始化静态成员
int Motor::leftSpeed = 0;
int Motor::rightSpeed = 0;

// ===================== 编码器 =====================
volatile uint32_t pulseCount = 0;

void IRAM_ATTR encoderISR() { pulseCount++; }

class Encoders {
public:
    static void init() {
        pinMode(PIN_LEFT_ENC, INPUT_PULLUP);
        pinMode(PIN_RIGHT_ENC, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(PIN_LEFT_ENC), encoderISR, FALLING);
        attachInterrupt(digitalPinToInterrupt(PIN_RIGHT_ENC), encoderISR, FALLING);
        reset();
    }
    static void reset() { pulseCount = 0; }
    static uint32_t count() { return pulseCount; }
    static uint32_t delta(uint32_t last) { return pulseCount - last; }
};

// ===================== 传感器 =====================
class Sensors {
public:
    static void init() {
        pinMode(PIN_LEFT_SENSOR, INPUT);
        pinMode(PIN_RIGHT_SENSOR, INPUT);
        pinMode(PIN_NOISE, INPUT);
    }
    
    static int left() { 
        int raw = analogRead(PIN_LEFT_SENSOR);
        return 4095 - raw;  // 反转
    }
    
    static int right() { 
        int raw = analogRead(PIN_RIGHT_SENSOR);
        return 4095 - raw;  // 反转
    }
    
    static int noise() { return analogRead(PIN_NOISE); }
    
    static bool hasObstacle() {
        return left() > SENSOR_THRESHOLD && right() > SENSOR_THRESHOLD;
    }
    
    static bool isClear() {
        return left() < CLEAR_THRESHOLD && right() < CLEAR_THRESHOLD;
    }
};

// ===================== 种群 =====================
class Population {
public:
    Gene genes[POPULATION_SIZE];
    
    void init() {
        for (int i = 0; i < POPULATION_SIZE; i++) {
            genes[i].id = i;
            genes[i].k_turn = 1;        // ★★★ 改为1，减小转向系数
            genes[i].speed_bias = 200;  // ★★★ 改为200，提高基准速度
            genes[i].generation = 0;
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
    
    // 运行时状态
    bool testRunning = false;
    uint32_t testStart = 0;
    uint32_t lastPulse = 0;
    uint32_t stuckStart = 0;
    bool inChaos = false;
    uint32_t chaosStart = 0;
    int chaosStep = 0;
    bool chaosEscaped = false;
    
    void init() {
        EEPROMStorage::init();
        pop.load();
        if (pop.genes[0].generation == 0 && pop.genes[0].id == 0) {
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

private:
    enum State { STATE_LOAD, STATE_TEST, STATE_CHAOS, STATE_SCORE, STATE_EVALUATE, STATE_REPRODUCE };
    State state = STATE_LOAD;
    
    void loadNextGene() {
        Logger::logf("📂 Loading gene %d/%d", currentIdx + 1, POPULATION_SIZE);
        
        Gene& g = currentGene();
        Encoders::reset();
        lastPulse = 0;
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
        }
        
        Gene& g = currentGene();
        uint32_t now = millis();
        
        uint32_t pulseDelta = Encoders::delta(lastPulse);
        bool stuck = false;
        
        if (pulseDelta < 3 || Sensors::hasObstacle()) {
            if (stuckStart == 0) stuckStart = now;
            if (now - stuckStart > STUCK_THRESHOLD_MS) stuck = true;
        } else {
            stuckStart = 0;
        }
        
        if (stuck && !inChaos) {
            enterChaos(g);
            return;
        }
        
        if (now - testStart >= TEST_DURATION_MS) {
            Motor::stop();
            g.survival_ms = TEST_DURATION_MS;
            g.distance_ticks = Encoders::count();
            g.obstacle_count = 0;
            pop.save();
            testRunning = false;
            state = STATE_SCORE;
            Logger::logf("✅ Test complete: %lu ms", g.survival_ms);
            return;
        }
        
        if (!inChaos) {
            int left = Sensors::left();   
            int right = Sensors::right(); 
            
            // ★★★ 改进的速度计算 ★★★
            int steer = right - left;
            
            // 基准速度 (确保足够大)
            int baseSpeed = g.speed_bias - 50;  // 200-50=150
            baseSpeed = constrain(baseSpeed, 80, 200);
            
            // 转向修正 (限制最大转向力度)
            int turnFactor = g.k_turn * 5;  // k_turn=1 → 5
            int maxTurn = baseSpeed * 0.5;  // 最大转向不超过基准速度的50%
            int turnCorrection = constrain(steer * turnFactor / 100, -maxTurn, maxTurn);
            
            int pwmLeft = baseSpeed + 30 - turnCorrection;
            int pwmRight = baseSpeed + 30 + turnCorrection;
            
            // 确保正向前进
            if (pwmLeft <= 0) pwmLeft = 20;
            if (pwmRight <= 0) pwmRight = 20;
            
            // 死区补偿
            if (pwmLeft > 0 && pwmLeft < 80) pwmLeft = 80;
            if (pwmRight > 0 && pwmRight < 80) pwmRight = 80;
            
            pwmLeft = constrain(pwmLeft, -255, 255);
            pwmRight = constrain(pwmRight, -255, 255);
            
            Motor::setSpeed(pwmLeft, pwmRight);
            lastPulse = Encoders::count();
        }
    }
    
    void enterChaos(Gene& g) {
        state = STATE_CHAOS;
        inChaos = true;
        chaosStart = millis();
        chaosStep = 0;
        chaosEscaped = false;
        g.chaos_count++;
        Logger::log("🌀 CHAOS INJECTED!");
    }
    
    void runChaos() {
        Gene& g = currentGene();
        uint32_t now = millis();
        
        if (chaosStep < CHAOS_SAMPLE_COUNT) {
            Motor::setRandom();
            delay(CHAOS_STEP_MS);
            
            if (Sensors::isClear() || Encoders::delta(lastPulse) > 10) {
                chaosEscaped = true;
                g.chaos_escape_count++;
                pop.save();
                Logger::log("✅ CHAOS ESCAPED!");
                inChaos = false;
                state = STATE_TEST;
                Motor::stop();
                stuckStart = 0;
                return;
            }
            chaosStep++;
            lastPulse = Encoders::count();
        } else {
            Logger::log("⏰ CHAOS TIMEOUT");
            Motor::stop();
            g.survival_ms = now - testStart;
            g.distance_ticks = Encoders::count();
            pop.save();
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
        pop.save();
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
        
        server.on("/status", HTTP_GET, [this]() {
            String s = "running:" + String(engine.isRunning() ? "1" : "0") + "\n";
            s += "testing:" + String(engine.isTesting() ? "1" : "0") + "\n";
            s += "generation:" + String(engine.generation()) + "\n";
            s += "individual:" + String(engine.currentIdx) + "\n";
            s += "test_time:" + String(engine.testTime()) + "\n";
            s += "pulse:" + String(Encoders::count()) + "\n";
            s += "sensor_L:" + String(Sensors::left()) + "\n";
            s += "sensor_R:" + String(Sensors::right()) + "\n";
            s += "raw_L:" + String(analogRead(PIN_LEFT_SENSOR)) + "\n";
            s += "raw_R:" + String(analogRead(PIN_RIGHT_SENSOR)) + "\n";
            s += "motor_L:" + String(Motor::getLeftSpeed()) + "\n";
            s += "motor_R:" + String(Motor::getRightSpeed()) + "\n";
            server.send(200, "text/plain", s);
        });
        
        server.on("/population", HTTP_GET, [this]() {
            String csv = "id,k_turn,speed_bias,generation,survival_ms,distance,chaos,escaped,score\n";
            for (int i = 0; i < POPULATION_SIZE; i++) {
                Gene g;
                EEPROMStorage::readGene(i, g);
                csv += String(g.id) + "," + String(g.k_turn) + "," + String(g.speed_bias) + ",";
                csv += String(g.generation) + "," + String(g.survival_ms) + ",";
                csv += String(g.distance_ticks) + "," + String(g.chaos_count) + ",";
                csv += String(g.chaos_escape_count) + "," + String(g.score()) + "\n";
            }
            server.send(200, "text/plain", csv);
        });
        
        server.on("/cmd/motor/forward", HTTP_GET, [this]() {
            Motor::setSpeed(150, 150);
            server.send(200, "text/plain", "FORWARD ON");
        });
        
        server.on("/cmd/motor/backward", HTTP_GET, [this]() {
            Motor::setSpeed(-150, -150);
            server.send(200, "text/plain", "BACKWARD ON");
        });
        
        server.on("/cmd/motor/left", HTTP_GET, [this]() {
            Motor::setSpeed(-150, 150);
            server.send(200, "text/plain", "LEFT ON");
        });
        
        server.on("/cmd/motor/right", HTTP_GET, [this]() {
            Motor::setSpeed(150, -150);
            server.send(200, "text/plain", "RIGHT ON");
        });
        
        server.on("/cmd/motor/stop", HTTP_GET, [this]() {
            Motor::stop();
            server.send(200, "text/plain", "STOP");
        });
        
        server.begin();
        Logger::log("🌐 Web server: http://192.168.4.1");
    }
    
    void loop() { 
        server.handleClient(); 
    }

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
    <title>Car Evolution v5.4</title>
    <style>
        * { margin:0; padding:0; box-sizing:border-box; }
        body { font-family:Arial; background:#1a1a2e; padding:20px; color:#eee; max-width:600px; margin:0 auto; }
        .box { background:#16213e; padding:20px; border-radius:12px; margin:10px 0; }
        h1 { color:#e94560; text-align:center; }
        .grid { display:grid; grid-template-columns:1fr 1fr; gap:8px; margin:10px 0; }
        .btn { padding:12px; border-radius:8px; text-align:center; font-weight:bold; color:#fff; cursor:pointer; border:none; font-size:16px; width:100%; }
        .btn-green { background:#2ecc71; color:#1a1a2e; }
        .btn-red { background:#e74c3c; }
        .btn-orange { background:#f39c12; color:#1a1a2e; }
        .btn-blue { background:#3498db; }
        .btn-purple { background:#9c27b0; }
        .btn:active { opacity:0.7; }
        .info { display:flex; justify-content:space-between; padding:4px 0; border-bottom:1px solid #1a1a2e; }
        .info-label { color:#888; }
        .info-value { color:#00d4ff; font-weight:bold; }
        .motor-grid { display:grid; grid-template-columns:1fr 1fr 1fr; gap:6px; margin:8px 0; }
        .footer { font-size:12px; color:#666; text-align:center; margin-top:12px; }
        .footer span { color:#00d4ff; }
        .sensor-box { background:#0a0a1a; padding:8px 12px; border-radius:6px; margin:8px 0; }
        .sensor-row { display:flex; justify-content:space-between; font-size:14px; padding:2px 0; }
        .sensor-label { color:#888; }
        .sensor-high { color:#e94560; font-weight:bold; }
        .sensor-low { color:#2ecc71; font-weight:bold; }
        .motor-box { background:#0a0a1a; padding:8px 12px; border-radius:6px; margin:8px 0; }
        .motor-row { display:flex; justify-content:space-between; font-size:14px; padding:2px 0; }
        .motor-label { color:#888; }
        .motor-value { font-weight:bold; font-family:monospace; }
        .motor-forward { color:#2ecc71; }
        .motor-backward { color:#e74c3c; }
        .motor-stop { color:#666; }
        .raw-value { color:#555; font-size:11px; margin-left:8px; }
        .section-title { color:#888; font-size:12px; margin-bottom:4px; }
    </style>
</head>
<body>
<div class='box'>
    <h1>🚗 Evolution v5.4</h1>
    
    <div style='margin:10px 0;'>
        <div class='info'><span class='info-label'>状态</span><span class='info-value' id='state'>--</span></div>
        <div class='info'><span class='info-label'>世代</span><span class='info-value' id='generation'>--</span></div>
        <div class='info'><span class='info-label'>个体</span><span class='info-value' id='individual'>--</span></div>
        <div class='info'><span class='info-label'>测试时间</span><span class='info-value' id='test_time'>--</span></div>
        <div class='info'><span class='info-label'>编码器</span><span class='info-value' id='pulse'>--</span></div>
    </div>
    
    <div class='sensor-box'>
        <div class='section-title'>📡 传感器 (反转后)</div>
        <div class='sensor-row'>
            <span class='sensor-label'>⬅ 左传感器</span>
            <span id='sensor_L' class='sensor-low'>--</span>
            <span class='raw-value' id='raw_L'></span>
        </div>
        <div class='sensor-row'>
            <span class='sensor-label'>➡ 右传感器</span>
            <span id='sensor_R' class='sensor-low'>--</span>
            <span class='raw-value' id='raw_R'></span>
        </div>
    </div>
    
    <div class='motor-box'>
        <div class='section-title'>⚡ 电机PWM (正=前进)</div>
        <div class='motor-row'>
            <span class='motor-label'>⬅ 左电机</span>
            <span id='motor_L' class='motor-value motor-stop'>--</span>
        </div>
        <div class='motor-row'>
            <span class='motor-label'>➡ 右电机</span>
            <span id='motor_R' class='motor-value motor-stop'>--</span>
        </div>
    </div>
    
    <div class='grid'>
        <button class='btn btn-green' onclick='cmd("/start")'>▶ START</button>
        <button class='btn btn-red' onclick='cmd("/stop")'>⏹ STOP</button>
        <button class='btn btn-orange' onclick='cmd("/reset")'>🔄 RESET</button>
        <button class='btn btn-purple' onclick='window.open("/population")'>📊 数据</button>
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

<div class='footer'><span>✅ 传感器已反转</span> (遮挡=高值) | v5.4</div>

<script>
function cmd(url) {
    fetch(url)
        .then(r => r.text())
        .then(data => { 
            alert('✅ ' + data); 
            refresh(); 
        })
        .catch(err => alert('❌ 错误'));
}

function refresh() {
    fetch('/status')
        .then(r => r.text())
        .then(t => {
            const data = {};
            t.split('\n').forEach(l => {
                const p = l.split(':');
                if(p.length === 2) {
                    data[p[0].trim()] = p[1].trim();
                }
            });
            
            // 更新所有状态字段
            const fields = ['state', 'generation', 'individual', 'test_time', 'pulse'];
            fields.forEach(key => {
                const el = document.getElementById(key);
                if(el && data[key] !== undefined) {
                    el.textContent = data[key];
                }
            });
            
            // 更新传感器
            if(data['sensor_L'] !== undefined) {
                const el = document.getElementById('sensor_L');
                const val = parseInt(data['sensor_L']);
                el.textContent = val;
                el.className = val > 2000 ? 'sensor-high' : 'sensor-low';
            }
            if(data['sensor_R'] !== undefined) {
                const el = document.getElementById('sensor_R');
                const val = parseInt(data['sensor_R']);
                el.textContent = val;
                el.className = val > 2000 ? 'sensor-high' : 'sensor-low';
            }
            
            // 显示原始值
            if(data['raw_L'] !== undefined) {
                document.getElementById('raw_L').textContent = '(原始:' + data['raw_L'] + ')';
            }
            if(data['raw_R'] !== undefined) {
                document.getElementById('raw_R').textContent = '(原始:' + data['raw_R'] + ')';
            }
            
            // 更新电机速度
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
            
            // 更新状态文字
            if(data['running'] !== undefined && data['testing'] !== undefined) {
                const stateEl = document.getElementById('state');
                if(data['running'] === '1' && data['testing'] === '1') {
                    stateEl.textContent = '🏃 测试中';
                    stateEl.style.color = '#2ecc71';
                } else if(data['running'] === '1') {
                    stateEl.textContent = '⏳ 等待中';
                    stateEl.style.color = '#f39c12';
                } else {
                    stateEl.textContent = '⏹ 已停止';
                    stateEl.style.color = '#e74c3c';
                }
            }
        })
        .catch(err => console.error('刷新错误:', err));
}

// 每2秒刷新一次
setInterval(refresh, 2000);
// 立即刷新
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
    Logger::log("  Car Evolution v5.4 - Full Fixed");
    Logger::log("========================================");
    Logger::log("  ✅ 传感器反逻辑已修正");
    Logger::log("  ✅ 速度计算已优化");
    Logger::log("  ✅ 页面动态显示已修复");
    Logger::log("========================================");
    
    Motor::init();
    Sensors::init();
    Encoders::init();
    EEPROMStorage::init();
    
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