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

/*
 * ================================================================
 * 项目名称：仿草履虫应激机制 - 脱困能力进化系统
 * 版本：    v7.1 - 全面修复版（编译修复）
 * 适配日期: 2026-08-09
 * 
 * ================================================================
 * 【v7.1 修复内容】
 * ================================================================
 * 1. ✅ 修复电机转向定义（验证 HIGH/LOW/LOW/HIGH）
 * 2. ✅ 重新校准左右电机增益（消除右转偏航）
 * 3. ✅ 添加双轮转速差卡死检测（双维度判定）
 * 4. ✅ 修复碰撞计数器衰减逻辑
 * 5. ✅ 添加传感器基准值校准（启动时自动校准）
 * 6. ✅ 添加速度平滑过渡（每帧最大变化15）
 * 7. ✅ 添加转向死区（差值<50时直行）
 * 8. ✅ 修复混沌脱困后计数器重置
 * 9. ✅ 添加直行偏好（小偏差时优先直行）
 * 10. ✅ 修复摇摆检测阈值
 * 11. ✅ 修复编译错误（Gene前向声明 + totalDelta命名冲突）
 * ================================================================
 */

/*
 * ================================================================
 * 项目名称：仿草履虫应激机制 - 脱困能力进化系统
 * 版本：    v7.2 - 鲁棒持久化版
 * 适配日期: 2026-08-09
 * 
 * ================================================================
 * 【v7.2 核心改进】
 * ================================================================
 * 1. ✅ 鲁棒SPIFFS存储（3次挂载重试）
 * 2. ✅ 批量写入策略（每10秒或5条记录）
 * 3. ✅ 原子文件操作（临时文件+重命名）
 * 4. ✅ 写入失败3次重试
 * 5. ✅ 文件完整性验证
 * 6. ✅ 自动清理旧数据（保留500条）
 * 7. ✅ 电源中断保护
 * 8. ✅ 双缓冲区（RAM+Flash）
 * 9. ✅ 强制保存API
 * 10. ✅ 存储状态监控
 * 
 * ================================================================
 * 【为什么v7.2能保存成功】
 * ================================================================
 * - 原方案：每次记录都写Flash → 易失败
 * - v7.2：先存RAM，批量写入 → 高成功率
 * - 临时文件+原子重命名 → 断电不损坏原文件
 * - 三重写入重试 → 临时失败自动恢复
 * ================================================================
 */

/*
 * ================================================================
 * 项目名称：仿草履虫应激机制 - 脱困能力进化系统
 * 版本：    v7.2 - 鲁棒持久化版（完整编译通过）
 * 适配日期: 2026-08-09
 * 
 * ================================================================
 * 【进化流程 - 完整步骤】
 * ================================================================
 * 
 * 第一步：系统初始化 (setup)
 *   ├── 1.1 串口初始化 (Logger)
 *   ├── 1.2 SPIFFS挂载 (3次重试)
 *   ├── 1.3 加载历史数据 (从Flash恢复)
 *   ├── 1.4 电机初始化
 *   ├── 1.5 传感器校准 (自动测基准值)
 *   ├── 1.6 编码器初始化
 *   ├── 1.7 种群初始化 (16个个体)
 *   ├── 1.8 WiFi AP启动 (192.168.4.1)
 *   └── 1.9 Web服务器启动
 * 
 * 第二步：进化循环 (loop → engine.update)
 *   ├── 2.1 载入个体 (STATE_LOAD)
 *   │   └── 重置编码器、计数器、加载基因参数
 *   │
 *   ├── 2.2 生存测试 (STATE_TEST)
 *   │   ├── 读取左右传感器ADC值
 *   │   ├── 计算障碍等级 (安全/注意/障碍/危险)
 *   │   ├── 转向逻辑 (左侧障碍→右转，右侧障碍→左转)
 *   │   ├── 速度控制 (越近越慢)
 *   │   ├── 困境检测 (每帧检查)
 *   │   │   ├── 卡死检测 (绝对卡死 + 旋转卡死)
 *   │   │   ├── 持续碰撞检测 (带衰减)
 *   │   │   ├── 反复摇摆检测
 *   │   │   └── 探索停滞检测
 *   │   └── 触发混沌 或 测试完成(30秒)
 *   │
 *   ├── 2.3 混沌干预 (STATE_CHAOS)
 *   │   ├── 选择策略 (基于历史成功率)
 *   │   ├── 执行策略 (6种：直冲/旋转/随机/转向/后退/脉冲)
 *   │   ├── 脱困判定 (传感器清空 或 编码器大幅移动)
 *   │   ├── 记录脱困数据 (脱困率、脱困时间)
 *   │   └── 超时判定 (15步未脱困)
 *   │
 *   ├── 2.4 后退复位 (STATE_BACKWARD_RESET)
 *   │   └── 测试完成后后退2秒，避免卡在障碍物上
 *   │
 *   ├── 2.5 评分记录 (STATE_SCORE)
 *   │   ├── 计算适应度 = 脱困率×0.7 + 速度×0.3
 *   │   ├── 记录到RAM历史
 *   │   └── 自动保存到Flash (批量写入)
 *   │
 *   ├── 2.6 种群评估 (STATE_EVALUATE)
 *   │   ├── 计算所有个体适应度
 *   │   ├── 按适应度排序
 *   │   └── 统计平均值、最大值、标准差
 *   │
 *   └── 2.7 繁殖下一代 (STATE_REPRODUCE)
 *       ├── 精英保留 (fitness≥0.4的个体)
 *       ├── 杂交 (精英个体交叉配对)
 *       ├── 变异 (引入随机噪声)
 *       └── 新世代开始
 * 
 * 第三步：数据持久化 (自动)
 *   ├── 每10秒 或 每5条记录 → 批量写入Flash
 *   ├── 写入方式：临时文件 → 原子重命名
 *   ├── 写入失败：3次自动重试
 *   └── 停止时：强制保存所有未写入数据
 * 
 * 第四步：Web监控 (http://192.168.4.1)
 *   ├── 实时状态查看 (世代/个体/传感器/电机)
 *   ├── 种群数据导出 (CSV)
 *   ├── 历史数据导出 (全部/最近N代)
 *   ├── 存储状态监控
 *   ├── 强制保存按钮
 *   └── 手动电机控制 (前进/后退/左转/右转/停止)
 * 
 * ================================================================
 * 【v7.2 核心改进】
 * ================================================================
 * 1. ✅ 鲁棒SPIFFS存储（3次挂载重试）
 * 2. ✅ 批量写入策略（每10秒或5条记录）
 * 3. ✅ 原子文件操作（临时文件+重命名）
 * 4. ✅ 写入失败3次重试
 * 5. ✅ 文件完整性验证
 * 6. ✅ 自动清理旧数据（保留500条）
 * 7. ✅ 电源中断保护
 * 8. ✅ 双缓冲区（RAM+Flash）
 * 9. ✅ 强制保存API
 * 10. ✅ 存储状态监控
 * 11. ✅ 修复所有编译错误
 * ================================================================
 */

#include <WiFi.h>
#include <WebServer.h>
#include <FS.h>
#include <SPIFFS.h>
#include <vector>
#include <algorithm>
#include <cstdarg>
#include <cmath>

// ===================== 硬件引脚宏 =====================
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
#define TEST_DURATION_MS    30000
#define STUCK_THRESHOLD_MS  2000

// ===================== 卡死检测参数 =====================
#define SPINNING_STUCK_SPEED_DIFF  70
#define SPINNING_STUCK_MIN_SPEED   20
#define SPINNING_STUCK_PULSE_LIMIT 10

// ===================== 平滑过渡参数 =====================
#define MAX_SPEED_CHANGE_PER_FRAME 15

// ===================== 转向死区 =====================
#define SENSOR_DIFF_DEAD_ZONE  50

// ===================== 混沌触发参数 =====================
#define CHAOS_MIN_INTERVAL_MS   5000
#define COLLISION_THRESHOLD     8
#define SWING_THRESHOLD         4
#define EXPLORE_STALL_MS        3000
#define CHAOS_SAMPLE_COUNT      15
#define CHAOS_STEP_MS           100
#define MAX_HISTORY_GENERATIONS 1000
#define BACKWARD_RESET_DURATION 2000
#define BACKWARD_SPEED -80

// ===================== 存储参数 =====================
#define STORAGE_SAVE_INTERVAL_MS  10000
#define STORAGE_BATCH_THRESHOLD   5
#define STORAGE_MAX_RETRIES       3
#define STORAGE_MAX_HISTORY_SIZE  500

// ===================== GP2Y0A21YK0F ADC阈值 =====================
#define CLEAR_ADC_THRESH       600
#define OBSTACLE_ADC_THRESH    1400
#define DANGER_ADC_THRESH      2600

// ===================== 困境类型枚举 =====================
enum DilemmaType {
    DILEMMA_NONE = 0,
    DILEMMA_STUCK,
    DILEMMA_COLLISION,
    DILEMMA_SWING,
    DILEMMA_EXPLORE_STALL
};

// ===================== 前向声明 =====================
struct Gene;
struct HistoryRecord;

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

// ===================== HistoryRecord 结构体 =====================
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

// ===================== 全局历史数组 =====================
HistoryRecord ramHistory[MAX_HISTORY_GENERATIONS];
int historyCount = 0;

// ===================== 鲁棒存储系统 =====================
class RobustStorage {
private:
    static const char* HISTORY_FILE;
    static const char* TEMP_FILE;
    static bool fsReady;
    static bool isFlushing;
    static uint32_t lastSaveTime;
    static uint32_t unsavedCount;
    static uint32_t totalSaved;
    static uint32_t totalFailed;
    static String pendingBuffer;
    static const size_t MAX_BUFFER_SIZE = 4096;
    
public:
    static void init() {
        fsReady = false;
        isFlushing = false;
        lastSaveTime = 0;
        unsavedCount = 0;
        totalSaved = 0;
        totalFailed = 0;
        pendingBuffer.reserve(MAX_BUFFER_SIZE);
        
        Logger::log("🔧 初始化SPIFFS...");
        bool mounted = false;
        for (int attempt = 0; attempt < STORAGE_MAX_RETRIES; attempt++) {
            if (SPIFFS.begin(true)) {
                mounted = true;
                break;
            }
            Logger::logf("⚠️ 挂载失败，重试 %d/%d", attempt + 1, STORAGE_MAX_RETRIES);
            delay(500);
        }
        
        if (!mounted) {
            Logger::log("❌ SPIFFS挂载失败！数据仅存RAM");
            return;
        }
        
        fsReady = true;
        Logger::log("✅ SPIFFS挂载成功");
        checkStorageHealth();
        int loaded = loadHistoryToRAM();
        Logger::logf("📂 从Flash加载 %d 条历史记录", loaded);
        printStorageStatus();
    }
    
    static void checkStorageHealth() {
        if (!fsReady) return;
        size_t total = SPIFFS.totalBytes();
        size_t used = SPIFFS.usedBytes();
        float usage = (float)used / total * 100;
        Logger::logf("💾 Flash: %u KB / %u KB (%.1f%%)", used/1024, total/1024, usage);
        if (usage > 90) {
            Logger::log("⚠️ Flash使用率过高！");
            pruneOldHistory();
        }
        if (SPIFFS.exists(HISTORY_FILE)) {
            File f = SPIFFS.open(HISTORY_FILE, "r");
            if (!f) {
                Logger::log("⚠️ 历史文件损坏，重新创建");
                SPIFFS.remove(HISTORY_FILE);
            } else {
                size_t size = f.size();
                f.close();
                if (size > 1024 * 1024) {
                    Logger::log("⚠️ 历史文件过大，压缩中...");
                    pruneOldHistory();
                }
            }
        }
    }
    
    static void addRecord(const HistoryRecord& record) {
        char line[256];
        snprintf(line, sizeof(line), 
            "%lu,%lu,%.3f,%d,%d,%d,%d,%.3f,%lu,%lu,%d\n",
            millis(),
            record.generation,
            record.fitnessScore,
            record.k_turn,
            record.speed_bias,
            record.chaos_total,
            record.chaos_escaped,
            record.escape_rate,
            record.avg_escape_time_ms,
            record.fastest_escape_ms,
            record.preferred_action
        );
        
        pendingBuffer += String(line);
        unsavedCount++;
        totalSaved++;
        
        if (unsavedCount >= STORAGE_BATCH_THRESHOLD || 
            pendingBuffer.length() > MAX_BUFFER_SIZE / 2) {
            flushBuffer();
        }
    }
    
    static bool flushBuffer() {
        if (!fsReady || isFlushing) return false;
        if (pendingBuffer.length() == 0) {
            unsavedCount = 0;
            return true;
        }
        
        isFlushing = true;
        bool success = false;
        
        for (int retry = 0; retry < STORAGE_MAX_RETRIES; retry++) {
            if (doAtomicWrite()) {
                success = true;
                break;
            }
            Logger::logf("⚠️ 写入失败，重试 %d/%d", retry + 1, STORAGE_MAX_RETRIES);
            delay(100 * (retry + 1));
        }
        
        if (success) {
            pendingBuffer = "";
            unsavedCount = 0;
            lastSaveTime = millis();
        } else {
            totalFailed += unsavedCount;
            Logger::logf("❌ 写入失败，丢失 %d 条记录", unsavedCount);
        }
        
        isFlushing = false;
        return success;
    }
    
    static bool doAtomicWrite() {
        if (!fsReady) return false;
        if (pendingBuffer.length() == 0) return true;
        
        File temp = SPIFFS.open(TEMP_FILE, "w");
        if (!temp) {
            Logger::log("❌ 无法创建临时文件");
            return false;
        }
        
        if (SPIFFS.exists(HISTORY_FILE)) {
            File src = SPIFFS.open(HISTORY_FILE, "r");
            if (src) {
                uint8_t buffer[512];
                while (src.available()) {
                    size_t n = src.read(buffer, sizeof(buffer));
                    temp.write(buffer, n);
                }
                src.close();
            }
        }
        
        size_t written = temp.print(pendingBuffer);
        temp.close();
        
        if (written != pendingBuffer.length()) {
            SPIFFS.remove(TEMP_FILE);
            return false;
        }
        
        if (SPIFFS.exists(HISTORY_FILE)) {
            SPIFFS.remove(HISTORY_FILE);
        }
        if (!SPIFFS.rename(TEMP_FILE, HISTORY_FILE)) {
            return false;
        }
        
        File verify = SPIFFS.open(HISTORY_FILE, "r");
        if (!verify) return false;
        size_t finalSize = verify.size();
        verify.close();
        return finalSize > 0;
    }
    
    static int loadHistoryToRAM() {
        if (!fsReady) return 0;
        if (!SPIFFS.exists(HISTORY_FILE)) return 0;
        
        File file = SPIFFS.open(HISTORY_FILE, "r");
        if (!file) return 0;
        
        size_t fileSize = file.size();
        if (fileSize == 0) {
            file.close();
            SPIFFS.remove(HISTORY_FILE);
            return 0;
        }
        
        int count = 0;
        String line;
        while (file.available() && count < MAX_HISTORY_GENERATIONS) {
            line = file.readStringUntil('\n');
            line.trim();
            if (line.length() < 20) continue;
            
            int fields[11] = {0};
            int idx = 0;
            int start = 0;
            for (int i = 0; i < line.length() && idx < 11; i++) {
                if (line[i] == ',') {
                    fields[idx++] = line.substring(start, i).toInt();
                    start = i + 1;
                }
            }
            
            if (idx >= 10) {
                ramHistory[count].generation = fields[1];
                ramHistory[count].fitnessScore = fields[2];
                ramHistory[count].k_turn = fields[3];
                ramHistory[count].speed_bias = fields[4];
                ramHistory[count].chaos_total = fields[5];
                ramHistory[count].chaos_escaped = fields[6];
                ramHistory[count].escape_rate = fields[7];
                ramHistory[count].avg_escape_time_ms = fields[8];
                ramHistory[count].fastest_escape_ms = fields[9];
                ramHistory[count].preferred_action = fields[10];
                count++;
            }
        }
        file.close();
        return count;
    }
    
    static bool pruneOldHistory() {
        if (!fsReady) return false;
        if (!SPIFFS.exists(HISTORY_FILE)) return true;
        
        File file = SPIFFS.open(HISTORY_FILE, "r");
        if (!file) return false;
        
        std::vector<String> lines;
        lines.reserve(STORAGE_MAX_HISTORY_SIZE + 10);
        while (file.available()) {
            String line = file.readStringUntil('\n');
            if (line.length() > 10) lines.push_back(line);
        }
        file.close();
        
        if (lines.size() <= STORAGE_MAX_HISTORY_SIZE) return true;
        
        int keep = STORAGE_MAX_HISTORY_SIZE;
        int start = lines.size() - keep;
        
        File newFile = SPIFFS.open(TEMP_FILE, "w");
        if (!newFile) return false;
        for (int i = start; i < lines.size(); i++) {
            newFile.println(lines[i]);
        }
        newFile.close();
        
        SPIFFS.remove(HISTORY_FILE);
        SPIFFS.rename(TEMP_FILE, HISTORY_FILE);
        Logger::logf("🗑️ 清理历史: %d → %d 条", lines.size(), keep);
        return true;
    }
    
    static void forceSave() {
        if (pendingBuffer.length() > 0) {
            Logger::log("💾 强制保存...");
            flushBuffer();
        }
    }
    
    static void printStorageStatus() {
        if (!fsReady) {
            Logger::log("📊 存储状态: 未就绪 (仅RAM)");
            return;
        }
        Logger::logf("📊 存储状态: 已保存 %lu 条, 待保存 %lu 条, 失败 %lu 次",
                     totalSaved, unsavedCount, totalFailed);
        size_t total = SPIFFS.totalBytes();
        size_t used = SPIFFS.usedBytes();
        Logger::logf("📊 Flash: %u KB / %u KB (%.1f%%)", used/1024, total/1024, (float)used/total*100);
    }
    
    static bool isReady() { return fsReady; }
    static bool hasPending() { return pendingBuffer.length() > 0; }
    static uint32_t getPendingCount() { return unsavedCount; }
    static uint32_t getTotalSaved() { return totalSaved; }
    static uint32_t getTotalFailed() { return totalFailed; }
    
    static void tick() {
        if (!fsReady) return;
        uint32_t now = millis();
        if (now - lastSaveTime > STORAGE_SAVE_INTERVAL_MS && pendingBuffer.length() > 0) {
            flushBuffer();
        }
    }
};

// ===================== 静态成员初始化 =====================
const char* RobustStorage::HISTORY_FILE = "/evo_history.csv";
const char* RobustStorage::TEMP_FILE = "/evo_temp.tmp";
bool RobustStorage::fsReady = false;
bool RobustStorage::isFlushing = false;
uint32_t RobustStorage::lastSaveTime = 0;
uint32_t RobustStorage::unsavedCount = 0;
uint32_t RobustStorage::totalSaved = 0;
uint32_t RobustStorage::totalFailed = 0;
String RobustStorage::pendingBuffer = "";

// ===================== 传感器校准 =====================
class SensorCalibration {
private:
    static int leftBase, rightBase;
    
public:
    static void calibrate() {
        Logger::log("🔧 传感器校准中... (请确保无遮挡)");
        delay(1000);
        const int samples = 50;
        long sumL = 0, sumR = 0;
        for (int i = 0; i < samples; i++) {
            sumL += analogRead(PIN_SENSOR_LEFT);
            sumR += analogRead(PIN_SENSOR_RIGHT);
            delay(10);
        }
        leftBase = sumL / samples;
        rightBase = sumR / samples;
        Logger::logf("✅ 校准完成: 左=%d 右=%d", leftBase, rightBase);
    }
    static int getLeft() { return analogRead(PIN_SENSOR_LEFT) - leftBase; }
    static int getRight() { return analogRead(PIN_SENSOR_RIGHT) - rightBase; }
    static int getLeftRaw() { return analogRead(PIN_SENSOR_LEFT); }
    static int getRightRaw() { return analogRead(PIN_SENSOR_RIGHT); }
    static int getObstacleLevel() {
        int l = getLeft(), r = getRight();
        int maxVal = max(l, r);
        if (maxVal > DANGER_ADC_THRESH - leftBase) return 3;
        if (maxVal > OBSTACLE_ADC_THRESH - leftBase) return 2;
        if (maxVal > CLEAR_ADC_THRESH - leftBase) return 1;
        return 0;
    }
    static bool hasObstacle() {
        return (getLeft() > OBSTACLE_ADC_THRESH - leftBase || 
                getRight() > OBSTACLE_ADC_THRESH - leftBase);
    }
    static bool isClear() {
        return (getLeft() < CLEAR_ADC_THRESH - leftBase && 
                getRight() < CLEAR_ADC_THRESH - leftBase);
    }
    static bool isDanger() {
        return (getLeft() > DANGER_ADC_THRESH - leftBase || 
                getRight() > DANGER_ADC_THRESH - leftBase);
    }
};
int SensorCalibration::leftBase = 0;
int SensorCalibration::rightBase = 0;

// ===================== 电机控制 =====================
class Motor {
private:
    static int leftSpeed, rightSpeed, targetLeftSpeed, targetRightSpeed;
public:
    static void init() {
        leftSpeed = rightSpeed = targetLeftSpeed = targetRightSpeed = 0;
        pinMode(PIN_LEFT_PWM, OUTPUT); pinMode(PIN_LEFT_DIR2, OUTPUT);
        pinMode(PIN_RIGHT_PWM, OUTPUT); pinMode(PIN_RIGHT_DIR2, OUTPUT);
        ledcAttach(PIN_LEFT_PWM, 5000, 8);
        ledcAttach(PIN_RIGHT_PWM, 5000, 8);
        ledcWrite(PIN_LEFT_PWM, 0);
        ledcWrite(PIN_RIGHT_PWM, 0);
        stop();
    }
    static void stop() {
        leftSpeed = rightSpeed = targetLeftSpeed = targetRightSpeed = 0;
        digitalWrite(PIN_LEFT_PWM, LOW); digitalWrite(PIN_LEFT_DIR2, LOW);
        digitalWrite(PIN_RIGHT_PWM, LOW); digitalWrite(PIN_RIGHT_DIR2, LOW);
        ledcWrite(PIN_LEFT_PWM, 0); ledcWrite(PIN_RIGHT_PWM, 0);
    }
    static void setSpeed(int left, int right) {
        const float leftGain = 1.15f, rightGain = 1.05f;
        targetLeftSpeed = constrain(left, -255, 255);
        targetRightSpeed = constrain(right, -255, 255);
        
        int deltaL = targetLeftSpeed - leftSpeed;
        int deltaR = targetRightSpeed - rightSpeed;
        if (abs(deltaL) > MAX_SPEED_CHANGE_PER_FRAME) {
            leftSpeed += (deltaL > 0 ? MAX_SPEED_CHANGE_PER_FRAME : -MAX_SPEED_CHANGE_PER_FRAME);
        } else { leftSpeed = targetLeftSpeed; }
        if (abs(deltaR) > MAX_SPEED_CHANGE_PER_FRAME) {
            rightSpeed += (deltaR > 0 ? MAX_SPEED_CHANGE_PER_FRAME : -MAX_SPEED_CHANGE_PER_FRAME);
        } else { rightSpeed = targetRightSpeed; }
        
        int pwmL = constrain((int)round(leftSpeed * leftGain), -255, 255);
        int pwmR = constrain((int)round(rightSpeed * rightGain), -255, 255);
        
        if (pwmL > 0) { digitalWrite(PIN_LEFT_DIR2, LEFT_FORWARD); ledcWrite(PIN_LEFT_PWM, pwmL); }
        else if (pwmL < 0) { digitalWrite(PIN_LEFT_DIR2, LEFT_REVERSE); ledcWrite(PIN_LEFT_PWM, -pwmL); }
        else { digitalWrite(PIN_LEFT_DIR2, LOW); ledcWrite(PIN_LEFT_PWM, 0); }
        
        if (pwmR > 0) { digitalWrite(PIN_RIGHT_DIR2, RIGHT_FORWARD); ledcWrite(PIN_RIGHT_PWM, pwmR); }
        else if (pwmR < 0) { digitalWrite(PIN_RIGHT_DIR2, RIGHT_REVERSE); ledcWrite(PIN_RIGHT_PWM, -pwmR); }
        else { digitalWrite(PIN_RIGHT_DIR2, LOW); ledcWrite(PIN_RIGHT_PWM, 0); }
    }
    static int getLeftSpeed() { return leftSpeed; }
    static int getRightSpeed() { return rightSpeed; }
};
int Motor::leftSpeed = 0, Motor::rightSpeed = 0;
int Motor::targetLeftSpeed = 0, Motor::targetRightSpeed = 0;

// ===================== 编码器 =====================
volatile int32_t leftEncoderCount = 0, rightEncoderCount = 0, totalPulseCount = 0;
volatile uint16_t obstacleInterruptCount = 0;

void IRAM_ATTR leftEncoderISR() {
    if (digitalRead(PIN_LEFT_ENC_B) == HIGH) leftEncoderCount++;
    else leftEncoderCount--;
    totalPulseCount++;
}
void IRAM_ATTR rightEncoderISR() {
    if (digitalRead(PIN_RIGHT_ENC_B) == HIGH) rightEncoderCount++;
    else rightEncoderCount--;
    totalPulseCount++;
}
void IRAM_ATTR obstacleISR() { obstacleInterruptCount++; }

class Encoders {
private:
    static int32_t lastLeftCount, lastRightCount;
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
        leftEncoderCount = rightEncoderCount = totalPulseCount = 0;
        obstacleInterruptCount = 0;
        lastLeftCount = lastRightCount = 0;
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
    static int getLeftSpeed() {
        static int32_t last = 0; static uint32_t lastTime = 0;
        uint32_t now = millis();
        if (now - lastTime < 100) return 0;
        int speed = (leftEncoderCount - last) * 1000 / (now - lastTime);
        last = leftEncoderCount; lastTime = now;
        return speed;
    }
    static int getRightSpeed() {
        static int32_t last = 0; static uint32_t lastTime = 0;
        uint32_t now = millis();
        if (now - lastTime < 100) return 0;
        int speed = (rightEncoderCount - last) * 1000 / (now - lastTime);
        last = rightEncoderCount; lastTime = now;
        return speed;
    }
    static bool isStuck(int& stuckType) {
        int32_t lDelta = leftDelta(), rDelta = rightDelta();
        uint32_t totalDeltaVal = totalDelta();
        bool absoluteStuck = (abs(lDelta) < 2 && abs(rDelta) < 2);
        int leftSpd = getLeftSpeed(), rightSpd = getRightSpeed();
        int speedDiff = abs(leftSpd - rightSpd);
        int minSpeed = min(abs(leftSpd), abs(rightSpd));
        bool spinningStuck = (speedDiff > SPINNING_STUCK_SPEED_DIFF && 
                             minSpeed > SPINNING_STUCK_MIN_SPEED && 
                             totalDeltaVal < SPINNING_STUCK_PULSE_LIMIT);
        if (absoluteStuck) { stuckType = 0; return true; }
        else if (spinningStuck) { stuckType = 1; return true; }
        return false;
    }
};
int32_t Encoders::lastLeftCount = 0, Encoders::lastRightCount = 0;
uint32_t Encoders::lastTotalCount = 0;

// ===================== 传感器类 =====================
class Sensors {
public:
    static void init() {
        pinMode(PIN_SENSOR_LEFT, INPUT);
        pinMode(PIN_SENSOR_RIGHT, INPUT);
        pinMode(PIN_NOISE_SOURCE, INPUT);
        SensorCalibration::calibrate();
        Logger::log("✅ Sensors initialized");
    }
    static int left() { return SensorCalibration::getLeft(); }
    static int right() { return SensorCalibration::getRight(); }
    static int leftRaw() { return SensorCalibration::getLeftRaw(); }
    static int rightRaw() { return SensorCalibration::getRightRaw(); }
    static uint16_t readNoise() { return analogRead(PIN_NOISE_SOURCE); }
    static int getObstacleLevel() { return SensorCalibration::getObstacleLevel(); }
    static bool hasObstacle() { return SensorCalibration::hasObstacle(); }
    static bool isClear() { return SensorCalibration::isClear(); }
    static bool isDanger() { return SensorCalibration::isDanger(); }
};

// ===================== Gene结构体 =====================
struct Gene {
    uint16_t id;
    int8_t k_turn;
    uint8_t speed_bias;
    uint32_t generation;
    uint8_t chaos_total;
    uint8_t chaos_escaped;
    float escape_rate;
    uint32_t avg_escape_time_ms;
    uint32_t last_escape_time_ms;
    uint32_t fastest_escape_ms;
    uint32_t previous_chaos_start;
    uint8_t preferred_action;
    uint8_t action_history[10];
    uint32_t survival_ms;
    uint32_t distance_ticks;
    uint16_t obstacle_count;
    
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
    void recordAction(uint8_t action) {
        for (int i = 9; i > 0; i--) action_history[i] = action_history[i-1];
        action_history[0] = action;
        uint8_t counts[6] = {0};
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

// ===================== recordToRAM =====================
void recordToRAM(const Gene& g, uint32_t gen) {
    if (historyCount >= MAX_HISTORY_GENERATIONS) {
        Logger::log("⚠️ RAM历史缓冲区已满！");
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
    
    RobustStorage::addRecord(ramHistory[historyCount]);
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
            genes[i].chaos_total = 0;
            genes[i].chaos_escaped = 0;
            genes[i].escape_rate = 0.0f;
            genes[i].avg_escape_time_ms = 0;
            genes[i].last_escape_time_ms = 0;
            genes[i].fastest_escape_ms = 999999;
            genes[i].previous_chaos_start = 0;
            genes[i].preferred_action = 0;
            for (int j = 0; j < 10; j++) genes[i].action_history[j] = 0;
        }
        Logger::log("✅ Population initialized");
    }
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
    int stuckType = 0;
    bool inChaos = false;
    uint32_t chaosStart = 0;
    int chaosStep = 0;
    bool chaosEscaped = false;
    uint32_t lastChaosTriggerTime = 0;
    DilemmaType currentDilemma = DILEMMA_NONE;
    uint8_t currentChaosStrategy = 0;
    uint16_t collisionCounter = 0;
    uint16_t swingCounter = 0;
    uint32_t lastObstacleChange = 0;
    
    struct StrategyStats {
        uint8_t strategy;
        uint32_t successCount;
        uint32_t totalAttempts;
        float successRate;
    };
    StrategyStats strategyStats[6];
    
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
    
    enum RunPhase { PHASE_IDLE, PHASE_NORMAL_TEST, PHASE_CHAOS };
    
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
        Logger::logf("✅ Evolution v7.2 initialized, generation %lu", currentGen);
    }
    
    void start() { running = true; currentIdx = 0; state = STATE_LOAD; Logger::log("▶ Evolution started"); }
    void stop() {
        running = false;
        testRunning = false;
        Motor::stop();
        if (RobustStorage::hasPending()) {
            Logger::log("💾 停止前保存数据...");
            RobustStorage::forceSave();
        }
        RobustStorage::printStorageStatus();
        Logger::log("⏹ Evolution stopped");
    }
    void setControlMode(bool enable) { controlMode = enable; }
    
    void update() {
        if (!running) return;
        switch (state) {
            case STATE_LOAD: loadNextGene(); break;
            case STATE_TEST: runTest(); break;
            case STATE_CHAOS: runChaos(); break;
            case STATE_BACKWARD_RESET: {
                if (millis() - backwardResetStart >= BACKWARD_RESET_DURATION) {
                    Motor::stop();
                    state = STATE_SCORE;
                }
                break;
            }
            case STATE_SCORE: calcScore(); break;
            case STATE_EVALUATE: evaluate(); break;
            case STATE_REPRODUCE: reproduce(); break;
        }
    }
    
    Gene& currentGene() { return pop.genes[currentIdx]; }
    uint32_t generation() { return currentGen; }
    bool isRunning() { return running; }
    bool isTesting() { return testRunning; }
    uint32_t testTime() { return testRunning ? (millis() - testStart) : 0; }
    RunPhase getCurrentPhase() {
        if (!running || !testRunning) return PHASE_IDLE;
        if (inChaos) return PHASE_CHAOS;
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
        const char* names[] = {"全速直冲", "原地旋转", "随机运动", "强力转向", "后退冲刺", "脉冲震荡"};
        return (strategy < 6) ? names[strategy] : "未知";
    }
    void getStatistics(float& avg, float& maxVal, float& minVal, float& stddev) {
        float sum = 0; maxVal = 0; minVal = 999.0f;
        for (int i = 0; i < POPULATION_SIZE; i++) {
            float f = pop.genes[i].getFitness();
            sum += f;
            if (f > maxVal) maxVal = f;
            if (f < minVal) minVal = f;
        }
        avg = sum / POPULATION_SIZE;
        float variance = 0;
        for (int i = 0; i < POPULATION_SIZE; i++) {
            float diff = pop.genes[i].getFitness() - avg;
            variance += diff * diff;
        }
        stddev = sqrt(variance / POPULATION_SIZE);
    }

private:
    void loadNextGene() {
        Logger::logf("📂 Loading gene %d/%d", currentIdx + 1, POPULATION_SIZE);
        Gene& g = currentGene();
        Encoders::reset();
        lastTotalPulse = 0;
        stuckStart = 0;
        stuckType = 0;
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
        g.previous_chaos_start = 0;
        state = STATE_TEST;
        Logger::logf("🧬 Gene ID:%d k:%d speed:%d", g.id, g.k_turn, g.speed_bias);
    }
    
    void runTest() {
        if (!testRunning) { testRunning = true; testStart = millis(); Logger::log("🏃 Test started"); }
        Gene& g = currentGene();
        uint32_t now = millis();
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
            Logger::log("✅ Test complete, backward reset");
            return;
        }
        
        if (!inChaos) {
            int left = Sensors::left(), right = Sensors::right();
            int level = Sensors::getObstacleLevel();
            int baseSpeed = (level == 3) ? 40 : (level == 2) ? 60 : (level == 1) ? 80 : 100;
            int diff = left - right;
            int pwmL, pwmR;
            
            if (abs(diff) < SENSOR_DIFF_DEAD_ZONE) {
                int wander = (random(0, 100) < 10) ? random(-10, 11) : 0;
                pwmL = baseSpeed + wander;
                pwmR = baseSpeed - wander;
            } else if (level >= 2) {
                float ratio = constrain(abs(diff) / 2500.0, 0.0, 1.0);
                int turn = baseSpeed * ratio * 0.35;
                if (diff > 50) { pwmL = baseSpeed - turn; pwmR = baseSpeed + turn; }
                else if (diff < -50) { pwmL = baseSpeed + turn; pwmR = baseSpeed - turn; }
                else { pwmL = baseSpeed; pwmR = baseSpeed; }
            } else {
                float ratio = constrain(abs(diff) / 2000.0, 0.0, 1.0);
                int turn = baseSpeed * ratio * 0.1;
                if (diff > 30) { pwmL = baseSpeed - turn; pwmR = baseSpeed + turn; }
                else if (diff < -30) { pwmL = baseSpeed + turn; pwmR = baseSpeed - turn; }
                else { pwmL = baseSpeed; pwmR = baseSpeed; }
            }
            
            Motor::setSpeed(constrain(pwmL, 20, 200), constrain(pwmR, 20, 200));
            static uint32_t lastDebug = 0;
            if (now - lastDebug > 3000) { lastDebug = now; Logger::logf("🔍 L:%d R:%d PWM:%d/%d", left, right, pwmL, pwmR); }
        }
    }
    
    DilemmaType detectDilemma() {
        uint32_t now = millis();
        int left = Sensors::left(), right = Sensors::right();
        bool isDanger = Sensors::isDanger();
        int stuckTypeLocal = 0;
        
        if (Encoders::isStuck(stuckTypeLocal)) {
            if (stuckStart == 0) { stuckStart = now; stuckType = stuckTypeLocal; }
            if (now - stuckStart > STUCK_THRESHOLD_MS) {
                Logger::logf("⛔ 卡死: %s", stuckTypeLocal == 0 ? "绝对" : "旋转");
                return DILEMMA_STUCK;
            }
        } else { stuckStart = 0; }
        
        if (isDanger) {
            collisionCounter++;
            if (collisionCounter > COLLISION_THRESHOLD) return DILEMMA_COLLISION;
        } else if (collisionCounter > 0) { collisionCounter = max(0, (int)collisionCounter - 1); }
        
        static int lastLD = 0, lastRD = 0;
        int lSpd = Motor::getLeftSpeed(), rSpd = Motor::getRightSpeed();
        int lDir = (lSpd > 10) ? 1 : (lSpd < -10) ? -1 : 0;
        int rDir = (rSpd > 10) ? 1 : (rSpd < -10) ? -1 : 0;
        if (lDir != 0 && rDir != 0 && (lDir != lastLD || rDir != lastRD)) {
            swingCounter++;
            if (swingCounter > SWING_THRESHOLD && Encoders::totalDelta() < 15) return DILEMMA_SWING;
        } else { swingCounter = max(0, (int)swingCounter - 1); }
        lastLD = lDir; lastRD = rDir;
        
        if (Sensors::hasObstacle()) {
            if (lastObstacleChange == 0) lastObstacleChange = now;
            static int lastLV = 0, lastRV = 0;
            if (abs(left - lastLV) > 50 || abs(right - lastRV) > 50) {
                lastObstacleChange = now;
                lastLV = left; lastRV = right;
            }
            if (now - lastObstacleChange > EXPLORE_STALL_MS) return DILEMMA_EXPLORE_STALL;
        } else { lastObstacleChange = now; }
        
        return DILEMMA_NONE;
    }
    
    uint8_t selectChaosStrategy(DilemmaType dilemma, Gene& g) {
        uint16_t noise = Sensors::readNoise();
        if (g.preferred_action < 6 && strategyStats[g.preferred_action].successRate > 0.3f) {
            return g.preferred_action;
        }
        uint8_t rec = 0;
        switch(dilemma) {
            case DILEMMA_STUCK: rec = (noise & 1) ? 3 : 4; break;
            case DILEMMA_COLLISION: rec = (noise & 1) ? 4 : 0; break;
            case DILEMMA_SWING: rec = (noise & 1) ? 5 : 2; break;
            case DILEMMA_EXPLORE_STALL: rec = (noise & 1) ? 2 : 0; break;
            default: {
                float total = 0;
                for (int i = 0; i < 6; i++) total += strategyStats[i].successRate + 0.1f;
                float randVal = (noise % 1000) / 1000.0f, cum = 0;
                for (int i = 0; i < 6; i++) {
                    cum += (strategyStats[i].successRate + 0.1f) / total;
                    if (randVal <= cum) { rec = i; break; }
                }
                break;
            }
        }
        if ((noise & 3) == 0) { rec = (noise >> 2) & 7; if (rec >= 6) rec = 6 - rec; }
        return rec;
    }
    
    void executeChaosStrategy(uint8_t strategy, int intensity) {
        float s = intensity / 100.0f;
        switch(strategy) {
            case 0: Motor::setSpeed(140*s, 140*s); break;
            case 1: { int dir = (millis()/500)%2?1:-1; Motor::setSpeed(dir*130*s, -dir*130*s); break; }
            case 2: Motor::setSpeed(random(-120*s,121*s), random(-120*s,121*s)); break;
            case 3: { int dir = (millis()/300)%2?1:-1; Motor::setSpeed(dir*140*s, -dir*140*s*0.8); break; }
            case 4: Motor::setSpeed(-130*s, -130*s); break;
            case 5: {
                int phase = (millis()/200)%4, spd = 120*s;
                switch(phase) {
                    case 0: Motor::setSpeed(spd, spd); break;
                    case 1: Motor::setSpeed(-spd, spd); break;
                    case 2: Motor::setSpeed(-spd, -spd); break;
                    case 3: Motor::setSpeed(spd, -spd); break;
                }
                break;
            }
        }
    }
    
    void enterChaos(Gene& g, DilemmaType dilemma) {
        state = STATE_CHAOS;
        inChaos = true;
        chaosStart = millis();
        chaosStep = 0;
        chaosEscaped = false;
        currentDilemma = dilemma;
        g.chaos_total++;
        g.previous_chaos_start = chaosStart;
        g.last_escape_time_ms = 0;
        currentChaosStrategy = selectChaosStrategy(dilemma, g);
        strategyStats[currentChaosStrategy].totalAttempts++;
        Logger::logf("🌀 CHAOS #%d Type:%s Strategy:%s", g.chaos_total, getDilemmaName(), getStrategyName(currentChaosStrategy));
    }
    
    void runChaos() {
        Gene& g = currentGene();
        uint32_t now = millis();
        if (chaosStep < CHAOS_SAMPLE_COUNT) {
            static uint32_t lastChaosTime = 0;
            if (now - lastChaosTime >= CHAOS_STEP_MS) {
                lastChaosTime = now;
                int intensity = 40 + (chaosStep * 60 / CHAOS_SAMPLE_COUNT);
                executeChaosStrategy(currentChaosStrategy, intensity);
                g.recordAction(currentChaosStrategy);
                int steps = CHAOS_SAMPLE_COUNT + (currentDilemma == DILEMMA_STUCK ? 5 : 0) + (currentDilemma == DILEMMA_COLLISION ? 3 : 0);
                chaosStep++;
                if (chaosStep >= steps) {
                    Motor::stop();
                    g.last_escape_time_ms = 0;
                    strategyStats[currentChaosStrategy].totalAttempts++;
                    updateStrategyStats();
                    g.survival_ms = now - testStart;
                    g.distance_ticks = Encoders::totalCount();
                    g.obstacle_count = Encoders::obstacleCount();
                    inChaos = false; testRunning = false; state = STATE_SCORE;
                    return;
                }
                if (Sensors::isClear() || Encoders::totalDelta() > 30) {
                    g.chaos_escaped++;
                    uint32_t escapeTime = now - g.previous_chaos_start;
                    g.last_escape_time_ms = escapeTime;
                    if (g.avg_escape_time_ms == 0) g.avg_escape_time_ms = escapeTime;
                    else g.avg_escape_time_ms = (g.avg_escape_time_ms * (g.chaos_escaped - 1) + escapeTime) / g.chaos_escaped;
                    if (escapeTime < g.fastest_escape_ms) g.fastest_escape_ms = escapeTime;
                    g.escape_rate = (float)g.chaos_escaped / g.chaos_total;
                    strategyStats[currentChaosStrategy].successCount++;
                    g.preferred_action = currentChaosStrategy;
                    updateStrategyStats();
                    lastChaosTriggerTime = now;
                    collisionCounter = 0; swingCounter = 0; lastObstacleChange = 0; stuckStart = 0;
                    Logger::logf("✅ ESCAPED! Rate:%.2f Time:%lu ms", g.escape_rate, escapeTime);
                    inChaos = false; state = STATE_TEST; Motor::stop();
                    return;
                }
            }
        }
    }
    
    void updateStrategyStats() {
        for (int i = 0; i < 6; i++) {
            if (strategyStats[i].totalAttempts > 0) {
                strategyStats[i].successRate = (float)strategyStats[i].successCount / strategyStats[i].totalAttempts;
            }
        }
    }
    
    void calcScore() {
        Gene& g = currentGene();
        Logger::logf("📊 Fitness: %.3f (total:%d escaped:%d rate:%.2f)", 
            g.getFitness(), g.chaos_total, g.chaos_escaped, g.escape_rate);
        recordToRAM(g, currentGen);
        currentIdx++;
        if (currentIdx >= POPULATION_SIZE) { currentIdx = 0; state = STATE_EVALUATE; }
        else { state = STATE_LOAD; }
    }
    
    void evaluate() {
        Logger::log("📊 Evaluating...");
        for (int i = 0; i < POPULATION_SIZE; i++) {
            Logger::logf("   Gene %d: %.3f (%s)", pop.genes[i].id, pop.genes[i].getFitness(), pop.genes[i].getFitnessGrade());
        }
        pop.sortByFitness();
        float avg, maxVal, minVal, stddev;
        getStatistics(avg, maxVal, minVal, stddev);
        Logger::logf("📈 Gen %lu - Avg:%.3f Max:%.3f Min:%.3f", currentGen, avg, maxVal, minVal);
        int survival = 0;
        for (int i = 0; i < POPULATION_SIZE; i++) { if (pop.genes[i].getFitness() >= 0.4f) survival++; }
        if (survival < 6) survival = 6;
        Logger::logf("📊 Survival: %d", survival);
        state = STATE_REPRODUCE;
    }
    
    void reproduce() {
        Logger::log("🧬 Reproducing...");
        Gene nextGen[POPULATION_SIZE];
        if (controlMode) {
            for (int i = 0; i < POPULATION_SIZE; i++) {
                nextGen[i] = pop.genes[i];
                nextGen[i].generation = pop.genes[i].generation + 1;
                resetGene(nextGen[i]);
            }
        } else {
            int survival = 0;
            for (int i = 0; i < POPULATION_SIZE; i++) { if (pop.genes[i].getFitness() >= 0.4f) survival++; }
            if (survival < 6) survival = 6;
            for (int i = 0; i < survival; i++) {
                nextGen[i] = pop.genes[i];
                nextGen[i].generation = pop.genes[i].generation + 1;
                resetGene(nextGen[i]);
            }
            for (int i = survival; i < POPULATION_SIZE; i++) {
                uint16_t noise = Sensors::readNoise();
                int p1 = noise & (survival - 1);
                int p2 = (noise >> 2) & (survival - 1);
                if (p1 >= survival) p1 = survival - 1;
                if (p2 >= survival) p2 = survival - 1;
                nextGen[i] = pop.genes[p1];
                nextGen[i].speed_bias = pop.genes[p2].speed_bias;
                nextGen[i].generation = pop.genes[p1].generation + 1;
                nextGen[i].k_turn = (pop.genes[p1].k_turn + pop.genes[p2].k_turn) / 2;
                resetGene(nextGen[i]);
                nextGen[i].mutate(noise, currentGen);
                nextGen[i].id = i;
                if (pop.genes[p1].preferred_action < 6) {
                    nextGen[i].preferred_action = pop.genes[p1].preferred_action;
                }
            }
        }
        for (int i = 0; i < POPULATION_SIZE; i++) pop.genes[i] = nextGen[i];
        currentGen = pop.maxGeneration();
        Logger::logf("🌟 New generation: %lu", currentGen);
        state = STATE_LOAD;
    }
    
    void resetGene(Gene& g) {
        g.survival_ms = 0; g.distance_ticks = 0; g.obstacle_count = 0;
        g.chaos_total = 0; g.chaos_escaped = 0; g.escape_rate = 0.0f;
        g.avg_escape_time_ms = 0; g.last_escape_time_ms = 0; g.previous_chaos_start = 0;
    }
};

// ===================== Web服务器 =====================
class MyWebServer {
private:
    EvolutionEngine& engine;
    WebServer server;
    
public:
    MyWebServer(EvolutionEngine& eng) : engine(eng), server(80) {}
    
    void init() {
        server.on("/", HTTP_GET, [this]() { server.send(200, "text/html", page()); });
        server.on("/start", HTTP_GET, [this]() { engine.start(); server.send(200, "text/plain", "STARTED"); });
        server.on("/stop", HTTP_GET, [this]() { engine.stop(); server.send(200, "text/plain", "STOPPED"); });
        server.on("/reset", HTTP_GET, [this]() { engine.stop(); server.send(200, "text/plain", "RESET"); delay(100); ESP.restart(); });
        server.on("/control", HTTP_GET, [this]() {
            String mode = server.arg("mode");
            if (mode == "on") { engine.setControlMode(true); server.send(200, "text/plain", "Control ON"); }
            else if (mode == "off") { engine.setControlMode(false); server.send(200, "text/plain", "Control OFF"); }
            else { server.send(200, "text/plain", "Use /control?mode=on|off"); }
        });
        
        server.on("/status", HTTP_GET, [this]() {
            String s = "";
            s += "running:" + String(engine.isRunning() ? "1" : "0") + "\n";
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
            s += "control_mode:" + String(engine.controlMode ? "1" : "0") + "\n";
            s += "dilemma:" + String(engine.getDilemmaName()) + "\n";
            s += "chaos_strategy:" + String(engine.getStrategyName(engine.currentChaosStrategy)) + "\n";
            s += "storage_ready:" + String(RobustStorage::isReady() ? "1" : "0") + "\n";
            s += "storage_pending:" + String(RobustStorage::getPendingCount()) + "\n";
            s += "storage_saved:" + String(RobustStorage::getTotalSaved()) + "\n";
            s += "storage_failed:" + String(RobustStorage::getTotalFailed()) + "\n";
            server.send(200, "text/plain", s);
        });
        
        server.on("/storage/status", HTTP_GET, [this]() {
            String s = "=== 存储状态 v7.2 ===\n";
            s += "就绪: " + String(RobustStorage::isReady() ? "✅ 是" : "❌ 否") + "\n";
            s += "待保存: " + String(RobustStorage::getPendingCount()) + " 条\n";
            s += "已保存: " + String(RobustStorage::getTotalSaved()) + " 条\n";
            s += "失败: " + String(RobustStorage::getTotalFailed()) + " 次\n";
            if (RobustStorage::isReady()) {
                size_t total = SPIFFS.totalBytes(), used = SPIFFS.usedBytes();
                s += "Flash: " + String(used/1024) + " KB / " + String(total/1024) + " KB (" + String((float)used/total*100, 1) + "%)\n";
            }
            server.send(200, "text/plain", s);
        });
        
        server.on("/storage/flush", HTTP_GET, [this]() {
            RobustStorage::forceSave();
            server.send(200, "text/plain", "强制保存完成");
        });
        
        server.on("/population", HTTP_GET, [this]() {
            String csv = "id,k_turn,speed_bias,generation,chaos_total,chaos_escaped,escape_rate,avg_escape_ms,fastest_escape_ms,preferred_action,fitness\n";
            for (int i = 0; i < POPULATION_SIZE; i++) {
                Gene& g = engine.pop.genes[i];
                csv += String(g.id) + "," + String(g.k_turn) + "," + String(g.speed_bias) + ",";
                csv += String(g.generation) + "," + String(g.chaos_total) + "," + String(g.chaos_escaped) + ",";
                csv += String(g.escape_rate, 3) + "," + String(g.avg_escape_time_ms) + ",";
                csv += String(g.fastest_escape_ms) + "," + String(g.preferred_action) + ",";
                csv += String(g.getFitness(), 3) + "\n";
            }
            server.send(200, "text/csv", csv);
        });
        
        server.on("/statistics", HTTP_GET, [this]() {
            float avg, maxVal, minVal, stddev;
            engine.getStatistics(avg, maxVal, minVal, stddev);
            String json = "{\"generation\":" + String(engine.generation()) + 
                         ",\"average\":" + String(avg, 3) +
                         ",\"max\":" + String(maxVal, 3) +
                         ",\"min\":" + String(minVal, 3) +
                         ",\"stddev\":" + String(stddev, 3) + "}";
            server.send(200, "application/json", json);
        });
        
        server.on("/history/export", HTTP_GET, [this]() {
            String csv = "Generation,GeneID,Fitness,k_turn,SpeedBias,ChaosTotal,ChaosEscaped,EscapeRate,AvgEscape_ms,FastestEscape_ms,PreferredAction\n";
            for (int i = 0; i < historyCount; i++) {
                csv += String(ramHistory[i].generation) + ",";
                csv += String(i % POPULATION_SIZE) + ",";
                csv += String(ramHistory[i].fitnessScore, 3) + ",";
                csv += String(ramHistory[i].k_turn) + ",";
                csv += String(ramHistory[i].speed_bias) + ",";
                csv += String(ramHistory[i].chaos_total) + ",";
                csv += String(ramHistory[i].chaos_escaped) + ",";
                csv += String(ramHistory[i].escape_rate, 3) + ",";
                csv += String(ramHistory[i].avg_escape_time_ms) + ",";
                csv += String(ramHistory[i].fastest_escape_ms) + ",";
                csv += String(ramHistory[i].preferred_action) + "\n";
            }
            server.sendHeader("Content-Type", "text/csv");
            server.sendHeader("Content-Disposition", "attachment; filename=evolution_v72.csv");
            server.send(200, "text/csv", csv);
        });
        
        server.on("/history/export/latest", HTTP_GET, [this]() {
            int n = server.arg("n").toInt();
            if (n < 1 || n > historyCount) n = min(10, historyCount);
            String csv = "Generation,GeneID,Fitness,k_turn,SpeedBias,ChaosTotal,ChaosEscaped,EscapeRate,AvgEscape_ms,FastestEscape_ms,PreferredAction\n";
            int start = max(0, historyCount - n);
            for (int i = start; i < historyCount; i++) {
                csv += String(ramHistory[i].generation) + ",";
                csv += String(i % POPULATION_SIZE) + ",";
                csv += String(ramHistory[i].fitnessScore, 3) + ",";
                csv += String(ramHistory[i].k_turn) + ",";
                csv += String(ramHistory[i].speed_bias) + ",";
                csv += String(ramHistory[i].chaos_total) + ",";
                csv += String(ramHistory[i].chaos_escaped) + ",";
                csv += String(ramHistory[i].escape_rate, 3) + ",";
                csv += String(ramHistory[i].avg_escape_time_ms) + ",";
                csv += String(ramHistory[i].fastest_escape_ms) + ",";
                csv += String(ramHistory[i].preferred_action) + "\n";
            }
            server.sendHeader("Content-Type", "text/csv");
            server.sendHeader("Content-Disposition", "attachment; filename=evolution_latest_v72.csv");
            server.send(200, "text/csv", csv);
        });
        
        server.on("/cmd/motor/forward", HTTP_GET, [this]() { Motor::setSpeed(100, 100); server.send(200, "text/plain", "Forward"); });
        server.on("/cmd/motor/backward", HTTP_GET, [this]() { Motor::setSpeed(-100, -100); server.send(200, "text/plain", "Backward"); });
        server.on("/cmd/motor/left", HTTP_GET, [this]() { Motor::setSpeed(-80, 80); server.send(200, "text/plain", "Left"); });
        server.on("/cmd/motor/right", HTTP_GET, [this]() { Motor::setSpeed(80, -80); server.send(200, "text/plain", "Right"); });
        server.on("/cmd/motor/stop", HTTP_GET, [this]() { Motor::stop(); server.send(200, "text/plain", "Stopped"); });
        
        server.begin();
        Logger::log("🌐 Web: http://192.168.4.1");
    }
    
    void loop() { server.handleClient(); }
    
    String page() {
        return R"raw(
<!DOCTYPE html>
<html>
<head>
    <meta charset='UTF-8'>
    <meta name='viewport' content='width=device-width, initial-scale=1.0'>
    <title>v7.2 进化系统</title>
    <style>
        * { margin:0; padding:0; box-sizing:border-box; }
        body { font-family:Arial; background:#1a1a2e; padding:16px; color:#eee; max-width:600px; margin:0 auto; }
        .box { background:#16213e; padding:16px; border-radius:12px; margin:10px 0; }
        h1 { color:#e94560; text-align:center; font-size:20px; }
        .version { color:#00d4ff; font-size:12px; text-align:center; }
        .grid { display:grid; grid-template-columns:1fr 1fr; gap:8px; margin:10px 0; }
        .btn { padding:12px; border-radius:8px; text-align:center; font-weight:bold; cursor:pointer; border:none; font-size:15px; width:100%; }
        .btn-g { background:#2ecc71; color:#1a1a2e; }
        .btn-r { background:#e74c3c; }
        .btn-o { background:#f39c12; color:#1a1a2e; }
        .btn-b { background:#3498db; }
        .btn-p { background:#9c27b0; }
        .btn-c { background:#00bcd4; color:#1a1a2e; }
        .btn-t { background:#00897b; }
        .info { display:flex; justify-content:space-between; padding:3px 0; border-bottom:1px solid #1a1a2e; }
        .info-l { color:#888; }
        .info-v { color:#00d4ff; font-weight:bold; }
        .badge { display:inline-block; padding:2px 10px; border-radius:10px; font-size:11px; font-weight:bold; }
        .badge-idle { background:#666; }
        .badge-test { background:#2ecc71; color:#1a1a2e; }
        .badge-chaos { background:#9c27b0; }
        .badge-v72 { background:#00d4ff; color:#1a1a2e; padding:2px 8px; border-radius:10px; font-size:11px; }
        .row { display:flex; justify-content:space-between; font-size:13px; padding:2px 0; }
        .label { color:#888; }
        .val { font-weight:bold; }
        .ok { color:#2ecc71; }
        .warn { color:#f39c12; }
        .err { color:#e74c3c; }
        .footer { font-size:11px; color:#666; text-align:center; margin-top:10px; }
        .storage-box { background:#0a0a1a; padding:8px 12px; border-radius:6px; margin:8px 0; border-left:3px solid #00d4ff; }
        .fitness-box { background:#0a0a1a; padding:8px 12px; border-radius:6px; margin:8px 0; border-left:3px solid #e94560; }
        .subtitle { text-align:center; font-size:12px; color:#888; margin:4px 0; }
        .btn-sm { padding:6px 12px; font-size:12px; }
        .motor-grid { display:grid; grid-template-columns:1fr 1fr 1fr; gap:5px; margin:6px 0; }
    </style>
</head>
<body>
<div class='box'>
    <h1>🚗 v7.2 进化系统</h1>
    <div class='version'>🔒 鲁棒持久化 | 批量写入 | 断电保护</div>
    <div class='subtitle'>
        <span class='badge-v72'>💾 双缓冲区</span>
        <span class='badge-v72'>⚡ 原子操作</span>
        <span class='badge-v72'>🔄 自动重试</span>
    </div>
    
    <div class='storage-box'>
        <div class='label'>💾 存储</div>
        <div class='row'><span class='label'>状态</span><span id='st_status' class='ok'>--</span></div>
        <div class='row'><span class='label'>待保存</span><span id='st_pending' class='warn'>--</span></div>
        <div class='row'><span class='label'>已保存/失败</span><span id='st_stats' class='ok'>--</span></div>
        <div style='margin-top:4px;'>
            <button class='btn btn-c btn-sm' onclick='cmd("/storage/flush")'>💾 强制保存</button>
            <button class='btn btn-t btn-sm' onclick='window.open("/storage/status")'>📊 详情</button>
        </div>
    </div>
    
    <div class='fitness-box'>
        <div class='label'>🎯 适应度 = 脱困率×0.7 + 速度×0.3</div>
        <div class='row'><span class='label'>当前适应度</span><span class='ok' id='fitness'>--</span></div>
        <div class='row'><span class='label'>脱困率</span><span class='ok' id='esc_rate'>--</span></div>
    </div>
    
    <div style='text-align:center;margin:8px 0'>
        <span id='phase' class='badge badge-idle'>⏹ 空闲</span>
        <span id='dilemma' style='margin-left:8px;font-size:12px;color:#888;'></span>
    </div>
    
    <div class='grid'>
        <button class='btn btn-g' onclick='cmd("/start")'>▶ START</button>
        <button class='btn btn-r' onclick='cmd("/stop")'>⏹ STOP</button>
        <button class='btn btn-o' onclick='cmd("/reset")'>🔄 RESET</button>
        <button class='btn btn-p' onclick='window.open("/population")'>📊 种群</button>
    </div>
    
    <div style='margin:6px 0;'>
        <div class='info'><span class='info-l'>状态</span><span class='info-v' id='state'>--</span></div>
        <div class='info'><span class='info-l'>世代</span><span class='info-v' id='gen'>--</span></div>
        <div class='info'><span class='info-l'>个体</span><span class='info-v' id='ind'>--</span></div>
        <div class='info'><span class='info-l'>测试时间</span><span class='info-v' id='t_time'>--</span></div>
    </div>
    
    <div style='background:#0a0a1a;padding:8px 12px;border-radius:6px;margin:8px 0;'>
        <div class='row'><span class='label'>⬅ 左传感器</span><span id='sL'>--</span></div>
        <div class='row'><span class='label'>➡ 右传感器</span><span id='sR'>--</span></div>
        <div class='row'><span class='label'>编码器 L/R</span><span id='enc'>--</span></div>
        <div class='row'><span class='label'>总脉冲</span><span id='pulse'>--</span></div>
        <div class='row'><span class='label'>左/右电机</span><span id='motor'>--</span></div>
    </div>
</div>

<div class='box'>
    <h3 style='color:#3498db;font-size:14px;'>🔧 电机测试</h3>
    <div class='motor-grid'>
        <button class='btn btn-g' onclick='cmd("/cmd/motor/forward")'>⬆ 前进</button>
        <button class='btn btn-r' onclick='cmd("/cmd/motor/backward")'>⬇ 后退</button>
        <button class='btn btn-b' onclick='cmd("/cmd/motor/left")'>⬅ 左转</button>
        <button class='btn btn-b' onclick='cmd("/cmd/motor/right")'>➡ 右转</button>
        <button class='btn btn-o' onclick='cmd("/cmd/motor/stop")'>⏹ 停止</button>
    </div>
</div>

<div class='box'>
    <h3 style='color:#00d4ff;font-size:13px;'>📥 数据导出</h3>
    <div class='grid'>
        <a href='/history/export' class='btn btn-c btn-sm' style='text-decoration:none;text-align:center;padding:8px;border-radius:6px;'>📊 全部历史</a>
        <a href='/history/export/latest?n=10' class='btn btn-t btn-sm' style='text-decoration:none;text-align:center;padding:8px;border-radius:6px;'>📥 最近10代</a>
        <a href='/history/export/latest?n=50' class='btn btn-t btn-sm' style='text-decoration:none;text-align:center;padding:8px;border-radius:6px;'>📥 最近50代</a>
    </div>
</div>

<div class='footer'>
    ✅ v7.2 鲁棒持久化 | 批量写入 | 原子操作 | 断电保护
</div>

<script>
function cmd(url) { fetch(url).then(()=>refresh()); }
function refresh() {
    fetch('/status').then(r=>r.text()).then(t=>{
        const d={}; t.split('\n').forEach(l=>{const p=l.split(':'); if(p.length===2) d[p[0].trim()]=p[1].trim();});
        ['state','gen','ind','t_time','pulse'].forEach(k=>{const el=document.getElementById(k); if(el&&d[k]!==undefined) el.textContent=d[k];});
        if(d['sensor_L']!==undefined) document.getElementById('sL').textContent=d['sensor_L'];
        if(d['sensor_R']!==undefined) document.getElementById('sR').textContent=d['sensor_R'];
        if(d['encoder_L']!==undefined && d['encoder_R']!==undefined) document.getElementById('enc').textContent=d['encoder_L']+'/'+d['encoder_R'];
        if(d['motor_L']!==undefined && d['motor_R']!==undefined) document.getElementById('motor').textContent=d['motor_L']+'/'+d['motor_R'];
        if(d['storage_ready']!==undefined) {
            const el=document.getElementById('st_status');
            if(d['storage_ready']==='1') { el.textContent='✅ 就绪'; el.className='ok'; }
            else { el.textContent='❌ 仅RAM'; el.className='err'; }
        }
        if(d['storage_pending']!==undefined) document.getElementById('st_pending').textContent=d['storage_pending']+' 条';
        if(d['storage_saved']!==undefined && d['storage_failed']!==undefined) document.getElementById('st_stats').textContent=d['storage_saved']+'/'+d['storage_failed'];
        if(d['phase']!==undefined) {
            const el=document.getElementById('phase');
            const p=parseInt(d['phase']);
            if(p===2) { el.textContent='🌀 混沌'; el.className='badge badge-chaos'; }
            else if(p===1) { el.textContent='✅ 测试中'; el.className='badge badge-test'; }
            else { el.textContent='⏹ 空闲'; el.className='badge badge-idle'; }
        }
        if(d['dilemma']!==undefined) document.getElementById('dilemma').textContent='⚠️ '+d['dilemma'];
    }).catch(()=>{});
    fetch('/statistics').then(r=>r.json()).then(d=>{
        if(d.average!==undefined) document.getElementById('fitness').textContent=d.average.toFixed(3);
        if(d.max!==undefined) document.getElementById('esc_rate').textContent=d.max.toFixed(3);
    }).catch(()=>{});
}
setInterval(refresh, 1500);
refresh();
</script>
</body>
</html>
)raw";
    }
};

// ===================== 全局对象 =====================
EvolutionEngine engine;
MyWebServer webServer(engine);

// ===================== 主程序 =====================
void setup() {
    Logger::init();
    Logger::log("========================================");
    Logger::log("  v7.2 鲁棒持久化进化系统");
    Logger::log("  脱困能力优先 | 断电保护 | 批量写入");
    Logger::log("========================================");
    Logger::log("  进化流程:");
    Logger::log("  ① 初始化 → ② 载入个体 → ③ 生存测试");
    Logger::log("  ④ 困境检测 → ⑤ 混沌干预 → ⑥ 评分");
    Logger::log("  ⑦ 评估 → ⑧ 繁殖 → 回到②");
    Logger::log("========================================");
    
    // 1. 存储系统初始化（最先）
    RobustStorage::init();
    
    // 2. 硬件初始化
    Motor::init();
    Sensors::init();
    Encoders::init();
    
    // 3. 进化引擎初始化
    engine.init();
    
    // 4. WiFi AP
    WiFi.mode(WIFI_AP);
    IPAddress ip(192, 168, 4, 1);
    WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
    WiFi.softAP(WIFI_SSID, WIFI_PASSWORD);
    Logger::logf("📡 WiFi: %s (192.168.4.1)", WIFI_SSID);
    
    // 5. Web服务器
    webServer.init();
    
    // 6. LED闪烁
    pinMode(PIN_LED, OUTPUT);
    for (int i = 0; i < 3; i++) {
        digitalWrite(PIN_LED, HIGH); delay(200);
        digitalWrite(PIN_LED, LOW); delay(200);
    }
    
    Logger::log("✅ System ready!");
    Logger::log("🌐 http://192.168.4.1");
    Logger::log("========================================");
}

void loop() {
    engine.update();
    webServer.loop();
    RobustStorage::tick();
    
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