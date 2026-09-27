
/*
* ================================================================
*              OEE 进化系统 v10.12-EXSIMP
*        极简保存逻辑 + POPULATION_SIZE=10 + TEST_DURATION_MS=25s
* ================================================================
*20260925：ArchiveMax 改为600，computeNovelty() 里的 float distances[NOVELTY_ARCHIVE_MAX]; 改成 static float distances[NOVELTY_ARCHIVE_MAX];；
* 版本: v10.12-EXSIMP
* 日期: 2026-09-17
* 前身: v10.12-GenLimit
* 目标: 在 v10.12 全部数据格式/算法改进的基础上, 把"保存逻辑"
*       回退到 v9_22v22 的极简形式, 解决"第 10 代卡住"问题;
*       同时把 POPULATION_SIZE 10, TEST_DURATION_MS
*       从 30000 改为 25000.
*
* ================================================================
* 【为什么叫 EXSIMP】
* ================================================================
* EXSIMP = Extreme Simplification of Persistence
*   指"保存逻辑极简化"。
*
* v10.12 的保存逻辑在一次代际切换中执行 4 次写操作:
*   1. savePopulationForGeneration(prevGen, ..., "post-test")
*   2. 预检存储 (可能触发 enforceRetention)
*   3. savePopulationForGeneration(newGen, ..., "post-evolve") + 重试
*   4. enforceRetention after commit
*
* v9_22v22 的保存逻辑只执行 1 次写操作:
*   1. savePopulationForGeneration(newGen, ...)
*
* v10.12-EXSIMP 回到 v22 的 1 次写操作, 但保留 v10.12 的所有
* 数据格式/算法/诊断改进。
*
* ================================================================
* 【核心证据: v9_22v22 vs v10.12 对比】
* ================================================================
* 9月6日23点, v9_22v22 跑到第21代, 无卡顿。
* 同期, v10.12 跑到第10代第1个个体时卡住。
*
* 对比结论:
*   - v9_22v22 的 getStoredGenerations() 因 File.name() 不带斜杠
*     而永远返回空 vector
*   - 结果: cleanOldPopulations / deleteOldestGeneration /
*     deleteGeneration 全部从不被调用
*   - v9_22v22 从不删任何代, 文件线性累积, 第10代时约90个文件,
*     离 SPIFFS 目录项上限(约128)尚远
*
*   - v10.12 加了 normPath(), getStoredGenerations() 第一次返回
*     真实代列表
*   - 结果: cleanOldPopulations(2) 第一次真正删代
*   - 加上 v10.12 新增的 enforceRetention, 第10代时第一次执行
*     "删1~3代" 或 "删1~8代"
*   - 第10代时 SPIFFS 上约120个文件, 接近目录项上限
*   - 加上 collectGenerationFiles 遍历整个目录, 堆压力大
*   - 疑似在删代过程中 SPIFFS 目录项/堆出现异常
*
* ================================================================
* 【v10.12-EXSIMP 相对 v10.12 的全部改动 — 共 5 项】
* ================================================================
*
* ────────────────────────────────────────────────────────────────
* [E1] nextIndividualImpl() 保存逻辑极简化
* ────────────────────────────────────────────────────────────────
* 问题: v10.12 的 nextIndividualImpl 在一次代际切换中执行 4 次
*       保存相关操作:
*         1. savePopulationForGeneration(prevGen, ..., "post-test")
*         2. 预检存储 + enforceRetention
*         3. savePopulationForGeneration(newGen, ..., "post-evolve")
*            失败后重试
*         4. enforceRetention after commit
*       第10代时, enforceRetention 第一次执行删代, 疑似触发
*       SPIFFS 目录项/堆异常。
*
* 修复: 回退到 v9_22v22 的极简形式:
*         1. evolveImpl()
*         2. savePopulationForGeneration(newGen, ...)
*         3. commitGeneration(newGen) / rollback
*       删除 post-test 保存, 删除预检, 删除 enforceRetention,
*       删除重试。
*
* 位置: EvolutionEngine::nextIndividualImpl()
* 风险: 低(逻辑变简单, 行为可预测)
*
* ────────────────────────────────────────────────────────────────
* [E2] 删除 ensureSpace 里的应急删代
* ────────────────────────────────────────────────────────────────
* 问题: v10.12 的 ensureSpace 在 cleanupAttempts > 3 时调
*       deleteOldestGeneration(true), 主动删除最旧代。
*
* 修复: 改为只告警, 不删除:
*         Logger::log("🚨 Emergency: storage critical, manual cleanup required");
*         cleanupAttempts = 0;
*
* 位置: RollingStorage::ensureSpace()
* 风险: 低(保留人工清理路径)
*
* ────────────────────────────────────────────────────────────────
* [E3] 删除 nextIndividualImpl 里的应急删代
* ────────────────────────────────────────────────────────────────
* 问题: v10.12 的 nextIndividualImpl 在 saveFailCount > 3 时调
*       RollingStorage::deleteOldestGeneration(true)。
*
* 修复: 改为只告警, 不删除:
*         Logger::log("🚨 3 consecutive save failures, manual cleanup required");
*         saveFailCount = 0;
*
* 位置: EvolutionEngine::nextIndividualImpl()
* 风险: 低(保留人工清理路径)
*
* ────────────────────────────────────────────────────────────────
* [E4] POPULATION_SIZE 从 10 改为 8
* ────────────────────────────────────────────────────────────────
* 问题: v10.12 每代 10 个体, 每代产生的文件数比 v9_22v22 多约 25%。
*       第10代时 SPIFFS 上累计约120个文件, 接近目录项上限。
*
* 修复: 回到 v9_22v22 的 8 个体/代。每代文件数减 20%, 第10代
*       累计约90个文件, 离目录项上限更远。
*
* 位置: #define POPULATION_SIZE
* 风险: 低(参数调整, 数组大小/循环次数自动适应)
*
* ────────────────────────────────────────────────────────────────
* [E5] TEST_DURATION_MS 从 30000 改为 25000
* ────────────────────────────────────────────────────────────────
* 问题: v10.12 每人体 30 秒, 一代 10 个体 = 5 分钟。
*       跑20代需100分钟。
*
* 修复: 改为 25 秒/人体, 一代 8 个体 = 3分20秒。
*       跑20代需66.7分钟。
*
* 位置: #define TEST_DURATION_MS
* 风险: 低(参数调整, COND_TIME 自动适应)
*
* ================================================================
* 【v10.12 的全部改进 (EXSIMP 保留)】
* ================================================================
*
* 以下改进全部保留, 不做任何回退:
*
* ────────────────────────────────────────────────────────────────
* 数据格式类
* ────────────────────────────────────────────────────────────────
* - normPath() 路径规范化 (v10.5)
*   13处 File.name() 返回值处理, 修复目录扫描函数
* - BehaviorDescriptor 48→64 字节 (v10.8)
*   新增 4 个混沌特征维度: chaosFrameRatio, chaosSpeedDeltaL,
*   chaosSpeedDeltaR, chaosPwmVariance
* - FILE_VERSION 0x0009 → 0x000A (v10.8)
* - deserializeIndividual 双分支兼容 0x0007/0x000A (v10.8)
* - ChaosSnapshotEntry 加 rawNoiseL/rawNoiseR (v10.6)
* - ChaosSnapshotHeader v1 → v2 (v10.6)
* - ChaoticTestRecord 加 chaosExitReason/chaosInterruptedCount (v10.6)
* - oe_history.csv 注释行 (v10.10)
*
* ────────────────────────────────────────────────────────────────
* 算法修复类
* ────────────────────────────────────────────────────────────────
* - sensorAsymmetry 分母 fabsf (v10.8)
* - chaosTotalDuration 改 micros() (v10.8)
* - normalize() 混沌 NaN 保护 (v10.9)
* - updateMaxValues() 混沌 NaN 保护 (v10.9)
* - chaosPwmVariance 上限 65025 (v10.9)
* - distance() CHAOS_WEIGHT=0.5 (v10.9)
* - COND 条件范围修复 (v10.10)
*
* ────────────────────────────────────────────────────────────────
* 存储完整性类
* ────────────────────────────────────────────────────────────────
* - experimentReady 就绪强校验 (v10.4)
* - 断电恢复链 (v10.4/v10.5)
*   findLatestGeneration / loadExperimentState / forceActivate
* - 数据分级保护 L1~L4 (v9.22)
* - NoveltyArchive 安全保存 3 次重试 (v9.22)
* - RobustStorage 缓冲保留 + 自动重挂载 (v9.22)
*
* ────────────────────────────────────────────────────────────────
* 诊断类
* ────────────────────────────────────────────────────────────────
* - ensureSpace 健康度日志 (v10.11)
* - startCurrentTest 诊断增强 (v10.11)
* - population 导出诊断 (v10.10)
*
* ────────────────────────────────────────────────────────────────
* 硬件配置类
* ────────────────────────────────────────────────────────────────
* - PIN_NOISE_SOURCE GPIO1 → GPIO6 (v10.6)
* - analogReadResolution(12) + ADC_11db (v10.6)
* - updateChaos 补真实传感器值 (v10.6)
*
* ================================================================
* 【数据兼容性】
* ================================================================
* - pop_gen_N.bin:        格式与 v10.12 一致 (FILE_VERSION 0x000A,
*                         BehaviorDescriptor 64字节)
*                         ⚠️ POPULATION_SIZE=8, 与 v10.12 的 10
*                         不兼容。旧文件加载会失败
* - frm_N_iM.bin:         格式与 v10.12 一致
* - novelty_archive.bin:  格式与 v10.12 一致
* - nova_gen_N.bin:       格式与 v10.12 一致
* - oe_history.csv:       格式与 v10.12 一致 (# 注释表头)
* - chaos_history.csv:    格式与 v10.12 一致
* - chaos_snaps_*.bin:    格式与 v10.12 一致 (v2)
*
* ⚠️ 重要: 由于 POPULATION_SIZE 从 10 改为 8, 建议烧录前格式化
*          SPIFFS, 从第1代开始新实验。
*          否则旧文件 (10个体) 会因 popSize 不匹配而加载失败。
*
* ================================================================
* 【上板观察】
* ================================================================
* - setup() 打印 "POPULATION_SIZE = 8, TEST_DURATION_MS = 25000"
* - 每代 8 个体, 每人体 25 秒
* - 一代时长 3分20秒, 20代时长 66.7分钟
* - 第10代时不触发 enforceRetention, 不删任何代
* - 第10代累计文件数约90个, 离 SPIFFS 目录项上限(约128)较远
* - 断电重启: 恢复到原代数 (POPULATION_SIZE=8 的文件可加载)
* - 串口 ls: 显示图标 (🧬/📹/🌪️/📌), 路径带斜杠
*
* ================================================================
* 【待论文侧同步】
* ================================================================
* - POPULATION_SIZE 从 10 改为 8, 论文统计样本需同步
* - TEST_DURATION_MS 从 30000 改为 25000, 论文 6.2.1 节参数需同步
* - 保存逻辑极简化, 论文 3.5 节"存储可靠性"描述需同步
*
* ================================================================
* 【未改动 (与 v10.12 一致)】
* ================================================================
* - RollingStorage::progressiveCleanup(): 保留
*   (cleanExpiredFrameLogs + cleanOldPopulations(2))
* - RollingStorage::deleteGeneration(): 保留定义, 但不再被主动调用
* - RollingStorage::enforceRetention 系列函数: 保留定义, 但不再被调用
* - RollingStorage::deleteOldestGeneration(): 保留定义, 但不再被调用
* - EvolutionEngine::initImpl(): 保留 D-1/D-2 保护
* - setup(): 保留禁止自动重建第1代
*
* ================================================================
* 【核心设计原则 (EXSIMP)】
* ================================================================
* 1. 保存逻辑极简: 一次代际切换只写一次种群文件
* 2. 不主动删代: 空间不足时告警, 由人工通过 Web UI 清理
* 3. 保留全部数据格式/算法/诊断改进
* 4. 每代 8 个体, 25 秒/人体, 兼顾统计样本与实验时长
*
* ================================================================
* 修改日期: 2026-09-16
* 修改人: 系统优化 (基于 v9_22v22 vs v10.12 对比 + 用户反馈)
* ================================================================
*/


// ================================================================
// ★★★ SPIFFS 格式化控制 ★★★
// ================================================================
#define FORMAT_SPIFFS_ON_BOOT 0
// ================================================================
// ★★★ 新增配置宏 - 定期保存 ★★★
// ================================================================
#define FORCE_SAVE_INTERVAL_MS 300000   // 30000秒强制保存一次
// ================================================================
// ================================================================
// ★★★ 滚动保留策略 (enforceRetention 使用) ★★★
// ================================================================
#define RETENTION_MIN_KEEP_GENERATIONS   7    // 最低保留代数(含当前代)
#define RETENTION_MAX_KEEP_GENERATIONS   15   // 最高保留代数(超过则强制清理)
#define RETENTION_ENABLE_AUTO_DELETE     1    // 1=允许删旧代, 0=只告警不删
#define RETENTION_PROTECT_PREV_GEN       1    // 1=保护上一代不被删

// ================================================================
// ★★★ 第 1 代重建总开关 (必须人工修改) ★★★
//   false: 系统永远不允许自动重建第 1 代
//   true : 允许系统在"无任何种群文件"时自动重建第 1 代
//   修改此宏后必须重新烧录, 运行时不可改
// ================================================================
#define ALLOW_AUTO_REBUILD_GEN1          0

// 头文件包含
// ================================================================
#include <WiFi.h>
#include <WebServer.h>
#include <FS.h>
#include <SPIFFS.h>
#include <vector>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <esp_random.h>

// ================================================================
// PhysicalRandom - 基于ESP32-S3硬件TRNG的物理真随机数发生器
// 严格替代PRNG，确保所有随机性源于物理熵（半导体热噪声与闪烁噪声）
// ================================================================
class PhysicalRandom {
public:
    static inline uint32_t get() {
        return esp_random();
    }
    static inline int32_t getRange(int32_t min, int32_t max) {
        if (min >= max) return min;
        uint32_t range = (uint32_t)(max - min);
        return min + (esp_random() % range);
    }
    static inline float getFloat() {
        return (esp_random() >> 8) * (1.0f / 16777216.0f);
    }
};

// ================================================================
// 前向声明（文件开头）
// ================================================================
class RobustStorage;
class RollingStorage;
class FileUtils;
class NoveltyArchive;
class GeneStorage;
class EvolutionEngine;
class CarWebServer;

// 硬件引脚定义
// ================================================================
#define PIN_SENSOR_LEFT     4
#define PIN_SENSOR_RIGHT    5
#define PIN_NOISE_SOURCE    6
#define PIN_OBSTACLE_INT    39
#define PIN_LED             14
#define PIN_LEFT_PWM        9
#define PIN_LEFT_DIR2       46
#define PIN_RIGHT_PWM       16
#define PIN_RIGHT_DIR2      15
#define PIN_LEFT_ENC_A      18
#define PIN_LEFT_ENC_B      17
#define PIN_RIGHT_ENC_A     11
#define PIN_RIGHT_ENC_B     10

// ================================================================
// LEDC 配置
// ================================================================
#define LEDC_FREQ           5000
#define LEDC_RESOLUTION     8
#define LEDC_CHANNEL_LEFT   0
#define LEDC_CHANNEL_RIGHT  1

// ================================================================
// 电机方向定义
// ================================================================
#define LEFT_FORWARD   HIGH
#define LEFT_REVERSE   LOW
#define RIGHT_FORWARD  LOW
#define RIGHT_REVERSE  HIGH

// ================================================================
// 电机增益补偿
// ================================================================
#define LEFT_FWD_GAIN       1.00f
#define RIGHT_FWD_GAIN      1.30f
#define LEFT_REV_GAIN       1.30f
#define RIGHT_REV_GAIN      1.30f
#define LEFT_DEADZONE       35
#define RIGHT_DEADZONE      20
#define MOTOR_PWM_MAX       255

// ================================================================
// WiFi 配置
// ================================================================
#define WIFI_SSID           "CarLogger"
#define WIFI_PASSWORD       "12345678"

// ================================================================
// 进化参数
// ================================================================
#define POPULATION_SIZE     10
#define TEST_DURATION_MS    25000
#define LOOP_DELAY_MS       10

// ================================================================
// ★★★ 滚动保留策略 ★★★
// ================================================================
#define SPIFFS_MIN_GENERATIONS    10
#define SPIFFS_MAX_GENERATIONS    15

// ================================================================
// ★★★ 帧日志采样策略 ★★★
// ================================================================
#define FRAME_SAMPLE_RATE_NORMAL  8
#define FRAME_SAMPLE_RATE_STUCK   4
#define FRAME_SAMPLE_RATE_CHAOS   1
#define FRAME_SAMPLE_RATE_ESCAPE  2

// ================================================================
// ★★★ 混沌脱困参数 ★★★
// ================================================================
#define CHAOS_ESCAPE_PWM          200
#define CHAOS_RECOVER_STABLE_FRAMES 20
#define MAX_STUCK_WINDOW 20

// ================================================================
// 行为规则参数
// ================================================================
#define MAX_RULES           16
#define MIN_RULES           2
#define MAX_RULE_DURATION   2000
#define MIN_RULE_DURATION   50

// ================================================================
// Novelty Search 参数
// ================================================================
#define NOVELTY_ARCHIVE_MAX    600
#define NOVELTY_K_NEAREST      5
#define NOVELTY_ADD_THRESHOLD  0.15f

// ================================================================
// 存储参数
// ================================================================
#define STORAGE_SAVE_INTERVAL_MS  10000
#define FRAME_LOG_SIZE            4096
#define RAM_LOG_BUFFER_SIZE       6000
#define SPIFFS_MAX_INDIVIDUALS_PER_GEN 16
#define INCREMENTAL_SAVE_INTERVAL 500
#define INCREMENTAL_SAVE_MIN_FRAMES 50

// ================================================================
// ★★★ v10.5-SPIFFSNormFix 版本号与二进制魔数常量 ★★★
// ================================================================
#define FIRMWARE_VERSION "v10.12-EXSIMP"
#define VERSION_MARKER_FILE        "/version.marker"
#define MAGIC_GENE_POP           0x47454E45
#define MAGIC_FRAME_LOG          0x46524D45

// #define BINARY_FORMAT_VERSION 0x000A  // [v10.8审计] 从未被引用，注释掉避免维护混淆
#define NOVA_FORMAT_VERSION   0x0001

// ================================================================
// ★★★ 数据分级定义 (L1-L4) ★★★
// ================================================================
#define DATA_LEVEL_CORE     1
#define DATA_LEVEL_GENE     2
#define DATA_LEVEL_POP      3
#define DATA_LEVEL_PROCESS  4

// ================================================================
// ★★★ 混沌触发时间尺度参数 ★★★
// ================================================================
#define CHAOS_TIMEOUT_MIN       2000
#define CHAOS_TIMEOUT_MAX       6000
#define CHAOS_FORCE_TIMEOUT_MIN 3000
#define CHAOS_FORCE_TIMEOUT_MAX 8000

// ================================================================
// 检测机制参数
// ================================================================
#define STUCK_CLEAR_DEBOUNCE_MS     150
#define BOUNCE_WINDOW_MS            500
#define BOUNCE_THRESHOLD            3
#define NO_PROGRESS_TIMEOUT_MS      1000
#define SPEED_ASYMMETRY_RATIO       2.5f

// ================================================================
// 状态枚举
// ================================================================
enum MotorState : uint8_t {
    STATE_IDLE      = 0,
    STATE_WALKING   = 1,
    STATE_STUCK     = 2,
    STATE_CHAOS     = 3,
};

// ================================================================
// 条件类型枚举
// ================================================================
enum CondType : uint8_t {
    COND_SENSOR_LEFT  = 0,
    COND_SENSOR_RIGHT = 1,
    COND_SENSOR_BOTH  = 2,
    COND_SENSOR_ANY   = 3,
    COND_DISTANCE     = 4,
    COND_TIME         = 5,
    COND_IDLE         = 6,
    COND_ALWAYS       = 7
};
#define COND_TYPE_MAX 8

// ================================================================
// 操作符枚举
// ================================================================
enum CondOp : uint8_t {
    OP_LESS    = 0,
    OP_GREATER = 1,
    OP_EQUAL   = 2
};
#define OP_TYPE_MAX 3

// ================================================================
// 行为规则结构体
// ================================================================
struct __attribute__((packed)) BehaviorRule {
    uint8_t  condType;
    int16_t  condValue;
    uint8_t  condOp;
    int16_t  motorL;
    int16_t  motorR;
    uint16_t durationMs;
    uint8_t  nextRule;
    uint8_t  _padding;

    void randomize() {
        condType   = (uint8_t)PhysicalRandom::getRange(0, COND_TYPE_MAX);
        condOp     = (uint8_t)PhysicalRandom::getRange(0, OP_TYPE_MAX);
        // [v10.10] 根据 condType 生成合理的 condValue 范围 (修复 COND_IDLE 死规则问题)
        switch (condType) {
            case COND_SENSOR_LEFT:
            case COND_SENSOR_RIGHT:
            case COND_SENSOR_ANY:
                condValue = PhysicalRandom::getRange(0, 4096);
                break;
            case COND_SENSOR_BOTH:
                condValue = PhysicalRandom::getRange(0, 8192);
                break;
            case COND_DISTANCE:
                condValue = PhysicalRandom::getRange(-3000, 3000);
                break;
            case COND_TIME:
                condValue = PhysicalRandom::getRange(0, TEST_DURATION_MS);
                break;
            case COND_IDLE:
                condValue = PhysicalRandom::getRange(0, 2);  // 0 或 1，与 evaluateCondition 返回值匹配
                break;
            case COND_ALWAYS:
                condValue = 0;
                break;
        }
        motorL     = (int16_t)PhysicalRandom::getRange(-255, 256);
        motorR     = (int16_t)PhysicalRandom::getRange(-255, 256);
        durationMs = (uint16_t)PhysicalRandom::getRange(MIN_RULE_DURATION, MAX_RULE_DURATION + 1);
        nextRule   = (uint8_t)PhysicalRandom::getRange(0, MAX_RULES);
        _padding   = 0;
    }

    void clamp() {
        motorL     = constrain(motorL, -255, 255);
        motorR     = constrain(motorR, -255, 255);
        durationMs = constrain(durationMs, MIN_RULE_DURATION, MAX_RULE_DURATION);
        condType   = constrain(condType, (uint8_t)0, (uint8_t)(COND_TYPE_MAX - 1));
        condOp     = constrain(condOp,   (uint8_t)0, (uint8_t)(OP_TYPE_MAX - 1));
        // [v10.10] 根据 condType 限制 condValue 范围 (修复 COND_IDLE 死规则问题)
        switch (condType) {
            case COND_IDLE:
                condValue = constrain(condValue, 0, 1);
                break;
            case COND_TIME:
                condValue = constrain(condValue, 0, TEST_DURATION_MS);
                break;
            case COND_SENSOR_LEFT:
            case COND_SENSOR_RIGHT:
            case COND_SENSOR_ANY:
                condValue = constrain(condValue, 0, 4095);
                break;
            case COND_SENSOR_BOTH:
                condValue = constrain(condValue, 0, 8190);
                break;
            case COND_DISTANCE:
                condValue = constrain(condValue, -3000, 3000);
                break;
            case COND_ALWAYS:
                condValue = 0;
                break;
        }
        nextRule   = constrain(nextRule, (uint8_t)0, (uint8_t)(MAX_RULES - 1));
        _padding   = 0;
    }

    bool compare(int value, int op, int threshold) const {
        switch (op) {
            case OP_LESS:    return value < threshold;
            case OP_GREATER: return value > threshold;
            case OP_EQUAL:   return abs(value - threshold) <= 10;
            default:         return false;
        }
    }
};

// ================================================================
// 行为描述符
// ================================================================
struct __attribute__((packed)) BehaviorDescriptor {
    float leftSensorMean, rightSensorMean;
    float sensorVariance, sensorAsymmetry;
    float avgSpeed, speedVariance, turnBias, totalDistance;
    float forwardRatio, turnRatio, reverseRatio, idleRatio;
    // ★ v10.8 新增：混沌特征
    float chaosFrameRatio;      // 混沌帧占比 [0, 1]
    float chaosSpeedDeltaL;     // 混沌期间左轮平均速度 - 全程平均速度
    float chaosSpeedDeltaR;     // 混沌期间右轮平均速度 - 全程平均速度
    float chaosPwmVariance;     // 混沌期间 PWM 方差

    void init() {
        leftSensorMean = rightSensorMean = 0;
        sensorVariance = sensorAsymmetry = 0;
        avgSpeed = speedVariance = turnBias = totalDistance = 0;
        forwardRatio = turnRatio = reverseRatio = idleRatio = 0;
        chaosFrameRatio = 0;
        chaosSpeedDeltaL = chaosSpeedDeltaR = 0;
        chaosPwmVariance = 0;
    }

    float distance(const BehaviorDescriptor& other) const {
        float sum = 0, d;
        d = leftSensorMean - other.leftSensorMean; sum += d*d;
        d = rightSensorMean - other.rightSensorMean; sum += d*d;
        d = sensorVariance - other.sensorVariance; sum += d*d;
        d = sensorAsymmetry - other.sensorAsymmetry; sum += d*d;
        d = avgSpeed - other.avgSpeed; sum += d*d;
        d = speedVariance - other.speedVariance; sum += d*d;
        d = turnBias - other.turnBias; sum += d*d;
        d = totalDistance - other.totalDistance; sum += d*d;
        d = forwardRatio - other.forwardRatio; sum += d*d;
        d = turnRatio - other.turnRatio; sum += d*d;
        d = reverseRatio - other.reverseRatio; sum += d*d;
        d = idleRatio - other.idleRatio; sum += d*d;
        // ★ v10.9 混沌特征加权，避免主导 distance
        const float CHAOS_WEIGHT = 0.5f;
        d = chaosFrameRatio - other.chaosFrameRatio; sum += CHAOS_WEIGHT * d*d;
        d = chaosSpeedDeltaL - other.chaosSpeedDeltaL; sum += CHAOS_WEIGHT * d*d;
        d = chaosSpeedDeltaR - other.chaosSpeedDeltaR; sum += CHAOS_WEIGHT * d*d;
        d = chaosPwmVariance - other.chaosPwmVariance; sum += CHAOS_WEIGHT * d*d;
        return sqrtf(sum);
    }

    void normalize(const BehaviorDescriptor& maxVals) {
        if (maxVals.leftSensorMean > 0) leftSensorMean /= maxVals.leftSensorMean;
        if (maxVals.rightSensorMean > 0) rightSensorMean /= maxVals.rightSensorMean;
        if (maxVals.sensorVariance > 0) sensorVariance /= maxVals.sensorVariance;
        if (maxVals.avgSpeed > 0) avgSpeed /= maxVals.avgSpeed;
        if (maxVals.speedVariance > 0) speedVariance /= maxVals.speedVariance;
        if (maxVals.totalDistance > 0) totalDistance /= maxVals.totalDistance;
        if (maxVals.turnBias > 0) turnBias /= maxVals.turnBias;
        if (maxVals.sensorAsymmetry > 0) sensorAsymmetry /= maxVals.sensorAsymmetry;
        if (maxVals.forwardRatio > 0) forwardRatio /= maxVals.forwardRatio;
        if (maxVals.turnRatio > 0) turnRatio /= maxVals.turnRatio;
        if (maxVals.reverseRatio > 0) reverseRatio /= maxVals.reverseRatio;
        if (maxVals.idleRatio > 0) idleRatio /= maxVals.idleRatio;
        // ★ v10.9 normalize 混沌特征零值/NaN 保护
        if (maxVals.chaosFrameRatio > 0 && !isnan(chaosFrameRatio))
            chaosFrameRatio /= maxVals.chaosFrameRatio;
        else
            chaosFrameRatio = 0;
        if (maxVals.chaosSpeedDeltaL > 0 && !isnan(chaosSpeedDeltaL))
            chaosSpeedDeltaL /= maxVals.chaosSpeedDeltaL;
        else
            chaosSpeedDeltaL = 0;
        if (maxVals.chaosSpeedDeltaR > 0 && !isnan(chaosSpeedDeltaR))
            chaosSpeedDeltaR /= maxVals.chaosSpeedDeltaR;
        else
            chaosSpeedDeltaR = 0;
        if (maxVals.chaosPwmVariance > 0 && !isnan(chaosPwmVariance))
            chaosPwmVariance /= maxVals.chaosPwmVariance;
        else
            chaosPwmVariance = 0;
    }
};

// ================================================================
// 帧日志条目
// ================================================================
struct __attribute__((packed)) FrameLogEntry {
    uint32_t timestamp_ms;
    int16_t  sensorLeft;
    int16_t  sensorRight;
    uint8_t  motorLeftPWM;
    uint8_t  motorRightPWM;
    uint8_t  directionL : 1;
    uint8_t  directionR : 1;
    uint8_t  chaosActive : 1;
    uint8_t  isChaosFrame : 1;
    uint8_t  state : 3;
    uint8_t  reserved : 1;
};

struct __attribute__((packed)) CompressedFrameEntry {
    uint32_t timestamp_ms;
    int16_t  sensorLeft;
    int16_t  sensorRight;
    int8_t   motorLeftPWM;
    int8_t   motorRightPWM;
    uint8_t  directionL : 1;
    uint8_t  directionR : 1;
    uint8_t  chaosActive : 1;
    uint8_t  isChaosFrame : 1;
    uint8_t  state : 3;
    uint8_t  reserved : 1;
};

struct __attribute__((packed)) FileHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t headerSize;
    uint32_t crc32;
    uint32_t frameCount;
    uint32_t generation;
    uint16_t individual;
    uint16_t reserved;
};

struct __attribute__((packed)) GeneBinaryHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t popSize;
    uint32_t generation;
    uint32_t experimentId;
};

struct __attribute__((packed)) FrameLogHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t headerSize;
    uint32_t crc32;
    uint32_t frameCount;
    uint32_t generation;
    uint16_t individual;
    uint16_t reserved;
};

struct __attribute__((packed)) HistoryRecord {
    uint32_t timestamp;
    uint32_t generation;
    float    noveltyScore;
    uint32_t survivalTime;
    int32_t  distance_ticks;
    uint8_t  ruleCount;
    uint8_t  reserved[3];
};

struct __attribute__((packed)) ChaoticTestRecord {
    uint32_t timestamp;
    uint32_t generation;
    uint32_t individual;
    uint32_t chaosTriggerCount;
    uint32_t chaosTotalDuration;
    uint32_t chaosMaxDuration;
    uint32_t chaosFirstTime;
    uint32_t chaosLastTime;
    int32_t  baselineDistance;
    int32_t  chaosDistance;
    uint16_t baselineFrames;
    uint16_t chaosFrames;
    float    baselineAvgSpeedL;
    float    baselineAvgSpeedR;
    float    chaosAvgSpeedL;
    float    chaosAvgSpeedR;
    uint8_t  chaosSuccess;                // [审计D4] 过程指标: 编码器稳定退出标志
    uint8_t  chaosExitReason;             // [审计D4修复] 混沌退出原因: 0=编码器稳定 1=超时 2=个体终止
    uint8_t  chaosInterruptedCount;       // [审计D3修复] 混沌被异常中断次数(非正常退出)
    uint8_t  testTerminatedBy;
    uint8_t  reserved[2];
};

#define MAX_CHAOS_SNAPSHOTS 8

struct __attribute__((packed)) ChaosSnapshotEntry {
    int16_t  sensorLeft;
    int16_t  sensorRight;
    int8_t   motorLeftPWM;
    int8_t   motorRightPWM;
    uint16_t durationMs;
    uint32_t timestamp_ms;
    int16_t  rawNoiseL;      // [v10.6] 混沌快照时刻的原始 ADC 值 (左通道采样)
    int16_t  rawNoiseR;      // [v10.6] 混沌快照时刻的原始 ADC 值 (右通道采样)
};

struct __attribute__((packed)) ChaosSnapshotHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t headerSize;
    uint32_t crc32;
    uint8_t  count;
    uint8_t  reserved[3];
};

// ================================================================
// 前向声明
// ================================================================
struct Gene;
class Logger;
class SensorCalibration;
class RobustStorage;
class GeneStorage;
class NoveltyArchive;
class MotorController;
class EvolutionEngine;
class CarWebServer;
class RAMLogBuffer;
class RollingStorage;
class FileUtils;

// ================================================================
// Logger 类
// ================================================================
class Logger {
public:
    static void init() { Serial.begin(115200); delay(500); }
    static void log(const char* msg) {
        Serial.println("[OE] " + String(millis()/1000) + "s " + String(msg));
    }
    static void logf(const char* fmt, ...) {
        char buf[256];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        log(buf);
    }
};

// ================================================================
// [v10.5] SPIFFS 路径规范化函数 (强制规范 S4)
//   背景: Arduino-ESP32 Core >= 2.0.6 的 File.name() 返回的文件名
//         不带前导斜杠, 而本项目历史代码大量使用 startsWith("/xxx")
//         判断, 导致所有目录扫描函数失效。
//   规范: 所有 File.name() 的返回值必须先经本函数规范化后使用。
//   见文件顶端 "SPIFFS 路径规范化 强制规范" 区块。
// ================================================================
static inline String normPath(const String& name) {
    return name.startsWith("/") ? name : ("/" + name);
}

// ================================================================
// SensorCalibration 类
// ================================================================
class SensorCalibration {
private:
    static int leftBase, rightBase;
    static int16_t obstacleThreshold;
    static int16_t clearThreshold;

public:
    static void calibrate() {
        Logger::log("Sensor calibrating...");
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
        obstacleThreshold = 1500;
        clearThreshold = 600;
        Logger::logf("Calibration done: L=%d R=%d", leftBase, rightBase);
    }

    static void autoCalibrate() {
        Logger::log("[CAL] Calibrating sensors in empty space...");
        long sumL = 0, sumR = 0;
        for (int i = 0; i < 50; i++) {
            sumL += analogRead(PIN_SENSOR_LEFT);
            sumR += analogRead(PIN_SENSOR_RIGHT);
            delay(10);
        }
        int avgL = (int)(sumL / 50);
        int avgR = (int)(sumR / 50);
        setBaseline(avgL, avgR);
        Logger::logf("[CAL] Baseline set: L=%d, R=%d", avgL, avgR);
    }

    static void setBaseline(int l, int r) { leftBase = l; rightBase = r; }
    static int getBaselineLeft()  { return leftBase; }
    static int getBaselineRight() { return rightBase; }

    static void setThresholds(int16_t obs, int16_t clr) {
        obstacleThreshold = obs;
        clearThreshold = clr;
    }

    static int getLeftRaw()  { return analogRead(PIN_SENSOR_LEFT); }
    static int getRightRaw() { return analogRead(PIN_SENSOR_RIGHT); }

    static int16_t readLeft() {
        int raw = analogRead(PIN_SENSOR_LEFT);
        int diff = raw - leftBase;
        if (diff < -32000) diff = -32000;
        if (diff >  32000) diff =  32000;
        return (int16_t)diff;
    }
    static int16_t readRight() {
        int raw = analogRead(PIN_SENSOR_RIGHT);
        int diff = raw - rightBase;
        if (diff < -32000) diff = -32000;
        if (diff >  32000) diff =  32000;
        return (int16_t)diff;
    }

    static bool isObstacleLeft()  { return readLeft()  > obstacleThreshold; }
    static bool isObstacleRight() { return readRight() > obstacleThreshold; }
    static bool isClear() {
        return (readLeft() < clearThreshold && readRight() < clearThreshold);
    }
    static uint16_t readNoise() { return analogRead(PIN_NOISE_SOURCE); }
};

int SensorCalibration::leftBase = 0;
int SensorCalibration::rightBase = 0;
int16_t SensorCalibration::obstacleThreshold = 1500;
int16_t SensorCalibration::clearThreshold = 600;

// ================================================================
// CRC32工具
// ================================================================
class CRC32 {
public:
    static uint32_t calculate(const uint8_t* data, size_t len) {
        uint32_t crc = 0xFFFFFFFF;
        for (size_t i = 0; i < len; i++) {
            crc ^= data[i];
            for (int j = 0; j < 8; j++) {
                if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320;
                else crc >>= 1;
            }
        }
        return ~crc;
    }
    static bool verify(const uint8_t* data, size_t len, uint32_t expected) {
        return calculate(data, len) == expected;
    }
};

// ================================================================
// FileUtils 类声明
// ================================================================
class FileUtils {
public:
    static bool atomicWrite(const String& path, const uint8_t* data, size_t len);
    static bool atomicWriteString(const String& path, const String& content);
    static String safeRead(const String& path);
    static String readStringCapped(const String& path, size_t maxBytes);
    static size_t getFileSize(const String& path);
    static bool exists(const String& path);
};

// ================================================================
// RobustStorage 类
// ================================================================
class RobustStorage {
private:
    static const char* HISTORY_FILE;
    static bool fsReady;
    static uint32_t lastSaveTime;
    static uint32_t unsavedCount;
    static uint32_t totalSaved;
    static String pendingBuffer;
    static String chaosPendingBuffer;
    static uint32_t chaosUnsavedCount;
    static const size_t MAX_BUFFER_SIZE = 4096;
    static HistoryRecord ramHistory[1000];
    static int ramHistoryCount;
    static uint32_t lastFsRetryTime;
    static bool pendingBufferDirty;
    static const uint32_t FS_RETRY_INTERVAL = 5000;

    static bool appendToFile(const char* path, const String& data) {
        if (!fsReady) return false;
        File file = SPIFFS.open(path, FILE_APPEND);
        if (!file) {
            file = SPIFFS.open(path, FILE_WRITE);
            if (!file) return false;
        }
        size_t written = file.print(data);
        file.close();
        return written == data.length();
    }

    static bool isValidHistoryValue(uint32_t ts, uint32_t gen, float novelty,
        uint32_t survival, int32_t dist, uint8_t rules) {
        if (ts > 3600000UL) return false;
        if (gen > 1000000UL) return false;
        if (novelty < 0.0f || novelty > 10.0f) return false;
        if (survival > 3600000UL) return false;
        if (rules < MIN_RULES || rules > MAX_RULES) return false;
        return true;
    }

    static void loadHistory() {
        if (!fsReady) return;
        File file = SPIFFS.open(HISTORY_FILE, FILE_READ);
        if (!file) return;
        int count = 0;
        bool firstLine = true;
        while (file.available() && count < 1000) {
            String line = file.readStringUntil('\n');
            if (line.length() > 0 && line[line.length()-1] == '\r') {
                line = line.substring(0, line.length()-1);
            }
            if (line.length() < 10) { firstLine = false; continue; }

            if (firstLine) {
                firstLine = false;
                if (line.startsWith("timestamp") || line.startsWith("#")) {
                    continue;
                }
            }

            int partIdx = 0;
            String parts[6];
            for (int i = 0; i < line.length() && partIdx < 6; i++) {
                if (line[i] == ',') { partIdx++; continue; }
                parts[partIdx] += line[i];
            }
            if (partIdx == 5) {
                uint32_t ts = parts[0].toInt();
                uint32_t gen = parts[1].toInt();
                float novelty = parts[2].toFloat();
                uint32_t survival = parts[3].toInt();
                int32_t dist = parts[4].toInt();
                uint8_t rules = (uint8_t)parts[5].toInt();

                if (!isValidHistoryValue(ts, gen, novelty, survival, dist, rules)) {
                    continue;
                }

                ramHistory[count].timestamp = ts;
                ramHistory[count].generation = gen;
                ramHistory[count].noveltyScore = novelty;
                ramHistory[count].survivalTime = survival;
                ramHistory[count].distance_ticks = dist;
                ramHistory[count].ruleCount = rules;
                count++;
            }
        }
        ramHistoryCount = count;
        file.close();
        Logger::logf("📋 历史记录加载完成: %d 条", count);
    }

    static bool tryRemountSPIFFS() {
        if (fsReady) return true;
        uint32_t now = millis();
        if (now - lastFsRetryTime < FS_RETRY_INTERVAL) return false;
        lastFsRetryTime = now;
        Logger::log("🔄 Attempting to remount SPIFFS...");
        SPIFFS.end();
        if (SPIFFS.begin(false)) {
            fsReady = true;
            Logger::log("✅ SPIFFS remounted successfully");
            loadHistory();
            flushBuffer();
            flushChaosBuffer();
            return true;
        }
        return false;
    }

public:
    static void init() {
        fsReady = false;
        lastSaveTime = 0;
        unsavedCount = 0;
        totalSaved = 0;
        ramHistoryCount = 0;
        pendingBuffer.reserve(MAX_BUFFER_SIZE);
        chaosPendingBuffer.reserve(MAX_BUFFER_SIZE);
        chaosUnsavedCount = 0;
        lastFsRetryTime = 0;
        pendingBufferDirty = false;

        #if FORMAT_SPIFFS_ON_BOOT
            Logger::log("⚠️ FORMAT_SPIFFS_ON_BOOT is ENABLED - formatting SPIFFS...");
            if (SPIFFS.format()) {
                Logger::log("⚠️ SPIFFS formatted (user requested)");
            } else {
                Logger::log("❌ SPIFFS format failed!");
            }
        #endif

        if (!SPIFFS.begin(false)) {
            Logger::log("========================================");
            Logger::log("❌ SPIFFS mount FAILED");
            Logger::log("   🔒 AUTO-FORMAT DISABLED - Data preserved");
            Logger::log("   ⚠️ Running in RAM-only mode");
            Logger::log("========================================");
            fsReady = false;
        } else {
            fsReady = true;
            Logger::log("✅ SPIFFS mounted successfully");
            loadHistory();
        }
    }

    static void addRecord(const HistoryRecord& record, bool forceFlush = false) {
        if (ramHistoryCount < 1000) {
            ramHistory[ramHistoryCount++] = record;
        } else {
            for (int i = 1; i < 1000; i++) ramHistory[i-1] = ramHistory[i];
            ramHistory[999] = record;
        }
        char line[256];
        snprintf(line, sizeof(line), "%lu,%lu,%.4f,%lu,%ld,%d\n",
                 record.timestamp, record.generation, record.noveltyScore,
                 record.survivalTime, record.distance_ticks, record.ruleCount);
        pendingBuffer += String(line);
        unsavedCount++;
        totalSaved++;
        pendingBufferDirty = true;
        if (forceFlush || pendingBuffer.length() > MAX_BUFFER_SIZE || unsavedCount >= 5) {
            flushBuffer();
        }
    }

    static bool flushBuffer() {
        if (pendingBuffer.length() == 0) {
            unsavedCount = 0;
            pendingBufferDirty = false;
            return true;
        }
        if (!fsReady) {
            if (!tryRemountSPIFFS()) {
                if (pendingBuffer.length() > MAX_BUFFER_SIZE * 4) {
                    int lastNewline = pendingBuffer.lastIndexOf('\n', pendingBuffer.lastIndexOf('\n') - 1);
                    if (lastNewline > 0) {
                        pendingBuffer = pendingBuffer.substring(lastNewline + 1);
                    }
                }
                lastSaveTime = millis();
                return false;
            }
        }
        if (!SPIFFS.exists(HISTORY_FILE)) {
            String header = "# Note: rows are ordered by individual index within each generation\ntimestamp,generation,noveltyScore,survivalTime,distance_ticks,ruleCount\n";
            appendToFile(HISTORY_FILE, header);
        }
        bool success = appendToFile(HISTORY_FILE, pendingBuffer);
        if (success) {
            pendingBuffer = "";
            unsavedCount = 0;
            pendingBufferDirty = false;
            lastSaveTime = millis();
        } else {
            Logger::log("⚠️ flushBuffer: write failed, data retained in buffer");
            lastSaveTime = millis();
        }
        return success;
    }

    static bool flushChaosBuffer() {
        if (chaosPendingBuffer.length() == 0) {
            chaosUnsavedCount = 0;
            return true;
        }
        if (!fsReady) {
            if (!tryRemountSPIFFS()) {
                if (chaosPendingBuffer.length() > MAX_BUFFER_SIZE * 4) {
                    int lastNewline = chaosPendingBuffer.lastIndexOf('\n', chaosPendingBuffer.lastIndexOf('\n') - 1);
                    if (lastNewline > 0) {
                        chaosPendingBuffer = chaosPendingBuffer.substring(lastNewline + 1);
                    }
                }
                return false;
            }
        }
        File checkFile = SPIFFS.open(CHAOS_HISTORY_FILE, FILE_READ);
        bool needHeader = !checkFile || checkFile.size() == 0;
        if (checkFile) checkFile.close();
        if (needHeader) {
            chaosPendingBuffer = String("timestamp,generation,individual,chaosTriggerCount,chaosTotalDuration,"
                "chaosMaxDuration,chaosFirstTime,chaosLastTime,"
                "baselineDistance,chaosDistance,baselineFrames,chaosFrames,"
                "baselineAvgSpeedL,baselineAvgSpeedR,chaosAvgSpeedL,chaosAvgSpeedR,"
                "chaosSuccess,chaosExitReason,chaosInterruptedCount,testTerminatedBy\n") + chaosPendingBuffer;
        }
        bool success = appendToFile(CHAOS_HISTORY_FILE, chaosPendingBuffer);
        if (success) {
            chaosPendingBuffer = "";
            chaosUnsavedCount = 0;
        } else {
            Logger::log("⚠️ flushChaosBuffer: write failed, data retained in buffer");
        }
        return success;
    }

    static void forceSave() {
        if (pendingBuffer.length() > 0) flushBuffer();
        if (chaosPendingBuffer.length() > 0) flushChaosBuffer();
    }

    static bool isReady() { return fsReady; }
    static uint32_t getTotalSaved() { return totalSaved; }
    static bool hasPendingData() { return pendingBuffer.length() > 0 || chaosPendingBuffer.length() > 0; }

    static void tick() {
        if (millis() - lastSaveTime > STORAGE_SAVE_INTERVAL_MS) {
            if (pendingBuffer.length() > 0) flushBuffer();
            if (chaosPendingBuffer.length() > 0) flushChaosBuffer();
        }
    }

    static void clearAll() {
        pendingBuffer = "";
        unsavedCount = 0;
        ramHistoryCount = 0;
        chaosPendingBuffer = "";
        chaosUnsavedCount = 0;
        pendingBufferDirty = false;
    }

    static String getCSVData() {
        if (pendingBuffer.length() > 0) flushBuffer();
        if (fsReady && SPIFFS.exists(HISTORY_FILE)) {
            return FileUtils::readStringCapped(HISTORY_FILE, 131072);
        }
        String csv = "# Note: rows are ordered by individual index within each generation\ntimestamp,generation,noveltyScore,survivalTime,distance_ticks,ruleCount\n";
        for (int i = 0; i < ramHistoryCount; i++) {
            csv += String(ramHistory[i].timestamp) + ",";
            csv += String(ramHistory[i].generation) + ",";
            csv += String(ramHistory[i].noveltyScore, 4) + ",";
            csv += String(ramHistory[i].survivalTime) + ",";
            csv += String(ramHistory[i].distance_ticks) + ",";
            csv += String(ramHistory[i].ruleCount) + "\n";
        }
        return csv;
    }

    static const char* CHAOS_HISTORY_FILE;

    static void addChaosRecord(const ChaoticTestRecord& record, bool forceFlush = false) {
        char line[512];
        snprintf(line, sizeof(line),
            "%lu,%lu,%d,%d,%lu,%lu,%lu,%lu,%ld,%ld,%d,%d,%.2f,%.2f,%.2f,%.2f,%d,%d,%d,%d\n",
            record.timestamp, record.generation, record.individual,
            record.chaosTriggerCount, record.chaosTotalDuration,
            record.chaosMaxDuration, record.chaosFirstTime,
            record.chaosLastTime,
            (long)record.baselineDistance, (long)record.chaosDistance,
            record.baselineFrames, record.chaosFrames,
            record.baselineAvgSpeedL, record.baselineAvgSpeedR,
            record.chaosAvgSpeedL, record.chaosAvgSpeedR,
            record.chaosSuccess, record.chaosExitReason, record.chaosInterruptedCount, record.testTerminatedBy);

        chaosPendingBuffer += String(line);
        chaosUnsavedCount++;
        totalSaved++;
        if (forceFlush || chaosPendingBuffer.length() > MAX_BUFFER_SIZE || chaosUnsavedCount >= 5) {
            flushChaosBuffer();
        }
    }

    static String getChaosHistoryCSV() {
        if (!fsReady) return "";
        return FileUtils::readStringCapped(CHAOS_HISTORY_FILE, 131072);
    }

    static bool formatSPIFFS(bool confirmed = false) {
        if (!confirmed) {
            Logger::log("⚠️ formatSPIFFS() called without confirmation");
            return false;
        }
        Logger::log("========================================");
        Logger::log("⚠️ MANUAL SPIFFS FORMAT EXECUTED");
        Logger::log("========================================");
        SPIFFS.end();
        bool success = SPIFFS.format();
        if (success) {
            Logger::log("✅ SPIFFS formatted successfully");
            fsReady = SPIFFS.begin(false);
            if (fsReady) {
                Logger::log("✅ SPIFFS remounted");
            } else {
                Logger::log("❌ SPIFFS remount failed");
            }
        } else {
            Logger::log("❌ SPIFFS format failed");
        }
        return success;
    }

    static float getStorageHealth() {
        if (!fsReady) return 0.0f;
        size_t total = SPIFFS.totalBytes();
        size_t used = 0;
        File root = SPIFFS.open("/");
        if (root) {
            while (File f = root.openNextFile()) {
                used += f.size();
                f.close();
            }
            root.close();
        }
        float ratio = (float)used / total;
        if (ratio < 0.3f) return 1.0f;
        if (ratio < 0.6f) return 0.8f;
        if (ratio < 0.8f) return 0.5f;
        return 0.2f;
    }
};

// ★★★ RobustStorage 静态成员定义 ★★★
const char* RobustStorage::HISTORY_FILE = "/oe_history.csv";
const char* RobustStorage::CHAOS_HISTORY_FILE = "/chaos_history.csv";
bool RobustStorage::fsReady = false;
uint32_t RobustStorage::lastSaveTime = 0;
uint32_t RobustStorage::unsavedCount = 0;
uint32_t RobustStorage::totalSaved = 0;
String RobustStorage::pendingBuffer = "";
String RobustStorage::chaosPendingBuffer = "";
uint32_t RobustStorage::chaosUnsavedCount = 0;
HistoryRecord RobustStorage::ramHistory[1000];
int RobustStorage::ramHistoryCount = 0;
uint32_t RobustStorage::lastFsRetryTime = 0;
bool RobustStorage::pendingBufferDirty = false;

// ================================================================
// FileUtils 方法实现
// ================================================================
bool FileUtils::atomicWrite(const String& path, const uint8_t* data, size_t len) {
    if (!RobustStorage::isReady()) return false;
    String tempPath = path + "~";
    for (int retry = 0; retry < 3; retry++) {
        if (SPIFFS.exists(tempPath)) SPIFFS.remove(tempPath);
        File tempFile = SPIFFS.open(tempPath, FILE_WRITE);
        if (!tempFile) { delay(10); continue; }
        size_t written = tempFile.write(data, len);
        tempFile.close();
        if (written != len) { SPIFFS.remove(tempPath); delay(10); continue; }
        if (!SPIFFS.rename(tempPath, path)) {
            if (SPIFFS.exists(path)) SPIFFS.remove(path);
            if (!SPIFFS.rename(tempPath, path)) {
                SPIFFS.remove(tempPath);
                delay(10);
                continue;
            }
        }
        return true;
    }
    return false;
}

bool FileUtils::atomicWriteString(const String& path, const String& content) {
    return atomicWrite(path, (const uint8_t*)content.c_str(), content.length());
}

String FileUtils::safeRead(const String& path) {
    if (!RobustStorage::isReady()) return "";
    if (!SPIFFS.exists(path)) return "";
    File file = SPIFFS.open(path, FILE_READ);
    if (!file) return "";
    String content = file.readString();
    file.close();
    return content;
}

String FileUtils::readStringCapped(const String& path, size_t maxBytes) {
    if (!RobustStorage::isReady()) return "";
    if (!SPIFFS.exists(path)) return "";
    File file = SPIFFS.open(path, FILE_READ);
    if (!file) return "";
    String out;
    size_t sz = file.size();
    if (sz > maxBytes) {
        out.reserve(maxBytes);
        uint8_t* tmp = (uint8_t*)malloc(maxBytes);
        if (tmp) {
            size_t n = file.read(tmp, maxBytes);
            out.concat((const char*)tmp, n);
            free(tmp);
        }
    } else {
        out = file.readString();
    }
    file.close();
    return out;
}

size_t FileUtils::getFileSize(const String& path) {
    if (!RobustStorage::isReady()) return 0;
    if (!SPIFFS.exists(path)) return 0;
    File file = SPIFFS.open(path, FILE_READ);
    if (!file) return 0;
    size_t size = file.size();
    file.close();
    return size;
}

bool FileUtils::exists(const String& path) {
    if (!RobustStorage::isReady()) return false;
    return SPIFFS.exists(path);
}


// ================================================================
// TieredStorageManager 类
//   [v10.5] 全部目录扫描经 normPath() 规范化
// ================================================================
class TieredStorageManager {
public:
    static int getDataLevel(const String& filename) {
        if (filename == "/novelty_archive.bin" ||
            filename == "/novelty_archive.bin.bak" ||
            filename == "/novelty_archive.emergency.bin") {
            return DATA_LEVEL_CORE;
        }
        if (filename.startsWith("/gen_") ||
            filename.startsWith("/chaos_g")) {
            return DATA_LEVEL_GENE;
        }
        if (filename.startsWith("/pop_gen_")) {
            return DATA_LEVEL_POP;
        }
        if (filename.startsWith("/frm_") ||
            filename == "/oe_history.csv" ||
            filename == "/chaos_history.csv" ||
            filename == "/experiment_state.mrk") {
            return DATA_LEVEL_PROCESS;
        }
        return 0;
    }

    static String getLevelName(int level) {
        switch(level) {
            case DATA_LEVEL_CORE:    return "🔒 L1 核心";
            case DATA_LEVEL_GENE:    return "🧬 L2 基因";
            case DATA_LEVEL_POP:     return "📦 L3 种群";
            case DATA_LEVEL_PROCESS: return "📋 L4 过程";
            default:                 return "❓ 未知";
        }
    }

    static bool hasActiveDependency(const String& filename, uint32_t currentGen) {
        if (filename.startsWith("/pop_gen_")) {
            String numStr = filename.substring(9, filename.lastIndexOf('.'));
            uint32_t gen = numStr.toInt();
            if (gen > 0 && gen == currentGen) {
                return true;
            }
        }
        if (filename.startsWith("/gen_") || filename.startsWith("/chaos_")) {
            return true;
        }
        return false;
    }

    static int cleanL3AndL4(bool confirmed = false, bool previewOnly = false, uint32_t currentGen = 0) {
        if (!confirmed) {
            Logger::log("⚠️ cleanL3AndL4() called without confirmation");
            return -1;
        }
        if (!RobustStorage::isReady()) return 0;

        std::vector<String> toDelete;
        std::vector<String> protectedByDependency;

        File root = SPIFFS.open("/");
        if (root) {
            while (File f = root.openNextFile()) {
                String name = normPath(f.name());   // [v10.5] 规范化
                int level = getDataLevel(name);

                if (level == DATA_LEVEL_POP || level == DATA_LEVEL_PROCESS) {
                    if (hasActiveDependency(name, currentGen)) {
                        protectedByDependency.push_back(name);
                        Logger::logf("🔒 Protected (dependency): %s", name.c_str());
                        continue;
                    }
                    if (level == DATA_LEVEL_POP) {
                        String numStr = name.substring(9, name.lastIndexOf('.'));
                        uint32_t gen = numStr.toInt();
                        if (gen == currentGen) {
                            protectedByDependency.push_back(name);
                            Logger::logf("🔒 Protected (current gen): %s", name.c_str());
                            continue;
                        }
                    }
                    toDelete.push_back(name);
                }
                f.close();
            }
            root.close();
        }

        if (previewOnly) {
            Logger::log("📋 Preview of files to delete:");
            for (const String& name : toDelete) {
                Logger::logf("  🗑️ %s", name.c_str());
            }
            Logger::logf("📋 Protected: %d files", protectedByDependency.size());
            return toDelete.size();
        }

        int deleted = 0;
        for (const String& name : toDelete) {
            if (SPIFFS.remove(name)) {
                Logger::logf("🗑️ Deleted: %s", name.c_str());
                deleted++;
            }
        }

        Logger::logf("✅ Cleaned %d files (L3+L4), %d protected by dependency",
                     deleted, protectedByDependency.size());
        return deleted;
    }

    static std::vector<uint32_t> getPopulationGenerations() {
        std::vector<uint32_t> gens;
        if (!RobustStorage::isReady()) return gens;

        File root = SPIFFS.open("/");
        if (!root) return gens;

        while (File f = root.openNextFile()) {
            String name = normPath(f.name());   // [v10.5] 规范化
            if (name.startsWith("/pop_gen_") && name.endsWith(".bin")) {
                String numStr = name.substring(9, name.lastIndexOf('.'));
                uint32_t gen = numStr.toInt();
                if (gen > 0) gens.push_back(gen);
            }
            f.close();
        }
        root.close();

        std::sort(gens.begin(), gens.end());
        return gens;
    }

    static String getCleanPreview(uint32_t currentGen = 0) {
        if (!RobustStorage::isReady()) return "SPIFFS not ready";

        std::vector<String> toDelete;
        std::vector<String> protectedFiles;

        File root = SPIFFS.open("/");
        if (root) {
            while (File f = root.openNextFile()) {
                String name = normPath(f.name());   // [v10.5] 规范化
                int level = getDataLevel(name);

                if (level == DATA_LEVEL_CORE || level == DATA_LEVEL_GENE) {
                    protectedFiles.push_back(name);
                    continue;
                }
                if (level == DATA_LEVEL_POP || level == DATA_LEVEL_PROCESS) {
                    if (hasActiveDependency(name, currentGen)) {
                        protectedFiles.push_back(name + " (dependency)");
                        continue;
                    }
                    if (level == DATA_LEVEL_POP) {
                        String numStr = name.substring(9, name.lastIndexOf('.'));
                        uint32_t gen = numStr.toInt();
                        if (gen == currentGen) {
                            protectedFiles.push_back(name + " (current)");
                            continue;
                        }
                    }
                    toDelete.push_back(name);
                }
                f.close();
            }
            root.close();
        }

        String result = "📋 Clean Preview:\n";
        result += "🗑️ To delete (" + String(toDelete.size()) + " files):\n";
        for (const String& name : toDelete) {
            result += "  - " + name + "\n";
        }
        result += "\n🔒 Protected (" + String(protectedFiles.size()) + " files):\n";
        for (const String& name : protectedFiles) {
            result += "  - " + name + "\n";
        }
        return result;
    }

    static String getStorageStats() {
        if (!RobustStorage::isReady()) return "SPIFFS not ready";

        String result = "";
        size_t coreSize = 0, geneSize = 0, popSize = 0, processSize = 0;
        int coreCount = 0, geneCount = 0, popCount = 0, processCount = 0;

        File root = SPIFFS.open("/");
        if (root) {
            while (File f = root.openNextFile()) {
                String name = normPath(f.name());   // [v10.5] 规范化
                size_t size = f.size();
                int level = getDataLevel(name);

                switch(level) {
                    case DATA_LEVEL_CORE:
                        coreSize += size; coreCount++;
                        break;
                    case DATA_LEVEL_GENE:
                        geneSize += size; geneCount++;
                        break;
                    case DATA_LEVEL_POP:
                        popSize += size; popCount++;
                        break;
                    case DATA_LEVEL_PROCESS:
                        processSize += size; processCount++;
                        break;
                }
                f.close();
            }
            root.close();
        }

        result += "L1 核心: " + String(coreCount) + " 文件, " + String(coreSize/1024) + " KB\n";
        result += "L2 基因: " + String(geneCount) + " 文件, " + String(geneSize/1024) + " KB\n";
        result += "L3 种群: " + String(popCount) + " 文件, " + String(popSize/1024) + " KB\n";
        result += "L4 过程: " + String(processCount) + " 文件, " + String(processSize/1024) + " KB\n";

        size_t total = SPIFFS.totalBytes();
        size_t used = coreSize + geneSize + popSize + processSize;
        result += "总计: " + String(used/1024) + " KB / " + String(total/1024) + " KB";
        result += " (" + String(100 * used / total) + "%)\n";

        return result;
    }
};

// ================================================================
// FirmwareVersionManager 类
// ================================================================
class FirmwareVersionManager {
public:
    static bool isNewVersion() {
        if (!RobustStorage::isReady()) return false;
        if (!SPIFFS.exists(VERSION_MARKER_FILE)) {
            writeVersionMarker();
            Logger::log("📌 First boot: version marker created (data preserved)");
            return false;
        }
        File marker = SPIFFS.open(VERSION_MARKER_FILE, FILE_READ);
        if (!marker) return false;
        String savedVersion = marker.readString();
        marker.close();
        savedVersion.trim();

        if (savedVersion != String(FIRMWARE_VERSION)) {
            Logger::log("========================================");
            Logger::log("⚠️ VERSION CHANGE DETECTED");
            Logger::logf("   Old: %s", savedVersion.c_str());
            Logger::logf("   New: %s", FIRMWARE_VERSION);
            Logger::log("   🔒 DATA PRESERVED - Auto-clear DISABLED");
            Logger::log("   📋 Use Web UI for manual cleanup");
            Logger::log("========================================");
            writeVersionMarker();
            return false;
        }
        return false;
    }

    static void cleanAllData() {
        Logger::log("========================================");
        Logger::log("⚠️ cleanAllData() called - DATA PRESERVED");
        Logger::log("   🔒 Auto-clear is DISABLED");
        Logger::log("   📋 Use Web UI for manual cleanup");
        Logger::log("========================================");
    }

    static void writeVersionMarker() {
        if (!RobustStorage::isReady()) return;
        if (SPIFFS.exists(VERSION_MARKER_FILE)) SPIFFS.remove(VERSION_MARKER_FILE);
        File marker = SPIFFS.open(VERSION_MARKER_FILE, FILE_WRITE);
        if (marker) { marker.println(FIRMWARE_VERSION); marker.close(); }
    }
};

// ================================================================
// Gene 结构体
// ================================================================
struct Gene {
    uint8_t ruleCount;
    BehaviorRule rules[MAX_RULES];
    uint32_t survival_time;
    int32_t  distance_ticks;
    float noveltyScore;
    BehaviorDescriptor behavior;
    BehaviorDescriptor baselineBehavior;

    int16_t obstacleThreshold;
    int16_t clearThreshold;
    int16_t encoderDiffThreshold;
    int16_t encoderDiffMin;
    int16_t wheelSpinThreshold;
    int16_t wheelStopThreshold;
    uint8_t stuckWindowSize;
    int16_t chaosNoiseAmplifier;
    int16_t chaosMinPwm;
    uint16_t chaosTimeoutMs;
    uint16_t chaosForceTimeoutMs;

    uint8_t  chaosRuleCount;
    uint8_t  chaosRulesStartIndex;
    bool     hasChaosRules;

    void init() {
        ruleCount = PhysicalRandom::getRange(MIN_RULES, MAX_RULES + 1);
        for (int i = 0; i < ruleCount; i++) rules[i].randomize();
        survival_time = 0; distance_ticks = 0;
        noveltyScore = 0; behavior.init(); baselineBehavior.init();
        obstacleThreshold    = PhysicalRandom::getRange(800, 2501);
        clearThreshold       = PhysicalRandom::getRange(200, 1201);
        encoderDiffThreshold = PhysicalRandom::getRange(10, 101);
        encoderDiffMin       = PhysicalRandom::getRange(1, 21);
        wheelSpinThreshold   = PhysicalRandom::getRange(5, 81);
        wheelStopThreshold   = PhysicalRandom::getRange(1, 11);
        stuckWindowSize      = PhysicalRandom::getRange(3, 21);
        chaosNoiseAmplifier  = PhysicalRandom::getRange(50, 401);
        chaosMinPwm          = PhysicalRandom::getRange(5, 81);
        chaosTimeoutMs       = PhysicalRandom::getRange(CHAOS_TIMEOUT_MIN, CHAOS_TIMEOUT_MAX + 1);
        chaosForceTimeoutMs  = PhysicalRandom::getRange(CHAOS_FORCE_TIMEOUT_MIN, CHAOS_FORCE_TIMEOUT_MAX + 1);
        chaosRuleCount       = 0;
        chaosRulesStartIndex = 0;
        hasChaosRules        = false;
    }

    bool evaluateCondition(int ruleIdx, int leftSensor, int rightSensor,
                           int32_t distance, uint32_t elapsed,
                           MotorState currentState) const;

    void mutate(float mutationRate) {
        for (int i = 0; i < ruleCount; i++) {
            if (PhysicalRandom::getRange(0, 1000) < mutationRate * 1000) { rules[i].condValue += PhysicalRandom::getRange(-200, 201); rules[i].clamp(); }
            if (PhysicalRandom::getRange(0, 1000) < mutationRate * 1000) { rules[i].motorL += PhysicalRandom::getRange(-50, 51); rules[i].clamp(); }
            if (PhysicalRandom::getRange(0, 1000) < mutationRate * 1000) { rules[i].motorR += PhysicalRandom::getRange(-50, 51); rules[i].clamp(); }
            if (PhysicalRandom::getRange(0, 1000) < mutationRate * 500) { rules[i].durationMs += PhysicalRandom::getRange(-200, 201); rules[i].clamp(); }

            if (PhysicalRandom::getRange(0, 1000) < mutationRate * 300) {
                rules[i].condType = (uint8_t)PhysicalRandom::getRange(0, COND_TYPE_MAX);
                rules[i].clamp();
            }
            if (PhysicalRandom::getRange(0, 1000) < mutationRate * 300) {
                rules[i].condOp = (uint8_t)PhysicalRandom::getRange(0, OP_TYPE_MAX);
                rules[i].clamp();
            }
            if (PhysicalRandom::getRange(0, 1000) < mutationRate * 200) {
                rules[i].nextRule = (uint8_t)PhysicalRandom::getRange(0, min((int)ruleCount, MAX_RULES));
                rules[i].clamp();
            }
        }

        if (PhysicalRandom::getRange(0, 1000) < mutationRate * 150 && ruleCount < MAX_RULES) {
            rules[ruleCount].randomize(); ruleCount++;
        }
        if (PhysicalRandom::getRange(0, 1000) < mutationRate * 100 && ruleCount > MIN_RULES) {
            int delIdx = PhysicalRandom::getRange(0, ruleCount); rules[delIdx] = rules[ruleCount - 1]; ruleCount--;
        }
        if (PhysicalRandom::getRange(0, 1000) < mutationRate * 80 && ruleCount >= 2) {
            int a = PhysicalRandom::getRange(0, ruleCount), b = PhysicalRandom::getRange(0, ruleCount);
            if (a != b) { BehaviorRule tmp = rules[a]; rules[a] = rules[b]; rules[b] = tmp; }
        }
        if (PhysicalRandom::getRange(0, 1000) < mutationRate * 500) { obstacleThreshold    += PhysicalRandom::getRange(-100, 101); obstacleThreshold    = constrain(obstacleThreshold,    800, 2500); }
        if (PhysicalRandom::getRange(0, 1000) < mutationRate * 500) { clearThreshold       += PhysicalRandom::getRange(-50, 51);   clearThreshold       = constrain(clearThreshold,       200, 1200); }
        if (PhysicalRandom::getRange(0, 1000) < mutationRate * 500) { encoderDiffThreshold += PhysicalRandom::getRange(-5, 6);     encoderDiffThreshold = constrain(encoderDiffThreshold, 10,  100); }
        if (PhysicalRandom::getRange(0, 1000) < mutationRate * 500) { encoderDiffMin       += PhysicalRandom::getRange(-2, 3);     encoderDiffMin       = constrain(encoderDiffMin,       1,   20); }
        if (PhysicalRandom::getRange(0, 1000) < mutationRate * 500) { wheelSpinThreshold   += PhysicalRandom::getRange(-5, 6);     wheelSpinThreshold   = constrain(wheelSpinThreshold,   5,   80); }
        if (PhysicalRandom::getRange(0, 1000) < mutationRate * 500) { wheelStopThreshold   += PhysicalRandom::getRange(-1, 2);     wheelStopThreshold   = constrain(wheelStopThreshold,   1,   10); }
        if (PhysicalRandom::getRange(0, 1000) < mutationRate * 300) { stuckWindowSize      += PhysicalRandom::getRange(-2, 3);     stuckWindowSize      = constrain(stuckWindowSize,      3,   20); }
        if (PhysicalRandom::getRange(0, 1000) < mutationRate * 500) { chaosNoiseAmplifier  += PhysicalRandom::getRange(-30, 31);   chaosNoiseAmplifier  = constrain(chaosNoiseAmplifier,  50,  400); }
        if (PhysicalRandom::getRange(0, 1000) < mutationRate * 500) { chaosMinPwm          += PhysicalRandom::getRange(-5, 6);     chaosMinPwm          = constrain(chaosMinPwm,          5,   80); }
        if (PhysicalRandom::getRange(0, 1000) < mutationRate * 500) {
            chaosTimeoutMs += PhysicalRandom::getRange(-300, 301);
            chaosTimeoutMs = constrain(chaosTimeoutMs, CHAOS_TIMEOUT_MIN, CHAOS_TIMEOUT_MAX);
        }
        if (PhysicalRandom::getRange(0, 1000) < mutationRate * 500) {
            chaosForceTimeoutMs += PhysicalRandom::getRange(-500, 501);
            chaosForceTimeoutMs = constrain(chaosForceTimeoutMs, CHAOS_FORCE_TIMEOUT_MIN, CHAOS_FORCE_TIMEOUT_MAX);
        }
    }

    static void crossover(const Gene& p1, const Gene& p2, Gene& child) {
        int cut1 = PhysicalRandom::getRange(0, p1.ruleCount), cut2 = PhysicalRandom::getRange(0, p2.ruleCount);
        int fromP1 = cut1, fromP2 = p2.ruleCount - cut2;
        child.ruleCount = constrain(fromP1 + fromP2, MIN_RULES, MAX_RULES);
        for (int i = 0; i < fromP1 && i < child.ruleCount; i++) child.rules[i] = p1.rules[i];
        for (int i = 0; i < fromP2 && (fromP1 + i) < child.ruleCount; i++) child.rules[fromP1 + i] = p2.rules[cut2 + i];
        for (int i = fromP1 + fromP2; i < child.ruleCount; i++) child.rules[i] = p1.rules[i % p1.ruleCount];
        child.survival_time = 0; child.distance_ticks = 0;
        child.noveltyScore = 0; child.behavior.init(); child.baselineBehavior.init();
        child.obstacleThreshold    = (PhysicalRandom::getRange(0,2) == 0) ? p1.obstacleThreshold    : p2.obstacleThreshold;
        child.clearThreshold       = (PhysicalRandom::getRange(0,2) == 0) ? p1.clearThreshold       : p2.clearThreshold;
        child.encoderDiffThreshold = (PhysicalRandom::getRange(0,2) == 0) ? p1.encoderDiffThreshold : p2.encoderDiffThreshold;
        child.encoderDiffMin       = (PhysicalRandom::getRange(0,2) == 0) ? p1.encoderDiffMin       : p2.encoderDiffMin;
        child.wheelSpinThreshold   = (PhysicalRandom::getRange(0,2) == 0) ? p1.wheelSpinThreshold   : p2.wheelSpinThreshold;
        child.wheelStopThreshold   = (PhysicalRandom::getRange(0,2) == 0) ? p1.wheelStopThreshold   : p2.wheelStopThreshold;
        child.stuckWindowSize      = (PhysicalRandom::getRange(0,2) == 0) ? p1.stuckWindowSize      : p2.stuckWindowSize;
        child.chaosNoiseAmplifier  = (PhysicalRandom::getRange(0,2) == 0) ? p1.chaosNoiseAmplifier  : p2.chaosNoiseAmplifier;
        child.chaosMinPwm          = (PhysicalRandom::getRange(0,2) == 0) ? p1.chaosMinPwm          : p2.chaosMinPwm;
        child.chaosTimeoutMs       = (PhysicalRandom::getRange(0,2) == 0) ? p1.chaosTimeoutMs       : p2.chaosTimeoutMs;
        child.chaosForceTimeoutMs  = (PhysicalRandom::getRange(0,2) == 0) ? p1.chaosForceTimeoutMs  : p2.chaosForceTimeoutMs;

        int chaosInherited = 0;
        if (p1.hasChaosRules && p2.hasChaosRules) {
            const Gene& donor = (PhysicalRandom::getRange(0, 2) == 0) ? p1 : p2;
            int donorStart = donor.chaosRulesStartIndex;
            int donorCount = donor.chaosRuleCount;
            while (child.ruleCount < MAX_RULES && chaosInherited < donorCount) {
                child.rules[child.ruleCount] = donor.rules[donorStart + chaosInherited];
                child.ruleCount++;
                chaosInherited++;
            }
        } else if (p1.hasChaosRules) {
            while (child.ruleCount < MAX_RULES && chaosInherited < p1.chaosRuleCount) {
                child.rules[child.ruleCount] = p1.rules[p1.chaosRulesStartIndex + chaosInherited];
                child.ruleCount++;
                chaosInherited++;
            }
        } else if (p2.hasChaosRules) {
            while (child.ruleCount < MAX_RULES && chaosInherited < p2.chaosRuleCount) {
                child.rules[child.ruleCount] = p2.rules[p2.chaosRulesStartIndex + chaosInherited];
                child.ruleCount++;
                chaosInherited++;
            }
        }
        if (chaosInherited > 0) {
            child.hasChaosRules = true;
            child.chaosRuleCount = chaosInherited;
            child.chaosRulesStartIndex = child.ruleCount - chaosInherited;
        } else {
            child.hasChaosRules = false;
            child.chaosRuleCount = 0;
            child.chaosRulesStartIndex = 0;
        }
    }
};

// ================================================================
// Gene::evaluateCondition
// ================================================================
bool Gene::evaluateCondition(int ruleIdx, int leftSensor, int rightSensor,
                             int32_t distance, uint32_t elapsed,
                             MotorState currentState) const {
    const BehaviorRule& r = rules[ruleIdx];
    int value = 0;
    switch (r.condType) {
        case COND_SENSOR_LEFT:
            value = leftSensor + (int)(PhysicalRandom::getFloat() * 200 - 100);
            break;
        case COND_SENSOR_RIGHT:
            value = rightSensor + (int)(PhysicalRandom::getFloat() * 200 - 100);
            break;
        case COND_SENSOR_BOTH:
            value = leftSensor + rightSensor;
            break;
        case COND_SENSOR_ANY:
            value = max(leftSensor, rightSensor);
            break;
        case COND_DISTANCE:
            value = (int)distance;
            break;
        case COND_TIME:
            value = (int)elapsed;
            break;
        case COND_IDLE:
            value = (currentState == STATE_IDLE) ? 1 : 0;
            break;
        case COND_ALWAYS:
            return true;
        default:
            return false;
    }
    return r.compare(value, r.condOp, r.condValue);
}

// ================================================================
// RollingStorage 类
//   [v10.5]  所有 File.name() 经 normPath() 规范化
//   [v10.12] 新增 enforceRetention(): 真正的滚动删除
// ================================================================
class RollingStorage {
private:
    static const int MAX_GENERATIONS = SPIFFS_MAX_GENERATIONS;
    static const int MAX_INDIVIDUALS_PER_GEN = SPIFFS_MAX_INDIVIDUALS_PER_GEN;
    static uint32_t freeSpaceThreshold;
    static int savedCount[100];
    static uint32_t currentGenerationForStorage;
    static int cleanupAttempts;
    static uint32_t lastCleanupWarningTime;

    struct ActiveDependency {
        uint32_t generation;
        int individual;
        bool isActive;
        uint32_t startTime;
    };
    static ActiveDependency activeDependencies[16];
    static int dependencyCount;

    static const uint32_t SAFETY_MARGIN = 4096;

    // ------------------------------------------------------------
    // 基础工具
    // ------------------------------------------------------------
    static size_t getFreeSpace() {
        if (!RobustStorage::isReady()) return 0;
        size_t used = 0;
        File root = SPIFFS.open("/");
        if (!root) return 0;
        while (File f = root.openNextFile()) {
            used += f.size();
            f.close();
        }
        root.close();
        size_t total = SPIFFS.totalBytes();
        return (total > used) ? (total - used) : 0;
    }

    static bool hasActiveDependency(uint32_t gen) {
        uint32_t now = millis();
        for (int i = 0; i < dependencyCount; i++) {
            if (now - activeDependencies[i].startTime > 60000) {
                activeDependencies[i].isActive = false;
                continue;
            }
            if (activeDependencies[i].isActive &&
                activeDependencies[i].generation == gen) {
                return true;
            }
        }
        return false;
    }

    static void cleanExpiredDependencies() {
        uint32_t now = millis();
        for (int i = 0; i < dependencyCount; i++) {
            if (now - activeDependencies[i].startTime > 60000) {
                activeDependencies[i].isActive = false;
            }
        }
    }

    // ------------------------------------------------------------
    // 收集某一代的所有配套文件
    //   覆盖: pop / nova / gen / frm / chaos_snaps
    // ------------------------------------------------------------
    static void collectGenerationFiles(uint32_t gen, std::vector<String>& out) {
        if (!RobustStorage::isReady()) return;
        if (gen == 0) return;

        const String popPath         = "/pop_gen_" + String(gen) + ".bin";
        const String novaPath        = "/nova_gen_" + String(gen) + ".bin";
        const String genPrefix       = "/gen_" + String(gen) + "_";
        const String frmPrefix       = "/frm_" + String(gen) + "_";
        const String chaosSnapPrefix = "/chaos_snaps_g" + String(gen) + "_";

        if (SPIFFS.exists(popPath))  out.push_back(popPath);
        if (SPIFFS.exists(novaPath)) out.push_back(novaPath);

        File root = SPIFFS.open("/");
        if (!root) return;
        while (File f = root.openNextFile()) {
            String name = normPath(f.name());   // [v10.5] 规范化
            if (name.startsWith(genPrefix) ||
                name.startsWith(frmPrefix) ||
                name.startsWith(chaosSnapPrefix)) {
                out.push_back(name);
            }
            f.close();
        }
        root.close();
    }

    // ------------------------------------------------------------
    // 扫描 SPIFFS 上所有"代"
    //   与 getStoredGenerations() 的区别:
    //     getStoredGenerations() 只认 /pop_gen_*.bin
    //     本函数额外扫描 /gen_*.csv / /frm_*.bin / /chaos_snaps_*.bin
    //     /nova_gen_*.bin, 用于发现残留代
    // ------------------------------------------------------------
    static std::vector<uint32_t> scanAllGenerations() {
        std::vector<uint32_t> gens;
        if (!RobustStorage::isReady()) return gens;

        auto pushUnique = [&](uint32_t g) {
            if (g == 0) return;
            for (auto x : gens) if (x == g) return;
            gens.push_back(g);
        };

        File root = SPIFFS.open("/");
        if (!root) return gens;
        while (File f = root.openNextFile()) {
            String name = normPath(f.name());
            if (name.startsWith("/pop_gen_") && name.endsWith(".bin")) {
                uint32_t g = name.substring(9, name.lastIndexOf('.')).toInt();
                pushUnique(g);
            } else if (name.startsWith("/nova_gen_") && name.endsWith(".bin")) {
                uint32_t g = name.substring(10, name.lastIndexOf('.')).toInt();
                pushUnique(g);
            } else if (name.startsWith("/gen_") && name.endsWith(".csv")) {
                int us = name.indexOf('_', 5);
                if (us > 5) pushUnique(name.substring(5, us).toInt());
            } else if (name.startsWith("/frm_") && name.endsWith(".bin")) {
                int us = name.indexOf('_', 5);
                if (us > 5) pushUnique(name.substring(5, us).toInt());
            } else if (name.startsWith("/chaos_snaps_g") && name.endsWith(".bin")) {
                int us = name.indexOf('_', 14);
                if (us > 14) pushUnique(name.substring(14, us).toInt());
            }
            f.close();
        }
        root.close();

        std::sort(gens.begin(), gens.end());
        return gens;
    }

    // ------------------------------------------------------------
    // 判断某一代是否可删
    // ------------------------------------------------------------
    static bool isGenerationDeletable(uint32_t gen, uint32_t protectPrevGen) {
        if (gen == 0) return false;
        if (gen == currentGenerationForStorage) return false;

#if RETENTION_PROTECT_PREV_GEN
        if (gen == protectPrevGen && protectPrevGen > 0) return false;
#endif
        if (hasActiveDependency(gen)) return false;
        return true;
    }

    // ------------------------------------------------------------
    // 删除一整代(含所有配套文件)
    // ------------------------------------------------------------
    static int deleteGenerationFull(uint32_t gen) {
        if (!RobustStorage::isReady()) return 0;
        if (gen == 0) return 0;

        std::vector<String> files;
        collectGenerationFiles(gen, files);
        if (files.empty()) return 0;

        int deleted = 0;
        for (const String& path : files) {
            if (SPIFFS.remove(path)) {
                deleted++;
            } else {
                Logger::logf("⚠️ retention: failed to remove %s", path.c_str());
            }
        }
        if (deleted > 0) {
            Logger::logf("🧹 retention: deleted gen %lu (%d files)", gen, deleted);
        }
        return deleted;
    }

    // ------------------------------------------------------------
    // 只删旧帧日志 (保留最近 3 个)
    //   这是轻量清理, 与 enforceRetention 并行
    // ------------------------------------------------------------
    static int cleanExpiredFrameLogs() {
        if (!RobustStorage::isReady()) return 0;
        std::vector<String> toDelete;
        File root = SPIFFS.open("/");
        if (!root) return 0;

        while (File f = root.openNextFile()) {
            String name = normPath(f.name());
            if (name.startsWith("/frm_") && name.endsWith(".bin")) {
                toDelete.push_back(name);
            }
            f.close();
        }
        root.close();

        std::sort(toDelete.begin(), toDelete.end());
        int keepCount = 3;
        int deleted = 0;
        if ((int)toDelete.size() > keepCount) {
            for (int i = 0; i < (int)toDelete.size() - keepCount; i++) {
                if (SPIFFS.remove(toDelete[i])) {
                    deleted++;
                    Logger::logf("🧹 Deleted old frame log: %s", toDelete[i].c_str());
                }
            }
        }
        return deleted;
    }

    // ------------------------------------------------------------
    // 只删旧种群 (保留最近 keepGenerations 代)
    //   与 enforceRetention 的区别:
    //     cleanOldPopulations 只删 /pop_gen_*.bin
    //     enforceRetention 删一整代(含配套文件)且受 MIN/MAX 约束
    // ------------------------------------------------------------
    static int cleanOldPopulations(int keepGenerations) {
        if (!RobustStorage::isReady()) return 0;
        auto gens = getStoredGenerations();
        if ((int)gens.size() <= keepGenerations) return 0;

        uint32_t currentGen = currentGenerationForStorage;
        int deleted = 0;

        for (auto gen : gens) {
            if (gen == currentGen) continue;
            if (hasActiveDependency(gen)) continue;

            bool isRecent = false;
            int recentCount = 0;
            for (int i = gens.size() - 1; i >= 0 && recentCount < keepGenerations; i--) {
                if (gens[i] == gen) { isRecent = true; break; }
                recentCount++;
            }
            if (isRecent) continue;

            String popPath = "/pop_gen_" + String(gen) + ".bin";
            if (SPIFFS.exists(popPath) && SPIFFS.remove(popPath)) {
                deleted++;
                Logger::logf("🧹 Deleted old population: gen %lu", gen);
            }
        }
        return deleted;
    }

    // ------------------------------------------------------------
    // 渐进式清理 (空间不足时调用)
    //   不再依赖 deleteGeneration, 改用 enforceRetention
    // ------------------------------------------------------------
    static bool progressiveCleanup(size_t requiredBytes) {
        Logger::log("🔄 Starting progressive cleanup...");
        size_t free = getFreeSpace();
        int maxIterations = 5;

        for (int i = 0; i < maxIterations; i++) {
            if (free >= requiredBytes + SAFETY_MARGIN) {
                Logger::logf("✅ Cleanup done, free: %d bytes", free);
                return true;
            }
            int deleted = cleanExpiredFrameLogs();
            if (deleted > 0) {
                Logger::logf("  🧹 Iteration %d: deleted %d frame logs", i + 1, deleted);
                free = getFreeSpace();
                if (free >= requiredBytes + SAFETY_MARGIN) return true;
            }
            int retentionDeleted = enforceRetention();
            if (retentionDeleted > 0) {
                Logger::logf("  🧹 Iteration %d: retention deleted %d files", i + 1, retentionDeleted);
                free = getFreeSpace();
                if (free >= requiredBytes + SAFETY_MARGIN) return true;
            }
            delay(50);
        }
        Logger::log("⚠️ Progressive cleanup failed to free enough space");
        return false;
    }

    // ------------------------------------------------------------
    // 列出所有已存代 (只认 /pop_gen_*.bin)
    // ------------------------------------------------------------
    static std::vector<uint32_t> getStoredGenerations() {
        std::vector<uint32_t> gens;
        if (!RobustStorage::isReady()) return gens;
        File root = SPIFFS.open("/");
        if (!root) return gens;
        while (File f = root.openNextFile()) {
            String name = normPath(f.name());
            if (name.startsWith("/pop_gen_") && name.endsWith(".bin")) {
                uint32_t gen = name.substring(9, name.lastIndexOf('.')).toInt();
                if (gen > 0) {
                    bool exists = false;
                    for (auto g : gens) if (g == gen) { exists = true; break; }
                    if (!exists) gens.push_back(gen);
                }
            }
            f.close();
        }
        root.close();
        std::sort(gens.begin(), gens.end());
        return gens;
    }

public:
    // ============================================================
    // 依赖登记 (保留 v10.12 原接口)
    // ============================================================
    static void registerDependency(uint32_t generation, int individual) {
        uint32_t now = millis();
        cleanExpiredDependencies();
        for (int i = 0; i < dependencyCount; i++) {
            if (activeDependencies[i].generation == generation &&
                activeDependencies[i].individual == individual) {
                activeDependencies[i].isActive = true;
                activeDependencies[i].startTime = now;
                return;
            }
        }
        if (dependencyCount < 16) {
            activeDependencies[dependencyCount].generation = generation;
            activeDependencies[dependencyCount].individual = individual;
            activeDependencies[dependencyCount].isActive = true;
            activeDependencies[dependencyCount].startTime = now;
            dependencyCount++;
            Logger::logf("🔗 Dependency registered: gen=%lu, ind=%d", generation, individual);
        } else {
            Logger::logf("⚠️ Dependency full! gen=%lu, ind=%d", generation, individual);
        }
    }

    static void unregisterDependency(uint32_t generation, int individual) {
        for (int i = 0; i < dependencyCount; i++) {
            if (activeDependencies[i].generation == generation &&
                activeDependencies[i].individual == individual) {
                activeDependencies[i].isActive = false;
                Logger::logf("🔓 Dependency unregistered: gen=%lu, ind=%d", generation, individual);
                break;
            }
        }
    }

    static String getDependencyInfo() {
        String info = "=== Dependency Info ===\n";
        info += "Active: " + String(dependencyCount) + "/16\n";
        for (int i = 0; i < 16 && i < dependencyCount; i++) {
            if (activeDependencies[i].isActive) {
                info += "  gen=" + String(activeDependencies[i].generation);
                info += ", ind=" + String(activeDependencies[i].individual);
                info += ", age=" + String((millis() - activeDependencies[i].startTime) / 1000) + "s\n";
            }
        }
        return info;
    }

    // ============================================================
    // [核心] 滚动保留策略
    //
    //   语义:
    //     1. 扫描 SPIFFS 上所有"代"
    //     2. 保留最新 RETENTION_MIN_KEEP_GENERATIONS 代
    //     3. 若总代数 > RETENTION_MAX_KEEP_GENERATIONS,
    //        从最旧的代开始删, 直到总数 <= MAX
    //     4. 永远不删 currentGenerationForStorage
    //     5. 永远不删 GeneStorage::getCurrentGeneration()
    //     6. 永远不删上一代 (若 RETENTION_PROTECT_PREV_GEN=1)
    //     7. 无任何 /pop_gen_*.bin 时, 只报告, 不重建第 1 代
    //
    //   返回: 实际删除的文件数
    // ============================================================
    static int enforceRetention(uint32_t* outRemainingGens = nullptr,
                                int* outRemainingCount = nullptr) {
        if (!RobustStorage::isReady()) {
            Logger::log("⚠️ enforceRetention: SPIFFS not ready");
            if (outRemainingCount) *outRemainingCount = 0;
            return 0;
        }

#if !RETENTION_ENABLE_AUTO_DELETE
        Logger::log("ℹ️ enforceRetention: auto-delete DISABLED, only report");
#endif

        std::vector<uint32_t> allGens = scanAllGenerations();
        if (allGens.empty()) {
            Logger::log("⚠️ enforceRetention: no generations found on SPIFFS");
            if (outRemainingCount) *outRemainingCount = 0;
            return 0;
        }

        uint32_t currentGen    = currentGenerationForStorage;
        uint32_t geneStoreGen  = currentGenerationForStorage;
        uint32_t protectPrevGen = 0;
#if RETENTION_PROTECT_PREV_GEN
        if (geneStoreGen > 1) protectPrevGen = geneStoreGen - 1;
        else if (currentGen > 1) protectPrevGen = currentGen - 1;
#endif

        Logger::logf("📊 enforceRetention: %d gens found, current=%lu, geneStore=%lu, protectPrev=%lu",
                     (int)allGens.size(), currentGen, geneStoreGen, protectPrevGen);

        int total = (int)allGens.size();
        int keepFrom = total - RETENTION_MIN_KEEP_GENERATIONS;
        if (keepFrom < 0) keepFrom = 0;

        std::vector<uint32_t> toDelete;
        for (int i = 0; i < total; i++) {
            uint32_t g = allGens[i];
            if (i >= keepFrom) continue;
            if (!isGenerationDeletable(g, protectPrevGen)) {
                Logger::logf("🔒 retention: protect gen %lu", g);
                continue;
            }
            toDelete.push_back(g);
        }

        if (total > RETENTION_MAX_KEEP_GENERATIONS) {
            int needToDelete = total - RETENTION_MAX_KEEP_GENERATIONS;
            for (int i = 0; i < total && needToDelete > 0; i++) {
                uint32_t g = allGens[i];
                if (i >= keepFrom) break;
                if (!isGenerationDeletable(g, protectPrevGen)) continue;
                bool already = false;
                for (auto x : toDelete) if (x == g) { already = true; break; }
                if (!already) toDelete.push_back(g);
                needToDelete--;
            }
        }

        int deletedFiles = 0;
#if RETENTION_ENABLE_AUTO_DELETE
        for (uint32_t g : toDelete) {
            deletedFiles += deleteGenerationFull(g);
        }
#else
        for (uint32_t g : toDelete) {
            Logger::logf("ℹ️ retention: would delete gen %lu (auto-delete disabled)", g);
        }
#endif

        std::vector<uint32_t> remaining = scanAllGenerations();
        if (outRemainingCount) *outRemainingCount = (int)remaining.size();
        if (outRemainingGens) {
            int n = min((int)remaining.size(), RETENTION_MAX_KEEP_GENERATIONS);
            for (int i = 0; i < n; i++) outRemainingGens[i] = remaining[i];
        }

        Logger::logf("✅ enforceRetention: deleted %d files, %d gens remaining",
                     deletedFiles, (int)remaining.size());
        return deletedFiles;
    }

    // ============================================================
    // 初始化 / 代设置
    // ============================================================
    static void init() {
        freeSpaceThreshold = 50 * 1024;
        currentGenerationForStorage = 0;
        cleanupAttempts = 0;
        lastCleanupWarningTime = 0;
        dependencyCount = 0;
        for (int i = 0; i < 16; i++) {
            activeDependencies[i].generation = 0;
            activeDependencies[i].individual = -1;
            activeDependencies[i].isActive = false;
            activeDependencies[i].startTime = 0;
        }
        memset(savedCount, 0, sizeof(savedCount));
        Logger::logf("RollingStorage: minKeep=%d, maxKeep=%d, autoDelete=%d",
                     RETENTION_MIN_KEEP_GENERATIONS,
                     RETENTION_MAX_KEEP_GENERATIONS,
                     RETENTION_ENABLE_AUTO_DELETE);
        Logger::log("🔒 Dependency tracking enabled (16 slots)");
    }

    static void setCurrentGeneration(uint32_t gen) {
        currentGenerationForStorage = gen;
    }

    static uint32_t getCurrentGeneration() {
        return currentGenerationForStorage;
    }

    // ============================================================
    // 空间检查
    //   不再调用 deleteOldestGeneration (已删除)
    //   改为调用 enforceRetention + cleanExpiredFrameLogs
    // ============================================================
       static bool ensureSpace(size_t requiredBytes, uint32_t currentGen = 0) {
        if (!RobustStorage::isReady()) {
            Logger::log("❌ ensureSpace: SPIFFS not ready");
            return false;
        }
        cleanExpiredDependencies();
        size_t free = getFreeSpace();
        size_t needed = requiredBytes + SAFETY_MARGIN;

        // [v10.12 保留] 存储健康度日志
        size_t total = SPIFFS.totalBytes();
        Logger::logf("💾 ensureSpace: free=%d/%d bytes, need=%d",
                     (int)free, (int)total, (int)needed);

        if (free >= needed) {
            return true;
        }
        Logger::logf("⚠️ Space check: free=%d, need=%d", free, needed);
        if (currentGen == 0) {
            currentGen = currentGenerationForStorage;
        }
        if (progressiveCleanup(requiredBytes)) {
            return true;
        }
        cleanupAttempts++;
        if (cleanupAttempts > 3) {
            Logger::log("🚨 Emergency: storage critical, manual cleanup required");
            cleanupAttempts = 0;
        }
        if (millis() - lastCleanupWarningTime > 30000) {
            lastCleanupWarningTime = millis();
            Logger::log("========================================");
            Logger::log("❌ STORAGE SPACE CRITICAL");
            Logger::logf("   Free: %d bytes, Need: %d bytes", free, needed);
            Logger::log("   📋 Manual cleanup required via Web UI");
            Logger::log("========================================");
        }
        return false;
    }

    // ============================================================
    // 帧日志保存 (保留 v10.12 原逻辑)
    // ============================================================
    static bool saveFrameLog(uint32_t generation, int individual,
                             const FrameLogEntry* log, int count, int head = 0,
                             int startOffset = 0, int saveCount = -1,
                             bool isIncremental = false) {
        if (!RobustStorage::isReady() || count == 0) return false;
        if (saveCount < 0) saveCount = count;
        if (startOffset + saveCount > count) saveCount = count - startOffset;
        if (saveCount <= 0) return false;

        size_t estimatedSize = saveCount * sizeof(CompressedFrameEntry) + sizeof(FrameLogHeader) + 512;
        if (!ensureSpace(estimatedSize, generation)) {
            Logger::logf("❌ saveFrameLog gen=%lu id=%d: insufficient space", generation, individual);
            return false;
        }

        int exportCount = min(saveCount, FRAME_LOG_SIZE);
        size_t dataSize = exportCount * sizeof(CompressedFrameEntry);
        uint8_t* dataBuffer = (uint8_t*)malloc(dataSize);
        if (!dataBuffer) return false;

        uint8_t* ptr = dataBuffer;
        FrameLogEntry prev = {0};
        for (int i = 0; i < exportCount; i++) {
            int idx = (head - count + startOffset + i + FRAME_LOG_SIZE) % FRAME_LOG_SIZE;
            const FrameLogEntry& e = log[idx];
            CompressedFrameEntry compressed;
            compressed.timestamp_ms = e.timestamp_ms;
            compressed.sensorLeft = e.sensorLeft;
            compressed.sensorRight = e.sensorRight;
            compressed.directionL = e.directionL;
            compressed.directionR = e.directionR;
            compressed.chaosActive = e.chaosActive;
            compressed.isChaosFrame = e.isChaosFrame;
            compressed.state = e.state;
            compressed.reserved = 0;

            if (i == 0) {
                compressed.motorLeftPWM = e.motorLeftPWM;
                compressed.motorRightPWM = e.motorRightPWM;
            } else {
                compressed.motorLeftPWM = constrain(e.motorLeftPWM - prev.motorLeftPWM, -128, 127);
                compressed.motorRightPWM = constrain(e.motorRightPWM - prev.motorRightPWM, -128, 127);
            }
            memcpy(ptr, &compressed, sizeof(CompressedFrameEntry));
            ptr += sizeof(CompressedFrameEntry);
            prev = e;
        }

        FrameLogHeader header;
        header.magic = MAGIC_FRAME_LOG;
        header.version = 0x0007;
        header.headerSize = sizeof(FrameLogHeader);
        header.frameCount = exportCount;
        header.generation = generation;
        header.individual = individual;
        header.reserved = 0;
        header.crc32 = CRC32::calculate(dataBuffer, dataSize);

        size_t totalSize = sizeof(FrameLogHeader) + dataSize;
        uint8_t* finalBuffer = (uint8_t*)malloc(totalSize);
        if (!finalBuffer) { free(dataBuffer); return false; }

        memcpy(finalBuffer, &header, sizeof(FrameLogHeader));
        memcpy(finalBuffer + sizeof(FrameLogHeader), dataBuffer, dataSize);
        free(dataBuffer);

        String path = "/frm_" + String(generation) + "_i" + String(individual) + (isIncremental ? ".inc.bin" : ".bin");
        bool success = FileUtils::atomicWrite(path, finalBuffer, totalSize);
        free(finalBuffer);

        if (success) {
            savedCount[generation % 100]++;
            Logger::logf("💾 saveFrameLog gen=%lu id=%d ✅", generation, individual);
        } else {
            Logger::logf("❌ saveFrameLog gen=%lu id=%d FAILED", generation, individual);
        }
        return success;
    }

    static bool incrementalSave(uint32_t generation, int individual,
                                const FrameLogEntry* log, int count, int head = 0) {
        (void)generation; (void)individual; (void)log; (void)count; (void)head;
        return false;
    }

    static void resetGenerationCounter(uint32_t gen) {
        savedCount[gen % 100] = 0;
    }

    // ============================================================
    // 状态输出
    // ============================================================
    static String getStorageStatus() {
        String status = "=== Storage Status ===\n";
        size_t free = getFreeSpace();
        status += "Free space: " + String(free / 1024) + " KB\n";
        auto gens = getStoredGenerations();
        status += "Stored generations (pop only): " + String(gens.size()) + "\n";
        for (auto g : gens) {
            bool hasDep = hasActiveDependency(g);
            status += "  Gen " + String(g) + (hasDep ? " 🔒 (active)" : "");
            if (g == currentGenerationForStorage) status += " ← current";
            status += "\n";
        }
        status += "\n📋 Retention: minKeep=" + String(RETENTION_MIN_KEEP_GENERATIONS)
                + ", maxKeep=" + String(RETENTION_MAX_KEEP_GENERATIONS)
                + ", autoDelete=" + String(RETENTION_ENABLE_AUTO_DELETE) + "\n";
        status += "🔒 AUTO-REBUILD GEN1: "
                + String(ALLOW_AUTO_REBUILD_GEN1 ? "ENABLED" : "DISABLED") + "\n";
        return status;
    }

    static size_t getFreeSpaceKB() {
        return getFreeSpace() / 1024;
    }

    static std::vector<uint32_t> getStoredGenerationsPublic() {
        return getStoredGenerations();
    }

    static void resetCleanupAttempts() {
        cleanupAttempts = 0;
    }
};

// ★★★ RollingStorage 静态成员定义 ★★★
uint32_t RollingStorage::freeSpaceThreshold = 50 * 1024;
int RollingStorage::savedCount[100] = {0};
uint32_t RollingStorage::currentGenerationForStorage = 0;
int RollingStorage::cleanupAttempts = 0;
uint32_t RollingStorage::lastCleanupWarningTime = 0;
RollingStorage::ActiveDependency RollingStorage::activeDependencies[16];
int RollingStorage::dependencyCount = 0;

// ================================================================
// GeneStorage 类
//   [v10.5] findLatestGeneration() 经 normPath() 规范化
// ================================================================
class GeneStorage {
private:
    static const uint32_t MAX_GENERATIONS_PER_EXPERIMENT = 0xFFFFFFFF;
    static const uint16_t FILE_VERSION = 0x000A;
    static const uint32_t FILE_MAGIC = 0x47454E45;
    static const char* POP_PREFIX;
    static const char* GEN_RECORD_PREFIX;
    static const char* FRAME_LOG_PREFIX;
    static const char* EXPERIMENT_MARKER;
    static uint32_t currentExperimentId;
    static uint32_t currentGeneration;
    static bool experimentActive;
    static uint32_t pendingGeneration;
    static bool hasPendingGeneration;
    static bool pendingStateSave;

    static size_t serializeIndividual(const Gene& g, uint8_t* buffer, size_t maxSize) {
        uint8_t* ptr = buffer;
        size_t used = 0;

        if (used + 1 > maxSize) return 0;
        uint8_t rc = constrain(g.ruleCount, MIN_RULES, MAX_RULES);
        *ptr = rc; ptr++; used += 1;

        size_t rulesSize = rc * sizeof(BehaviorRule);
        if (used + rulesSize > maxSize) return 0;
        memcpy(ptr, g.rules, rulesSize);
        ptr += rulesSize; used += rulesSize;

        if (used + 4 > maxSize) return 0;
        memcpy(ptr, &g.survival_time, 4);
        ptr += 4; used += 4;

        if (used + 4 > maxSize) return 0;
        memcpy(ptr, &g.distance_ticks, 4);
        ptr += 4; used += 4;

        if (used + 4 > maxSize) return 0;
        memcpy(ptr, &g.noveltyScore, 4);
        ptr += 4; used += 4;

        if (used + 19 > maxSize) return 0;
        memcpy(ptr, &g.obstacleThreshold, 2); ptr += 2; used += 2;
        memcpy(ptr, &g.clearThreshold, 2); ptr += 2; used += 2;
        memcpy(ptr, &g.encoderDiffThreshold, 2); ptr += 2; used += 2;
        memcpy(ptr, &g.encoderDiffMin, 2); ptr += 2; used += 2;
        memcpy(ptr, &g.wheelSpinThreshold, 2); ptr += 2; used += 2;
        memcpy(ptr, &g.wheelStopThreshold, 2); ptr += 2; used += 2;
        *ptr = g.stuckWindowSize; ptr++; used += 1;
        memcpy(ptr, &g.chaosNoiseAmplifier, 2); ptr += 2; used += 2;
        memcpy(ptr, &g.chaosMinPwm, 2); ptr += 2; used += 2;
        memcpy(ptr, &g.chaosTimeoutMs, 2); ptr += 2; used += 2;
        memcpy(ptr, &g.chaosForceTimeoutMs, 2); ptr += 2; used += 2;

        if (used + 3 > maxSize) return 0;
        *ptr = g.hasChaosRules ? 1 : 0; ptr++; used += 1;
        *ptr = g.chaosRuleCount; ptr++; used += 1;
        *ptr = g.chaosRulesStartIndex; ptr++; used += 1;

        if (used + sizeof(BehaviorDescriptor) > maxSize) return 0;
        memcpy(ptr, &g.behavior, sizeof(BehaviorDescriptor));
        ptr += sizeof(BehaviorDescriptor);
        used += sizeof(BehaviorDescriptor);

        return used;
    }

    static size_t deserializeIndividual(const uint8_t* buffer, size_t maxSize, Gene& g, uint16_t fileVersion) {
        const uint8_t* ptr = buffer;
        size_t used = 0;

        if (used + 1 > maxSize) return 0;
        g.ruleCount = *ptr; ptr++; used += 1;
        if (g.ruleCount > MAX_RULES) g.ruleCount = MAX_RULES;

        size_t rulesSize = g.ruleCount * sizeof(BehaviorRule);
        if (used + rulesSize > maxSize) return 0;
        memcpy(g.rules, ptr, rulesSize);
        ptr += rulesSize; used += rulesSize;

        if (used + 12 > maxSize) return 0;
        memcpy(&g.survival_time, ptr, 4); ptr += 4; used += 4;
        memcpy(&g.distance_ticks, ptr, 4); ptr += 4; used += 4;
        memcpy(&g.noveltyScore, ptr, 4); ptr += 4; used += 4;

        if (fileVersion >= 0x0005) {
            if (used + 12 > maxSize) return 0;
            memcpy(&g.obstacleThreshold, ptr, 2); ptr += 2; used += 2;
            memcpy(&g.clearThreshold, ptr, 2); ptr += 2; used += 2;
            memcpy(&g.encoderDiffThreshold, ptr, 2); ptr += 2; used += 2;
            memcpy(&g.encoderDiffMin, ptr, 2); ptr += 2; used += 2;
            memcpy(&g.wheelSpinThreshold, ptr, 2); ptr += 2; used += 2;
            memcpy(&g.wheelStopThreshold, ptr, 2); ptr += 2; used += 2;

            if (used + 1 > maxSize) return 0;
            g.stuckWindowSize = *ptr; ptr++; used += 1;

            if (used + 8 > maxSize) return 0;
            memcpy(&g.chaosNoiseAmplifier, ptr, 2); ptr += 2; used += 2;
            memcpy(&g.chaosMinPwm, ptr, 2); ptr += 2; used += 2;
            memcpy(&g.chaosTimeoutMs, ptr, 2); ptr += 2; used += 2;
            memcpy(&g.chaosForceTimeoutMs, ptr, 2); ptr += 2; used += 2;

            if (fileVersion < 0x0009) {
                float tmpBiasL, tmpBiasR;
                if (used + 8 > maxSize) return 0;
                memcpy(&tmpBiasL, ptr, 4); ptr += 4; used += 4;
                memcpy(&tmpBiasR, ptr, 4); ptr += 4; used += 4;
            }

            if (fileVersion >= 0x0006) {
                if (used + 3 > maxSize) return 0;
                g.hasChaosRules = (*ptr == 1); ptr++; used += 1;
                g.chaosRuleCount = *ptr; ptr++; used += 1;
                g.chaosRulesStartIndex = *ptr; ptr++; used += 1;
            } else {
                g.hasChaosRules = false;
                g.chaosRuleCount = 0;
                g.chaosRulesStartIndex = 0;
            }
        } else {
            g.obstacleThreshold = 1500;
            g.clearThreshold = 600;
            g.encoderDiffThreshold = 30;
            g.encoderDiffMin = 5;
            g.wheelSpinThreshold = 20;
            g.wheelStopThreshold = 3;
            g.stuckWindowSize = 10;
            g.chaosNoiseAmplifier = 180;
            g.chaosMinPwm = 20;
            g.chaosTimeoutMs = 3000;
            g.chaosForceTimeoutMs = 5000;
            g.hasChaosRules = false;
            g.chaosRuleCount = 0;
            g.chaosRulesStartIndex = 0;
        }

        if (fileVersion >= 0x000A) {
            if (used + sizeof(BehaviorDescriptor) > maxSize) return 0;
            memcpy(&g.behavior, ptr, sizeof(BehaviorDescriptor));
            ptr += sizeof(BehaviorDescriptor);
            used += sizeof(BehaviorDescriptor);
        } else if (fileVersion >= 0x0007) {
            // 旧版 BehaviorDescriptor 是 48 字节（12 float）
            if (used + 48 > maxSize) return 0;
            memcpy(&g.behavior, ptr, 48);
            // 新字段清零
            g.behavior.chaosFrameRatio = 0;
            g.behavior.chaosSpeedDeltaL = 0;
            g.behavior.chaosSpeedDeltaR = 0;
            g.behavior.chaosPwmVariance = 0;
            ptr += 48;
            used += 48;
        } else {
            g.behavior.init();
        }

        g.baselineBehavior.init();
        return used;
    }

    static void scheduleStateSave() {
        pendingStateSave = true;
    }

    static void flushPendingStateSave() {
        if (!pendingStateSave) return;
        if (!RobustStorage::isReady()) {
            Logger::log("⚠️ Cannot flush state save: SPIFFS not ready");
            return;
        }
        String content = String(currentExperimentId) + "," + String(currentGeneration);
        if (FileUtils::atomicWriteString(EXPERIMENT_MARKER, content)) {
            pendingStateSave = false;
            Logger::logf("💾 State saved: ID=%lu, gen=%lu", currentExperimentId, currentGeneration);
        } else {
            Logger::log("⚠️ State save failed, will retry later");
        }
    }

    static bool safeSaveState() {
        if (!RobustStorage::isReady()) return false;
        String content = String(currentExperimentId) + "," + String(currentGeneration);
        for (int retry = 0; retry < 3; retry++) {
            if (FileUtils::atomicWriteString(EXPERIMENT_MARKER, content)) {
                pendingStateSave = false;
                return true;
            }
            delay(50);
        }
        pendingStateSave = true;
        return false;
    }

public:
    static void init() {
        currentExperimentId = 0;
        currentGeneration = 0;
        experimentActive = false;
        pendingGeneration = 0;
        hasPendingGeneration = false;
        pendingStateSave = false;

        if (!RobustStorage::isReady()) {
            Logger::log("SPIFFS unavailable, using RAM mode");
            return;
        }
        loadExperimentState();
        if (pendingStateSave) {
            flushPendingStateSave();
        }
        Logger::logf("Gene storage initialized, current gen=%lu, active=%d",
                     currentGeneration, experimentActive);
    }

    static void forceActivate() {
        experimentActive = true;
        if (currentGeneration == 0) {
            uint32_t latest = findLatestGeneration();
            if (latest > 0) {
                currentGeneration = latest;
            } else {
                currentGeneration = 1;
                Logger::log("⚠️ No existing data - will create new population on init");
            }
        }
        safeSaveState();
        Logger::logf("🔧 Force activated: gen=%lu, active=%d",
                     currentGeneration, experimentActive);
    }

    static void loadExperimentState() {
        if (!RobustStorage::isReady()) {
            experimentActive = false;
            return;
        }
        String content = FileUtils::safeRead(EXPERIMENT_MARKER);

        if (content.length() == 0) {
            uint32_t latest = findLatestGeneration();
            if (latest > 0) {
                Logger::logf("✅ Recovered to generation %lu (no state file)", latest);
                currentExperimentId = (uint32_t)millis();
                currentGeneration = latest;
                RollingStorage::setCurrentGeneration(latest);
                experimentActive = true;
                scheduleStateSave();
                return;
            }
            experimentActive = false;
            Logger::log("📋 No state file and no population found. New experiment will be created.");
            return;
        }

        int commaPos = content.indexOf(',');
        if (commaPos < 0) {
            experimentActive = false;
            return;
        }
        uint32_t savedExpId = content.substring(0, commaPos).toInt();
        uint32_t savedGen = content.substring(commaPos + 1).toInt();

        String popPath = "/pop_gen_" + String(savedGen) + ".bin";
        if (SPIFFS.exists(popPath) && verifyPopulationFile(popPath, POPULATION_SIZE)) {
            currentExperimentId = savedExpId;
            currentGeneration = savedGen;
            RollingStorage::setCurrentGeneration(savedGen);
            experimentActive = true;
            Logger::logf("✅ Loaded state: exp=%lu, gen=%lu", currentExperimentId, currentGeneration);
            return;
        }

        if (savedGen > 1) {
            uint32_t prevGen = savedGen - 1;
            String prevPath = "/pop_gen_" + String(prevGen) + ".bin";
            if (SPIFFS.exists(prevPath) && verifyPopulationFile(prevPath, POPULATION_SIZE)) {
                Logger::logf("✅ Recovered to previous generation: %lu (state was %lu)", prevGen, savedGen);
                currentExperimentId = savedExpId;
                currentGeneration = prevGen;
                RollingStorage::setCurrentGeneration(prevGen);
                experimentActive = true;
                scheduleStateSave();
                return;
            }
        }

        uint32_t latest = findLatestGeneration();
        if (latest > 0) {
            Logger::logf("✅ Recovered to latest available generation: %lu (state was %lu)", latest, savedGen);
            currentExperimentId = savedExpId;
            currentGeneration = latest;
            RollingStorage::setCurrentGeneration(latest);
            experimentActive = true;
            scheduleStateSave();
            return;
        }

        Logger::log("========================================");
        Logger::log("❌ No recoverable generation found");
        Logger::log("   🔒 DATA PRESERVED - Not auto-resetting");
        Logger::log("   📋 Manual action required via Web UI");
        Logger::log("========================================");
        currentExperimentId = savedExpId;
        currentGeneration = savedGen;
        experimentActive = false;
    }

    static void startNewExperiment() {
        if (!RobustStorage::isReady()) {
            Logger::log("❌ startNewExperiment: SPIFFS not ready");
            return;
        }
        uint32_t latest = findLatestGeneration();
        if (latest > 0) {
            Logger::logf("⚠️ Existing data found (gen %lu) - continuing", latest);
            currentExperimentId = (uint32_t)millis();
            currentGeneration = latest;
            RollingStorage::setCurrentGeneration(latest);
            experimentActive = true;
            safeSaveState();
            Logger::logf("📌 New experiment session: ID=%lu, gen=%lu (data preserved)",
                         currentExperimentId, currentGeneration);
        } else {
            currentExperimentId = (uint32_t)millis();
            currentGeneration = 1;
            RollingStorage::setCurrentGeneration(1);
            experimentActive = true;
            safeSaveState();
            Logger::logf("📌 New experiment initialized: ID=%lu, gen=1",
                         currentExperimentId);
        }
    }

    static void setCurrentGeneration(uint32_t gen, bool saveState = true) {
        currentGeneration = gen;
        RollingStorage::setCurrentGeneration(gen);
        if (saveState) {
            safeSaveState();
            Logger::logf("📌 Generation set to %lu (state saved)", gen);
        } else {
            scheduleStateSave();
            Logger::logf("📌 Generation set to %lu (state pending)", gen);
        }
    }

    static void incrementGeneration() {
        uint32_t newGen = currentGeneration + 1;
        pendingGeneration = newGen;
        hasPendingGeneration = true;
        Logger::logf("📈 Generation incremented to %lu (pending, state not saved)", newGen);
    }

    static uint32_t getCurrentGeneration() { return currentGeneration; }
    static uint32_t getMaxGenerations() { return MAX_GENERATIONS_PER_EXPERIMENT; }
    static float getProgress() { return (float)currentGeneration / MAX_GENERATIONS_PER_EXPERIMENT; }
    static bool isExperimentComplete() { return currentGeneration >= MAX_GENERATIONS_PER_EXPERIMENT; }
    static bool isExperimentActive() { return experimentActive; }

    static bool shouldStartNewExperiment() {
        return false;
    }

    static bool savePopulation(Gene* population, int size) {
        return savePopulationForGeneration(currentGeneration, population, size);
    }

    static bool loadPopulation(uint32_t generation, Gene* population, int size) {
        Logger::logf("📖 loadPopulation: loading gen=%lu, popSize=%d", generation, size);
        if (!RobustStorage::isReady()) {
            Logger::log("❌ loadPopulation: SPIFFS not ready");
            return false;
        }
        String path = String(POP_PREFIX) + String(generation) + ".bin";
        Logger::logf("  File path: %s", path.c_str());
        if (!SPIFFS.exists(path)) {
            Logger::logf("❌ loadPopulation: file not found: %s", path.c_str());
            return false;
        }
        size_t fileSize = FileUtils::getFileSize(path);
        if (fileSize == 0) {
            Logger::logf("❌ loadPopulation: file size 0: %s", path.c_str());
            return false;
        }
        Logger::logf("  File size: %d bytes", fileSize);
        File file = SPIFFS.open(path, FILE_READ);
        if (!file) {
            Logger::logf("❌ loadPopulation: cannot open: %s", path.c_str());
            return false;
        }
        uint8_t* buffer = (uint8_t*)malloc(fileSize);
        if (!buffer) { file.close(); return false; }
        file.read(buffer, fileSize);
        file.close();

        if (fileSize < sizeof(GeneBinaryHeader)) {
            Logger::logf("❌ loadPopulation: file too small for header (%d)", fileSize);
            free(buffer);
            return false;
        }
        GeneBinaryHeader geneHeader;
        memcpy(&geneHeader, buffer, sizeof(GeneBinaryHeader));
        if (geneHeader.magic != MAGIC_GENE_POP) {
            Logger::logf("❌ loadPopulation: invalid magic 0x%08X (expect 0x%08X)",
                         geneHeader.magic, MAGIC_GENE_POP);
            free(buffer);
            return false;
        }
        uint16_t fileVersion = geneHeader.version;
        uint16_t popSize = geneHeader.popSize;
        Logger::logf("  fileVersion=0x%04X, popSize=%d, gen=%lu",
                     fileVersion, popSize, geneHeader.generation);
        if (popSize != size) {
            Logger::logf("❌ loadPopulation: size mismatch: file=%d, expected=%d", popSize, size);
            free(buffer);
            return false;
        }
        uint8_t* ptr = buffer + sizeof(GeneBinaryHeader);
        size_t remaining = fileSize - sizeof(GeneBinaryHeader);
        for (int i = 0; i < size; i++) {
            size_t read = deserializeIndividual(ptr, remaining, population[i], fileVersion);
            if (read == 0) {
                Logger::logf("❌ loadPopulation: deserialization failed for individual %d", i);
                free(buffer);
                return false;
            }
            ptr += read;
            remaining -= read;
        }
        free(buffer);
        Logger::logf("✅ loadPopulation: loaded %d individuals from gen %lu", size, generation);
        return true;
    }

    static bool populationFileExists(uint32_t generation) {
        if (!RobustStorage::isReady()) return false;
        String path = String(POP_PREFIX) + String(generation) + ".bin";
        return SPIFFS.exists(path);
    }

    static bool savePopulationForGeneration(uint32_t gen, Gene* population, int size,
                                            const char* tag = "auto") {
        Logger::logf("💾 savePopulationForGeneration [%s]: gen=%lu, popSize=%d", tag, gen, size);
        if (!RobustStorage::isReady()) {
            Logger::log("❌ savePopulationForGeneration: SPIFFS not ready");
            return false;
        }
        size_t totalSize = sizeof(GeneBinaryHeader);
        for (int i = 0; i < size; i++) {
            uint8_t rc = constrain(population[i].ruleCount, MIN_RULES, MAX_RULES);
            size_t indivSize = 1 + rc * sizeof(BehaviorRule) + 4 + 4 + 4 + 12 + 1 + 8 + 3
                               + sizeof(BehaviorDescriptor);
            totalSize += indivSize;
        }
        Logger::logf("  Total size: %d bytes", totalSize);
        if (!RollingStorage::ensureSpace(totalSize + 512)) {
            Logger::logf("❌ savePopulationForGeneration: insufficient SPIFFS space (need %d)", totalSize + 512);
            return false;
        }
        uint8_t* buffer = (uint8_t*)malloc(totalSize);
        if (!buffer) {
            Logger::log("❌ savePopulationForGeneration: malloc failed");
            return false;
        }
        uint8_t* ptr = buffer;
        GeneBinaryHeader geneHeader;
        geneHeader.magic = MAGIC_GENE_POP;
        geneHeader.version = FILE_VERSION;
        geneHeader.popSize = size;
        geneHeader.generation = gen;
        geneHeader.experimentId = currentExperimentId;
        memcpy(ptr, &geneHeader, sizeof(GeneBinaryHeader));
        ptr += sizeof(GeneBinaryHeader);
        for (int i = 0; i < size; i++) {
            size_t written = serializeIndividual(population[i], ptr, totalSize - (ptr - buffer));
            if (written == 0) {
                free(buffer);
                Logger::logf("❌ savePopulationForGeneration: serialization failed for individual %d", i);
                return false;
            }
            ptr += written;
        }
        size_t finalSize = ptr - buffer;
        String tempPath = "/t" + String(gen) + ".tmp";
        String targetPath = String(POP_PREFIX) + String(gen) + ".bin";
        if (!FileUtils::atomicWrite(tempPath, buffer, finalSize)) {
            free(buffer);
            Logger::logf("❌ savePopulationForGeneration: atomicWrite failed for gen=%lu", gen);
            return false;
        }
        free(buffer);
        if (!verifyPopulationFile(tempPath, size)) {
            SPIFFS.remove(tempPath);
            Logger::logf("❌ savePopulationForGeneration: verification failed for gen=%lu", gen);
            return false;
        }
        if (SPIFFS.exists(targetPath)) {
            SPIFFS.remove(targetPath);
        }
        if (!SPIFFS.rename(tempPath, targetPath)) {
            SPIFFS.remove(tempPath);
            Logger::logf("❌ savePopulationForGeneration: rename failed for gen=%lu", gen);
            return false;
        }
        if (!verifyPopulationFile(targetPath, size)) {
            SPIFFS.remove(targetPath);
            Logger::logf("❌ savePopulationForGeneration: final verification failed for gen=%lu", gen);
            return false;
        }
        Logger::logf("💾 savePopulationForGeneration [%s]: gen=%lu saved ✅", tag, gen);
        return true;
    }

    static bool verifyPopulationFile(const String& path, int expectedSize) {
        if (!RobustStorage::isReady() || !SPIFFS.exists(path)) return false;
        File file = SPIFFS.open(path, FILE_READ);
        if (!file) return false;
        size_t fileSize = file.size();
        if (fileSize < sizeof(GeneBinaryHeader)) {
            file.close();
            return false;
        }
        uint8_t* buffer = (uint8_t*)malloc(fileSize);
        if (!buffer) { file.close(); return false; }
        file.read(buffer, fileSize);
        file.close();
        GeneBinaryHeader geneHeader;
        memcpy(&geneHeader, buffer, sizeof(GeneBinaryHeader));
        if (geneHeader.magic != MAGIC_GENE_POP) { free(buffer); return false; }
        bool valid = (geneHeader.popSize == expectedSize && geneHeader.popSize <= POPULATION_SIZE);
        free(buffer);
        return valid;
    }

    static void commitGeneration(uint32_t gen) {
        currentGeneration = gen;
        RollingStorage::setCurrentGeneration(gen);
        safeSaveState();
        pendingGeneration = 0;
        hasPendingGeneration = false;
        Logger::logf("✅ Generation %lu committed (state.mrk updated)", gen);
    }

    static void rollbackPendingGeneration() {
        if (hasPendingGeneration) {
            Logger::logf("⚠️ Rolling back pending generation %lu", pendingGeneration);
            String tempPath = String(POP_PREFIX) + "temp_gen_" + String(pendingGeneration) + ".bin.tmp";
            if (SPIFFS.exists(tempPath)) SPIFFS.remove(tempPath);
            pendingGeneration = 0;
            hasPendingGeneration = false;
        }
    }

    static bool commitPendingGeneration() {
        if (!hasPendingGeneration) {
            Logger::log("⚠️ No pending generation to commit");
            return false;
        }
        uint32_t gen = pendingGeneration;
        Logger::logf("🔄 Committing pending generation %lu", gen);
        String targetPath = String(POP_PREFIX) + String(gen) + ".bin";
        if (!SPIFFS.exists(targetPath)) {
            Logger::logf("❌ Cannot commit gen %lu: population file not found!", gen);
            rollbackPendingGeneration();
            return false;
        }
        if (!verifyPopulationFile(targetPath, POPULATION_SIZE)) {
            Logger::logf("❌ Cannot commit gen %lu: population file verification failed!", gen);
            rollbackPendingGeneration();
            return false;
        }
        commitGeneration(gen);
        return true;
    }

    static bool hasPendingGenerationToCommit() { return hasPendingGeneration; }
    static uint32_t getPendingGeneration() { return pendingGeneration; }

    static uint32_t findLatestGeneration() {
        if (!RobustStorage::isReady()) return 0;
        uint32_t latest = 0;
        File root = SPIFFS.open("/");
        if (!root) return 0;
        while (File f = root.openNextFile()) {
            String name = normPath(f.name());   // [v10.5] 规范化
            if (name.startsWith(POP_PREFIX) && name.endsWith(".bin")) {
                String numStr = name.substring(strlen(POP_PREFIX), name.lastIndexOf('.'));
                uint32_t gen = numStr.toInt();
                if (gen > 0 && gen < 1000000) {
                    if (gen > latest) latest = gen;
                }
            }
            f.close();
        }
        root.close();
        return latest;
    }

    static bool saveIndividualRecord(uint32_t generation, int individual, const Gene& gene) {
        if (!RobustStorage::isReady()) return false;
        size_t estimatedSize = 512 + (size_t)gene.ruleCount * 64;
        if (!RollingStorage::ensureSpace(estimatedSize)) {
            Logger::logf("❌ saveIndividualRecord: insufficient space");
            return false;
        }
        String path = String(GEN_RECORD_PREFIX) + String(generation) + "_id_" + String(individual) + ".csv";
        String content = "# individual_meta: survival_time=" + String(gene.survival_time)
                       + " distance_ticks=" + String(gene.distance_ticks)
                       + " noveltyScore=" + String(gene.noveltyScore, 6)
                       + " obstacleThreshold=" + String(gene.obstacleThreshold)
                       + " clearThreshold=" + String(gene.clearThreshold)
                       + " encoderDiffThreshold=" + String(gene.encoderDiffThreshold)
                       + " encoderDiffMin=" + String(gene.encoderDiffMin)
                       + " wheelSpinThreshold=" + String(gene.wheelSpinThreshold)
                       + " wheelStopThreshold=" + String(gene.wheelStopThreshold)
                       + " stuckWindowSize=" + String(gene.stuckWindowSize)
                       + " chaosNoiseAmplifier=" + String(gene.chaosNoiseAmplifier)
                       + " chaosMinPwm=" + String(gene.chaosMinPwm)
                       + " chaosTimeoutMs=" + String(gene.chaosTimeoutMs)
                       + " chaosForceTimeoutMs=" + String(gene.chaosForceTimeoutMs)
                       + " hasChaosRules=" + String(gene.hasChaosRules ? 1 : 0)
                       + " chaosRuleCount=" + String(gene.chaosRuleCount)
                       + " chaosRulesStartIndex=" + String(gene.chaosRulesStartIndex) + "\n";
        content += "ruleIndex,condType,condValue,condOp,motorL,motorR,durationMs,nextRule,isChaosRule\n";
        for (int i = 0; i < gene.ruleCount; i++) {
            const BehaviorRule& r = gene.rules[i];
            bool isChaos = (gene.hasChaosRules &&
                            i >= gene.chaosRulesStartIndex &&
                            i < gene.chaosRulesStartIndex + gene.chaosRuleCount);
            content += String(i) + "," + String(r.condType) + "," + String(r.condValue) + ",";
            content += String(r.condOp) + "," + String(r.motorL) + "," + String(r.motorR) + ",";
            content += String(r.durationMs) + "," + String(r.nextRule) + ",";
            content += (isChaos ? "1" : "0");
            content += "\n";
        }
        bool success = FileUtils::atomicWriteString(path, content);
        Logger::logf("💾 saveIndividualRecord gen=%lu id=%d %s", generation, individual, success ? "✅" : "❌");
        return success;
    }

    static bool saveChaosRecord(const ChaoticTestRecord& record) {
        (void)record;
        return true;
    }

    static void clearAllExperimentData(bool confirmed = false) {
        if (!confirmed) {
            Logger::log("⚠️ clearAllExperimentData() called without confirmation");
            Logger::log("   Call with confirmed=true to execute");
            return;
        }
        if (!RobustStorage::isReady()) return;
        Logger::log("========================================");
        Logger::log("⚠️ MANUAL DATA CLEAR");
        Logger::log("   🔒 L1+L2 DATA IS PROTECTED");
        Logger::log("   📋 Only L3+L4 will be deleted");
        Logger::log("========================================");

        std::vector<String> toDelete;
        std::vector<String> protectedFiles;

        File root = SPIFFS.open("/");
        if (root) {
            while (File file = root.openNextFile()) {
                String name = normPath(file.name());   // [v10.5] 规范化
                int level = TieredStorageManager::getDataLevel(name);

                if (level == DATA_LEVEL_CORE || level == DATA_LEVEL_GENE) {
                    protectedFiles.push_back(name);
                    continue;
                }
                if (level == DATA_LEVEL_POP || level == DATA_LEVEL_PROCESS) {
                    toDelete.push_back(name);
                }
                file.close();
            }
            root.close();
        }
        Logger::log("🔒 PROTECTED (L1+L2):");
        for (const String& name : protectedFiles) {
            Logger::logf("  ✅ %s", name.c_str());
        }
        int deleted = 0;
        for (const String& name : toDelete) {
            if (SPIFFS.remove(name)) {
                Logger::logf("  🗑️ Deleted: %s", name.c_str());
                deleted++;
            }
        }
        currentExperimentId = (uint32_t)millis();
        experimentActive = true;
        pendingGeneration = 0;
        hasPendingGeneration = false;
        safeSaveState();
        Logger::logf("✅ Clear complete: %d files deleted, %d protected",
                     deleted, protectedFiles.size());
    }

    static void tick() {
        if (pendingStateSave) {
            flushPendingStateSave();
        }
    }
};

const char* GeneStorage::POP_PREFIX = "/pop_gen_";
const char* GeneStorage::GEN_RECORD_PREFIX = "/gen_";
const char* GeneStorage::FRAME_LOG_PREFIX = "/frm_";
const char* GeneStorage::EXPERIMENT_MARKER = "/experiment_state.mrk";
uint32_t GeneStorage::currentExperimentId = 0;
uint32_t GeneStorage::currentGeneration = 0;
bool GeneStorage::experimentActive = false;
uint32_t GeneStorage::pendingGeneration = 0;
bool GeneStorage::hasPendingGeneration = false;
bool GeneStorage::pendingStateSave = false;

// ================================================================
// NoveltyArchive 类
// ================================================================
class NoveltyArchive {
private:
    BehaviorDescriptor archive[NOVELTY_ARCHIVE_MAX];
    int archiveSize;
    BehaviorDescriptor maxValues;
    int lastSavedSize;
    static const int MAX_SAVE_RETRIES = 3;
    static const char* ARCHIVE_FILE;
    static const char* BACKUP_FILE;
    static const char* TEMP_FILE;
    static const char* EMERGENCY_FILE;

    void updateMaxValues(const BehaviorDescriptor& desc) {
        if (desc.leftSensorMean > maxValues.leftSensorMean) maxValues.leftSensorMean = desc.leftSensorMean;
        if (desc.rightSensorMean > maxValues.rightSensorMean) maxValues.rightSensorMean = desc.rightSensorMean;
        if (desc.sensorVariance > maxValues.sensorVariance) maxValues.sensorVariance = desc.sensorVariance;
        if (desc.avgSpeed > maxValues.avgSpeed) maxValues.avgSpeed = desc.avgSpeed;
        if (desc.speedVariance > maxValues.speedVariance) maxValues.speedVariance = desc.speedVariance;
        if (desc.totalDistance > maxValues.totalDistance) maxValues.totalDistance = desc.totalDistance;
        if (desc.turnBias > maxValues.turnBias) maxValues.turnBias = desc.turnBias;
        float absAsym = fabsf(desc.sensorAsymmetry);
        if (absAsym > maxValues.sensorAsymmetry) maxValues.sensorAsymmetry = absAsym;
        if (desc.forwardRatio > maxValues.forwardRatio) maxValues.forwardRatio = desc.forwardRatio;
        if (desc.turnRatio > maxValues.turnRatio) maxValues.turnRatio = desc.turnRatio;
        if (desc.reverseRatio > maxValues.reverseRatio) maxValues.reverseRatio = desc.reverseRatio;
        if (desc.idleRatio > maxValues.idleRatio) maxValues.idleRatio = desc.idleRatio;
        // ★ v10.9 updateMaxValues 混沌特征 NaN 保护
        if (!isnan(desc.chaosFrameRatio) && desc.chaosFrameRatio > maxValues.chaosFrameRatio)
            maxValues.chaosFrameRatio = desc.chaosFrameRatio;
        if (!isnan(desc.chaosSpeedDeltaL)) {
            float a = fabsf(desc.chaosSpeedDeltaL);
            if (a > maxValues.chaosSpeedDeltaL) maxValues.chaosSpeedDeltaL = a;
        }
        if (!isnan(desc.chaosSpeedDeltaR)) {
            float a = fabsf(desc.chaosSpeedDeltaR);
            if (a > maxValues.chaosSpeedDeltaR) maxValues.chaosSpeedDeltaR = a;
        }
        if (!isnan(desc.chaosPwmVariance) && desc.chaosPwmVariance > maxValues.chaosPwmVariance)
            maxValues.chaosPwmVariance = desc.chaosPwmVariance;
    }

    bool tryLoadFromFile(const char* path) {
        size_t size = FileUtils::getFileSize(path);
        if (size < 12) {
            Logger::logf("⚠️ tryLoadFromFile: file too small %s (%d bytes)", path, size);
            return false;
        }
        uint8_t* buf = (uint8_t*)malloc(size);
        if (!buf) return false;
        File f = SPIFFS.open(path, FILE_READ);
        if (!f) { free(buf); return false; }
        size_t rd = f.read(buf, size);
        f.close();
        if (rd != size) { free(buf); return false; }
        if (memcmp(buf, "ARCH", 4) != 0) {
            Logger::logf("⚠️ tryLoadFromFile: invalid magic %s", path);
            free(buf);
            return false;
        }
        uint16_t ver; memcpy(&ver, buf + 4, 2);
        uint16_t cnt; memcpy(&cnt, buf + 6, 2);
        uint32_t crc; memcpy(&crc, buf + 8, 4);
        if (ver != 1 || cnt > NOVELTY_ARCHIVE_MAX) {
            Logger::logf("⚠️ tryLoadFromFile: invalid version/count %s", path);
            free(buf);
            return false;
        }
        size_t expectPayload = (size_t)12 * sizeof(float) + (size_t)cnt * 12 * sizeof(float);
        size_t expectPayloadNew = (size_t)16 * sizeof(float) + (size_t)cnt * 16 * sizeof(float);
        if (size - 12 != expectPayload && size - 12 != expectPayloadNew) {
            Logger::logf("⚠️ tryLoadFromFile: size mismatch %s", path);
            free(buf);
            return false;
        }
        if (CRC32::calculate(buf + 12, size - 12) != crc) {
            Logger::logf("⚠️ tryLoadFromFile: CRC mismatch %s", path);
            free(buf);
            return false;
        }
        archiveSize = cnt;
        bool isNewFormat = (size - 12 == expectPayloadNew);
        uint8_t* p = buf + 12;
        if (isNewFormat) {
            memcpy(&maxValues, p, 16 * sizeof(float));
            p += 16 * sizeof(float);
            for (int i = 0; i < archiveSize; i++) {
                memcpy(&archive[i], p, 16 * sizeof(float));
                p += 16 * sizeof(float);
            }
        } else {
            memcpy(&maxValues, p, 12 * sizeof(float));
            maxValues.chaosFrameRatio = 0;
            maxValues.chaosSpeedDeltaL = 0;
            maxValues.chaosSpeedDeltaR = 0;
            maxValues.chaosPwmVariance = 0;
            p += 12 * sizeof(float);
            for (int i = 0; i < archiveSize; i++) {
                memcpy(&archive[i], p, 12 * sizeof(float));
                archive[i].chaosFrameRatio = 0;
                archive[i].chaosSpeedDeltaL = 0;
                archive[i].chaosSpeedDeltaR = 0;
                archive[i].chaosPwmVariance = 0;
                p += 12 * sizeof(float);
            }
        }
        free(buf);
        Logger::logf("✅ tryLoadFromFile: loaded %d behaviors from %s", archiveSize, path);
        return true;
    }

    bool saveToPath(const char* path) {
        if (!RobustStorage::isReady()) return false;
        if (archiveSize == 0) {
            Logger::log("⚠️ saveToPath: archive empty, skipping");
            return false;
        }
        size_t payloadSize = (size_t)16 * sizeof(float) + (size_t)archiveSize * 16 * sizeof(float);
        size_t totalSize = 12 + payloadSize;
        uint8_t* payload = (uint8_t*)malloc(payloadSize);
        if (!payload) return false;
        uint8_t* p = payload;
        memcpy(p, &maxValues, 16 * sizeof(float));
        p += 16 * sizeof(float);
        for (int i = 0; i < archiveSize; i++) {
            memcpy(p, &archive[i], 16 * sizeof(float));
            p += 16 * sizeof(float);
        }
        uint32_t crc = CRC32::calculate(payload, payloadSize);
        free(payload);
        size_t totalBufSize = 12 + payloadSize;
        uint8_t* buf = (uint8_t*)malloc(totalBufSize);
        if (!buf) return false;
        p = buf;
        memcpy(p, "ARCH", 4); p += 4;
        uint16_t ver = 1; memcpy(p, &ver, 2); p += 2;
        uint16_t cnt = (uint16_t)archiveSize; memcpy(p, &cnt, 2); p += 2;
        memcpy(p, &crc, 4); p += 4;
        p = buf + 12;
        memcpy(p, &maxValues, 16 * sizeof(float));
        p += 16 * sizeof(float);
        for (int i = 0; i < archiveSize; i++) {
            memcpy(p, &archive[i], 16 * sizeof(float));
            p += 16 * sizeof(float);
        }
        bool ok = FileUtils::atomicWrite(path, buf, totalBufSize);
        free(buf);
        if (ok) {
            Logger::logf("💾 saveToPath: saved %d behaviors to %s (CRC=0x%08X)", archiveSize, path, crc);
        }
        return ok;
    }

public:
    void init() {
        archiveSize = 0;
        maxValues.init();
        lastSavedSize = 0;
    }

    float computeNovelty(const BehaviorDescriptor& desc) {
        if (archiveSize == 0) {
            static bool warned = false;
            if (!warned) {
                Logger::log("⚠️ Archive is EMPTY - novelty scores = 1.0");
                warned = true;
            }
            return 1.0f;
        }
        BehaviorDescriptor norm = desc;
        norm.normalize(maxValues);
        static float distances[NOVELTY_ARCHIVE_MAX];
        int count = 0;
        for (int i = 0; i < archiveSize; i++) {
            BehaviorDescriptor archNorm = archive[i];
            archNorm.normalize(maxValues);
            distances[count++] = norm.distance(archNorm);
        }
        int k = min(NOVELTY_K_NEAREST, count);
        for (int i = 0; i < k; i++)
            for (int j = i + 1; j < count; j++)
                if (distances[j] < distances[i]) {
                    float tmp = distances[i]; distances[i] = distances[j]; distances[j] = tmp;
                }
        float sum = 0;
        for (int i = 0; i < k; i++) sum += distances[i];
        return sum / k;
    }

    bool addIfNovel(const BehaviorDescriptor& desc) {
        if (archiveSize == 0) {
            archive[archiveSize++] = desc;
            updateMaxValues(desc);
            Logger::log("📝 First behavior added to archive");
            return true;
        }
        float novelty = computeNovelty(desc);
        if (novelty < NOVELTY_ADD_THRESHOLD && archiveSize > 0) return false;
        if (archiveSize < NOVELTY_ARCHIVE_MAX) {
            archive[archiveSize++] = desc;
        } else {
            memmove(&archive[0], &archive[1], (NOVELTY_ARCHIVE_MAX - 1) * sizeof(BehaviorDescriptor));
            archive[NOVELTY_ARCHIVE_MAX - 1] = desc;
        }
        updateMaxValues(desc);
        return true;
    }

    BehaviorDescriptor extractFromFrameLog(const FrameLogEntry* frameLog, int frameCount, int32_t totalDistance, int head = 0, bool excludeChaosFrames = false) {
        BehaviorDescriptor desc; desc.init();
        if (frameCount == 0) return desc;
        float sumL = 0, sumR = 0, sumSpeed = 0, sumSpeedSq = 0, sumTurnDiff = 0;
        int forwardFrames = 0, turnFrames = 0, reverseFrames = 0, idleFrames = 0;
        int usedFrames = 0;
        // ★ v10.8 混沌统计
        int chaosFrames = 0;
        float chaosSpeedLSum = 0, chaosSpeedRSum = 0, chaosPwmSqSum = 0;
        for (int i = 0; i < frameCount; i++) {
            int idx = (head - frameCount + i + FRAME_LOG_SIZE) % FRAME_LOG_SIZE;
            const FrameLogEntry& e = frameLog[idx];
            if (excludeChaosFrames && e.isChaosFrame) continue;
            usedFrames++;
            sumL += e.sensorLeft; sumR += e.sensorRight;
            float speed = (e.motorLeftPWM + e.motorRightPWM) / 2.0f;
            sumSpeed += speed; sumSpeedSq += speed * speed;
            sumTurnDiff += (float)e.motorLeftPWM - (float)e.motorRightPWM;
            if (e.motorLeftPWM < 10 && e.motorRightPWM < 10) idleFrames++;
            else if (e.directionL == 0 && e.directionR == 0) reverseFrames++;
            else if (abs((int)e.motorLeftPWM - (int)e.motorRightPWM) > 50) turnFrames++;
            else forwardFrames++;
            // ★ v10.8 混沌帧统计
            if (e.isChaosFrame) {
                chaosFrames++;
                chaosSpeedLSum += e.motorLeftPWM;
                chaosSpeedRSum += e.motorRightPWM;
                chaosPwmSqSum += (float)e.motorLeftPWM * (float)e.motorLeftPWM;
            }
        }
        int n = (usedFrames > 0) ? usedFrames : frameCount;
        if (usedFrames == 0) return desc;
        desc.leftSensorMean = sumL / n;
        desc.rightSensorMean = sumR / n;
        desc.avgSpeed = sumSpeed / n;
        desc.speedVariance = (sumSpeedSq / n) - (desc.avgSpeed * desc.avgSpeed);
        if (desc.speedVariance < 0) desc.speedVariance = 0;
        desc.turnBias = sumTurnDiff / (float)n / 255.0f;
        float asymDenom = fabsf(desc.leftSensorMean) + fabsf(desc.rightSensorMean);
        desc.sensorAsymmetry = (asymDenom > 1.0f)
            ? (desc.leftSensorMean - desc.rightSensorMean) / asymDenom
            : 0.0f;
        desc.totalDistance = (float)abs(totalDistance) / 1000.0f;
        int activeFrames = n - idleFrames;
        if (activeFrames > 0) {
            desc.forwardRatio = (float)forwardFrames / activeFrames;
            desc.turnRatio = (float)turnFrames / activeFrames;
            desc.reverseRatio = (float)reverseFrames / activeFrames;
        }
        desc.idleRatio = (float)idleFrames / n;
        float varL = 0, varR = 0;
        for (int i = 0; i < frameCount; i++) {
            int idx = (head - frameCount + i + FRAME_LOG_SIZE) % FRAME_LOG_SIZE;
            const FrameLogEntry& e = frameLog[idx];
            if (excludeChaosFrames && e.isChaosFrame) continue;
            float dL = e.sensorLeft - desc.leftSensorMean;
            float dR = e.sensorRight - desc.rightSensorMean;
            varL += dL*dL; varR += dR*dR;
        }
        desc.sensorVariance = (varL + varR) / (2.0f * n);
        // ★ v10.8 计算混沌特征
        desc.chaosFrameRatio = (float)chaosFrames / n;
        if (chaosFrames > 0) {
            float chaosAvgL = chaosSpeedLSum / chaosFrames;
            float chaosAvgR = chaosSpeedRSum / chaosFrames;
            desc.chaosSpeedDeltaL = chaosAvgL - desc.avgSpeed;
            desc.chaosSpeedDeltaR = chaosAvgR - desc.avgSpeed;
            desc.chaosPwmVariance = chaosPwmSqSum / chaosFrames - chaosAvgL * chaosAvgL;
            if (desc.chaosPwmVariance < 0) desc.chaosPwmVariance = 0;
            // ★ v10.9 限制上限 255^2 = 65025
            if (desc.chaosPwmVariance > 65025.0f) desc.chaosPwmVariance = 65025.0f;
        }
        return desc;
    }

    int getArchiveSize() const { return archiveSize; }
    int getLastSavedSize() const { return lastSavedSize; }

    void clear() {
        Logger::log("========================================");
        Logger::log("⚠️ NoveltyArchive::clear() called");
        Logger::log("   🔒 L1 CORE DATA - AUTO-CLEAR DISABLED");
        Logger::log("========================================");
    }

    bool clearConfirmed(bool confirmed = false) {
        if (!confirmed) {
            Logger::log("⚠️ clearConfirmed() called without confirmation");
            return false;
        }
        Logger::log("========================================");
        Logger::log("🗑️ MANUAL ARCHIVE CLEAR EXECUTED");
        Logger::log("========================================");
        archiveSize = 0;
        maxValues.init();
        lastSavedSize = 0;
        if (RobustStorage::isReady()) {
            if (SPIFFS.exists(ARCHIVE_FILE)) SPIFFS.remove(ARCHIVE_FILE);
            if (SPIFFS.exists(BACKUP_FILE)) SPIFFS.remove(BACKUP_FILE);
            if (SPIFFS.exists(TEMP_FILE)) SPIFFS.remove(TEMP_FILE);
            if (SPIFFS.exists(EMERGENCY_FILE)) SPIFFS.remove(EMERGENCY_FILE);
        }
        Logger::log("✅ Archive cleared (manual)");
        return true;
    }

    bool load() {
        if (!RobustStorage::isReady()) {
            Logger::log("❌ Novelty archive: SPIFFS not ready!");
            return false;
        }
        if (tryLoadFromFile(ARCHIVE_FILE)) {
            Logger::logf("✅ Novelty archive loaded: %d behaviors", archiveSize);
            lastSavedSize = archiveSize;
            return true;
        }
        if (tryLoadFromFile(BACKUP_FILE)) {
            Logger::log("⚠️ Novelty archive: loaded from backup!");
            if (saveToPath(ARCHIVE_FILE)) {
                Logger::log("✅ Novelty archive restored from backup to main");
            }
            lastSavedSize = archiveSize;
            return true;
        }
        if (tryLoadFromFile(EMERGENCY_FILE)) {
            Logger::log("🚨 Novelty archive: loaded from EMERGENCY file!");
            if (saveToPath(ARCHIVE_FILE)) {
                Logger::log("✅ Novelty archive restored from emergency to main");
            }
            lastSavedSize = archiveSize;
            return true;
        }
        if (archiveSize > 0) {
            Logger::logf("⚠️ File load failed, keeping in-memory archive (%d behaviors)", archiveSize);
            return true;
        }
        Logger::log("========================================");
        Logger::log("❌ Novelty archive NOT FOUND");
        Logger::log("   📋 Archive will be built from behavior data");
        Logger::log("========================================");
        archiveSize = 0;
        maxValues.init();
        lastSavedSize = 0;
        return false;
    }

    bool loadWithBackup() {
        return load();
    }

    bool save() {
        if (!RobustStorage::isReady()) {
            Logger::log("❌ Archive save: SPIFFS not ready");
            return false;
        }
        if (archiveSize == 0) {
            Logger::log("📝 Archive empty, skipping save");
            return false;
        }
        size_t payloadSize = (size_t)16 * sizeof(float) + (size_t)archiveSize * 16 * sizeof(float);
        size_t totalSize = 12 + payloadSize;
        size_t needed = totalSize + 4096;
        if (!RollingStorage::ensureSpace(needed, GeneStorage::getCurrentGeneration())) {
            Logger::log("⚠️ Archive save: insufficient space, attempting emergency cleanup");
            RollingStorage::enforceRetention();
            if (!RollingStorage::ensureSpace(needed, GeneStorage::getCurrentGeneration())) {
                Logger::log("❌ Archive save: still insufficient after cleanup");
                return false;
            }
        }
        uint8_t* buf = (uint8_t*)malloc(totalSize);
        if (!buf) return false;
        uint8_t* p = buf;
        memcpy(p, "ARCH", 4); p += 4;
        uint16_t ver = 1; memcpy(p, &ver, 2); p += 2;
        uint16_t cnt = (uint16_t)archiveSize; memcpy(p, &cnt, 2); p += 2;
        uint8_t* payloadStart = p + 4;
        uint8_t* payloadPtr = payloadStart;
        memcpy(payloadPtr, &maxValues, 16 * sizeof(float)); payloadPtr += 16 * sizeof(float);
        for (int i = 0; i < archiveSize; i++) {
            memcpy(payloadPtr, &archive[i], 16 * sizeof(float));
            payloadPtr += 16 * sizeof(float);
        }
        size_t payloadLen = payloadPtr - payloadStart;
        uint32_t crc = CRC32::calculate(payloadStart, payloadLen);
        memcpy(p, &crc, 4); p += 4;
        memcpy(p, payloadStart, payloadLen);

        for (int retry = 0; retry < 3; retry++) {
            String tempPath = String(ARCHIVE_FILE) + ".tmp";
            if (SPIFFS.exists(tempPath)) SPIFFS.remove(tempPath);
            if (FileUtils::atomicWrite(tempPath, buf, totalSize)) {
                if (tryLoadFromFile(tempPath.c_str())) {
                    if (SPIFFS.exists(ARCHIVE_FILE)) {
                        if (SPIFFS.exists(BACKUP_FILE)) SPIFFS.remove(BACKUP_FILE);
                        SPIFFS.rename(ARCHIVE_FILE, BACKUP_FILE);
                    }
                    SPIFFS.rename(tempPath, ARCHIVE_FILE);
                    if (tryLoadFromFile(ARCHIVE_FILE)) {
                        free(buf);
                        Logger::logf("💾 Archive saved: %d behaviors (retry=%d)", archiveSize, retry);
                        return true;
                    }
                }
            }
            delay(100 * (retry + 1));
        }
        String emergencyPath = String(EMERGENCY_FILE);
        if (FileUtils::atomicWrite(emergencyPath, buf, totalSize)) {
            Logger::logf("🚨 Archive saved to emergency file (size=%d)", totalSize);
            free(buf);
            return true;
        }
        free(buf);
        Logger::log("❌ Archive save: all attempts failed");
        return false;
    }

    bool saveIncrementalSnapshot(uint32_t generation) {
        if (!RobustStorage::isReady()) return false;
        int newCount = archiveSize - lastSavedSize;
        if (newCount < 0) newCount = 0;
        size_t payloadSize = 4 + 4 + 2 + (size_t)newCount * 64;
        size_t totalSize = 12 + payloadSize;
        if (!RollingStorage::ensureSpace(totalSize + 256, generation)) {
            Logger::logf("❌ nova_gen_%lu: insufficient space (need %d)", generation, totalSize);
            return false;
        }
        uint8_t* buf = (uint8_t*)malloc(totalSize);
        if (!buf) return false;
        uint8_t* p = buf;
        memcpy(p, "NOVG", 4); p += 4;
        uint16_t ver = 1; memcpy(p, &ver, 2); p += 2;
        uint16_t dummy = 0; memcpy(p, &dummy, 2); p += 2;
        uint32_t crc = 0; p += 4;
        uint8_t* payloadStart = p;
        uint32_t curSize = (uint32_t)archiveSize;
        uint32_t prevSize = (uint32_t)lastSavedSize;
        uint16_t inc = (uint16_t)newCount;
        memcpy(p, &curSize, 4); p += 4;
        memcpy(p, &prevSize, 4); p += 4;
        memcpy(p, &inc, 2); p += 2;
        for (int i = 0; i < newCount; i++) {
            int idx = lastSavedSize + i;
            if (idx >= NOVELTY_ARCHIVE_MAX) break;
            memcpy(p, &archive[idx], 64); p += 64;
        }
        size_t payloadLen = p - payloadStart;
        uint32_t calcCrc = CRC32::calculate(payloadStart, payloadLen);
        memcpy(buf + 8, &calcCrc, 4);
        String path = "/nova_gen_" + String(generation) + ".bin";
        bool ok = FileUtils::atomicWrite(path, buf, totalSize);
        free(buf);
        if (ok) {
            lastSavedSize = archiveSize;
            Logger::logf("💾 nova_gen_%lu: saved (total=%d, new=%d, %d bytes) ✅",
                         generation, archiveSize, newCount, totalSize);
        } else {
            Logger::logf("❌ nova_gen_%lu: save failed", generation);
        }
        return ok;
    }
};

const char* NoveltyArchive::ARCHIVE_FILE = "/novelty_archive.bin";
const char* NoveltyArchive::BACKUP_FILE = "/novelty_archive.bin.bak";
const char* NoveltyArchive::TEMP_FILE = "/novelty_archive.bin.tmp";
const char* NoveltyArchive::EMERGENCY_FILE = "/novelty_archive.emergency.bin";

// ================================================================
// RAMLogBuffer 类
// ================================================================
class RAMLogBuffer {
private:
    static CompressedFrameEntry buffer[RAM_LOG_BUFFER_SIZE];
    static int head;
    static int count;
    static uint32_t generation;
    static int individual;
    static bool hasData;
    static uint32_t testStartTime;

    static CompressedFrameEntry compressFrame(const FrameLogEntry& src, const FrameLogEntry* prev) {
        CompressedFrameEntry dst;
        dst.timestamp_ms = src.timestamp_ms;
        dst.sensorLeft = src.sensorLeft;
        dst.sensorRight = src.sensorRight;
        dst.directionL = src.directionL;
        dst.directionR = src.directionR;
        dst.chaosActive = src.chaosActive;
        dst.isChaosFrame = src.isChaosFrame;
        dst.state = src.state;
        dst.reserved = 0;
        if (prev) {
            dst.motorLeftPWM = constrain(src.motorLeftPWM - prev->motorLeftPWM, -128, 127);
            dst.motorRightPWM = constrain(src.motorRightPWM - prev->motorRightPWM, -128, 127);
        } else {
            dst.motorLeftPWM = src.motorLeftPWM;
            dst.motorRightPWM = src.motorRightPWM;
        }
        return dst;
    }

public:
    static void init() {
        head = 0;
        count = 0;
        generation = 0;
        individual = -1;
        hasData = false;
        testStartTime = 0;
        memset(buffer, 0, sizeof(buffer));
        Logger::logf("RAMLogBuffer initialized (%d frames)", RAM_LOG_BUFFER_SIZE);
    }

    static void startNewTest(uint32_t gen, int ind) {
        head = 0;
        count = 0;
        generation = gen;
        individual = ind;
        hasData = true;
        testStartTime = millis();
    }

    static void addFrame(const FrameLogEntry& frame) {
        if (!hasData) return;
        const FrameLogEntry* prev = NULL;
        static FrameLogEntry prevFrame;
        if (count > 0) {
            prev = &prevFrame;
        }
        CompressedFrameEntry compressed = compressFrame(frame, prev);
        buffer[head] = compressed;
        prevFrame = frame;
        head = (head + 1) % RAM_LOG_BUFFER_SIZE;
        if (count < RAM_LOG_BUFFER_SIZE) count++;
    }

    static String extractFrames(int startFrame, int endFrame) {
        if (!hasData || count == 0) return "No data available\n";
        if (startFrame < 0) startFrame = 0;
        if (endFrame >= count) endFrame = count - 1;
        if (startFrame > endFrame) return "Invalid range\n";
        int framesToExtract = endFrame - startFrame + 1;
        if (framesToExtract > 2000) {
            return "Too many frames (max 2000 per request)\n";
        }
        int startPos = (head - count + startFrame + RAM_LOG_BUFFER_SIZE) % RAM_LOG_BUFFER_SIZE;
        String output = "=== RAM Log Extract ===\n";
        output += "Generation: " + String(generation) + "\n";
        output += "Individual: " + String(individual) + "\n";
        output += "Frames: " + String(startFrame) + "-" + String(endFrame) +
                  " (total " + String(count) + " frames)\n";
        output += "Time(ms) | SensorL | SensorR | PWM_L | PWM_R | State\n";
        output += "---------|---------|---------|-------|-------|-------\n";
        int pwmL = 0, pwmR = 0;
        for (int i = 0; i < framesToExtract; i++) {
            int idx = (startPos + i) % RAM_LOG_BUFFER_SIZE;
            const CompressedFrameEntry& e = buffer[idx];
            pwmL = constrain(pwmL + e.motorLeftPWM, 0, 255);
            pwmR = constrain(pwmR + e.motorRightPWM, 0, 255);
            if (i == 0) {
                pwmL = e.motorLeftPWM;
                pwmR = e.motorRightPWM;
            }
            const char* stateNames[] = {"IDLE", "WALKING", "STUCK", "CHAOS"};
            output += String(e.timestamp_ms) + " | " +
                      String(e.sensorLeft) + " | " +
                      String(e.sensorRight) + " | " +
                      String(pwmL) + " | " +
                      String(pwmR) + " | " +
                      String(stateNames[e.state & 0x07]) + "\n";
        }
        return output;
    }

    static size_t extractBinary(uint8_t* outBuffer, size_t maxSize,
                                int startFrame, int endFrame) {
        if (!hasData || count == 0 || outBuffer == NULL) return 0;
        if (startFrame < 0) startFrame = 0;
        if (endFrame >= count) endFrame = count - 1;
        if (startFrame > endFrame) return 0;
        int framesToExtract = min(endFrame - startFrame + 1,
                                  (int)(maxSize / sizeof(CompressedFrameEntry)));
        int startPos = (head - count + startFrame + RAM_LOG_BUFFER_SIZE) % RAM_LOG_BUFFER_SIZE;
        uint8_t* ptr = outBuffer;
        for (int i = 0; i < framesToExtract; i++) {
            int idx = (startPos + i) % RAM_LOG_BUFFER_SIZE;
            memcpy(ptr, &buffer[idx], sizeof(CompressedFrameEntry));
            ptr += sizeof(CompressedFrameEntry);
        }
        return framesToExtract * sizeof(CompressedFrameEntry);
    }

    static int getFrameCount() { return count; }
    static uint32_t getGeneration() { return generation; }
    static int getIndividual() { return individual; }
    static bool hasDataAvailable() { return hasData && count > 0; }
    static void clear() { head = 0; count = 0; hasData = false; }
};

CompressedFrameEntry RAMLogBuffer::buffer[RAM_LOG_BUFFER_SIZE];
int RAMLogBuffer::head = 0;
int RAMLogBuffer::count = 0;
uint32_t RAMLogBuffer::generation = 0;
int RAMLogBuffer::individual = -1;
bool RAMLogBuffer::hasData = false;
uint32_t RAMLogBuffer::testStartTime = 0;
// ===================== 第 4 段开始 =====================

// ================================================================
// MotorController 类
// ================================================================
class MotorController {
private:
    static int      currentSpeedL, currentSpeedR;
    static volatile int32_t distanceTicks;
    static uint32_t lastMoveTime;
    static bool     motorEnabled;
    static uint32_t testStartTime;
    static int      leftSensorRaw, rightSensorRaw;
    static int      currentRuleIndex;
    static uint32_t ruleStartTime;
    static bool     ruleActive;
    static FrameLogEntry frameLog[FRAME_LOG_SIZE];
    static int      frameLogHead;
    static int      frameLogCount;

    static bool     lastLeftA, lastRightA;
    static uint32_t lastEncoderReadTime;

    static bool     actionInProgress;
    static uint32_t actionStartTime;
    static uint16_t actionDurationMs;
    static int32_t  actionLastTicks;
    static int      actionStallFrames;

    static volatile int32_t leftTicks;
    static volatile int32_t rightTicks;

    static MotorState motorState;
    static uint32_t stateEnterTime;
    static int32_t stateEntryDistance;
    static uint32_t stuckStartTime;
    static uint32_t accumulatedStuckTime;
    static int32_t lastDiff;
    static bool deathFlag;

    static int16_t  encoderDiffThreshold;
    static int16_t  encoderDiffMin;
    static int16_t  wheelSpinThreshold;
    static int16_t  wheelStopThreshold;
    static uint8_t  stuckWindowSize;
    static int16_t  chaosNoiseAmplifier;
    static int16_t  chaosMinPwm;
    static uint16_t chaosTimeoutMs;
    static uint16_t chaosForceTimeoutMs;
    static int16_t  obstacleThreshold;
    static int16_t  clearThreshold;

    static int32_t diffHistory[MAX_STUCK_WINDOW];
    static int historyIndex;
    static int historyCount;

    static uint32_t chaosStartTime;
    static uint32_t chaosStartMicros;

    static int      chaosStableFrameCount;
    static int32_t  lastChaosTicks;

    static int32_t  testChaosTriggerCount;
    static uint32_t testChaosTotalDuration;
    static uint32_t testChaosMaxDuration;
    static uint32_t testChaosFirstTriggerTime;
    static uint32_t testChaosLastTriggerTime;
    static int32_t  baselineDistanceTicks;
    static int32_t  chaosDistanceTicks;
    static int      baselineFrameCount;
    static int      baselineIdleFrames;      // [审计D1修复] IDLE状态帧数
    static int      baselineWalkingFrames;   // [审计D1修复] WALKING状态帧数
    static int      chaosFrameCount;
    static float    baselineSpeedLSum;
    static float    baselineSpeedRSum;
    static float    chaosSpeedLSum;
    static float    chaosSpeedRSum;
    static bool     chaosHappened;
    static int      chaosInterruptedCount;   // [审计D3修复] 混沌异常中断计数
    static uint8_t  lastChaosExitReason;     // [审计D4修复] 上次混沌退出原因
    static int32_t  chaosStartDistance;
    static int32_t  lastChaosEndDistance;
    static uint32_t lastIncrementalSaveFrame;

    static ChaosSnapshotEntry chaosSnapshots[MAX_CHAOS_SNAPSHOTS];
    static int      chaosSnapshotCount;
    static int8_t   lastChaosMotorL;
    static int8_t   lastChaosMotorR;
    static int      chaosSnapshotStableFrames;

    static uint32_t stuckClearTimer;
    static uint8_t  stuckBounceCount;
    static uint32_t lastStuckClearTime;
    static uint32_t lastStuckEnterTime;
    static int16_t  savedEncoderDiffThreshold;
    static uint32_t noProgressTimer;
    static int32_t  lastTotalTicks;

    static bool     escapePhase1Done;
    static uint32_t escapeStartTime;

        static int      frameSampleCounter;

    // ★ 新增：噪声直流估计（供 updateNoiseDc / readPhysicalNoise 共用）
    static float    noiseDcEstimate;
    static bool     noiseDcInit;

// 只更新 dc，不返回值（供 updateChaos / startChaos 预收敛调用）
static void updateNoiseDc() {
    int raw = analogRead(PIN_NOISE_SOURCE);

    if (!noiseDcInit) {
        noiseDcEstimate = (float)raw;
        noiseDcInit = true;
    }

    noiseDcEstimate = noiseDcEstimate * 0.99f + (float)raw * 0.01f;
}

// 只读噪声，用当前的 dc（不再自己更新 dc）
static float readPhysicalNoise() {
    int raw = analogRead(PIN_NOISE_SOURCE);
    float ac = (float)raw - noiseDcEstimate;

    const float AC_SCALE = 500.0f;
    return constrain(ac / AC_SCALE, -1.0f, 1.0f);
}

    static void flushChaosSnapshot() {
        if (chaosSnapshotCount > 0) {
            ChaosSnapshotEntry& snap = chaosSnapshots[chaosSnapshotCount - 1];
            snap.durationMs = chaosSnapshotStableFrames * 10;
            chaosSnapshotStableFrames = 0;
        }
    }

    static void clearChaosSnapshots() {
        chaosSnapshotCount = 0;
        memset(chaosSnapshots, 0, sizeof(chaosSnapshots));
        chaosSnapshotStableFrames = 0;
        lastChaosMotorL = 0;
        lastChaosMotorR = 0;
    }

    static void setMotorSpeed(int leftPWM, int rightPWM) {
        if (!motorEnabled) {
            ledcWrite(LEDC_CHANNEL_LEFT, 0);
            ledcWrite(LEDC_CHANNEL_RIGHT, 0);
            digitalWrite(PIN_LEFT_DIR2, LOW);
            digitalWrite(PIN_RIGHT_DIR2, LOW);
            currentSpeedL = 0;
            currentSpeedR = 0;
            return;
        }
        int outL;
        if (leftPWM > 0) {
            outL = constrain((int)(leftPWM * LEFT_FWD_GAIN), 0, MOTOR_PWM_MAX);
            if (outL > 0 && outL < LEFT_DEADZONE) outL = LEFT_DEADZONE;
        } else if (leftPWM < 0) {
            outL = constrain((int)(-leftPWM * LEFT_FWD_GAIN * LEFT_REV_GAIN), 0, MOTOR_PWM_MAX);
            if (outL > 0 && outL < LEFT_DEADZONE) outL = LEFT_DEADZONE;
            outL = -outL;
        } else {
            outL = 0;
        }
        int outR;
        if (rightPWM > 0) {
            outR = constrain((int)(rightPWM * RIGHT_FWD_GAIN), 0, MOTOR_PWM_MAX);
            if (outR > 0 && outR < RIGHT_DEADZONE) outR = RIGHT_DEADZONE;
        } else if (rightPWM < 0) {
            outR = constrain((int)(-rightPWM * RIGHT_FWD_GAIN * RIGHT_REV_GAIN), 0, MOTOR_PWM_MAX);
            if (outR > 0 && outR < RIGHT_DEADZONE) outR = RIGHT_DEADZONE;
            outR = -outR;
        } else {
            outR = 0;
        }
        if (outL > 0) {
            digitalWrite(PIN_LEFT_DIR2, LEFT_FORWARD);
            ledcWrite(LEDC_CHANNEL_LEFT, outL);
        } else if (outL < 0) {
            digitalWrite(PIN_LEFT_DIR2, LEFT_REVERSE);
            ledcWrite(LEDC_CHANNEL_LEFT, -outL);
        } else {
            digitalWrite(PIN_LEFT_DIR2, LOW);
            ledcWrite(LEDC_CHANNEL_LEFT, 0);
        }
        if (outR > 0) {
            digitalWrite(PIN_RIGHT_DIR2, RIGHT_FORWARD);
            ledcWrite(LEDC_CHANNEL_RIGHT, outR);
        } else if (outR < 0) {
            digitalWrite(PIN_RIGHT_DIR2, RIGHT_REVERSE);
            ledcWrite(LEDC_CHANNEL_RIGHT, -outR);
        } else {
            digitalWrite(PIN_RIGHT_DIR2, LOW);
            ledcWrite(LEDC_CHANNEL_RIGHT, 0);
        }
        currentSpeedL = abs(outL);
        currentSpeedR = abs(outR);
    }

    static void updateEncoders() {
        if (millis() - lastEncoderReadTime < 10) return;
        lastEncoderReadTime = millis();
        bool leftA = digitalRead(PIN_LEFT_ENC_A);
        bool rightA = digitalRead(PIN_RIGHT_ENC_A);
        if (leftA && !lastLeftA) {
            if (digitalRead(PIN_LEFT_ENC_B) == HIGH) leftTicks--;
            else leftTicks++;
        }
        if (rightA && !lastRightA) {
            if (digitalRead(PIN_RIGHT_ENC_B) == HIGH) rightTicks++;
            else rightTicks--;
        }
        distanceTicks = abs(leftTicks) + abs(rightTicks);
        lastLeftA = leftA;
        lastRightA = rightA;
    }

    static void clearStuckState() {
        if (motorState == STATE_STUCK || motorState == STATE_CHAOS) {
            motorState = STATE_IDLE;
        }
        if (stuckStartTime > 0) {
            accumulatedStuckTime += millis() - stuckStartTime;
        }
        stuckStartTime = 0;
    }

    static void setMotorState(MotorState newState) {
        if (motorState == newState) return;
        motorState = newState;
        stateEnterTime = millis();
        stateEntryDistance = distanceTicks;
        const char* stateNames[] = {"IDLE", "WALKING", "STUCK", "CHAOS"};
        Serial.printf("[STATE] → %s\n", stateNames[newState]);
    }

    static bool detectStuckWithWindow(int32_t diff) {
        diffHistory[historyIndex] = diff;
        historyIndex = (historyIndex + 1) % stuckWindowSize;
        if (historyCount < stuckWindowSize) historyCount++;
        if (historyCount < stuckWindowSize) return false;
        int increasingCount = 0;
        for (int i = 0; i < stuckWindowSize - 1; i++) {
            int curr = (historyIndex - 1 - i + stuckWindowSize) % stuckWindowSize;
            int prev = (historyIndex - 2 - i + stuckWindowSize) % stuckWindowSize;
            if (diffHistory[curr] > diffHistory[prev] + encoderDiffMin) {
                increasingCount++;
            }
        }
        return increasingCount >= (stuckWindowSize * 7 / 10);
    }

    static bool shouldSaveFrame() {
        frameSampleCounter++;
        MotorState state = getMotorState();
        switch(state) {
            case STATE_CHAOS:
                return (frameSampleCounter % FRAME_SAMPLE_RATE_CHAOS == 0);
            case STATE_STUCK:
                return (frameSampleCounter % FRAME_SAMPLE_RATE_STUCK == 0);
            default:
                return (frameSampleCounter % FRAME_SAMPLE_RATE_NORMAL == 0);
        }
    }




    static void logChaosSnapshot(int sensorL, int sensorR, int motorL, int motorR) {
    flushChaosSnapshot();
    if (chaosSnapshotCount < MAX_CHAOS_SNAPSHOTS) {
        ChaosSnapshotEntry& snap = chaosSnapshots[chaosSnapshotCount];
        snap.sensorLeft = sensorL;
        snap.sensorRight = sensorR;
        snap.motorLeftPWM = constrain(motorL, -128, 127);
        snap.motorRightPWM = constrain(motorR, -128, 127);
        snap.timestamp_ms = millis() - chaosStartTime;
        snap.rawNoiseL = (int16_t)analogRead(PIN_NOISE_SOURCE);   // [v10.6] 立即采样
        snap.rawNoiseR = (int16_t)analogRead(PIN_NOISE_SOURCE);   // [v10.6] 再采一次
        chaosSnapshotStableFrames = 1;
        chaosSnapshotCount++;
    }
}

public:
    static bool isStuck() { return motorState == STATE_STUCK; }
    static bool isChaosActive() { return motorState == STATE_CHAOS; }
    static bool isDead() { return deathFlag; }
    static MotorState getMotorState() { return motorState; }
    static uint32_t getStateDuration() { return millis() - stateEnterTime; }

    static int16_t getObstacleThreshold() { return obstacleThreshold; }
    static int16_t getClearThreshold() { return clearThreshold; }
    static int16_t getChaosNoiseAmplifier() { return chaosNoiseAmplifier; }
    static int16_t getChaosMinPwm() { return chaosMinPwm; }
    static uint16_t getChaosTimeoutMs() { return chaosTimeoutMs; }

static void incrementChaosInterruptedCount() { chaosInterruptedCount++; }

    static void syncGeneParams(const Gene& gene) {
        encoderDiffThreshold = gene.encoderDiffThreshold;
        encoderDiffMin       = gene.encoderDiffMin;
        wheelSpinThreshold   = gene.wheelSpinThreshold;
        wheelStopThreshold   = gene.wheelStopThreshold;
        stuckWindowSize      = gene.stuckWindowSize;
        chaosNoiseAmplifier  = gene.chaosNoiseAmplifier;
        chaosMinPwm          = gene.chaosMinPwm;
        chaosTimeoutMs       = gene.chaosTimeoutMs;
        chaosForceTimeoutMs  = gene.chaosForceTimeoutMs;
        obstacleThreshold    = gene.obstacleThreshold;
        clearThreshold       = gene.clearThreshold;
        SensorCalibration::setThresholds(obstacleThreshold, clearThreshold);
    }

    static uint32_t getTotalStuckTime() {
        uint32_t current = (motorState == STATE_STUCK && stuckStartTime > 0)
                           ? (millis() - stuckStartTime) : 0;
        return accumulatedStuckTime + current;
    }
    static int32_t getStateDistanceDelta() { return distanceTicks - stateEntryDistance; }
    static uint32_t getChaosDuration() {
        if (motorState != STATE_CHAOS) return 0;
        return millis() - stateEnterTime;
    }

    static int32_t getTestChaosTriggerCount() { return testChaosTriggerCount; }
    static uint32_t getTestChaosTotalDuration() { return testChaosTotalDuration; }
    static uint32_t getTestChaosMaxDuration() { return testChaosMaxDuration; }
    static uint32_t getTestChaosFirstTriggerTime() { return testChaosFirstTriggerTime; }
    static uint32_t getTestChaosLastTriggerTime() { return testChaosLastTriggerTime; }
    static int32_t getBaselineDistanceTicks() { return baselineDistanceTicks; }
    static int32_t getChaosDistanceTicks() { return chaosDistanceTicks; }
    static int getBaselineFrameCount() { return baselineFrameCount; }
    static int getBaselineIdleFrames() { return baselineIdleFrames; }    // [审计D1修复]
    static int getBaselineWalkingFrames() { return baselineWalkingFrames; } // [审计D1修复]
    static int getChaosFrameCount() { return chaosFrameCount; }
    static float getBaselineAvgSpeedL() {
        return baselineFrameCount > 0 ? baselineSpeedLSum / baselineFrameCount : 0.0f;
    }
    static float getBaselineAvgSpeedR() {
        return baselineFrameCount > 0 ? baselineSpeedRSum / baselineFrameCount : 0.0f;
    }
    static float getChaosAvgSpeedL() {
        return chaosFrameCount > 0 ? chaosSpeedLSum / chaosFrameCount : 0.0f;
    }
    static float getChaosAvgSpeedR() {
        return chaosFrameCount > 0 ? chaosSpeedRSum / chaosFrameCount : 0.0f;
    }
    static bool getChaosHappened() { return chaosHappened; }

    static int  getChaosSnapshotCount() { return chaosSnapshotCount; }
    static const ChaosSnapshotEntry* getChaosSnapshots() { return chaosSnapshots; }
    static int  getChaosInterruptedCount() { return chaosInterruptedCount; }  // [审计D3修复]
    static uint8_t getChaosExitReason() { return lastChaosExitReason; }       // [审计D4修复]

    static void resetChaosStatistics() {
        testChaosTriggerCount = 0;
        testChaosTotalDuration = 0;
        testChaosMaxDuration = 0;
        testChaosFirstTriggerTime = 0;
        testChaosLastTriggerTime = 0;
        baselineDistanceTicks = 0;
        chaosDistanceTicks = 0;
        baselineFrameCount = 0;
        baselineIdleFrames = 0;         // [审计D1修复] 初始化IDLE帧计数
        baselineWalkingFrames = 0;      // [审计D1修复] 初始化WALKING帧计数
        chaosFrameCount = 0;
        baselineSpeedLSum = 0;
        baselineSpeedRSum = 0;
        chaosSpeedLSum = 0;
        chaosSpeedRSum = 0;
        chaosHappened = false;
        chaosInterruptedCount = 0;         // [审计D3修复] 初始化中断计数
        lastChaosExitReason = 0;           // [审计D4修复] 初始化退出原因
        chaosStartDistance = 0;
        lastChaosEndDistance = 0;
        accumulatedStuckTime = 0;
        deathFlag = false;
        clearChaosSnapshots();
        stuckClearTimer = 0;
        stuckBounceCount = 0;
        lastStuckClearTime = 0;
        lastStuckEnterTime = 0;
        savedEncoderDiffThreshold = 30;
        noProgressTimer = 0;
        lastTotalTicks = 0;
        escapePhase1Done = false;
        escapeStartTime = 0;
        frameSampleCounter = 0;
        chaosStableFrameCount = 0;
        lastChaosTicks = 0;
    }

    static void resetStuck() {
        if (motorState == STATE_STUCK) clearStuckState();
    }

    static int32_t getLeftTicks() { return leftTicks; }
    static int32_t getRightTicks() { return rightTicks; }
    static int32_t getEncoderDiff() { return abs(leftTicks - rightTicks); }
    static int16_t getLeftSensor() { return leftSensorRaw; }
    static int16_t getRightSensor() { return rightSensorRaw; }
    static int16_t getChaosNoiseValue() { return chaosNoiseAmplifier; }
    static int getChaosTriggerCount() { return testChaosTriggerCount; }

    static void init() {
        currentSpeedL = 0; currentSpeedR = 0;
        distanceTicks = 0;
        leftTicks = 0; rightTicks = 0;
        lastMoveTime = millis();
        testStartTime = 0;
        leftSensorRaw = 0; rightSensorRaw = 0;
        currentRuleIndex = 0; ruleStartTime = 0; ruleActive = false;
        frameLogHead = 0; frameLogCount = 0;
        motorEnabled = false;
        lastLeftA = false; lastRightA = false;
        lastEncoderReadTime = 0;
        actionInProgress = false;
        actionStartTime = 0;
        actionLastTicks = 0;
        actionStallFrames = 0;
        motorState = STATE_IDLE;
        stateEnterTime = 0;
        stateEntryDistance = 0;
        stuckStartTime = 0;
        accumulatedStuckTime = 0;
        deathFlag = false;
        lastDiff = 0;
        historyIndex = 0;
        historyCount = 0;
        memset(diffHistory, 0, sizeof(diffHistory));
        chaosStartTime = 0;
        lastIncrementalSaveFrame = 0;
        chaosStableFrameCount = 0;
        lastChaosTicks = 0;
        testChaosTriggerCount = 0;
        testChaosTotalDuration = 0;
        testChaosMaxDuration = 0;
        testChaosFirstTriggerTime = 0;
        testChaosLastTriggerTime = 0;
        baselineDistanceTicks = 0;
        chaosDistanceTicks = 0;
        baselineFrameCount = 0;
        baselineIdleFrames = 0;         // [审计D1修复] 初始化IDLE帧计数
        baselineWalkingFrames = 0;      // [审计D1修复] 初始化WALKING帧计数
        chaosFrameCount = 0;
        baselineSpeedLSum = 0;
        baselineSpeedRSum = 0;
        chaosSpeedLSum = 0;
        chaosSpeedRSum = 0;
        chaosHappened = false;
        chaosInterruptedCount = 0;         // [审计D3修复] 初始化中断计数
        lastChaosExitReason = 0;           // [审计D4修复] 初始化退出原因
        chaosStartDistance = 0;
        chaosSnapshotCount = 0;
        memset(chaosSnapshots, 0, sizeof(chaosSnapshots));
        lastChaosMotorL = 0;
        lastChaosMotorR = 0;
        chaosSnapshotStableFrames = 0;
        stuckClearTimer = 0;
        stuckBounceCount = 0;
        lastStuckClearTime = 0;
        lastStuckEnterTime = 0;
        savedEncoderDiffThreshold = 30;
        noProgressTimer = 0;
        lastTotalTicks = 0;
        escapePhase1Done = false;
        escapeStartTime = 0;
        frameSampleCounter = 0;
        encoderDiffThreshold = 30;
        encoderDiffMin = 5;
        wheelSpinThreshold = 20;
        wheelStopThreshold = 3;
        stuckWindowSize = 10;
        chaosNoiseAmplifier = 180;
        chaosMinPwm = 20;
        chaosTimeoutMs = 3000;
        chaosForceTimeoutMs = 5000;
        obstacleThreshold = 1500;
        clearThreshold = 600;
        pinMode(PIN_LEFT_DIR2, OUTPUT);
        pinMode(PIN_RIGHT_DIR2, OUTPUT);
        digitalWrite(PIN_LEFT_DIR2, LOW);
        digitalWrite(PIN_RIGHT_DIR2, LOW);
        pinMode(PIN_LED, OUTPUT);
        ledcSetup(LEDC_CHANNEL_LEFT, LEDC_FREQ, LEDC_RESOLUTION);
        ledcSetup(LEDC_CHANNEL_RIGHT, LEDC_FREQ, LEDC_RESOLUTION);
        ledcAttachPin(PIN_LEFT_PWM, LEDC_CHANNEL_LEFT);
        ledcAttachPin(PIN_RIGHT_PWM, LEDC_CHANNEL_RIGHT);
        ledcWrite(LEDC_CHANNEL_LEFT, 0);
        ledcWrite(LEDC_CHANNEL_RIGHT, 0);
        stopMotors();
        Logger::log("Motor Controller initialized");
    }

    static void stopMotors() {
        setMotorSpeed(0, 0);
        digitalWrite(PIN_LEFT_DIR2, LOW);
        digitalWrite(PIN_RIGHT_DIR2, LOW);
        actionInProgress = false;
        ruleActive = false;
        clearStuckState();
    }

    static void enableMotor() { motorEnabled = true; stopMotors(); Logger::log("Motor enabled"); }
    static void disableMotor() { motorEnabled = false; stopMotors(); Logger::log("Motor disabled"); }
    static bool isMotorEnabled() { return motorEnabled; }

    static void startPhysicsAction(int leftPWM, int rightPWM, uint16_t minDurationMs) {
        if (!motorEnabled) { Logger::log("Motor locked"); return; }
        if (actionInProgress) stopMotors();
        actionInProgress = true;
        actionStartTime = millis();
        actionDurationMs = minDurationMs;
        actionLastTicks = distanceTicks;
        actionStallFrames = 0;
        if (motorState == STATE_IDLE || motorState == STATE_WALKING) {
            setMotorState(STATE_WALKING);
        }
        setMotorSpeed(leftPWM, rightPWM);
    }

    static void updatePhysicsAction() {
        if (!actionInProgress) return;
        uint32_t elapsed = millis() - actionStartTime;
        if (elapsed >= actionDurationMs) {
            actionInProgress = false;
            stopMotors();
            setMotorState(STATE_IDLE);
        }
    }

    static void forceStopAction() {
        if (actionInProgress) {
            actionInProgress = false;
            stopMotors();
            setMotorState(STATE_IDLE);
        }
    }
    static bool isActionInProgress() { return actionInProgress; }

    static void startChaos() {
        if (!motorEnabled) {
            Serial.println("[CHAOS] ❌ Motor locked, cannot start chaos");
            return;
        }
        if (motorState == STATE_CHAOS) {
            Serial.println("[CHAOS] ⏸ Already in chaos mode");
            return;
        }

                // ============================================================
        // ★ 新增：预收敛，让高通 dc 先对齐当前偏置
        // ============================================================
        for (int i = 0; i < 100; i++) {
            updateNoiseDc();
            delay(1);
        }
        // ============================================================

        forceStopAction();
        stopMotors();
        setMotorState(STATE_CHAOS);
        chaosStartTime = millis();
        chaosStartMicros = micros();
        testChaosTriggerCount++;
        baselineDistanceTicks += distanceTicks - lastChaosEndDistance;
        chaosStartDistance = distanceTicks;
        if (testChaosTriggerCount == 1) {
            testChaosFirstTriggerTime = millis() - testStartTime;
        }
        chaosHappened = true;
        chaosStableFrameCount = 0;
        lastChaosTicks = abs(leftTicks) + abs(rightTicks);
        Serial.println("========================================");
        Serial.println("!!! 🔥 CHAOS MODE TRIGGERED !!!");
        Serial.printf("  Trigger count: %d\n", testChaosTriggerCount);
        Serial.printf("  Encoder diff: %d\n", getEncoderDiff());
        Serial.printf("  Noise pin (GPIO%d): %d\n", PIN_NOISE_SOURCE, SensorCalibration::readNoise());
        Serial.println("  🔦 GPIO6 物理热噪声直接驱动");
        Serial.printf("  退出条件: 连续 %d 帧稳定 或 超时 %dms\n",
                      CHAOS_RECOVER_STABLE_FRAMES, chaosForceTimeoutMs);
        Serial.println("========================================");
        Logger::log("========================================");
        Logger::log("!!! CHAOS MODE TRIGGERED !!!");
        Logger::logf("  Trigger count: %d", testChaosTriggerCount);
        Logger::log("  🔦 GPIO6 物理热噪声直接驱动");
        Logger::log("========================================");
        escapePhase1Done = false;
        escapeStartTime = 0;
    }

    static void updateChaos() {
        if (motorState != STATE_CHAOS) return;
        updateNoiseDc();                         // ← 先更新 dc，只调一次

            float noiseL = readPhysicalNoise();      // ← 用同一个 dc
            float noiseR = readPhysicalNoise();      // ← 用同一个 dc
        float pwmL = noiseL * 255.0f;
        float pwmR = noiseR * 255.0f;
        pwmL = constrain(pwmL, -255.0f, 255.0f);
        pwmR = constrain(pwmR, -255.0f, 255.0f);
        setMotorSpeed((int)pwmL, (int)pwmR);
        leftSensorRaw  = SensorCalibration::readLeft();
        rightSensorRaw = SensorCalibration::readRight();
        static int lastSnapshotTime = 0;
        if (millis() - lastSnapshotTime > 100) {
            lastSnapshotTime = millis();
            int16_t snapL = (int16_t)pwmL;
            int16_t snapR = (int16_t)pwmR;

        static int dbgCount = 0;
        if (dbgCount < 30) {
            int raw = analogRead(PIN_NOISE_SOURCE);
            Serial.printf("[CHAOS-DBG] pin=%d raw=%d noiseL=%.3f noiseR=%.3f pwmL=%.1f pwmR=%.1f\n",
                          PIN_NOISE_SOURCE, raw, noiseL, noiseR, pwmL, pwmR);
            dbgCount++;
        }

            // [v10.5] 用真实传感器值代替硬编码 0
            logChaosSnapshot(leftSensorRaw, rightSensorRaw, snapL, snapR);
        }
        int32_t currentTicks = abs(leftTicks) + abs(rightTicks);
        int32_t delta = currentTicks - lastChaosTicks;
        lastChaosTicks = currentTicks;
        if (delta > wheelStopThreshold) {
            chaosStableFrameCount++;
        } else {
            chaosStableFrameCount = 0;
        }
        if (chaosStableFrameCount >= CHAOS_RECOVER_STABLE_FRAMES) {
            Serial.printf("[CHAOS] ✅ 脱困判定: 连续 %d 帧稳定变化 (delta=%ld)\n",
                          CHAOS_RECOVER_STABLE_FRAMES, delta);
            endChaos();
            return;
        }
        uint32_t chaosDurationUs = micros() - chaosStartMicros;
        uint32_t chaosDuration = chaosDurationUs / 1000;
        if (chaosDuration == 0 && chaosDurationUs > 0) chaosDuration = 1;
        if (chaosDuration >= chaosForceTimeoutMs) {
            Serial.printf("[CHAOS] ⏰ 超时退出: %lums >= %dms\n",
                          chaosDuration, chaosForceTimeoutMs);
            lastChaosExitReason = 1;  // [审计D4修复] 标记超时退出
            endChaos();
            return;
        }
        logFrame();
    }

    static bool endChaos() {
        if (motorState != STATE_CHAOS) return false;
        // [审计D4修复] 记录退出原因: 0=编码器稳定脱困 1=超时退出
        // 注意: 此处无法区分超时还是编码器稳定, 需要在调用点设置
        lastChaosExitReason = 0;  // 默认为编码器稳定(正常退出)
        setMotorState(STATE_IDLE);
        stopMotors();
        escapePhase1Done = false;
        escapeStartTime = 0;
        chaosStableFrameCount = 0;
        lastChaosTicks = 0;
        uint32_t chaosDurationUs = micros() - chaosStartMicros;
        uint32_t chaosDuration = chaosDurationUs / 1000;
        if (chaosDuration == 0 && chaosDurationUs > 0) chaosDuration = 1;
        testChaosTotalDuration += chaosDuration;
        if (chaosDuration > testChaosMaxDuration) testChaosMaxDuration = chaosDuration;
        testChaosLastTriggerTime = millis() - testStartTime;
        int32_t currentDist = distanceTicks;
        chaosDistanceTicks += currentDist - chaosStartDistance;
        lastChaosEndDistance = distanceTicks;
        Serial.printf("[CHAOS] ✅ Ended. Duration: %lums, total triggers: %d\n",
                      chaosDuration, testChaosTriggerCount);
        Logger::log("========================================");
        Logger::log("!!! CHAOS MODE ENDED !!!");
        Logger::logf("  Duration: %lums", chaosDuration);
        Logger::logf("  Total triggers: %d", testChaosTriggerCount);
        Logger::log("  Code resumed control");
        Logger::log("========================================");
        stuckStartTime = 0;
        return true;
    }

    static void startFrameLog() {
        frameLogHead = 0;
        frameLogCount = 0;
        testStartTime = millis();
        lastIncrementalSaveFrame = 0;
        frameSampleCounter = 0;
        setMotorState(STATE_IDLE);
    }

    static void logFrame() {
        if (!shouldSaveFrame()) {
            if (motorState == STATE_CHAOS) {
                chaosFrameCount++;
                chaosSpeedLSum += currentSpeedL;
                chaosSpeedRSum += currentSpeedR;
            } else {
                baselineFrameCount++;
                // [审计D1修复] 区分IDLE和WALKING状态帧数
                if (motorState == STATE_IDLE) {
                    baselineIdleFrames++;
                } else {
                    baselineWalkingFrames++;
                }
                baselineSpeedLSum += currentSpeedL;
                baselineSpeedRSum += currentSpeedR;
            }
            return;
        }
        if (frameLogCount < FRAME_LOG_SIZE) {
            FrameLogEntry& entry = frameLog[frameLogHead];
            entry.timestamp_ms = millis() - testStartTime;
            entry.sensorLeft = leftSensorRaw;
            entry.sensorRight = rightSensorRaw;
            entry.motorLeftPWM = currentSpeedL;
            entry.motorRightPWM = currentSpeedR;
            entry.directionL = (digitalRead(PIN_LEFT_DIR2) == LEFT_FORWARD) ? 1 : 0;
            entry.directionR = (digitalRead(PIN_RIGHT_DIR2) == RIGHT_FORWARD) ? 1 : 0;
            entry.state = (uint8_t)motorState;
            entry.chaosActive = (motorState == STATE_CHAOS) ? 1 : 0;
            entry.isChaosFrame = (motorState == STATE_CHAOS) ? 1 : 0;
            RAMLogBuffer::addFrame(entry);
            if (motorState == STATE_CHAOS) {
                chaosFrameCount++;
                chaosSpeedLSum += currentSpeedL;
                chaosSpeedRSum += currentSpeedR;
            } else {
                baselineFrameCount++;
                // [审计D1修复] 区分IDLE和WALKING状态帧数
                if (motorState == STATE_IDLE) {
                    baselineIdleFrames++;
                } else {
                    baselineWalkingFrames++;
                }
                baselineSpeedLSum += currentSpeedL;
                baselineSpeedRSum += currentSpeedR;
            }
            frameLogHead = (frameLogHead + 1) % FRAME_LOG_SIZE;
            frameLogCount++;
            if (frameLogCount % INCREMENTAL_SAVE_INTERVAL == 0) {
                RollingStorage::incrementalSave(
                    RAMLogBuffer::getGeneration(),
                    RAMLogBuffer::getIndividual(),
                    frameLog, frameLogCount, frameLogHead
                );
            }
        }
    }

    static void update(const Gene& gene) {
        if (!motorEnabled) { stopMotors(); return; }
        syncGeneParams(gene);
        updateEncoders();
        leftSensorRaw  = SensorCalibration::readLeft();
        rightSensorRaw = SensorCalibration::readRight();
        int32_t diff = abs(leftTicks - rightTicks);
        bool diffIncreasing = (diff > lastDiff + encoderDiffMin);
        bool leftSpinning = (currentSpeedL > wheelSpinThreshold);
        bool rightSpinning = (currentSpeedR > wheelSpinThreshold);
        bool leftStopped = (currentSpeedL < wheelStopThreshold);
        bool rightStopped = (currentSpeedR < wheelStopThreshold);
        bool anySpinning = leftSpinning || rightSpinning;
        bool windowStuck = detectStuckWithWindow(diff);
        bool diffStuck = (diff > encoderDiffThreshold && diffIncreasing &&
                          leftSpinning && rightSpinning);
        bool oneWheelStuck = ((leftSpinning && rightStopped) ||
                              (rightSpinning && leftStopped));
        bool largeDiffStuck = (diff > encoderDiffThreshold * 2 &&
                               anySpinning);
        float speedRatio = (currentSpeedL > currentSpeedR)
            ? (float)currentSpeedL / max(currentSpeedR, 1)
            : (float)currentSpeedR / max(currentSpeedL, 1);
        bool speedAsymmetryStuck = (speedRatio > SPEED_ASYMMETRY_RATIO && anySpinning);
        bool isStuckCondition = (windowStuck || diffStuck || oneWheelStuck ||
                                 largeDiffStuck || speedAsymmetryStuck);
        static uint32_t lastDebugLog = 0;
        if (millis() - lastDebugLog > 1000) {
            lastDebugLog = millis();
            const char* stateNames[] = {"IDLE", "WALKING", "STUCK", "CHAOS"};
            Serial.printf("[DEBUG] diff=%ld, inc=%d, L=%d R=%d, ratio=%.2f, stuck=%d, state=%s\n",
                          diff, diffIncreasing, currentSpeedL, currentSpeedR,
                          speedRatio, isStuckCondition, stateNames[motorState]);
        }
        if (isStuckCondition && motorState != STATE_CHAOS) {
            if (motorState != STATE_STUCK) {
                if (millis() - lastStuckClearTime < BOUNCE_WINDOW_MS) {
                    stuckBounceCount++;
                    if (stuckBounceCount > BOUNCE_THRESHOLD) {
                        Logger::logf("[STUCK] ⚠️ High bounce: %d in %d ms (threshold unchanged at %d)",
                                     stuckBounceCount, BOUNCE_WINDOW_MS, encoderDiffThreshold);
                        stuckBounceCount = 0;
                    }
                } else {
                    stuckBounceCount = 0;
                }
                lastStuckEnterTime = millis();
                setMotorState(STATE_STUCK);
                stuckStartTime = millis();
                Serial.printf("[STUCK] ✅ Detected! diff=%ld, L=%d, R=%d, ratio=%.2f\n",
                              diff, currentSpeedL, currentSpeedR, speedRatio);
            }
        }
        lastDiff = diff;
        if (motorState == STATE_STUCK) {
            uint32_t stuckDuration = millis() - stuckStartTime;
            uint32_t totalStuckTime = accumulatedStuckTime + stuckDuration;
            if (stuckDuration % 500 < 20) {
                Serial.printf("[CHAOS] ⏳ Stuck: %lums (accumulated: %lums) / %d ms\n",
                              stuckDuration, totalStuckTime, chaosTimeoutMs);
            }
            if (totalStuckTime >= chaosTimeoutMs) {
                Serial.printf("[CHAOS] 🚀 TRIGGERING! totalStuckTime=%lu (accumulated=%lu)\n",
                              totalStuckTime, accumulatedStuckTime);
                startChaos();
                logFrame();
                return;
            }
            bool shouldClear = (diff < encoderDiffThreshold && !diffIncreasing);
            if (shouldClear) {
                if (stuckClearTimer == 0) {
                    stuckClearTimer = millis();
                } else if (millis() - stuckClearTimer > STUCK_CLEAR_DEBOUNCE_MS) {
                    clearStuckState();
                    lastStuckClearTime = millis();
                    stuckClearTimer = 0;
                    Serial.printf("[STUCK] Cleared after %lu ms stable\n",
                                  millis() - lastStuckClearTime);
                }
            } else {
                stuckClearTimer = 0;
            }
        }
        if (motorState == STATE_CHAOS) {
            leftSensorRaw = SensorCalibration::getLeftRaw();
            rightSensorRaw = SensorCalibration::getRightRaw();
            logFrame();
            return;
        }
        if (actionInProgress) {
            leftSensorRaw = SensorCalibration::getLeftRaw();
            rightSensorRaw = SensorCalibration::getRightRaw();
            logFrame();
            return;
        }
        int32_t totalTicks = abs(leftTicks) + abs(rightTicks);
        if (motorState != STATE_CHAOS && motorState != STATE_STUCK) {
            if (abs(totalTicks - lastTotalTicks) < 5) {
                if (noProgressTimer == 0) noProgressTimer = millis();
                else if (millis() - noProgressTimer > NO_PROGRESS_TIMEOUT_MS) {
                    Serial.printf("[STUCK] Force: no progress (%ld ticks total)\n", totalTicks);
                    setMotorState(STATE_STUCK);
                    stuckStartTime = millis();
                    noProgressTimer = 0;
                }
            } else {
                noProgressTimer = 0;
            }
        }
        lastTotalTicks = totalTicks;
        float noise = (float)(SensorCalibration::readNoise() % 1000) / 1000.0f;
        uint32_t elapsed = millis() - testStartTime;
        int32_t dist = distanceTicks;
        if (!ruleActive) {
            for (int i = 0; i < gene.ruleCount; i++) {
                if (gene.evaluateCondition(i, leftSensorRaw, rightSensorRaw, dist, elapsed, motorState)) {
                    currentRuleIndex = i;
                    ruleStartTime = millis();
                    ruleActive = true;
                    const BehaviorRule& r = gene.rules[i];
                    float noiseMod = 0.7f + noise * 0.6f;
                    int modMotorL = constrain((int)(r.motorL * noiseMod), -255, 255);
                    int modMotorR = constrain((int)(r.motorR * noiseMod), -255, 255);
                    uint16_t modDuration = constrain((uint16_t)(r.durationMs * noiseMod),
                                                     MIN_RULE_DURATION, MAX_RULE_DURATION);
                    startPhysicsAction(modMotorL, modMotorR, modDuration);
                    logFrame();
                    return;
                }
            }
            if (!actionInProgress) {
                stopMotors();
                if (motorState == STATE_IDLE || motorState == STATE_WALKING) {
                    setMotorState(STATE_IDLE);
                }
            }
        } else {
            if (!actionInProgress) {
                const BehaviorRule& r = gene.rules[currentRuleIndex];
                if (r.nextRule > 0 && r.nextRule <= gene.ruleCount) {
                    currentRuleIndex = r.nextRule - 1;
                    ruleStartTime = millis();
                } else {
                    ruleActive = false;
                }
            }
        }
        logFrame();
    }

    static int32_t getDistanceTicks() { return distanceTicks; }
    static void resetDistanceTicks() { distanceTicks = 0; leftTicks = 0; rightTicks = 0; }
    static const FrameLogEntry* getFrameLog() { return frameLog; }
    static int getFrameLogCount() { return (frameLogCount < FRAME_LOG_SIZE) ? frameLogCount : FRAME_LOG_SIZE; }
    static int getFrameLogHead() { return frameLogHead; }
    static int getCurrentSpeedL() { return currentSpeedL; }
    static int getCurrentSpeedR() { return currentSpeedR; }

    static void saveChaosSnapshotsToSPIFFS(uint32_t gen, int ind) {
        if (!RobustStorage::isReady() || chaosSnapshotCount == 0) return;
        size_t payloadSize = 1 + chaosSnapshotCount * sizeof(ChaosSnapshotEntry);
        size_t totalSize = sizeof(ChaosSnapshotHeader) + payloadSize;
        if (!RollingStorage::ensureSpace(totalSize + 256)) {
            Logger::logf("❌ saveChaosSnapshots: insufficient space (need %d bytes)", totalSize);
            return;
        }
        String path = "/chaos_snaps_g" + String(gen) + "_i" + String(ind) + ".bin";
        uint8_t* buf = (uint8_t*)malloc(totalSize);
        if (buf) {
            ChaosSnapshotHeader header;
            header.magic = 0x4348534E;
            header.version = 0x0002;
            header.headerSize = sizeof(ChaosSnapshotHeader);
            header.count = (uint8_t)chaosSnapshotCount;
            header.reserved[0] = header.reserved[1] = header.reserved[2] = 0;
            uint8_t* payload = buf + sizeof(ChaosSnapshotHeader);
            payload[0] = chaosSnapshotCount;
            memcpy(payload + 1, chaosSnapshots, chaosSnapshotCount * sizeof(ChaosSnapshotEntry));
            header.crc32 = CRC32::calculate(payload, payloadSize);
            memcpy(buf, &header, sizeof(ChaosSnapshotHeader));
            bool success = FileUtils::atomicWrite(path, buf, totalSize);
            free(buf);
            if (success) {
                Logger::logf("💾 Chaos snapshots backed up: %s (%d snapshots, CRC=0x%08X) ✅",
                             path.c_str(), chaosSnapshotCount, header.crc32);
            } else {
                Logger::logf("❌ Chaos snapshots backup failed: %s", path.c_str());
            }
        }
    }
};
// ===================== 第 5 段开始 =====================

// ================================================================
// MotorController 静态变量定义
// ================================================================
int MotorController::currentSpeedL = 0;
int MotorController::currentSpeedR = 0;

float MotorController::noiseDcEstimate = 2048.0f;
bool  MotorController::noiseDcInit = false;



volatile int32_t MotorController::distanceTicks = 0;
volatile int32_t MotorController::leftTicks = 0;
volatile int32_t MotorController::rightTicks = 0;
bool MotorController::motorEnabled = false;
uint32_t MotorController::lastMoveTime = 0;
uint32_t MotorController::testStartTime = 0;
int MotorController::leftSensorRaw = 0;
int MotorController::rightSensorRaw = 0;
int MotorController::currentRuleIndex = 0;
uint32_t MotorController::ruleStartTime = 0;
bool MotorController::ruleActive = false;
FrameLogEntry MotorController::frameLog[FRAME_LOG_SIZE];
int MotorController::frameLogHead = 0;
int MotorController::frameLogCount = 0;
bool MotorController::lastLeftA = false;
bool MotorController::lastRightA = false;
uint32_t MotorController::lastEncoderReadTime = 0;
bool MotorController::actionInProgress = false;
uint32_t MotorController::actionStartTime = 0;
uint16_t MotorController::actionDurationMs = 0;
int32_t MotorController::actionLastTicks = 0;
int MotorController::actionStallFrames = 0;
MotorState MotorController::motorState = STATE_IDLE;
uint32_t MotorController::stateEnterTime = 0;
int32_t MotorController::stateEntryDistance = 0;
uint32_t MotorController::stuckStartTime = 0;
uint32_t MotorController::accumulatedStuckTime = 0;
int32_t MotorController::lastDiff = 0;
bool MotorController::deathFlag = false;
int16_t MotorController::encoderDiffThreshold = 30;
int16_t MotorController::encoderDiffMin = 5;
int16_t MotorController::wheelSpinThreshold = 20;
int16_t MotorController::wheelStopThreshold = 3;
uint8_t MotorController::stuckWindowSize = 10;
int16_t MotorController::chaosNoiseAmplifier = 180;
int16_t MotorController::chaosMinPwm = 20;
uint16_t MotorController::chaosTimeoutMs = 3000;
uint16_t MotorController::chaosForceTimeoutMs = 5000;
int16_t MotorController::obstacleThreshold = 1500;
int16_t MotorController::clearThreshold = 600;
int32_t MotorController::diffHistory[MAX_STUCK_WINDOW] = {0};
int MotorController::historyIndex = 0;
int MotorController::historyCount = 0;
uint32_t MotorController::chaosStartTime = 0;
uint32_t MotorController::chaosStartMicros = 0;
int MotorController::chaosStableFrameCount = 0;
int32_t MotorController::lastChaosTicks = 0;
uint32_t MotorController::lastIncrementalSaveFrame = 0;
int32_t MotorController::testChaosTriggerCount = 0;
uint32_t MotorController::testChaosTotalDuration = 0;
uint32_t MotorController::testChaosMaxDuration = 0;
uint32_t MotorController::testChaosFirstTriggerTime = 0;
uint32_t MotorController::testChaosLastTriggerTime = 0;
int32_t MotorController::baselineDistanceTicks = 0;
int32_t MotorController::chaosDistanceTicks = 0;
int MotorController::baselineFrameCount = 0;
int MotorController::baselineIdleFrames = 0;        // [审计D1修复]
int MotorController::baselineWalkingFrames = 0;     // [审计D1修复]
int MotorController::chaosFrameCount = 0;
float MotorController::baselineSpeedLSum = 0;
float MotorController::baselineSpeedRSum = 0;
float MotorController::chaosSpeedLSum = 0;
float MotorController::chaosSpeedRSum = 0;
bool MotorController::chaosHappened = false;
int MotorController::chaosInterruptedCount = 0;     // [审计D3修复]
uint8_t MotorController::lastChaosExitReason = 0;   // [审计D4修复]
int32_t MotorController::chaosStartDistance = 0;
int32_t MotorController::lastChaosEndDistance = 0;
ChaosSnapshotEntry MotorController::chaosSnapshots[MAX_CHAOS_SNAPSHOTS];
int MotorController::chaosSnapshotCount = 0;
int8_t MotorController::lastChaosMotorL = 0;
int8_t MotorController::lastChaosMotorR = 0;
int MotorController::chaosSnapshotStableFrames = 0;
uint32_t MotorController::stuckClearTimer = 0;
uint8_t MotorController::stuckBounceCount = 0;
uint32_t MotorController::lastStuckClearTime = 0;
uint32_t MotorController::lastStuckEnterTime = 0;
int16_t MotorController::savedEncoderDiffThreshold = 30;
uint32_t MotorController::noProgressTimer = 0;
int32_t MotorController::lastTotalTicks = 0;
bool MotorController::escapePhase1Done = false;
uint32_t MotorController::escapeStartTime = 0;
int MotorController::frameSampleCounter = 0;

// ================================================================
// EvolutionEngine 类
//   [v10.5] initImpl() 的 hasAnyPopulation 检查 +
//           resetToGeneration1() 的删除块 经 normPath() 规范化
// ================================================================
class EvolutionEngine {
private:
    static EvolutionEngine instance;
    Gene population[POPULATION_SIZE];
    NoveltyArchive archive;
    uint32_t currentGeneration;
    int currentIndividual;
    uint32_t testStartTime;
    bool testActive;
    bool pendingTransition;
    bool controlMode;
    float mutationRate;
    float bestNoveltyEver;

    static uint32_t lastForceSaveTime;
    static const uint32_t FORCE_SAVE_INTERVAL = 30000;
    static bool experimentReady;

    void initImpl() {
        experimentReady = false;
        currentGeneration = GeneStorage::getCurrentGeneration();

        Logger::log("===== EvolutionEngine::initImpl =====");
        Logger::logf("  GeneStorage generation: %lu", currentGeneration);
        Logger::logf("  GeneStorage active: %d", GeneStorage::isExperimentActive());

        if (currentGeneration > 0) {
            String popPath = "/pop_gen_" + String(currentGeneration) + ".bin";
            if (FileUtils::exists(popPath)) {
                if (GeneStorage::loadPopulation(currentGeneration, population, POPULATION_SIZE)) {
                    Logger::logf("✅ Population for gen %lu loaded successfully", currentGeneration);
                } else {
                    Logger::logf("❌ loadPopulation failed for gen %lu", currentGeneration);
                    currentGeneration = 0;
                }
            } else {
                Logger::logf("⚠️ /pop_gen_%lu.bin not found", currentGeneration);
                currentGeneration = 0;
            }
        }

        if (currentGeneration == 0) {
            uint32_t latest = GeneStorage::findLatestGeneration();
            Logger::logf("  findLatestGeneration() = %lu", latest);

            if (latest > 0) {
                currentGeneration = latest;
                GeneStorage::setCurrentGeneration(currentGeneration, false);
                if (GeneStorage::loadPopulation(currentGeneration, population, POPULATION_SIZE)) {
                    Logger::logf("✅ Recovered to latest generation %lu", currentGeneration);
                } else {
                    Logger::logf("❌ Recovery failed: loadPopulation failed for gen %lu", currentGeneration);
                    experimentReady = false;
                    Logger::log("  🔒 Refusing to rebuild gen 1. Manual action required.");
                    Logger::log("=====================================");
                    return;
                }
            } else {
                // ============================================================
                // [v10.5] hasAnyPopulation 检查: File.name() 经 normPath()
                // ============================================================
                bool hasAnyPopulation = false;
                if (RobustStorage::isReady()) {
                    File root = SPIFFS.open("/");
                    if (root) {
                        while (File f = root.openNextFile()) {
                            String name = normPath(f.name());   // [v10.5] 规范化
                            if (name.startsWith("/pop_gen_")) {
                                hasAnyPopulation = true;
                                f.close();
                                break;
                            }
                            f.close();
                        }
                        root.close();
                    }
                }

                if (hasAnyPopulation) {
                    Logger::log("❌ /pop_gen_* exists but none loadable.");
                    Logger::log("  🔒 Refusing to rebuild gen 1. Manual action required.");
                    Logger::log("=====================================");
                    experimentReady = false;
                    return;
                }

                Logger::log("📌 No population found. Creating new experiment at gen 1.");
                currentGeneration = 1;
                GeneStorage::setCurrentGeneration(currentGeneration, false);
                for (int i = 0; i < POPULATION_SIZE; i++) {
                    population[i].init();
                }
                if (!GeneStorage::savePopulationForGeneration(1, population, POPULATION_SIZE, "init")) {
                    Logger::log("❌ CRITICAL: Failed to save initial population!");
                    experimentReady = false;
                    Logger::log("=====================================");
                    return;
                }
                GeneStorage::commitGeneration(1);
                Logger::log("✅ Created and committed new population at gen 1");
            }
        }

        currentIndividual = 0;
        testActive = false;
        pendingTransition = false;
        controlMode = false;
        mutationRate = 0.15f;
        bestNoveltyEver = 0;
        lastForceSaveTime = 0;

        Logger::logf("🚀 Starting evolution from generation %lu", currentGeneration);

        bool archiveLoaded = archive.loadWithBackup();

        if (!archiveLoaded || archive.getArchiveSize() == 0) {
            Logger::log("⚠️ Archive empty. Rebuild DISABLED to prevent crash.");
            Logger::log("📋 Archive will be built incrementally from new behaviors.");
        }

        if (archive.getArchiveSize() == 0 && currentGeneration > 0) {
            Logger::log("📝 Attempting to extract behaviors from current population...");
            int extracted = 0;
            int maxExtract = min(20, POPULATION_SIZE);
            for (int i = 0; i < maxExtract; i++) {
                if (population[i].survival_time > 1000 && population[i].distance_ticks > 10) {
                    if (population[i].behavior.totalDistance > 0) {
                        if (archive.addIfNovel(population[i].behavior)) {
                            extracted++;
                        }
                    }
                }
            }
            if (extracted > 0) {
                Logger::logf("✅ Extracted %d behaviors from current population", extracted);
                archive.save();
                archiveLoaded = true;
            }
        }

        if (archiveLoaded) {
            Logger::logf("✅ Archive ready: %d behaviors", archive.getArchiveSize());
        } else {
            Logger::log("⚠️ Archive still empty. Running with novelty scores = 1.0");
        }

        experimentReady = true;
        Logger::logf("✅ EvolutionEngine ready: gen=%lu, popSize=%d",
                     currentGeneration, POPULATION_SIZE);
        Logger::log("=====================================");
    }

    void startCurrentTestImpl() {
        if (!experimentReady) {
            Logger::log("❌ startCurrentTest refused: experiment not ready");
            return;
        }
        if (currentGeneration == 0) {
            Logger::log("❌ startCurrentTest refused: currentGeneration == 0");
            return;
        }
        String popPath = "/pop_gen_" + String(currentGeneration) + ".bin";
        if (!FileUtils::exists(popPath)) {
            // [v10.11] 增强诊断: 列出所有存在的种群文件帮助定位问题
            Logger::logf("❌ startCurrentTest refused: %s not found", popPath.c_str());
            Logger::log("🔍 Available population files:");
            if (RobustStorage::isReady()) {
                File root = SPIFFS.open("/");
                if (root) {
                    bool foundAny = false;
                    while (File f = root.openNextFile()) {
                        String name = normPath(f.name());
                        if (name.startsWith("/pop_gen_") && name.endsWith(".bin")) {
                            Logger::logf("  📄 %s (%d bytes)", name.c_str(), (int)f.size());
                            foundAny = true;
                        }
                        f.close();
                    }
                    root.close();
                    if (!foundAny) {
                        Logger::log("  ⚠️ No pop_gen_*.bin files found on SPIFFS!");
                    }
                }
            }
            Logger::log("⚠️ Experiment cannot proceed - check storage health via 'storage' command");
            return;
        }
        if (!GeneStorage::loadPopulation(currentGeneration, population, POPULATION_SIZE)) {
            Logger::logf("❌ startCurrentTest refused: loadPopulation failed for gen %lu", currentGeneration);
            // [v10.11] 诊断: 检查文件大小
            size_t fsize = FileUtils::getFileSize(popPath);
            Logger::logf("  File size: %d bytes", (int)fsize);
            if (!RobustStorage::isReady()) {
                experimentReady = false;
                Logger::log("  🔒 SPIFFS not ready, experiment marked as not ready");
            } else {
                String popPath2 = "/pop_gen_" + String(currentGeneration) + ".bin";
                if (!FileUtils::exists(popPath2)) {
                    experimentReady = false;
                    Logger::log("  🔒 Population file missing during load, experiment marked as not ready");
                }
            }
            return;
        }

        testActive = true;
        testStartTime = millis();
        MotorController::resetDistanceTicks();
        MotorController::enableMotor();
        MotorController::startFrameLog();
        MotorController::resetStuck();

        RollingStorage::registerDependency(currentGeneration, currentIndividual);

        RAMLogBuffer::startNewTest(currentGeneration, currentIndividual);
        RollingStorage::resetGenerationCounter(currentGeneration);

        Logger::logf("▶️ Testing individual %d/%d (gen %lu) [rules=%d, chaosTimeout=%dms]",
                     currentIndividual+1, POPULATION_SIZE, currentGeneration,
                     population[currentIndividual].ruleCount,
                     population[currentIndividual].chaosTimeoutMs);
        MotorController::resetChaosStatistics();
    }

    void endCurrentTestImpl() {
        if (!testActive) return;
        testActive = false;

        RollingStorage::unregisterDependency(currentGeneration, currentIndividual);

        MotorController::forceStopAction();
        MotorController::disableMotor();

        Gene& g = population[currentIndividual];
        g.survival_time = millis() - testStartTime;
        g.distance_ticks = MotorController::getDistanceTicks();

        int snapshotCount = MotorController::getChaosSnapshotCount();
        g.chaosRuleCount = 0;
        g.chaosRulesStartIndex = 0;
        g.hasChaosRules = false;

        if (snapshotCount > 0) {
            MotorController::saveChaosSnapshotsToSPIFFS(currentGeneration, currentIndividual);
            const ChaosSnapshotEntry* snapshots = MotorController::getChaosSnapshots();
            int availableSlots = MAX_RULES - g.ruleCount;
            int rulesToInject = min(snapshotCount, availableSlots);
            if (rulesToInject > 0 && availableSlots > 0) {
                g.chaosRulesStartIndex = g.ruleCount;
                g.chaosRuleCount = rulesToInject;
                g.hasChaosRules = true;
                for (int i = 0; i < rulesToInject; i++) {
                    BehaviorRule chaosRule = inferRuleFromSnapshot(snapshots[i], g);
                    g.rules[g.ruleCount + i] = chaosRule;
                }
                g.ruleCount += rulesToInject;
                Logger::logf("🧬✨ CHAOS→GENE: Injected %d rules from chaos (total rules now %d)",
                             rulesToInject, g.ruleCount);
            }
        }

        bool added = false;
        if (MotorController::getFrameLogCount() == 0) {
            g.behavior.init();
            g.baselineBehavior.init();
            g.noveltyScore = 0.0f;
            Logger::logf("⚠️ Individual %d: no frame log data, skipping novelty computation", currentIndividual+1);
        } else {
            g.behavior = archive.extractFromFrameLog(
                MotorController::getFrameLog(), MotorController::getFrameLogCount(), g.distance_ticks,
                MotorController::getFrameLogHead(), false);
            g.baselineBehavior = archive.extractFromFrameLog(
                MotorController::getFrameLog(), MotorController::getFrameLogCount(), g.distance_ticks,
                MotorController::getFrameLogHead(), true);
            g.noveltyScore = archive.computeNovelty(g.behavior);
            added = archive.addIfNovel(g.behavior);
            if (added) {
                if (!archive.save()) {
                    Logger::logf("⚠️ Archive save failed at gen=%lu id=%d (data in RAM)",
                                 currentGeneration, currentIndividual);
                }
            }
        }

        HistoryRecord record;
        record.timestamp = millis() - testStartTime;
        record.generation = currentGeneration;
        record.noveltyScore = g.noveltyScore;
        record.survivalTime = g.survival_time;
        record.distance_ticks = g.distance_ticks;
        record.ruleCount = g.ruleCount;
        // [审计D3修复] 如果混沌发生过但未正常退出(被碰撞终止), 增加中断计数
        if (MotorController::getChaosHappened() && MotorController::isDead()) {
            MotorController::incrementChaosInterruptedCount();
        }
        RobustStorage::addRecord(record, true);
        RobustStorage::forceSave();

        Logger::logf("⏹️ Individual %d done: novelty=%.4f rules=%d chaosCount=%d %s",
                     currentIndividual+1, g.noveltyScore, g.ruleCount,
                     MotorController::getTestChaosTriggerCount(),
                     added ? "✨ [new behavior]" : "");

        GeneStorage::saveIndividualRecord(currentGeneration, currentIndividual, g);

        bool hadChaos = (MotorController::getTestChaosTriggerCount() > 0);
        if (hadChaos) {
            RollingStorage::saveFrameLog(currentGeneration, currentIndividual,
                                         MotorController::getFrameLog(),
                                         MotorController::getFrameLogCount(),
                                         MotorController::getFrameLogHead());
            Logger::logf("💾 Frame log saved (chaos detected, count=%d)",
                         MotorController::getTestChaosTriggerCount());
        } else {
            Logger::logf("⏭️ Frame log skipped (no chaos)");
        }

        if (g.noveltyScore > bestNoveltyEver) {
            bestNoveltyEver = g.noveltyScore;
            Logger::logf("🏆 New behavior record! novelty=%.4f", g.noveltyScore);
        }

        ChaoticTestRecord chaosRecord;
        chaosRecord.timestamp = millis() - testStartTime;
        chaosRecord.generation = currentGeneration;
        chaosRecord.individual = currentIndividual;
        chaosRecord.chaosTriggerCount = MotorController::getTestChaosTriggerCount();
        chaosRecord.chaosTotalDuration = MotorController::getTestChaosTotalDuration();
        chaosRecord.chaosMaxDuration = MotorController::getTestChaosMaxDuration();
        chaosRecord.chaosFirstTime = MotorController::getTestChaosFirstTriggerTime();
        chaosRecord.chaosLastTime = MotorController::getTestChaosLastTriggerTime();
        chaosRecord.baselineDistance = MotorController::getBaselineDistanceTicks();
        chaosRecord.chaosDistance = MotorController::getChaosDistanceTicks();
        chaosRecord.baselineFrames = MotorController::getBaselineFrameCount();
        chaosRecord.chaosFrames = MotorController::getChaosFrameCount();
        chaosRecord.baselineAvgSpeedL = MotorController::getBaselineAvgSpeedL();
        chaosRecord.baselineAvgSpeedR = MotorController::getBaselineAvgSpeedR();
        chaosRecord.chaosAvgSpeedL = MotorController::getChaosAvgSpeedL();
        chaosRecord.chaosAvgSpeedR = MotorController::getChaosAvgSpeedR();
        chaosRecord.chaosSuccess = MotorController::getChaosHappened() ? 1 : 0;
        chaosRecord.chaosExitReason = MotorController::getChaosExitReason();          // [审计D4修复]
        chaosRecord.chaosInterruptedCount = MotorController::getChaosInterruptedCount(); // [审计D3修复]
        chaosRecord.testTerminatedBy = MotorController::isDead() ? 1 : 0;
        chaosRecord.reserved[0] = 0;
        chaosRecord.reserved[1] = 0;

        RobustStorage::addChaosRecord(chaosRecord, true);
    }

        void nextIndividualImpl() {
        endCurrentTestImpl();
        currentIndividual++;

        if (currentIndividual >= POPULATION_SIZE) {
            uint32_t prevGen = GeneStorage::getCurrentGeneration();
            Logger::logf("💾 gen %lu → %lu: evolving...", prevGen, prevGen + 1);

            evolveImpl();
            currentIndividual = 0;

            uint32_t newGen = prevGen + 1;
            GeneStorage::setCurrentGeneration(newGen, false);
            RollingStorage::setCurrentGeneration(newGen);

            bool popSaved = GeneStorage::savePopulationForGeneration(newGen, population, POPULATION_SIZE, "post-evolve");

            if (popSaved) {
                GeneStorage::commitGeneration(newGen);
                currentGeneration = newGen;
                Logger::logf("✅ Generation %lu committed successfully", newGen);
            } else {
                Logger::logf("❌ savePopulationForGeneration failed for gen %lu, rolling back to %lu", newGen, prevGen);
                GeneStorage::rollbackPendingGeneration();
                RollingStorage::setCurrentGeneration(prevGen);
                GeneStorage::setCurrentGeneration(prevGen, true);
                currentGeneration = prevGen;
                Logger::logf("↩️ Rolled back to generation %lu", prevGen);

                static int saveFailCount = 0;
                saveFailCount++;
                if (saveFailCount > 3) {
                    Logger::log("🚨 3 consecutive save failures, manual cleanup required");
                    saveFailCount = 0;
                }
            }

            Logger::logf("📊 Entering generation %lu (archive: %d behaviors)",
                         currentGeneration, archive.getArchiveSize());
        }

        startCurrentTestImpl();
    }

    static BehaviorRule inferRuleFromSnapshot(const ChaosSnapshotEntry& snap, const Gene& gene) {
        BehaviorRule rule;
        int16_t obsThresh = gene.obstacleThreshold;
        int16_t clrThresh = gene.clearThreshold;
        bool leftHigh  = (snap.sensorLeft  > obsThresh);
        bool rightHigh = (snap.sensorRight > obsThresh);
        bool leftLow   = (snap.sensorLeft  < clrThresh);
        bool rightLow  = (snap.sensorRight < clrThresh);

        if (leftHigh && rightHigh) {
            rule.condType  = COND_SENSOR_BOTH;
            rule.condValue = max(snap.sensorLeft, snap.sensorRight);
        } else if (leftHigh || rightHigh) {
            rule.condType  = COND_SENSOR_ANY;
            rule.condValue = leftHigh ? snap.sensorLeft : snap.sensorRight;
        } else if (leftLow && rightLow) {
            rule.condType  = COND_IDLE;
            rule.condValue = 0;
        } else {
            rule.condType  = COND_ALWAYS;
            rule.condValue = 0;
        }
        rule.condOp = OP_GREATER;
        rule.motorL = snap.motorLeftPWM;
        rule.motorR = snap.motorRightPWM;
        rule.durationMs = max(snap.durationMs, (uint16_t)MIN_RULE_DURATION);
        rule.durationMs = min(rule.durationMs, (uint16_t)MAX_RULE_DURATION);
        rule.nextRule = 0;
        rule._padding = 0;
        return rule;
    }

    void evolveImpl() {
        if (controlMode) {
            Logger::log("Control mode: skipping evolution");
            for (int i = 0; i < POPULATION_SIZE; i++) {
                population[i].noveltyScore = 0;
                population[i].behavior.init();
            }
            return;
        }
        std::sort(population, population + POPULATION_SIZE,
            [](const Gene& a, const Gene& b) { return a.noveltyScore > b.noveltyScore; });
        int eliteCount = max(1, POPULATION_SIZE / 4);
        static Gene newPopulation[POPULATION_SIZE];
        for (int i = 0; i < eliteCount; i++) {
            newPopulation[i] = population[i];
            newPopulation[i].noveltyScore = 0;
            newPopulation[i].behavior.init();
        }
        for (int i = eliteCount; i < POPULATION_SIZE; i++) {
            int p1 = tournamentSelectByNovelty();
            int p2 = tournamentSelectByNovelty();
            Gene::crossover(population[p1], population[p2], newPopulation[i]);
            newPopulation[i].mutate(mutationRate);
            newPopulation[i].noveltyScore = 0;
            newPopulation[i].behavior.init();
            newPopulation[i].hasChaosRules = false;
            newPopulation[i].chaosRuleCount = 0;
            newPopulation[i].chaosRulesStartIndex = 0;
        }
        for (int i = 0; i < POPULATION_SIZE; i++) population[i] = newPopulation[i];
        float avg = getAvgNovelty();
        if (avg < bestNoveltyEver * 0.5f) mutationRate = min(mutationRate + 0.03f, 0.5f);
        else if (avg > bestNoveltyEver * 0.8f) mutationRate = max(mutationRate - 0.01f, 0.05f);
        Logger::logf("🧬 Evolution done, mutation rate=%.2f", mutationRate);

        archive.saveIncrementalSnapshot(currentGeneration);

        if (GeneStorage::isExperimentComplete()) {
            Logger::log("🎉 Experiment completed! All data saved.");
        }
    }

    int tournamentSelectByNovelty() {
        int tournamentSize = 2, best = PhysicalRandom::getRange(0, POPULATION_SIZE);
        for (int i = 1; i < tournamentSize; i++) {
            int candidate = PhysicalRandom::getRange(0, POPULATION_SIZE);
            if (population[candidate].noveltyScore > population[best].noveltyScore) best = candidate;
        }
        return best;
    }

    float getAvgNovelty() {
        float sum = 0;
        for (int i = 0; i < POPULATION_SIZE; i++) sum += population[i].noveltyScore;
        return sum / POPULATION_SIZE;
    }

public:
    static void periodicForceSave() {
        uint32_t now = millis();
        if (now - lastForceSaveTime < FORCE_SAVE_INTERVAL) {
            return;
        }
        lastForceSaveTime = now;
        RobustStorage::forceSave();
        if (instance.archive.getArchiveSize() > 0) {
            if (!instance.archive.save()) {
                Logger::log("⚠️ Periodic force save: archive save failed");
            } else {
                Logger::log("💾 Periodic force save: archive saved");
            }
            }
        if (instance.testActive) {
            Gene& g = instance.population[instance.currentIndividual];
            if (g.survival_time > 0 && g.ruleCount > 0) {
                GeneStorage::saveIndividualRecord(
                    instance.currentGeneration,
                    instance.currentIndividual,
                    g
                );
            }
        }
        
        RollingStorage::resetCleanupAttempts();
        Logger::log("📀 Periodic force save completed");
    }

    static void init() { instance.initImpl(); }
    static void startCurrentTest() { instance.startCurrentTestImpl(); }
    static void endCurrentTest() { instance.endCurrentTestImpl(); }
    static void nextIndividual() { instance.nextIndividualImpl(); }
    static bool isTestActive() { return instance.testActive; }
    static bool isExperimentReady() { return experimentReady; }
    static uint32_t getTestStartTime() { return instance.testStartTime; }
    static uint32_t getGeneration() { return instance.currentGeneration; }
    static int getIndividual() { return instance.currentIndividual; }
    static float getMutationRate() { return instance.mutationRate; }
    static float getBestNoveltyEver() { return instance.bestNoveltyEver; }

    static Gene& getCurrentGene() { return instance.population[instance.currentIndividual]; }
    static void setPendingTransition(bool p) { instance.pendingTransition = p; }
    static bool getPendingTransition() { return instance.pendingTransition; }
    static void clearPendingTransition() { instance.pendingTransition = false; }
    static void setMutationRate(float rate) { instance.mutationRate = constrain(rate, 0.01f, 1.0f); }
    static void setControlMode(bool enable) {
        instance.controlMode = enable;
        Logger::logf("Control mode: %s", enable ? "ON" : "OFF");
    }

    static void resetToGeneration1() {
        // [v10.5] 显式删除所有 /pop_gen_*.bin，File.name() 经 normPath()
        if (RobustStorage::isReady()) {
            std::vector<String> toDelete;
            File root = SPIFFS.open("/");
            if (root) {
                while (File f = root.openNextFile()) {
                    String name = normPath(f.name());   // [v10.5] 规范化
                    if (name.startsWith("/pop_gen_") && name.endsWith(".bin")) {
                        toDelete.push_back(name);
                    }
                    f.close();
                }
                root.close();
            }
            for (const String& name : toDelete) {
                SPIFFS.remove(name);
                Logger::logf("🗑️ Deleted: %s", name.c_str());
            }
        }
        instance.currentGeneration = 1;
        GeneStorage::setCurrentGeneration(1);
        instance.initImpl();
        Logger::log("✅ Reset to generation 1 (all old populations deleted)");
    }

    static int getArchiveSize() {
        return instance.archive.getArchiveSize();
    }
    static NoveltyArchive& getArchive() {
        return instance.archive;
    }
    static uint32_t findLatestGeneration() {
        return GeneStorage::findLatestGeneration();
    }
    static void forceSaveNow() {
        lastForceSaveTime = 0;
        periodicForceSave();
    }
};

EvolutionEngine EvolutionEngine::instance;
uint32_t EvolutionEngine::lastForceSaveTime = 0;
bool EvolutionEngine::experimentReady = false;

// ================================================================
// CarWebServer 类
//   [v10.5] /list/files 端点经 normPath() 规范化
// ================================================================
class CarWebServer {
private:
    static WebServer server;
    static bool isProcessingRequest;
    static uint32_t lastRequestTime;

    static String buildHTMLPage() {
        return R"rawliteral(<!DOCTYPE html>
<html>
<head>
<meta charset='UTF-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>OEE V10.5 - 监控面板</title>
<style>
*{margin:0;padding:0;box-sizing:border-box;}
body{font-family:-apple-system,system-ui,sans-serif;background:#0d1117;color:#e6edf3;padding:12px;max-width:480px;margin:0 auto;}
.header{background:linear-gradient(135deg,#161b22,#0d1117);border:1px solid #30363d;border-radius:12px;padding:16px 20px;margin-bottom:12px;}
.header h1{font-size:20px;color:#58a6ff;display:flex;align-items:center;gap:8px;}
.header h1 span{font-size:12px;color:#8b949e;font-weight:normal;}
.version-badge{background:#1f2937;color:#58a6ff;font-size:10px;padding:2px 8px;border-radius:10px;margin-left:8px;}
.card{background:#161b22;border:1px solid #30363d;border-radius:10px;padding:14px 16px;margin-bottom:10px;}
.card-title{font-size:11px;color:#8b949e;text-transform:uppercase;letter-spacing:0.5px;margin-bottom:8px;}
.btn{background:#21262d;color:#c9d1d9;border:1px solid #30363d;padding:8px 14px;border-radius:6px;cursor:pointer;font-size:12px;font-weight:500;transition:all 0.2s;flex:1;min-width:56px;text-align:center;}
.btn:hover{background:#30363d;border-color:#58a6ff;}
.btn-primary{background:#1f6feb;border-color:#58a6ff;color:#fff;}
.btn-primary:hover{background:#388bfd;}
.btn-danger{background:#da3633;border-color:#f85149;color:#fff;}
.btn-danger:hover{background:#f85149;}
.btn-warning{background:#d29922;border-color:#d29922;color:#fff;}
.btn-warning:hover{background:#e3b341;}
.btn-download{background:#1f6feb;border-color:#58a6ff;color:#fff;}
.btn-download:hover{background:#388bfd;}
.btn-group{display:flex;gap:6px;flex-wrap:wrap;margin-top:6px;}
.btn-group .btn{flex:1;min-width:60px;}
.stat-grid{display:grid;grid-template-columns:1fr 1fr 1fr;gap:6px;margin-top:6px;}
.stat-item{background:#0d1117;border-radius:6px;padding:6px 8px;text-align:center;}
.stat-label{font-size:8px;color:#8b949e;text-transform:uppercase;}
.stat-value{font-size:16px;font-weight:600;color:#f0f6fc;}
.stat-value.high{color:#3fb950;}
.stat-value.warn{color:#d29922;}
.stat-value.danger{color:#f85149;}
.stat-value.blue{color:#58a6ff;}
.sensor-grid{display:grid;grid-template-columns:1fr 1fr;gap:4px;margin-top:4px;}
.sensor-item{background:#0d1117;border-radius:4px;padding:4px 8px;font-size:11px;display:flex;justify-content:space-between;}
.sensor-item .label{color:#8b949e;}
.sensor-item .value{color:#58a6ff;font-weight:600;}
.state-matrix{display:flex;gap:4px;flex-wrap:wrap;margin-top:4px;}
.state-box{padding:4px 10px;border-radius:4px;border:2px solid #30363d;font-size:10px;font-weight:600;color:#8b949e;background:transparent;flex:1;text-align:center;transition:all 0.3s;}
.state-box.active{transform:scale(1.02);}
.state-idle.active{background:#8b949e;border-color:#8b949e;color:#0d1117;}
.state-walking.active{background:#3fb950;border-color:#3fb950;color:#0d1117;}
.state-stuck.active{background:#d29922;border-color:#d29922;color:#0d1117;animation:blink 0.5s infinite;}
.state-chaos.active{background:#f85149;border-color:#f85149;color:#0d1117;animation:blink 0.3s infinite;}
@keyframes blink{0%,100%{opacity:1;}50%{opacity:0.6;}}
.storage-bar{background:#0d1117;border-radius:4px;height:4px;overflow:hidden;margin-top:4px;}
.storage-fill{height:100%;border-radius:4px;transition:width 0.5s;}
.storage-fill.good{background:#3fb950;}
.storage-fill.warn{background:#d29922;}
.storage-fill.danger{background:#f85149;}
.warning-box{background:#1f2937;border-left:3px solid #3fb950;padding:4px 8px;font-size:10px;border-radius:4px;margin-top:4px;}
.warning-box.danger{border-left-color:#f85149;background:#2d1b1b;}
#cleanPreview{font-size:10px;color:#8b949e;margin-top:4px;background:#0d1117;padding:6px;border-radius:4px;max-height:120px;overflow:auto;display:none;border:1px solid #30363d;white-space:pre-wrap;}
</style>
</head>
<body>
<div class="header">
<h1>🚗 OEE <span id="fwVersion">-</span><span class="version-badge">极简鲁棒存储</span></h1>
<div style="display:flex;justify-content:space-between;margin-top:4px;font-size:12px;">
<span>代 <span id="gen" class="blue" style="font-weight:600;">-</span></span>
<span>个体 <span id="ind" class="blue" style="font-weight:600;">-</span></span>
<span>新奇度 <span id="nov" class="high" style="font-weight:600;">-</span></span>
<span>混沌 <span id="chaosCnt" class="warn" style="font-weight:600;">-</span></span>
</div>
</div>

<div class="card">
<div class="card-title">🔴 状态</div>
<div class="state-matrix" id="stateMatrix">
<div class="state-box state-idle" data-state="0">⏸ 空闲</div>
<div class="state-box state-walking" data-state="1">🚶 行走</div>
<div class="state-box state-stuck" data-state="2">⚠️ 卡住</div>
<div class="state-box state-chaos" data-state="3">🔥 混沌</div>
</div>
</div>

<div class="card">
<div class="card-title">📊 传感器</div>
<div class="sensor-grid">
<div class="sensor-item"><span class="label">⬅ 左传感器</span><span class="value" id="sL">-</span></div>
<div class="sensor-item"><span class="label">➡ 右传感器</span><span class="value" id="sR">-</span></div>
<div class="sensor-item"><span class="label">左编码器</span><span class="value" id="encL">-</span></div>
<div class="sensor-item"><span class="label">右编码器</span><span class="value" id="encR">-</span></div>
<div class="sensor-item" style="grid-column:span 2;background:#1a1a2e;"><span class="label">🌊 GPIO6 物理噪声</span><span class="value" id="noiseRaw">-</span></div>
</div>
<div class="stat-grid">
<div class="stat-item"><div class="stat-label">存活</div><div class="stat-value blue" id="survival">-</div></div>
<div class="stat-item"><div class="stat-label">距离</div><div class="stat-value" id="dist">-</div></div>
<div class="stat-item"><div class="stat-label">规则</div><div class="stat-value blue" id="rules">-</div></div>
</div>
</div>

<div class="card">
<div class="card-title">💾 存储 <span id="storageLabel">-</span></div>
<div class="storage-bar"><div class="storage-fill" id="storageFill" style="width:0%"></div></div>
<div id="storageMsg" class="warning-box">✅ 空间充足</div>
<div id="storageStats" style="font-size:10px;color:#8b949e;margin-top:4px;">加载中...</div>
</div>

<div class="card">
<div class="card-title">🧬 进化控制</div>
<div class="btn-group">
<button class="btn btn-primary" onclick="startExperiment()">▶ 启动测试</button>
<button class="btn btn-danger" onclick="api('/evolution?action=stop')">⏹ 停止</button>
<button class="btn" onclick="api('/evolution?action=next')">⏭ 下一个</button>
<button class="btn" onclick="location.reload()">🔄 刷新</button>
</div>
</div>

<div class="card">
<div class="card-title">🎮 电机控制</div>
<div class="btn-group">
<button class="btn btn-primary" onclick="motorCtrl('enable')">✅ 开启</button>
<button class="btn btn-danger" onclick="motorCtrl('disable')">❌ 关闭</button>
<button class="btn" onclick="motorCtrl('stop')">⏹ 停止</button>
</div>
<div id="motorStatus" style="font-size:11px;color:#8b949e;margin-top:4px;">电机: -</div>
</div>

<div class="card">
<div class="card-title">🧹 数据清理 <span class="warn">手动操作</span></div>
<div style="font-size:11px;color:#8b949e;margin-bottom:6px;">
  ⚠️ 删除后不可恢复，请确认数据已备份
  <br>📋 点击"预览"查看即将删除的文件（L3+L4可删除，L1+L2受保护）
</div>
<div class="btn-group">
<button class="btn" onclick="cleanPreview()">🔍 预览清理</button>
<button class="btn btn-warning" onclick="cleanLevel(3)">🧹 清理种群 (L3)</button>
<button class="btn btn-warning" onclick="cleanLevel(4)">🧹 清理过程 (L4)</button>
<button class="btn btn-danger" onclick="if(confirm('⚠️ 确定重置实验状态？L1+L2核心数据保留')){fetch('/admin/reset').then(()=>location.reload());}">🔄 重置状态</button>
</div>
<div id="cleanPreview">🔍 点击"预览清理"查看待删除文件</div>
</div>

<div class="card">
<div class="card-title">📥 数据导出</div>
<div class="btn-group">
<button class="btn btn-download" onclick="downloadFile('/download/history')">📋 历史CSV</button>
<button class="btn btn-download" onclick="downloadFile('/download/chaos')">🌪️ 混沌CSV</button>
<button class="btn btn-download" onclick="downloadFile('/download/population')">🧬 种群CSV</button>
<button class="btn btn-download" onclick="downloadFile('/download/novelty')">📦 核心存档</button>
</div>
</div>

<div class="card" style="border-color:#30363d;">
<div class="card-title">⚙️ 系统</div>
<div style="display:flex;gap:6px;flex-wrap:wrap;font-size:11px;color:#8b949e;">
<span>存储: <span id="freeSpace">-</span></span>
<span>存档: <span id="archiveSize">-</span></span>
</div>
</div>

<script>
function api(url){fetch(url).then(r=>r.json()).catch(e=>console.error(e));}

function startExperiment(){
    fetch('/status').then(r=>r.json()).then(d=>{
        if(!d.experimentReady){
            alert('❌ 实验未就绪\n\n请检查:\n- SPIFFS 是否挂载\n- /pop_gen_N.bin 是否存在\n- 串口日志中的 RECOVERY AUDIT 区块');
            return;
        }
        if(!d.currentPopExists){
            alert('❌ 当前代种群文件缺失\n\n/pop_gen_'+d.generation+'.bin 不存在');
            return;
        }
        fetch('/evolution?action=start').then(r=>r.json()).then(d2=>{
            if(d2.status==='started'){
                alert('✅ 实验已启动');
            }else{
                alert('❌ 启动失败: '+(d2.reason||'未知原因'));
            }
        }).catch(e=>alert('❌ 启动失败: '+e.message));
    }).catch(e=>alert('❌ 状态查询失败: '+e.message));
}

function motorCtrl(a){fetch('/motor?action='+a).then(r=>r.json()).then(d=>{document.getElementById('motorStatus').innerHTML='电机: '+d.status;});}

function downloadFile(url){fetch(url).then(r=>{if(!r.ok)throw new Error('下载失败');return r.blob();}).then(blob=>{const a=document.createElement('a');a.href=URL.createObjectURL(blob);a.download=url.split('/').pop()||'download.csv';document.body.appendChild(a);a.click();document.body.removeChild(a);}).catch(e=>alert('下载失败: '+e.message));}

function cleanPreview(){
    var el=document.getElementById('cleanPreview');
    el.style.display='block';
    el.textContent='⏳ 加载中...';
    fetch('/admin/clean/preview').then(r=>r.json()).then(d=>{
        el.textContent=d.preview||'无待删除文件';
    }).catch(e=>{
        el.textContent='❌ 预览失败: '+e.message;
    });
}

function cleanLevel(level){
    var levelNames = {3:'L3 种群数据', 4:'L4 过程数据'};
    var msg = '⚠️ 即将删除所有 '+levelNames[level]+'\n\n';
    msg += 'L1(核心) 和 L2(基因) 数据受保护，不会被删除\n';
    msg += '点击"确定"继续，点击"取消"中止';
    if(!confirm(msg)) return;
    fetch('/admin/clean/preview').then(r=>r.json()).then(d=>{
        var confirmMsg = '将删除以下文件:\n\n';
        confirmMsg += d.preview || '（无待删除文件）';
        confirmMsg += '\n\n确定继续？';
        if(!confirm(confirmMsg)) return;
        fetch('/admin/clean?level='+level,{method:'POST'}).then(r=>r.json()).then(d2=>{
            alert('✅ 清理完成\n已删除: '+d2.deleted+' 个文件\n受保护: '+d2.protected+' 个');
            location.reload();
        }).catch(e=>alert('❌ 清理失败: '+e.message));
    }).catch(e=>alert('❌ 预览失败: '+e.message));
}

setInterval(function(){
    fetch('/status').then(r=>r.json()).then(d=>{
        document.getElementById('fwVersion').textContent = d.firmwareVersion || '-';
        document.getElementById('gen').textContent=d.generation||'-';
        document.getElementById('ind').textContent=(d.individual!==undefined?d.individual+1:'-')+'/'+d.population;
        document.getElementById('nov').textContent=d.novelty!==undefined?d.novelty.toFixed(4):'-';
        document.getElementById('chaosCnt').textContent=d.chaosTriggerCount||0;
        document.getElementById('survival').textContent=d.survival!==undefined?(d.survival/1000).toFixed(1)+'s':'-';
        document.getElementById('dist').textContent=(d.distance||0)+' ticks';
        document.getElementById('rules').textContent=d.ruleCount||0;
        document.getElementById('sL').textContent=d.sensorLeft||0;
        document.getElementById('sR').textContent=d.sensorRight||0;
        document.getElementById('encL').textContent=d.leftTicks||0;
        document.getElementById('encR').textContent=d.rightTicks||0;
        document.getElementById('noiseRaw').textContent=d.noiseRaw||'0';
        document.getElementById('motorStatus').innerHTML='电机: '+(d.motorEnabled?'✅ 已启用':'❌ 已禁用');
        document.querySelectorAll('.state-box').forEach(el=>{
            el.classList.remove('active');
            if(parseInt(el.dataset.state)===d.stateCode){
                el.classList.add('active');
            }
        });
        document.getElementById('freeSpace').textContent=(d.freeSpaceKB||0)+' KB';
        document.getElementById('archiveSize').textContent=d.archiveSize||0;
        var health=d.storageHealth||1.0;
        var pct=Math.round(health*100);
        var fill=document.getElementById('storageFill');
        fill.style.width=pct+'%';
        var msg=document.getElementById('storageMsg');
        var freeKB=d.freeSpaceKB||0;
        if(pct<25){fill.className='storage-fill danger';msg.className='warning-box danger';msg.innerHTML='⚠️ 存储严重不足! ('+freeKB+' KB)';}
        else if(pct<50){fill.className='storage-fill warn';msg.className='warning-box';msg.style.borderLeftColor='#d29922';msg.innerHTML='⚠️ 存储偏低 ('+freeKB+' KB)';}
        else{fill.className='storage-fill good';msg.className='warning-box';msg.style.borderLeftColor='#3fb950';msg.innerHTML='✅ 空间充足 ('+freeKB+' KB)';}
        document.getElementById('storageLabel').textContent=pct+'%';
    });
    fetch('/storage/status').then(r=>r.json()).then(d=>{
        document.getElementById('storageStats').textContent=d.status||'';
    }).catch(e=>console.error(e));
},500);
</script>
</body></html>)rawliteral";
    }

    static void handleStatus() {
        if (isProcessingRequest) {
            if (millis() - lastRequestTime > 2000) {
                isProcessingRequest = false;
            } else {
                server.send(503, "application/json", "{\"error\":\"Busy\"}");
                return;
            }
        }
        isProcessingRequest = true;
        lastRequestTime = millis();

        Gene& g = EvolutionEngine::getCurrentGene();
        const char* stateNames[] = {"IDLE", "WALKING", "STUCK", "CHAOS"};

        size_t freeSpace = 0;
        if (RobustStorage::isReady()) {
            File root = SPIFFS.open("/");
            if (root) {
                size_t used = 0;
                int count = 0;
                File f = root.openNextFile();
                while (f && count < 100) {
                    used += f.size();
                    count++;
                    f.close();
                    f = root.openNextFile();
                }
                root.close();
                freeSpace = SPIFFS.totalBytes() - used;
            }
        }

        String json = "{";
        json += "\"firmwareVersion\":\"" + String(FIRMWARE_VERSION) + "\",";
        json += "\"experimentActive\":" + String(GeneStorage::isExperimentActive() ? "true" : "false") + ",";
        json += "\"experimentReady\":" + String(EvolutionEngine::isExperimentReady() ? "true" : "false") + ",";
        json += "\"geneStorageGeneration\":" + String(GeneStorage::getCurrentGeneration()) + ",";
        json += "\"latestPopGeneration\":" + String(GeneStorage::findLatestGeneration()) + ",";
        json += "\"currentPopExists\":" + String(GeneStorage::populationFileExists(GeneStorage::getCurrentGeneration()) ? "true" : "false") + ",";
        json += "\"generation\":" + String(EvolutionEngine::getGeneration()) + ",";
        json += "\"individual\":" + String(EvolutionEngine::getIndividual()) + ",";
        json += "\"population\":" + String(POPULATION_SIZE) + ",";
        json += "\"novelty\":" + String(g.noveltyScore, 4) + ",";
        json += "\"ruleCount\":" + String(g.ruleCount) + ",";
        json += "\"chaosTimeoutMs\":" + String(g.chaosTimeoutMs) + ",";
        json += "\"noiseRaw\":" + String(SensorCalibration::readNoise()) + ",";
        json += "\"state\":\"" + String(stateNames[MotorController::getMotorState()]) + "\",";
        json += "\"stateCode\":" + String(MotorController::getMotorState()) + ",";
        json += "\"survival\":" + String(millis() - EvolutionEngine::getTestStartTime()) + ",";
        json += "\"distance\":" + String(MotorController::getDistanceTicks()) + ",";
        json += "\"motorEnabled\":" + String(MotorController::isMotorEnabled() ? "true" : "false") + ",";
        json += "\"freeSpaceKB\":" + String(freeSpace / 1024) + ",";
        json += "\"storageHealth\":" + String(RobustStorage::getStorageHealth(), 2) + ",";
        json += "\"archiveSize\":" + String(EvolutionEngine::getArchiveSize()) + ",";
        json += "\"chaosActive\":" + String(MotorController::isChaosActive() ? "true" : "false") + ",";
        json += "\"sensorLeft\":" + String(MotorController::getLeftSensor()) + ",";
        json += "\"sensorRight\":" + String(MotorController::getRightSensor()) + ",";
        json += "\"leftTicks\":" + String(MotorController::getLeftTicks()) + ",";
        json += "\"rightTicks\":" + String(MotorController::getRightTicks()) + ",";
        json += "\"encoderDiff\":" + String(MotorController::getEncoderDiff()) + ",";
        json += "\"chaosTriggerCount\":" + String(MotorController::getChaosTriggerCount());
        json += "}";

        server.send(200, "application/json", json);
        isProcessingRequest = false;
    }

    static void handleMotor() {
        if (!server.hasArg("action")) { server.send(400, "{}"); return; }
        String action = server.arg("action");
        String status = "ok";
        if (action == "enable") { MotorController::enableMotor(); status = "已启用"; }
        else if (action == "disable") { MotorController::disableMotor(); status = "已禁用"; }
        else if (action == "stop") { MotorController::forceStopAction(); MotorController::stopMotors(); status = "已停止"; }
        else { server.send(400, "{}"); return; }
        server.send(200, "application/json", "{\"status\":\"" + status + "\"}");
    }

    static void handleEvolution() {
        if (!server.hasArg("action")) { server.send(400, "{}"); return; }
        String action = server.arg("action");
        if (action == "start") {
            if (!EvolutionEngine::isExperimentReady()) {
                Logger::log("❌ /evolution?action=start refused: experiment not ready");
                server.send(500, "application/json",
                    "{\"status\":\"error\",\"reason\":\"experiment_not_ready\"}");
                return;
            }
            if (GeneStorage::getCurrentGeneration() == 0) {
                Logger::log("❌ /evolution?action=start refused: currentGeneration == 0");
                server.send(500, "application/json",
                    "{\"status\":\"error\",\"reason\":\"generation_zero\"}");
                return;
            }
            String popPath = "/pop_gen_" + String(GeneStorage::getCurrentGeneration()) + ".bin";
            if (!FileUtils::exists(popPath)) {
                Logger::logf("❌ /evolution?action=start refused: %s not found", popPath.c_str());
                server.send(500, "application/json",
                    "{\"status\":\"error\",\"reason\":\"population_file_missing\"}");
                return;
            }

            if (GeneStorage::isExperimentComplete()) {
                GeneStorage::startNewExperiment();
                EvolutionEngine::init();
            }
            EvolutionEngine::startCurrentTest();
            server.send(200, "application/json", "{\"status\":\"started\"}");
        } else if (action == "stop") {
            EvolutionEngine::endCurrentTest();
            MotorController::stopMotors();
            server.send(200, "application/json", "{\"status\":\"stopped\"}");
        } else if (action == "next") {
            if (!EvolutionEngine::isExperimentReady()) {
                Logger::log("❌ /evolution?action=next refused: experiment not ready");
                server.send(500, "application/json",
                    "{\"status\":\"error\",\"reason\":\"experiment_not_ready\"}");
                return;
            }
            EvolutionEngine::nextIndividual();
            server.send(200, "application/json", "{\"status\":\"next\"}");
        } else {
            server.send(400, "{}");
        }
    }

    static void handleAdminReset() {
        Logger::log("🔄 Admin reset triggered");
        Logger::log("   🔒 L1+L2 DATA PROTECTED");
        if (EvolutionEngine::isTestActive()) {
            Logger::log("⚠️ Test active, stopping first");
            EvolutionEngine::endCurrentTest();
            MotorController::stopMotors();
        }
        GeneStorage::startNewExperiment();
        EvolutionEngine::resetToGeneration1();
        server.send(200, "application/json",
                   "{\"status\":\"reset\",\"core_protected\":true,\"current_gen\":1}");
    }

    static void handleStorageStatus() {
        String stats = TieredStorageManager::getStorageStats();
        stats.replace("\n", "\\n");
        stats.replace("\"", "\\\"");
        String json = "{\"status\":\"" + stats + "\"}";
        server.send(200, "application/json", json);
    }

    static void handleCleanPreview() {
        uint32_t currentGen = GeneStorage::getCurrentGeneration();
        String preview = TieredStorageManager::getCleanPreview(currentGen);
        preview.replace("\n", "\\n");
        preview.replace("\"", "\\\"");
        String json = "{\"preview\":\"" + preview + "\"}";
        server.send(200, "application/json", json);
    }

    static void handleClean() {
        if (!server.hasArg("level")) {
            server.send(400, "application/json", "{\"error\":\"Missing level parameter\"}");
            return;
        }
        int level = server.arg("level").toInt();
        uint32_t currentGen = GeneStorage::getCurrentGeneration();
        String preview = TieredStorageManager::getCleanPreview(currentGen);
        Logger::logf("📋 Clean preview for level %d:\n%s", level, preview.c_str());

        if (level == DATA_LEVEL_POP || level == DATA_LEVEL_PROCESS) {
            int deleted = TieredStorageManager::cleanL3AndL4(true, false, currentGen);
            int protected_count = 0;
            File root = SPIFFS.open("/");
            if (root) {
                while (File f = root.openNextFile()) {
                    String name = normPath(f.name());   // [v10.5] 规范化
                    int lvl = TieredStorageManager::getDataLevel(name);
                    if (lvl == DATA_LEVEL_CORE || lvl == DATA_LEVEL_GENE) {
                        protected_count++;
                    }
                    f.close();
                }
                root.close();
            }
            server.send(200, "application/json",
                       "{\"status\":\"ok\",\"deleted\":" + String(deleted) +
                       ",\"protected\":" + String(protected_count) + "}");
        } else {
            server.send(400, "application/json",
                       "{\"error\":\"Cannot delete L1 or L2 data\"}");
        }
    }

    static void handleDownloadHistory() {
        String data = RobustStorage::getCSVData();
        if (data.length() == 0) {
            data = "timestamp,generation,noveltyScore,survivalTime,distance_ticks,ruleCount\n";
        }
        server.sendHeader("Content-Type", "text/csv");
        server.sendHeader("Content-Disposition", "attachment; filename=evolution_history.csv");
        server.send(200, "text/csv", data);
    }

    static void handleDownloadChaos() {
        String data = RobustStorage::getChaosHistoryCSV();
        if (data.length() == 0) {
            data = "timestamp,generation,individual,chaosTriggerCount,chaosTotalDuration,"
                   "chaosMaxDuration,chaosFirstTime,chaosLastTime,"
                   "baselineDistance,chaosDistance,baselineFrames,chaosFrames,"
                   "baselineAvgSpeedL,baselineAvgSpeedR,chaosAvgSpeedL,chaosAvgSpeedR,"
                   "chaosSuccess,chaosExitReason,chaosInterruptedCount,testTerminatedBy\n";
        }
        server.sendHeader("Content-Type", "text/csv");
        server.sendHeader("Content-Disposition", "attachment; filename=chaos_records.csv");
        server.send(200, "text/csv", data);
    }

    static void handleDownloadPopulation() {
        String data = "generation,individual,ruleCount,survivalTime,distanceTicks,noveltyScore,"
                      "obstacleThreshold,clearThreshold,encoderDiffThreshold,encoderDiffMin,"
                      "wheelSpinThreshold,wheelStopThreshold,stuckWindowSize,"
                      "chaosNoiseAmplifier,chaosMinPwm,chaosTimeoutMs,chaosForceTimeoutMs,"
                      "hasChaosRules,chaosRuleCount\n";

            auto gens = RollingStorage::getStoredGenerationsPublic();
        if (RobustStorage::isReady()) {
            static Gene pop[POPULATION_SIZE];
            Logger::logf("📊 handleDownloadPopulation: %d generations found", (int)gens.size());
            for (uint32_t gen : gens) {
                if (!GeneStorage::loadPopulation(gen, pop, POPULATION_SIZE)) continue;
                for (int i = 0; i < POPULATION_SIZE; i++) {
                    const Gene& g = pop[i];
                    data += String(gen) + "," + String(i) + "," + String(g.ruleCount) + ","
                          + String(g.survival_time) + "," + String(g.distance_ticks) + ","
                          + String(g.noveltyScore, 6) + ","
                          + String(g.obstacleThreshold) + "," + String(g.clearThreshold) + ","
                          + String(g.encoderDiffThreshold) + "," + String(g.encoderDiffMin) + ","
                          + String(g.wheelSpinThreshold) + "," + String(g.wheelStopThreshold) + ","
                          + String(g.stuckWindowSize) + ","
                          + String(g.chaosNoiseAmplifier) + "," + String(g.chaosMinPwm) + ","
                          + String(g.chaosTimeoutMs) + "," + String(g.chaosForceTimeoutMs) + ","
                          + String(g.hasChaosRules ? 1 : 0) + "," + String(g.chaosRuleCount) + "\n";
                }
            }
        }

        // [v10.10] 诊断日志: 导出 population 时打印各个体数据概况
        int nonzeroCount = 0;
        for (uint32_t gen : gens) {
            static Gene pop_check[POPULATION_SIZE];
            if (GeneStorage::loadPopulation(gen, pop_check, POPULATION_SIZE)) {
                for (int i = 0; i < POPULATION_SIZE; i++) {
                    if (pop_check[i].survival_time > 0) nonzeroCount++;
                }
            }
        }
        Logger::logf("📊 Population export: %d generations, %d/%d individuals with valid data",
                     (int)gens.size(), nonzeroCount, (int)gens.size() * POPULATION_SIZE);

        server.sendHeader("Content-Type", "text/csv");
        server.sendHeader("Content-Disposition", "attachment; filename=population_summary.csv");
        server.send(200, "text/csv", data);
    }

    static void handleDownloadNovelty() {
        String path = "/novelty_archive.bin";
        if (!RobustStorage::isReady() || !SPIFFS.exists(path)) {
            server.send(404, "text/plain", "File not found");
            return;
        }
        File file = SPIFFS.open(path, FILE_READ);
        if (!file) {
            server.send(500, "text/plain", "Cannot open file");
            return;
        }
        server.sendHeader("Content-Type", "application/octet-stream");
        server.sendHeader("Content-Disposition", "attachment; filename=novelty_archive.bin");
        server.streamFile(file, "application/octet-stream");
        file.close();
        Logger::log("📥 Downloaded: novelty_archive.bin");
    }

public:
    static void init() {
        server.on("/", [](){ server.send(200, "text/html", buildHTMLPage()); });
        server.on("/status", handleStatus);
        server.on("/schema.json", []() {
            String json = "{";
            json += "\"BehaviorRule\":" + String(sizeof(BehaviorRule)) + ",";
            json += "\"BehaviorDescriptor\":" + String(sizeof(BehaviorDescriptor)) + ",";
            json += "\"FrameLogEntry\":" + String(sizeof(FrameLogEntry)) + ",";
            json += "\"CompressedFrameEntry\":" + String(sizeof(CompressedFrameEntry)) + ",";
            json += "\"FileHeader\":" + String(sizeof(FileHeader)) + ",";
            json += "\"HistoryRecord\":" + String(sizeof(HistoryRecord)) + ",";
            json += "\"ChaoticTestRecord\":" + String(sizeof(ChaoticTestRecord)) + ",";
            json += "\"ChaosSnapshotEntry\":" + String(sizeof(ChaosSnapshotEntry)) + ",";
            json += "\"ChaosSnapshotHeader\":" + String(sizeof(ChaosSnapshotHeader));
            json += "}";
            server.send(200, "application/json", json);
        });

        server.on("/storage/status", handleStorageStatus);
        server.on("/admin/clean/preview", handleCleanPreview);
        server.on("/admin/clean", HTTP_POST, handleClean);
        server.on("/admin/reset", handleAdminReset);
        server.on("/evolution", handleEvolution);
        server.on("/motor", handleMotor);
        server.on("/download/history", handleDownloadHistory);
        server.on("/download/chaos", handleDownloadChaos);
        server.on("/download/population", handleDownloadPopulation);
        server.on("/download/novelty", handleDownloadNovelty);

        server.on("/download/nova_gen", []() {
            if (!server.hasArg("gen")) { server.send(400, "text/plain", "Missing gen"); return; }
            uint32_t gen = server.arg("gen").toInt();
            String path = "/nova_gen_" + String(gen) + ".bin";
            if (!SPIFFS.exists(path)) { server.send(404, "text/plain", "Not found"); return; }
            File file = SPIFFS.open(path, FILE_READ);
            if (!file) { server.send(500, "text/plain", "Open failed"); return; }
            server.sendHeader("Content-Type", "application/octet-stream");
            server.sendHeader("Content-Disposition",
                              "attachment; filename=nova_gen_" + String(gen) + ".bin");
            server.streamFile(file, "application/octet-stream");
            file.close();
        });

        server.on("/download/pop", []() {
            if (!server.hasArg("gen")) {
                server.send(400, "text/plain", "Missing gen parameter");
                return;
            }
            uint32_t gen = server.arg("gen").toInt();
            String path = "/pop_gen_" + String(gen) + ".bin";
            if (!SPIFFS.exists(path)) {
                server.send(404, "text/plain", "File not found");
                return;
            }
            File file = SPIFFS.open(path, FILE_READ);
            if (!file) {
                server.send(500, "text/plain", "Cannot open file");
                return;
            }
            server.streamFile(file, "application/octet-stream");
            file.close();
        });

        server.on("/download/frame_bin", []() {
            if (!server.hasArg("gen") || !server.hasArg("id")) {
                server.send(400, "text/plain", "Missing gen or id parameter");
                return;
            }
            uint32_t gen = server.arg("gen").toInt();
            int id = server.arg("id").toInt();
            String path = "/frm_" + String(gen) + "_i" + String(id) + ".bin";
            if (!SPIFFS.exists(path)) {
                server.send(404, "text/plain", "File not found");
                return;
            }
            File file = SPIFFS.open(path, FILE_READ);
            if (!file) {
                server.send(500, "text/plain", "Cannot open file");
                return;
            }
            server.streamFile(file, "application/octet-stream");
            file.close();
        });

        server.on("/download/individual", []() {
            if (!server.hasArg("gen") || !server.hasArg("id")) {
                server.send(400, "text/plain", "Missing gen or id parameter");
                return;
            }
            uint32_t gen = server.arg("gen").toInt();
            int id = server.arg("id").toInt();
            String path = "/gen_" + String(gen) + "_id_" + String(id) + ".csv";
            if (!SPIFFS.exists(path)) {
                server.send(404, "text/plain", "File not found");
                return;
            }
            File file = SPIFFS.open(path, FILE_READ);
            if (!file) {
                server.send(500, "text/plain", "Cannot open file");
                return;
            }
            server.streamFile(file, "text/csv");
            file.close();
        });

        server.on("/download/chaos_snap", []() {
            if (!server.hasArg("gen") || !server.hasArg("id")) {
                server.send(400, "text/plain", "Missing gen or id parameter");
                return;
            }
            uint32_t gen = server.arg("gen").toInt();
            int id = server.arg("id").toInt();
            String path = "/chaos_snaps_g" + String(gen) + "_i" + String(id) + ".bin";
            if (!SPIFFS.exists(path)) {
                server.send(404, "text/plain", "File not found");
                return;
            }
            File file = SPIFFS.open(path, FILE_READ);
            if (!file) {
                server.send(500, "text/plain", "Cannot open file");
                return;
            }
            server.streamFile(file, "application/octet-stream");
            file.close();
        });

        // ================================================================
        // [v10.5] /list/files: File.name() 经 normPath() 规范化
        // ================================================================
        server.on("/list/files", []() {
            String json = "{\"files\":[";
            bool first = true;
            File root = SPIFFS.open("/");
            if (root) {
                while (File file = root.openNextFile()) {
                    if (!first) json += ",";
                    json += "\"" + normPath(file.name()) + "\"";   // [v10.5] 规范化
                    first = false;
                    file.close();
                }
                root.close();
            }
            json += "]}";
            server.send(200, "application/json", json);
        });
        server.onNotFound([](){ server.send(404, "text/plain", "Not Found"); });
        server.begin();
        Logger::log("Web server ready (v10.12-GenLimit)");
    }

    static void handleClient() {
        unsigned long start = millis();
        while ((millis() - start) < 50) {
            server.handleClient();
            delay(1);
        }
    }
};

WebServer CarWebServer::server(80);
bool CarWebServer::isProcessingRequest = false;
uint32_t CarWebServer::lastRequestTime = 0;

// ================================================================
// 串口命令函数声明
// ================================================================
void listSPIFFSFiles();
void printStatus();
void parseFrameLog(int gen, int id);
void parsePopulation(int gen);
void parseChaosRecord(int gen, int id);
void parseAllIndividuals(int gen);
void extractRamLog(int start, int end);
void showStorageStatus();

// ================================================================
// WiFi 初始化
// ================================================================
static bool wifiInitialized = false;
static bool wifiReady = false;

void initWiFi() {
    if (wifiInitialized) return;
    wifiInitialized = true;
    WiFi.mode(WIFI_AP);
    if (WiFi.softAP(WIFI_SSID, WIFI_PASSWORD)) {
        wifiReady = true;
        Logger::logf("WiFi AP: %s", WIFI_SSID);
        Logger::logf("IP: %s", WiFi.softAPIP().toString().c_str());
        CarWebServer::init();
    }
}

// ================================================================
// setup() [v10.6-WebDiag]
// ================================================================
void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("=== v10.11-Gen10Fix Struct Size (actual) ===");
    Serial.printf("BehaviorRule         = %d\n", sizeof(BehaviorRule));
    Serial.printf("BehaviorDescriptor   = %d\n", sizeof(BehaviorDescriptor));
    Serial.printf("FrameLogEntry        = %d\n", sizeof(FrameLogEntry));
    Serial.printf("CompressedFrameEntry = %d\n", sizeof(CompressedFrameEntry));
    Serial.printf("FileHeader           = %d\n", sizeof(FileHeader));
    Serial.printf("HistoryRecord        = %d\n", sizeof(HistoryRecord));
    Serial.printf("ChaoticTestRecord    = %d\n", sizeof(ChaoticTestRecord));
    Serial.printf("ChaosSnapshotEntry   = %d\n", sizeof(ChaosSnapshotEntry));
    Serial.printf("ChaosSnapshotHeader  = %d\n", sizeof(ChaosSnapshotHeader));
    Serial.println("==================================");

    Logger::log("========================================");
    Logger::logf("  OEE Test %s", FIRMWARE_VERSION);
    Logger::logf("  🛡️ %s 进化过渡存储失败自动恢复 + 诊断增强", FIRMWARE_VERSION);
    Logger::log("  ├── [N1] 全项目 File.name() 经 normPath() 规范化");
    Logger::log("  ├── [N2] 修复 getStoredGenerations 返回空");
    Logger::log("  ├── [N3] 修复 findLatestGeneration 返回 0");
    Logger::log("  ├── [N4] 修复断电恢复回退到第 1 代");
    Logger::log("  ├── [N5] 修复 population_snapshot.csv 空表");
    Logger::log("  └── [N6] 修复 ls 命令全部显示 📄");
    Logger::logf("  FORMAT_SPIFFS_ON_BOOT = %s",
                 FORMAT_SPIFFS_ON_BOOT ? "ENABLED ⚠️" : "DISABLED ✅");
    Logger::log("  Commands: ls, status, frame, pop, chaos, popall, ramlog, storage, reset, help");
    Logger::log("========================================");

    RobustStorage::init();

    // ============================================================
    // [v10.5] 冗余文件清理: File.name() 经 normPath()
    // ============================================================
    if (RobustStorage::isReady()) {
        int cleaned = 0;
        File root = SPIFFS.open("/");
        if (root) {
            while (File f = root.openNextFile()) {
                String name = normPath(f.name());   // [v10.5] 规范化
                bool shouldDelete = false;
                if (name.endsWith(".inc.bin")) shouldDelete = true;
                if (name.startsWith("/chaos_g") && name.endsWith(".csv")) shouldDelete = true;

                if (shouldDelete) {
                    f.close();
                    if (SPIFFS.remove(name)) {   // name 已带斜杠
                        cleaned++;
                    }
                    continue;
                }
                f.close();
            }
            root.close();
        }
        if (cleaned > 0) {
            Logger::logf("🧹 Cleaned %d redundant files (.inc.bin + chaos_g*.csv)", cleaned);
        }
    }

    #if FORMAT_SPIFFS_ON_BOOT
        Logger::log("⚠️ ⚠️ ⚠️ SPIFFS has been formatted!");
        Logger::log("⚠️ Please set FORMAT_SPIFFS_ON_BOOT to 0 and re-upload");
        Logger::log("⚠️ to enable data persistence!");
    #endif

    if (FirmwareVersionManager::isNewVersion()) {
        FirmwareVersionManager::cleanAllData();
    }

    GeneStorage::init();

    if (!GeneStorage::isExperimentActive()) {
        uint32_t latest = GeneStorage::findLatestGeneration();
        Logger::logf("⚠️ Experiment not active. findLatestGeneration() = %lu", latest);
        if (latest > 0) {
            Logger::logf("✅ Recovering to latest generation %lu", latest);
            GeneStorage::setCurrentGeneration(latest, true);
            GeneStorage::forceActivate();
        } else {
            Logger::log("📌 No population found. Creating new experiment at gen 1.");
            GeneStorage::startNewExperiment();
        }
    }

    if (!GeneStorage::isExperimentActive()) {
        Logger::log("⚠️ Still not active - using forceActivate...");
        GeneStorage::forceActivate();
    }

    Logger::logf("📌 GeneStorage active: %d, gen: %lu",
                 GeneStorage::isExperimentActive(), GeneStorage::getCurrentGeneration());

    RAMLogBuffer::init();
    RollingStorage::init();

    analogReadResolution(12);                                 // 12 位 (0~4095)
    analogSetPinAttenuation(PIN_NOISE_SOURCE, ADC_11db);      // 11dB 衰减, 满量程 3.3V
    pinMode(PIN_NOISE_SOURCE, INPUT);

    Logger::log("  🔹 ESP32-S3 片内TRNG (esp_random) 就绪检测...");
    Logger::log("  🔹 生成 5 个样本验证随机性:");
    for (int i = 0; i < 5; i++) {
        uint32_t raw = PhysicalRandom::get();
        float flt = PhysicalRandom::getFloat();
        Logger::logf("     [%d] Raw: 0x%08X  Float: %.4f", i, raw, flt);
    }
    Logger::log("  ✅ 硬件TRNG 输出正常 - 元算法层物理熵源已生效");
    Logger::log("========================================");

    Logger::log("  🔹 GPIO6 悬空 ADC 物理噪声源检测...");
    for (int i = 0; i < 5; i++) {
        int raw = analogRead(PIN_NOISE_SOURCE);
        Logger::logf("     [%d] GPIO%d ADC: %d", i, PIN_NOISE_SOURCE, raw);
    }
    Logger::log("  ✅ GPIO6 物理噪声源就绪 - 具身算法层混沌驱动已生效");
    Logger::log("========================================");

    SensorCalibration::autoCalibrate();
    MotorController::init();
    EvolutionEngine::init();

    Logger::log("===== EXPERIMENT RECOVERY AUDIT =====");
    Logger::logf("SPIFFS ready: %d", RobustStorage::isReady());
    Logger::logf("Marker exists: %d",
                 RobustStorage::isReady() ? SPIFFS.exists("/experiment_state.mrk") : 0);
    if (RobustStorage::isReady()) {
        Logger::logf("Marker content: %s",
                     FileUtils::safeRead("/experiment_state.mrk").c_str());
    }
    Logger::logf("GeneStorage generation: %lu", GeneStorage::getCurrentGeneration());
    Logger::logf("GeneStorage active: %d", GeneStorage::isExperimentActive());
    Logger::logf("Latest population generation: %lu", GeneStorage::findLatestGeneration());
    Logger::logf("Current pop exists: %d",
                 GeneStorage::populationFileExists(GeneStorage::getCurrentGeneration()));
    Logger::logf("EvolutionEngine ready: %d", EvolutionEngine::isExperimentReady());
    Logger::logf("EvolutionEngine generation: %lu", EvolutionEngine::getGeneration());
    Logger::log("=====================================");

    Logger::log("========================================");
    Logger::logf("  ✅ Ready (%s)", FIRMWARE_VERSION);
    Logger::log("  WiFi: CarLogger / 12345678");
    Logger::log("  http://192.168.4.1");
    Logger::log("  Serial: ls, status, frame, pop, chaos, popall, ramlog, storage, reset, help");
    Logger::log("========================================");
}

void loop() {
    if (!wifiInitialized) initWiFi();

    MotorController::updatePhysicsAction();

    if (MotorController::isChaosActive()) {
        MotorController::updateChaos();
    }

    if (EvolutionEngine::isTestActive()) {
        Gene& gene = EvolutionEngine::getCurrentGene();
        MotorController::update(gene);
        if (MotorController::isDead()) {
            Serial.println("☠️ Individual died (chaos force exit), ending test early");
            EvolutionEngine::endCurrentTest();
            EvolutionEngine::nextIndividual();
        }
        if (millis() - EvolutionEngine::getTestStartTime() > TEST_DURATION_MS) {
            EvolutionEngine::endCurrentTest();
            EvolutionEngine::nextIndividual();
        }
    }

    if (wifiReady) CarWebServer::handleClient();

    if (EvolutionEngine::getPendingTransition()) {
        EvolutionEngine::clearPendingTransition();
        EvolutionEngine::nextIndividual();
    }

    

    

    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        cmd.toLowerCase();

        if (cmd == "ls" || cmd == "list") {
            listSPIFFSFiles();
        } else if (cmd == "status") {
            printStatus();
        } else if (cmd == "help") {
            Serial.println("========================================");
            Serial.println("可用命令 (v10.11-Gen10Fix):");
            Serial.println("  ls / list          - 列出 SPIFFS 所有文件");
            Serial.println("  status             - 显示系统状态");
            Serial.println("  frame <代> <个体>  - 解析帧日志");
            Serial.println("  pop <代>           - 解析种群快照");
            Serial.println("  chaos <代> <个体>  - 解析混沌记录");
            Serial.println("  popall <代>        - 显示一代所有个体数据");
            Serial.println("  ramlog <开始> <结束> - 提取RAM日志");
            Serial.println("  ramlog all         - 提取全部RAM日志");
            Serial.println("  storage            - 查看SPIFFS存储状态");
            Serial.println("  reset              - 重置实验数据到第1代");
            Serial.println("  help               - 显示帮助");
            Serial.println("========================================");
        } else if (cmd == "reset") {
            Serial.println("🔄 Resetting experiment to generation 1...");
            GeneStorage::clearAllExperimentData();
            GeneStorage::startNewExperiment();
            EvolutionEngine::resetToGeneration1();
            Serial.println("✅ Reset complete");
        } else if (cmd.startsWith("frame ")) {
            int firstSpace = cmd.indexOf(' ');
            int secondSpace = cmd.indexOf(' ', firstSpace + 1);
            if (secondSpace > 0) {
                int gen = cmd.substring(firstSpace + 1, secondSpace).toInt();
                int id = cmd.substring(secondSpace + 1).toInt();
                parseFrameLog(gen, id);
            } else {
                Serial.println("用法: frame <代数> <个体>  例如: frame 2 0");
            }
        } else if (cmd.startsWith("pop ")) {
            int gen = cmd.substring(4).toInt();
            parsePopulation(gen);
        } else if (cmd.startsWith("chaos ")) {
            int firstSpace = cmd.indexOf(' ');
            int secondSpace = cmd.indexOf(' ', firstSpace + 1);
            if (secondSpace > 0) {
                int gen = cmd.substring(firstSpace + 1, secondSpace).toInt();
                int id = cmd.substring(secondSpace + 1).toInt();
                parseChaosRecord(gen, id);
            } else {
                Serial.println("用法: chaos <代数> <个体>  例如: chaos 2 0");
            }
        } else if (cmd.startsWith("popall ")) {
            int gen = cmd.substring(7).toInt();
            parseAllIndividuals(gen);
        } else if (cmd.startsWith("ramlog ")) {
            String params = cmd.substring(7);
            if (params == "all") {
                extractRamLog(0, RAMLogBuffer::getFrameCount() - 1);
            } else {
                int space = params.indexOf(' ');
                if (space > 0) {
                    int start = params.substring(0, space).toInt();
                    int end = params.substring(space + 1).toInt();
                    extractRamLog(start, end);
                } else {
                    Serial.println("用法: ramlog <开始> <结束>  或  ramlog all");
                }
            }
        } else if (cmd == "storage") {
            showStorageStatus();
        }
    }
    EvolutionEngine::periodicForceSave();
    RobustStorage::tick();
    
    
    
    
    delay(LOOP_DELAY_MS);
}

// ================================================================
// 串口命令函数实现
//   [v10.5] listSPIFFSFiles() 中 File.name() 经 normPath()
// ================================================================
void listSPIFFSFiles() {
    if (!SPIFFS.begin(true)) { Serial.println("❌ SPIFFS 挂载失败"); return; }
    File root = SPIFFS.open("/");
    if (!root) { Serial.println("❌ 无法打开根目录"); return; }
    Serial.println("========================================");
    Serial.println("📂 SPIFFS 文件列表:");
    Serial.println("========================================");
    int totalFiles = 0;
    size_t totalBytes = 0;
    while (File file = root.openNextFile()) {
        String name = normPath(file.name());   // [v10.5] 规范化
        size_t size = file.size();
        totalFiles++;
        totalBytes += size;
        if (name.startsWith("/pop_gen_")) Serial.printf("  🧬 %s (%d bytes)\n", name.c_str(), size);
        else if (name.startsWith("/gen_")) Serial.printf("  🧬 %s (%d bytes)\n", name.c_str(), size);
        else if (name.startsWith("/frm_")) {
            int frames = (size - sizeof(FileHeader)) / sizeof(CompressedFrameEntry);
            Serial.printf("  📹 %s (%d bytes, %d frames)\n", name.c_str(), size, frames);
        }
        else if (name.startsWith("/chaos_")) Serial.printf("  🌪️ %s (%d bytes)\n", name.c_str(), size);
        else if (name == "/oe_history.csv") Serial.printf("  📋 %s (%d bytes)\n", name.c_str(), size);
        else if (name == "/chaos_history.csv") Serial.printf("  🌪️ %s (%d bytes)\n", name.c_str(), size);
        else if (name == "/experiment_state.mrk") Serial.printf("  📌 %s (%d bytes)\n", name.c_str(), size);
        else Serial.printf("  📄 %s (%d bytes)\n", name.c_str(), size);
        file.close();
    }
    root.close();
    Serial.println("========================================");
    Serial.printf("总计: %d 个文件, %d bytes (%.2f KB)\n", totalFiles, totalBytes, totalBytes / 1024.0);
    Serial.println("========================================");
}

void printStatus() {
    const char* stateNames[] = {"IDLE", "WALKING", "STUCK", "CHAOS"};
    Gene& g = EvolutionEngine::getCurrentGene();
    Serial.println("========================================");
    Serial.println("📊 系统状态:");
    Serial.printf("  版本: %s\n", FIRMWARE_VERSION);
    Serial.printf("  格式化标志: %s\n", FORMAT_SPIFFS_ON_BOOT ? "⚠️ ENABLED" : "✅ DISABLED");
    Serial.printf("  代数: %d\n", EvolutionEngine::getGeneration());
    Serial.printf("  个体: %d/%d\n", EvolutionEngine::getIndividual() + 1, POPULATION_SIZE);
    Serial.printf("  新奇度: %.4f\n", g.noveltyScore);
    Serial.printf("  规则数: %d\n", g.ruleCount);
    Serial.printf("  混沌超时: %d ms\n", g.chaosTimeoutMs);
    Serial.printf("  状态: %s\n", stateNames[MotorController::getMotorState()]);
    Serial.printf("  距离: %d ticks\n", MotorController::getDistanceTicks());
    Serial.printf("  卡死: %s\n", MotorController::isStuck() ? "⚠️ 是" : "✅ 否");
    Serial.printf("  混沌: %s\n", MotorController::isChaosActive() ? "🔥 激活" : "⏸ 空闲");
    Serial.printf("  混沌触发次数: %d\n", MotorController::getChaosTriggerCount());
    Serial.printf("  SPIFFS: %s\n", RobustStorage::isReady() ? "✅ 可用" : "❌ 不可用");
    Serial.printf("  GeneStorage active: %d\n", GeneStorage::isExperimentActive());
    Serial.printf("  RAM日志: %d帧\n", RAMLogBuffer::getFrameCount());
    Serial.printf("  GPIO%d 物理噪声: %d\n", PIN_NOISE_SOURCE, SensorCalibration::readNoise());
    Serial.println("========================================");
}

void parseFrameLog(int gen, int id) {
    String path = "/frm_" + String(gen) + "_i" + String(id) + ".bin";
    if (!SPIFFS.exists(path)) { Serial.println("❌ 文件不存在: " + path); return; }
    File file = SPIFFS.open(path, FILE_READ);
    if (!file) { Serial.println("❌ 无法打开文件"); return; }
    FileHeader header;
    if (file.read((uint8_t*)&header, sizeof(FileHeader)) != sizeof(FileHeader)) {
        Serial.println("❌ 文件头读取失败");
        file.close();
        return;
    }
    Serial.println("========================================");
    Serial.printf("📹 帧日志: %s\n", path.c_str());
    Serial.printf("Magic: 0x%08X %s\n", header.magic, header.magic == 0x47454E45 ? "✅" : "❌");
    Serial.printf("Version: 0x%04X\n", header.version);
    Serial.printf("CRC: 0x%08X\n", header.crc32);
    Serial.printf("帧数: %d\n", header.frameCount);
    Serial.printf("代数: %d, 个体: %d\n", header.generation, header.individual);
    Serial.println("========================================");
    Serial.println("Time(ms) | SensorL | SensorR | PWM_L | PWM_R | State");
    Serial.println("---------|---------|---------|-------|-------|-------");
    int maxShow = min(50, (int)header.frameCount);
    int pwmL = 0, pwmR = 0;
    for (int i = 0; i < maxShow; i++) {
        CompressedFrameEntry e;
        if (file.read((uint8_t*)&e, sizeof(CompressedFrameEntry)) != sizeof(CompressedFrameEntry)) break;
        pwmL = constrain(pwmL + e.motorLeftPWM, 0, 255);
        pwmR = constrain(pwmR + e.motorRightPWM, 0, 255);
        if (i == 0) { pwmL = e.motorLeftPWM; pwmR = e.motorRightPWM; }
        const char* stateNames[] = {"IDLE", "WALKING", "STUCK", "CHAOS"};
        Serial.printf("%8d | %7d | %7d | %5d | %5d | %s\n",
            e.timestamp_ms, e.sensorLeft, e.sensorRight,
            pwmL, pwmR, stateNames[e.state & 0x07]);
    }
    if (header.frameCount > maxShow) Serial.printf("... (省略 %d 帧)\n", header.frameCount - maxShow);
    file.close();
    Serial.println("========================================");
}

void parsePopulation(int gen) {
    String path = "/pop_gen_" + String(gen) + ".bin";
    if (!SPIFFS.exists(path)) { Serial.println("❌ 文件不存在: " + path); return; }
    File file = SPIFFS.open(path, FILE_READ);
    if (!file) { Serial.println("❌ 无法打开文件"); return; }
    Serial.println("========================================");
    Serial.printf("🧬 种群快照: %s\n", path.c_str());
    Serial.println("========================================");
    uint32_t magic, expId, generation;
    uint16_t version, popSize;
    file.read((uint8_t*)&magic, 4);
    file.read((uint8_t*)&version, 2);
    file.read((uint8_t*)&popSize, 2);
    file.read((uint8_t*)&generation, 4);
    file.read((uint8_t*)&expId, 4);
    Serial.printf("Magic: 0x%08X (%s)\n", magic, magic == 0x47454E45 ? "✅ GENE" : "❌ 无效");
    Serial.printf("Version: 0x%04X\n", version);
    Serial.printf("Population: %d\n", popSize);
    Serial.printf("Generation: %d\n", generation);
    Serial.printf("Experiment ID: %d\n\n", expId);
    const char* condNames[] = {"L", "R", "BOTH", "ANY", "DIST", "TIME", "IDLE", "ALWAYS"};
    for (int i = 0; i < popSize && i < 16; i++) {
        uint8_t ruleCount;
        file.read(&ruleCount, 1);
        Serial.printf("--- Individual %d (%d rules) ---\n", i, ruleCount);
        for (int j = 0; j < ruleCount && j < MAX_RULES; j++) {
            BehaviorRule r;
            file.read((uint8_t*)&r, sizeof(BehaviorRule));
            Serial.printf("  [%d] %s val=%d op=%d L=%d R=%d dur=%d next=%d\n",
                j, condNames[r.condType], r.condValue, r.condOp,
                r.motorL, r.motorR, r.durationMs, r.nextRule);
        }
        file.seek(file.position() + 12 + 10 + 1 + 1 + 1);
        Serial.println();
    }
    file.close();
    Serial.println("========================================");
}

void parseChaosRecord(int gen, int id) {
    String path = "/chaos_g" + String(gen) + "_i" + String(id) + ".csv";
    if (!SPIFFS.exists(path)) { Serial.println("❌ 文件不存在: " + path); return; }
    File file = SPIFFS.open(path, FILE_READ);
    if (!file) { Serial.println("❌ 无法打开文件"); return; }
    Serial.println("========================================");
    Serial.printf("🌪️ 混沌记录: %s\n", path.c_str());
    Serial.println("========================================");
    while (file.available()) Serial.write(file.read());
    file.close();
    Serial.println("========================================");
}

void parseAllIndividuals(int gen) {
    Serial.println("========================================");
    Serial.printf("📊 第 %d 代 所有个体数据汇总\n", gen);
    Serial.println("========================================");
    Serial.println("ID | 规则数 | 新奇度 | 混沌次数 | 混沌超时 | 左轮速度 | 右轮速度 | 评价");
    Serial.println("---|--------|--------|---------|---------|---------|---------|------");
    for (int id = 0; id < POPULATION_SIZE; id++) {
        String genePath = "/gen_" + String(gen) + "_id_" + String(id) + ".csv";
        if (!SPIFFS.exists(genePath)) {
            Serial.printf("%2d | 无数据\n", id);
            continue;
        }
        int chaosCount = 0;
        float avgL = 0, avgR = 0;
        int ruleCount = 0;
        float novelty = 0;
        uint16_t chaosTimeoutMs = 1300;
        File geneFile = SPIFFS.open(genePath, FILE_READ);
        if (geneFile) {
            String content = geneFile.readString();
            geneFile.close();
            int lines = 0;
            for (int i = 0; i < content.length(); i++) if (content[i] == '\n') lines++;
            ruleCount = lines - 1;
            if (ruleCount < 0) ruleCount = 0;
            int chaosTimeoutPos = content.indexOf("chaosTimeoutMs=");
            if (chaosTimeoutPos > 0) {
                int endPos = content.indexOf(' ', chaosTimeoutPos);
                if (endPos < 0) endPos = content.indexOf('\n', chaosTimeoutPos);
                if (endPos < 0) endPos = content.length();
                chaosTimeoutMs = content.substring(chaosTimeoutPos + 15, endPos).toInt();
            }
        }
        String chaosPath = "/chaos_g" + String(gen) + "_i" + String(id) + ".csv";
        if (SPIFFS.exists(chaosPath)) {
            File chaosFile = SPIFFS.open(chaosPath, FILE_READ);
            if (chaosFile) {
                String content = chaosFile.readString();
                chaosFile.close();
                int lastNewline = content.lastIndexOf('\n');
                int prevNewline = content.lastIndexOf('\n', lastNewline - 1);
                if (lastNewline > 0 && prevNewline >= 0) {
                    String lastLine = content.substring(prevNewline + 1, lastNewline);
                    int fieldIdx = 0, start = 0, chaosCountVal = 0;
                    for (int i = 0; i < lastLine.length(); i++) {
                        if (lastLine[i] == ',') {
                            fieldIdx++;
                            if (fieldIdx == 3) chaosCountVal = lastLine.substring(start, i).toInt();
                            else if (fieldIdx == 12) avgL = lastLine.substring(start, i).toFloat();
                            else if (fieldIdx == 13) { avgR = lastLine.substring(start, i).toFloat(); break; }
                            start = i + 1;
                        }
                    }
                    chaosCount = chaosCountVal;
                }
            }
        }
        String eval;
        if (chaosCount >= 3) eval = "✅ 3轮混沌";
        else if (chaosCount >= 2) eval = "⚠️ 2轮混沌";
        else if (chaosCount >= 1) eval = "⚠️ 1轮混沌";
        else eval = "💀 无混沌";
        Serial.printf("%2d | %6d | %6.3f | %7d | %7d | %7.1f | %7.1f | %s\n",
            id, ruleCount, novelty, chaosCount, chaosTimeoutMs, avgL, avgR, eval.c_str());
    }
    Serial.println("========================================");
}

void extractRamLog(int start, int end) {
    Serial.println(RAMLogBuffer::extractFrames(start, end));
}

void showStorageStatus() {
    Serial.println(RollingStorage::getStorageStatus());
}

/*
* ================================================================
*                    v10.5-SPIFFSNormFix 修改日志
* ================================================================
*
* 版本: v10.5-SPIFFSNormFix
* 日期: 2026-09-13
* 前身: v10.4-RecoveryFix
* 目标: 在 v10.4 恢复链路修复的基础上, 修复 Arduino-ESP32 Core
*       2.3.10 环境下 File.name() 返回不带前导斜杠导致的所有
*       目录扫描函数失效问题。
*
* ================================================================
* 【核心根因】
* ================================================================
* Arduino-ESP32 Core 从 2.0.6 起, File.name() 返回的文件名不带
* 前导斜杠 (如 "pop_gen_1.bin" 而非 "/pop_gen_1.bin")。
* 而 v9.x~v10.4 代码大量使用 name.startsWith("/pop_gen_") 这类
* 带斜杠的前缀判断, 导致所有目录扫描函数在 Core 2.3.10 上
* 永远返回空。
*
* 【影响范围 (v10.4 及之前)】
*   - getStoredGenerations()     返回空 → population_snapshot.csv 空表
*   - findLatestGeneration()     返回 0  → 断电恢复失败回第 1 代
*   - cleanExpiredFrameLogs()    无法删除旧帧日志 → SPIFFS 撑满
*   - deleteGeneration()         无法删除旧代 → 存储无法清理
*   - TieredStorageManager 所有目录扫描失效 → 分级存储失效
*   - hasAnyPopulation 检查失效 → v10.4 的保护不生效
*   - resetToGeneration1() 无法删除旧种群
*   - setup() 冗余文件清理失效
*   - listSPIFFSFiles() 全部显示 📄
*   - /list/files 端点返回的文件名不含斜杠
*
* ================================================================
* 【v10.5 修改清单 — 共 13 处】
* ================================================================
* [1]  RollingStorage::getStoredGenerations()       File.name() 经 normPath()
* [2]  RollingStorage::cleanExpiredFrameLogs()      File.name() 经 normPath()
* [3]  RollingStorage::deleteGeneration()           File.name() 经 normPath() (2 处)
* [4]  GeneStorage::findLatestGeneration()          File.name() 经 normPath()
* [5]  TieredStorageManager::cleanL3AndL4()         File.name() 经 normPath()
* [6]  TieredStorageManager::getPopulationGenerations() File.name() 经 normPath()
* [7]  TieredStorageManager::getCleanPreview()      File.name() 经 normPath()
* [8]  TieredStorageManager::getStorageStats()      File.name() 经 normPath()
* [9]  EvolutionEngine::initImpl() hasAnyPopulation File.name() 经 normPath()
* [10] EvolutionEngine::resetToGeneration1()        File.name() 经 normPath()
* [11] setup() 冗余文件清理块                        File.name() 经 normPath()
* [12] listSPIFFSFiles()                            File.name() 经 normPath()
* [13] CarWebServer::init() /list/files 端点        File.name() 经 normPath()
*
* 另: GeneStorage::clearAllExperimentData() 和 CarWebServer::handleClean()
*     两处额外的目录扫描也一并经 normPath() 规范化。
*
* ================================================================
* 【规范固化】
* ================================================================
* 见文件顶端 "SPIFFS 路径规范化 强制规范" 区块。
* 规范 S1~S5 必须在所有后继版本中遵循。
* normPath() 定义在 Logger 类之后, 是所有目录扫描的唯一规范化入口。
*
* ================================================================
* 【上板观察】
* ================================================================
* - 串口 ls 输出: 🧬 /pop_gen_N.bin (带图标, 带斜杠)
* - 串口 storage 输出: Stored generations: N (N > 0)
* - 断电重启: 恢复到原代数, 不回第 1 代
* - /download/population: CSV 有数据行
* - DownParse 下载 population_summary.csv: 行数 = 代数 × 10
* - 串口 reset 后 ls: 只剩 pop_gen_1.bin
* - Web UI "清理种群 (L3)": 只删旧代, 保留当前代
*
* ================================================================
* 修改日期: 2026-09-16
* 修改人: 系统优化 (基于代码诊断分析 + 用户审计)
* ================================================================
*/
/*
* ================================================================
*                    代码迭代修改日志
*              OEE 进化系统 v10.12-GenLimit — SPIFFS代际清理 + 极简原则
* ================================================================
*
* 版本: v10.12-GenLimit SPIFFS代际清理 + 最低保留7代 + 存储预检
* 日期: 2026-09-16
* 前身: v10.11-Gen10Fix
* 目标: 基于v10.11代码，实现SPIFFS存储空间按代际自动清理(最低保留7代)
*
* ================================================================
* 【v10.8 相对 v10.7 的全部改动 — 共 7 项】
* ================================================================
*
* ────────────────────────────────────────────────────────────────
* [V8-1] sensorAsymmetry 分母保护修复 (诊断修改1)
* ────────────────────────────────────────────────────────────────
* 问题: max(L+R, 1.0f) 分母保护在 L+R<0 时失效，导致 noveltyScore 爆炸
* 修复: 改用 fabsf(desc.leftSensorMean) + fabsf(desc.rightSensorMean)
* 风险: 低风险
*
* ────────────────────────────────────────────────────────────────
* [V8-2] chaos_history.csv 补 2 列 (诊断修改2)
* ────────────────────────────────────────────────────────────────
* 问题: snprintf 格式字符串只有 18 个 % 占位符但传了 20 个参数
* 修复: 格式字符串补全为 20 个占位符
* 风险: 低风险
*
* ────────────────────────────────────────────────────────────────
* [V8-3] chaosTotalDuration 改用 micros() (诊断修改3)
* ────────────────────────────────────────────────────────────────
* 问题: millis() 精度 1ms，<1ms 的混沌事件记录为 0
* 修复: 新增 chaosStartMicros 变量，改用 micros() 计时
* 风险: 低风险
*
* ────────────────────────────────────────────────────────────────
* [V8-4] BehaviorDescriptor 加混沌特征 (诊断修改4)
* ────────────────────────────────────────────────────────────────
* 问题: 进化算法 novelty 计算缺少混沌行为特征维度
* 修复: 结构体扩展 4 个 float 字段(48→64字节)，更新 distance/normalize/updateMaxValues/extractFromFrameLog
* 二进制版本: 0x0009→0x000A，兼容旧文件反序列化
* 风险: 中风险(但兼容性处理到位)
*
* ────────────────────────────────────────────────────────────────
* [V8-5] GeneStorage::FILE_VERSION 升级 (审计P0致命修复)
* ────────────────────────────────────────────────────────────────
* 问题: FILE_VERSION 仍为 0x0009，但 deserializeIndividual() 检查 >=0x000A 走新格式
* 后果: 保存写 64 字节但加载只读 48 字节 → 种群文件解析完全错乱
* 修复: 0x0009 → 0x000A
* 风险: 致命(不修则种群文件不可用)
*
* ────────────────────────────────────────────────────────────────
* [V8-6] handleDownloadChaos() fallback header 补全 (审计P1修复)
* ────────────────────────────────────────────────────────────────
* 问题: fallback header 只有 18 列，缺少 chaosExitReason, chaosInterruptedCount
* 后果: CSV 文件为空时下载得到 18 列表头，与实际 20 列不一致
* 修复: 补全为 20 列
* 风险: 低(仅影响空文件下载场景)
*
* ────────────────────────────────────────────────────────────────
* [V8-7] FIRMWARE_VERSION 版本更新 + BINARY_FORMAT_VERSION 清理 (审计P1/P2修复)
* ────────────────────────────────────────────────────────────────
* 问题1: FIRMWARE_VERSION 仍为 "v10.7-WebDiag"，日志无法区分版本
* 修复1: 更新为 "v10.8-ChaosFeature"
* 问题2: BINARY_FORMAT_VERSION 宏定义从未被引用，造成维护混淆
* 修复2: 注释掉该宏定义
* 风险: 低
*
* ================================================================
* 修改日期: 2026-09-16
* 修改人: 系统优化 (基于代码诊断分析 + 用户审计 + 混沌特征反馈)
* ================================================================
*
* ────────────────────────────────────────────────────────────────
* [V9-1] normalize() 混沌特征零值/NaN 保护 (反馈方案1 P0)
* ────────────────────────────────────────────────────────────────
* 问题: normalize() 对 maxValues=0 的混沌字段没有保护，
*       当 archive 首次遇到混沌字段时 maxValues=0，不归一化，
*       导致 chaosPwmVariance=8355 等极端值直接进入 distance()
* 修复: 对 4 个混沌字段增加 !isnan() 保护，未归一化时置 0
* 风险: 低(修复 noveltyScore 爆炸根因)
*
* ────────────────────────────────────────────────────────────────
* [V9-2] updateMaxValues() 混沌特征 NaN 保护 (反馈方案2 P0)
* ────────────────────────────────────────────────────────────────
* 问题: updateMaxValues() 未检查 NaN，NaN 可能污染 maxValues
* 修复: 对 4 个混沌字段增加 isnan() 检查，NaN 值不参与最大值更新
* 风险: 低
*
* ────────────────────────────────────────────────────────────────
* [V9-3] extractFromFrameLog() chaosPwmVariance 范围限制 (反馈方案3 P1)
* ────────────────────────────────────────────────────────────────
* 问题: chaosPwmVariance 可能产生异常大值(远超 255²)
* 修复: 上限钳位到 65025.0f (255²)，防止异常值主导 distance
* 风险: 低
*
* ────────────────────────────────────────────────────────────────
* [V9-4] distance() 混沌特征加权 (反馈方案4 P1)
* ────────────────────────────────────────────────────────────────
* 问题: 混沌特征(尤其 chaosPwmVariance)量级远大于其他字段，
*       即使归一化后也可能主导 distance 计算
* 修复: 引入 CHAOS_WEIGHT=0.5f 对 4 个混沌特征维度加权
* 可调参数: CHAOS_WEIGHT 从 0.1 到 1.0 试，找到平衡点
* 风险: 低(需实验验证权重选择)
*
* ================================================================
*/

/*
* ================================================================
*                    代码迭代修改日志
*              OEE 进化系统 v10.7-WebDiag加软件高通滤波
* ================================================================
*
* 版本: v10.7-WebDiag GPIO 噪声源引脚迁移 + ADC 衰减配置 + 混沌快照诊断字段
* 日期: 2026-09-13
* 前身: v10.5-SPIFFSNormFix
* 目标: 在 v10.5 全部修复的基础上, 解决"混沌模式下电机 PWM 恒为 -128
*       无法反映物理噪声驱动"的问题; 并让 chaos_snaps 里的数据能够
*       自证"PWM 恒 -128"是真饱和还是派生逻辑错误。
*
* ================================================================
* 【v10.6 相对 v10.5 的全部改动 — 共 6 项】
* ================================================================
*
* ────────────────────────────────────────────────────────────────
* [G1] 噪声源引脚从 GPIO1 迁移到 GPIO6
* ────────────────────────────────────────────────────────────────
* 现象: 混沌模式下 chaos_snaps 里 motorLeftPWM/motorRightPWM
*       恒为 -128, 即使 GPIO 读数一直在无规律跳变。
* 根因: GPIO1 在 ESP32-S3 上是 USB D+, 连 USB 时被 USB CDC 外设
*       占用, analogRead(1) 读数失真 (恒定在 56~115)。
*       通过独立 Web 诊断程序确认: 拔掉 USB 后 GPIO1 跨度 1294,
*       不拔 USB 时跨度只有 2。
* 修复: 将噪声源引脚改为 GPIO6。
*       GPIO6 与 USB CDC 无关, 无论 USB 是否连接都能读热噪声。
* 影响: 混沌模式下 readPhysicalNoise() 读 GPIO6。
*       开发调试时不用拔 USB 数据线。
*       论文表述从 "GPIO1 悬空 ADC" 改为 "GPIO6 悬空 ADC"。
* 位置: 文件顶部 #define PIN_NOISE_SOURCE
*
* ────────────────────────────────────────────────────────────────
* [G2] setup() 显式配置 ADC 分辨率与衰减
* ────────────────────────────────────────────────────────────────
* 现象: 即使改到 GPIO6, 混沌快照里的 PWM 仍可能饱和。
* 根因: Arduino-ESP32 的 analogRead() 默认衰减是 ADC_0db
*       (0~1.1V 满量程)。GPIO6 悬空电压约 1.6V, 在 ADC_0db 下
*       会饱和到 4095, 导致 noise = +1.0, pwmL = +255,
*       constrain 后恒为 +127 (而不是期望的跳动)。
*       而 Web 诊断程序中显式配置了 ADC_11db (满量程 3.3V),
*       所以读数正常。
* 修复: 在 setup() 中显式配置:
*       analogReadResolution(12);                            // 12 位
*       analogSetPinAttenuation(PIN_NOISE_SOURCE, ADC_11db); // 满量程 3.3V
*       与 Web 诊断程序的 ADC 配置对齐。
* 影响: GPIO6 悬空电压 1.6V → ADC ≈ 2048 → noise ≈ 0,
*       PWM 在 -128 ~ +127 之间随机跳变。
* 位置: setup() 里的 [6] 物理噪声源引脚 块
*
* ────────────────────────────────────────────────────────────────
* [G3] ChaosSnapshotEntry 结构体新增 rawNoiseL / rawNoiseR 字段
* ────────────────────────────────────────────────────────────────
* 现象: chaos_snaps 里 PWM 恒为 -128, 但 GPIO 读数在跳,
*       无法判断 -128 是 "噪声饱和" 还是 "派生逻辑错误"。
* 根因: v10.5 的 ChaosSnapshotEntry 只记录派生后的 motorLeftPWM
*       / motorRightPWM, 不记录原始 analogRead 值。一旦 PWM 落在
*       constrain 的饱和区, 就无法反推 readPhysicalNoise() 的
*       输入 raw 值。
* 修复: ChaosSnapshotEntry 追加两个 int16_t 字段:
*       - rawNoiseL: 快照时刻的原始 ADC 采样值 1
*       - rawNoiseR: 快照时刻的原始 ADC 采样值 2
*       两次独立采样可观察相邻样本的跳变情况。
* 影响: ChaosSnapshotEntry 大小从 12 字节升到 16 字节。
*       二进制版本从 v1 升到 v2 (见 G5)。
*       DownParse 需同步解析 (按 header.version 分流)。
* 位置: struct ChaosSnapshotEntry
*
* ────────────────────────────────────────────────────────────────
* [G4] logChaosSnapshot() 里立即采样 raw 值
* ────────────────────────────────────────────────────────────────
* 修复: 在 logChaosSnapshot() 内部, 填充 snap.rawNoiseL/R 时
*       调用两次 analogRead(PIN_NOISE_SOURCE):
*       snap.rawNoiseL = (int16_t)analogRead(PIN_NOISE_SOURCE);
*       snap.rawNoiseR = (int16_t)analogRead(PIN_NOISE_SOURCE);
*       确保原始值与派生 PWM 在同一时刻记录。
* 位置: MotorController::logChaosSnapshot()
*
* ────────────────────────────────────────────────────────────────
* [G5] ChaosSnapshotHeader 版本号从 0x0001 升到 0x0002
* ────────────────────────────────────────────────────────────────
* 修复: saveChaosSnapshotsToSPIFFS() 里:
*       header.version = 0x0002;  // 每条快照新增 4 字节 raw
* 影响: DownParse 按 version 分流:
*       - version <  0x0002: 按 V1 格式解析 (12 字节/条)
*       - version >= 0x0002: 按 V2 格式解析 (16 字节/条)
*       旧文件和新文件可混存, DownParse 兼容处理。
* 位置: MotorController::saveChaosSnapshotsToSPIFFS()
*
* ────────────────────────────────────────────────────────────────
* [G6] 混沌快照调用点补真实传感器值 (logChaosSnapshot 调用)
* ────────────────────────────────────────────────────────────────
* 现象: chaos_snaps 里 sensorLeft / sensorRight 恒为 0。
* 根因: v10.5 updateChaos() 调用 logChaosSnapshot(0, 0, ...)
*       前两个参数硬编码为 0 (设计选择: 混沌不读传感器)。
* 修复: 改为 logChaosSnapshot(leftSensorRaw, rightSensorRaw,
*                              snapL, snapR);
*       leftSensorRaw / rightSensorRaw 在 updateChaos() 里已被
*       readLeft() / readRight() 刷新。
* 影响: chaos_snaps 里 sensorLeft / sensorRight 从恒 0
*       变成真实传感器差值。与 frm_*.csv 里对应帧的 sensor 值
*       可以交叉验证。
* 位置: MotorController::updateChaos()
*
* ────────────────────────────────────────────────────────────────
* [版本号变更]
* ────────────────────────────────────────────────────────────────
* FIRMWARE_VERSION: "v10.5-SPIFFSNormFix" → "v10.6-WebDiag"
* 二进制格式版本: 0x0009 保持不变 (pop_gen_N.bin 兼容)
* ChaosSnapshotEntry: 12 → 16 字节 (v1 → v2)
*
* ────────────────────────────────────────────────────────────────
* [数据兼容性]
* ────────────────────────────────────────────────────────────────
* - pop_gen_N.bin:        格式不变, v10.2 ~ v10.5 数据可直接加载
* - frm_N_iM.bin:         格式不变
* - novelty_archive.bin:  格式不变
* - nova_gen_N.bin:       格式不变
* - oe_history.csv:       格式不变
* - chaos_history.csv:    格式不变
* - chaos_snaps_*.bin:    ⚠️ 格式升级 (v1 → v2)
*                          旧文件 (v1) 仍保留, DownParse 兼容解析
*                          新文件 (v2) 含 rawNoiseL / rawNoiseR
*
* ────────────────────────────────────────────────────────────────
* [上板观察]
* ────────────────────────────────────────────────────────────────
* - setup() 打印结构体大小:
*     ChaosSnapshotEntry = 16
* - setup() 打印 "[6] 物理噪声源引脚" 后应有:
*     analogReadResolution(12)
*     analogSetPinAttenuation(6, ADC_11db)
* - 串口/Web 显示 GPIO6 物理噪声值应在 2000 附近
* - 混沌触发时, chaos_snaps 里:
*     - sensorLeft/sensorRight 是真实值 (非 0)
*     - rawNoiseL/rawNoiseR 在 2000 附近跳
*     - motorLeftPWM/motorRightPWM 在 -128 ~ +127 之间跳
*     - durationMs 有变化 (10/20/30 不等)
* - 断电重启: 恢复到原代数 (v10.5 的 normPath 修复保持有效)
* - 串口 ls: 显示图标 (🧬/📹/🌪️/📌), 路径带斜杠
*
* ────────────────────────────────────────────────────────────────
* [待论文侧同步]
* ────────────────────────────────────────────────────────────────
* - 论文 3.4.1 "GPIO1 悬空 ADC" → "GPIO6 悬空 ADC"
* - 论文 4.5.2 "GPIO1" → "GPIO6"
* - 附录 A 引脚定义表: 噪声源引脚从 GPIO1 改为 GPIO6
* - ChaosSnapshotEntry 数据字典加两个字段:
*     rawNoiseL, rawNoiseR
* - v10.6 未改动的已知问题 (沿用 v10.5):
*   · CHAOS_RECOVER_STABLE_FRAMES: 代码 20
*   · chaosForceTimeoutMs: 论文用 chaosDurationMax, 名称待统一
*   · 论文 2.3.5 的 11 个参数名称需与代码字段对齐
*
* ================================================================
* 【核心设计原则 (保持 v10.2 ~ v10.5 不变)】
* ================================================================
* 元算法层 (Gene/EvolutionEngine):
*   保留 PhysicalRandom (esp_random)
*   理由: 片内 TRNG 属物理熵源, 速度快, 不干扰实时控制
* 具身算法层 (MotorController::updateChaos):
*   使用 GPIO6 悬空 ADC 热噪声
*   理由: 论文 3.4.1/4.5.2 明确要求 "物理噪声直接驱动电机"
*
* ================================================================
* 【SPIFFS 路径规范化 (v10.5 保持)】
* ================================================================
* 见 v10.5 文件顶部 "SPIFFS 路径规范化 强制规范" 区块。
* v10.6 未改动 v10.5 的 13 处 normPath() 调用。
*
* ================================================================
* 【修改清单速查】
* ================================================================
* ┌────┬──────────────────────────────────────┬──────────────┬────────┐
* │ ID │ 修改位置                             │ 修改类型     │ 严重度 │
* ├────┼──────────────────────────────────────┼──────────────┼────────┤
* │ G1 │ #define PIN_NOISE_SOURCE             │ 引脚迁移     │ 致命   │
* │ G2 │ setup() [6] 物理噪声源引脚           │ 加 ADC 配置  │ 致命   │
* │ G3 │ struct ChaosSnapshotEntry            │ 加 2 字段    │ 诊断   │
* │ G4 │ MotorController::logChaosSnapshot()  │ 采 raw 值    │ 诊断   │
* │ G5 │ saveChaosSnapshotsToSPIFFS()         │ 版本 1→2     │ 兼容   │
* │ G6 │ MotorController::updateChaos()       │ 补 sensor 值 │ 数据质量│
* └────┴──────────────────────────────────────┴──────────────┴────────┘
*
* ================================================================
* 【验证要点】
* ================================================================
* 烧录 v10.6 后, 跑一次实验, 下载 chaos_snaps, 检查:
*
*   ┌────────────────────────────────┬────────────────────────────────┐
*   │ rawNoiseL / rawNoiseR 的表现    │ 结论                           │
*   ├────────────────────────────────┼────────────────────────────────┤
*   │ 2000 附近大幅跳变                │ GPIO 正常, 修复成功           │
*   │ 恒 < 1020                       │ GPIO6 在混沌期间被钳位         │
*   │ 恒 0 或 4095                    │ GPIO6 硬故障                   │
*   │ 与 PWM 值不匹配 (派生错位)       │ 派生逻辑 bug, 需排查           │
*   └────────────────────────────────┴────────────────────────────────┘
*
* ================================================================
* 修改日期: 2026-09-13
* 修改人: 系统优化
* ================================================================
*/
/*
* ================================================================
*                    代码迭代修改日志
*              OEE 进化系统 v10.11-Gen10Fix — COND_IDLE规则修复 + oe_history注释
* ================================================================
*
* 版本: v10.11-Gen10Fix 条件规则condValue范围修复 + oe_history表头注释
* 日期: 2026-09-16
* 前身: v10.9-ChaosNormFix
* 目标: 基于v10.9代码，应用数据分析诊断修复(问题二、三)及问题一诊断日志
*
* ================================================================
* 【v10.10 相对 v10.9 的全部改动 — 共 3 项】
* ================================================================
*
* ────────────────────────────────────────────────────────────────
* [V10-1] BehaviorRule::randomize() condValue 根据 condType 生成合理范围 (问题三修复)
* ────────────────────────────────────────────────────────────────
* 问题: condValue 统一随机生成 -3000~3000，导致 COND_IDLE 规则几乎永不匹配
*       (evaluateCondition 返回 0/1，但 condValue 可能是 768，OP_EQUAL 容差10无法匹配)
* 修复: 根据 condType 生成对应范围的 condValue:
*       - COND_IDLE: 0~1 (布尔值)
*       - COND_TIME: 0~TEST_DURATION_MS
*       - COND_SENSOR_LEFT/RIGHT/ANY: 0~4096
*       - COND_SENSOR_BOTH: 0~8192
*       - COND_DISTANCE: -3000~3000
*       - COND_ALWAYS: 0
* 风险: 低(修复死规则，提升进化质量)
*
* ────────────────────────────────────────────────────────────────
* [V10-2] BehaviorRule::clamp() condValue 根据 condType 限制范围 (问题三修复)
* ────────────────────────────────────────────────────────────────
* 问题: clamp() 统一将 condValue 限制在 -3000~3000，对 COND_IDLE 无效
* 修复: 根据 condType 分别限制:
*       - COND_IDLE: 0~1
*       - COND_TIME: 0~TEST_DURATION_MS
*       - COND_SENSOR_LEFT/RIGHT/ANY: 0~4095
*       - COND_SENSOR_BOTH: 0~8190
*       - COND_DISTANCE: -3000~3000
*       - COND_ALWAYS: 0
* 风险: 低(确保突变后 condValue 仍在合理范围)
*
* ────────────────────────────────────────────────────────────────
* [V10-3] oe_history.csv 表头添加注释说明 (问题二修复 - 方案B)
* ────────────────────────────────────────────────────────────────
* 问题: HistoryRecord 结构体缺少 individual 字段，数据分析时无法直接对应个体
* 修复: 在 CSV 表头添加注释行 "# Note: rows are ordered by individual index within each generation"
*       说明行按代内个体索引顺序排列，可通过 timestamp 顺序推断 individual
* 影响: 下游分析脚本需忽略以 # 开头的注释行
* 风险: 低(仅添加注释，不改变数据格式)
*
* ────────────────────────────────────────────────────────────────
* [V10-4] handleDownloadPopulation() 添加诊断日志 (问题一辅助排查)
* ────────────────────────────────────────────────────────────────
* 问题: population_snapshot.csv 中仅精英个体有数据，其他个体全 0
* 修复: 导出时打印各代各个体有效数据计数，辅助定位是保存时机问题还是文件损坏
* 风险: 低(仅日志输出)
*
* ================================================================
* 修改日期: 2026-09-16
* 修改人: 系统优化 (基于数据分析诊断)
* ================================================================
*/
/*
* ================================================================
*              OEE 进化系统 v10.11-Gen10Fix — 第10代停止修复
* ================================================================
*
* 版本: v10.11-Gen10Fix 进化过渡存储失败自动恢复 + 诊断增强
* 日期: 2026-09-16
* 前身: v10.10-CondFix
* 目标: 修复"实验跑到第10代第一个个体就停止"的问题
*
* ================================================================
* 【v10.11 相对 v10.10 的全部改动 — 共 3 项】
* ================================================================
*
* ────────────────────────────────────────────────────────────────
* [V11-1] nextIndividualImpl() 进化过渡存储失败自动恢复 (核心修复)
* ────────────────────────────────────────────────────────────────
* 问题: 当实验进行到第10代(或任何代际过渡)时，SPIFFS存储空间可能
*       已接近耗尽。savePopulationForGeneration(newGen, ...) 调用
*       可能因空间不足而失败，导致：
*       1. 代码回退到上一代
*       2. startCurrentTestImpl() 检查上一代种群文件是否存在
*       3. 但该文件可能在 ensureSpace→cleanOldPopulations 清理中被误删
*       4. 文件不存在 → startCurrentTestImpl 直接 return → 测试不启动
*       5. 实验"卡住"在该代第一个个体，不再前进
* 修复:
*   1. 在进化过渡前预检存储空间，如果不足则主动清理旧数据
*   2. savePopulationForGeneration 失败时，不再简单回退，而是：
*      a. 强制删除最旧代数释放空间
*      b. 重试保存
*      c. 仍失败则删除所有旧种群文件(保留当前代)后重试
*      d. 仍失败则标记 experimentReady=false 并输出致命错误
*   3. 每次失败都记录到 saveFailCount，超过阈值触发紧急清理
* 风险: 低(仅在空间不足时触发，正常实验不受影响)
*
* ────────────────────────────────────────────────────────────────
* [V11-2] startCurrentTestImpl() 增强诊断与恢复
* ────────────────────────────────────────────────────────────────
* 问题: 当种群文件不存在或加载失败时，函数直接 return，串口无任何
*       有用信息，用户无法诊断问题根因
* 修复:
*   1. 文件不存在时，列出 SPIFFS 中所有 pop_gen_* 文件，帮助用户
*      确认哪些代际文件实际存在
*   2. 加载失败时，检查文件大小和文件头，输出诊断信息
*   3. 任何拒绝启动的情况都输出清晰的错误原因
* 风险: 低(仅增强日志输出)
*
* ────────────────────────────────────────────────────────────────
* [V11-3] ensureSpace() 增加存储空间健康度日志
* ────────────────────────────────────────────────────────────────
* 问题: ensureSpace 在空间不足时只输出警告，用户无法从串口日志
*       直观了解存储空间使用情况
* 修复:
*   1. 每次 ensureSpace 调用时输出当前 SPIFFS 总空间、已用空间、
*      可用空间和所需空间
*   2. 当可用空间低于 10% 时输出红色警告
* 风险: 低(仅日志输出)
*
* ================================================================
* 修改日期: 2026-09-16
* 修改人: 系统优化 (基于用户反馈: 第10代第一个个体实验停止)
* ================================================================
*/

/*
* ================================================================
*                    代码迭代修改日志
*              OEE 进化系统 v10.6-WebDiag
* ================================================================
*
* 版本: v10.6-WebDiag 论文修改稿固件同步修正 + 审计断裂点修复
* 日期: 2026-09-14
* 前身: v10.5-SPIFFSNormFix
* 目标: 根据论文修改稿(D1-D7)和混沌机制链路依赖审计报告, 对固件代码
*       进行同步修正, 实现论文设计目的, 保证逻辑内洽.
*
* ================================================================
* 【v10.6 相对 v10.5 的全部改动 - 论文修改稿同步修正】
* ================================================================
*
* ────────────────────────────────────────────────────────────────
* [P1] FIRMWARE_VERSION 更新为 v10.6-WebDiag
* ────────────────────────────────────────────────────────────────
* 对应论文修改: D5 (论文6.2.1节参数从30秒修正为20秒)
* 代码现状: TEST_DURATION_MS 已为 20000 (20秒), 与修正后论文一致
* 修改: 更新版本号标识, 保持代码与论文版本同步
*
* ────────────────────────────────────────────────────────────────
* [P2] GPIO引脚日志引用统一修正 (GPIO1 -> GPIO6/PIN_NOISE_SOURCE)
* ────────────────────────────────────────────────────────────────
* 对应论文修改: D1/D2 论文中GPIO引脚描述同步
* 根因: v10.6之前版本将噪声源从GPIO1迁移到GPIO6, 但日志打印语句
*       中仍硬编码"GPIO1"文本, 造成日志与实际硬件不一致
* 修复: 所有日志打印中的GPIO1文本替换为GPIO6或使用PIN_NOISE_SOURCE宏
* 影响: 串口日志/Web诊断显示与实际引脚一致, 便于调试
* 位置: setup()中的噪声源检测日志, startChaos()中的混沌启动日志,
*       以及多个诊断端点
*
* ────────────────────────────────────────────────────────────────
* [P3] ChaoticTestRecord 新增 chaosExitReason 字段 (审计D4修复)
* ────────────────────────────────────────────────────────────────
* 对应论文修改: D4 (chaosSuccess过程指标vs结果指标语义澄清)
* 审计依据: 混沌机制链路依赖审计报告 D4断裂点 + 判定逻辑自洽性审计
* 新增字段:
*   - chaosExitReason: 混沌退出原因编码
*     0 = 编码器稳定脱困 (正常退出, 对应chaosSuccess=1)
*     1 = 超时退出 (chaosForceTimeoutMs达到, chaosSuccess=0)
*     2 = 个体被终止 (碰撞/看门狗等异常终止)
* 影响: CSV输出新增chaosExitReason列, 下游分析可区分退出类型
* 依赖矩阵检查: 仅影响ChaoticTestRecord->CSV输出链路, 不影响
*       状态机控制流和编码器数据流, 不会破坏已有链路
*
* ────────────────────────────────────────────────────────────────
* [P4] ChaoticTestRecord 新增 chaosInterruptedCount 字段 (审计D3修复)
* ────────────────────────────────────────────────────────────────
* 对应论文修改: D3 (chaosTotalDuration仅在正常退出时累加的说明)
* 审计依据: 混沌机制链路依赖审计报告 D3断裂点 + 统计口径自洽性审计
* 新增字段:
*   - chaosInterruptedCount: 混沌被异常中断次数
*     当个体在混沌期间被碰撞终止(非正常退出)时递增
*     与chaosTotalDuration形成互补: 正常退出时interrupted=0,
*     异常终止时interrupted++, 解决"有帧数无时长"的矛盾
* 影响: CSV输出新增chaosInterruptedCount列
* 依赖矩阵检查: 仅影响ChaoticTestRecord->CSV输出链路,
*       不修改现有字段语义, 不破坏已有数据解析
*
* ────────────────────────────────────────────────────────────────
* [P5] MotorController 新增统计字段和getter
* ────────────────────────────────────────────────────────────────
* 对应论文修改: D1 (baselineFrames含IDLE帧的说明)
* 新增字段:
*   - chaosInterruptedCount: 混沌异常中断计数(静态成员)
*   - lastChaosExitReason: 上次混沌退出原因(静态成员)
* 新增getter:
*   - getChaosInterruptedCount()
*   - getChaosExitReason()
* 影响: 为上层(EvolutionEngine)提供访问接口, 写入ChaoticTestRecord
*
* ────────────────────────────────────────────────────────────────
* [数据兼容性]
* ────────────────────────────────────────────────────────────────
* - pop_gen_N.bin:        格式不变
* - frm_N_iM.bin:         格式不变
* - chaos_snaps_*.bin:    格式不变 (v10.6已升级v2)
* - oe_history.csv:       格式不变
* - chaos_history.csv:    ⚠️ 新增2列: chaosExitReason, chaosInterruptedCount
*                          旧解析脚本需同步更新表头读取逻辑
*                          建议在列尾追加, 旧脚本忽略未知列可兼容
*
* ────────────────────────────────────────────────────────────────
* [待论文侧同步]
* ────────────────────────────────────────────────────────────────
* - 论文附录中混沌快照数据字典新增 chaosExitReason, chaosInterruptedCount
* - 论文6.3.3节引用chaosExitReason替代单一chaosSuccess判据
* - 论文数据导出脚本更新chaos_history.csv表头
*
* ================================================================
* 修改日期: 2026-09-14
* 修改人: 系统优化 (基于论文修改稿D1-D7 + 混沌机制链路依赖审计报告)
* ================================================================
*/

/*
* ================================================================
*              ★★★ SPIFFS 路径规范化 强制规范 ★★★
*                    (适用于所有后继版本)
* ================================================================
*
* 【背景事实】
*   Arduino-ESP32 Core 从 2.0.6 起, File.name() 返回的文件名
*   ★不带前导斜杠★。
*
*   实测环境: Arduino-ESP32 Core 2.3.10
*   实测行为:
*     SPIFFS.open("/pop_gen_1.bin", ...) 写入 → 磁盘文件名为
*     "pop_gen_1.bin" (不带斜杠)
*     遍历目录时 File.name() 返回 "pop_gen_1.bin" (不带斜杠)
*
*   历史代码 (v9.x~v10.4) 使用 name.startsWith("/pop_gen_") 判断,
*   在 Core 2.3.10 上永远失败, 导致:
*     - getStoredGenerations() 返回空
*     - findLatestGeneration() 返回 0
*     - 断电恢复失败, 回退到第 1 代
*     - population_snapshot.csv 只有表头
*     - ls 命令全部显示 📄 (无法识别文件类型)
*
* ────────────────────────────────────────────────────────────────
* 【规范 S1】文件名约定
* ────────────────────────────────────────────────────────────────
*   所有 SPIFFS 文件以 "/" 为根目录, 文件名为 "xxx.bin" / "xxx.csv"
*   形式, 磁盘上不含前导斜杠。
*
* ────────────────────────────────────────────────────────────────
* 【规范 S2】读写路径约定
* ────────────────────────────────────────────────────────────────
*   代码中所有"读写具体文件"的调用 (SPIFFS.open / SPIFFS.exists /
*   SPIFFS.remove / SPIFFS.rename / FileUtils::atomicWrite 等)
*   使用★带前导斜杠★的路径, 例如:
*       SPIFFS.open("/pop_gen_1.bin", FILE_READ)
*       SPIFFS.exists("/oe_history.csv")
*   语义为"根目录下的文件"。
*
* ────────────────────────────────────────────────────────────────
* 【规范 S3】目录扫描后的判断约定
* ────────────────────────────────────────────────────────────────
*   代码中所有"从 File.name() 获取文件名用于判断"的地方,
*   ★必须★先经 normPath() 规范化, 然后按带斜杠路径判断:
*
*       String name = normPath(f.name());
*       if (name.startsWith("/pop_gen_")) { ... }
*
*   禁止直接使用 File.name() 的原始返回值做 startsWith 判断。
*
* ────────────────────────────────────────────────────────────────
* 【规范 S4】normPath() 唯一入口
* ────────────────────────────────────────────────────────────────
*   normPath() 是唯一的规范化入口, 定义在文件顶部 (Logger 类之后,
*   任何使用它的类之前)。禁止在其他地方自行拼接斜杠。
*
* ────────────────────────────────────────────────────────────────
* 【规范 S5】新增目录扫描代码时的检查清单
* ────────────────────────────────────────────────────────────────
*   每次新增遍历 SPIFFS 目录的代码, 必须:
*     [ ] File.name() 的返回值经 normPath() 规范化
*     [ ] 前缀判断使用 "/xxx_" 形式 (带斜杠)
*     [ ] 后缀判断使用 ".bin" / ".csv" 形式 (无斜杠, 无空格)
*     [ ] 使用 SPIFFS.remove(path) 时 path 带斜杠
*     [ ] 新增函数在 commit 前全文 grep "f.name()" 确认无遗漏
*
* ================================================================
* 【本规范对应代码修改 (v10.4 → v10.5)】
* ================================================================
*   共 13 处:
*     [1]  RollingStorage::getStoredGenerations()
*     [2]  RollingStorage::cleanExpiredFrameLogs()
*     [3]  RollingStorage::deleteGeneration() (2 个 while 循环)
*     [4]  GeneStorage::findLatestGeneration()
*     [5]  TieredStorageManager::cleanL3AndL4()
*     [6]  TieredStorageManager::getPopulationGenerations()
*     [7]  TieredStorageManager::getCleanPreview()
*     [8]  TieredStorageManager::getStorageStats()
*     [9]  EvolutionEngine::initImpl() 的 hasAnyPopulation 检查
*     [10] EvolutionEngine::resetToGeneration1() 的删除块
*     [11] setup() 的冗余文件清理块
*     [12] listSPIFFSFiles() 串口命令
*     [13] CarWebServer::init() 的 /list/files 端点
*
* ================================================================
* 修改日期: 2026-09-13
* ================================================================
*//*
* ================================================================
*                    代码迭代修改日志
*              OEE 进化系统 v10.4-RecoveryFix
* ================================================================
*
* 版本: v10.4-RecoveryFix 断电恢复链路修复 + 实验就绪状态强校验
* 日期: 2026-09-13
* 前身: v10.3-DataFix
* 目标: 修复"断电后实验回退到第 1 代"与"早期数据存在但 Web 无法
*       正常启动"两条致命链路的根因, 并建立实验就绪状态强校验机制
*
* ================================================================
* 【v10.4 相对 v10.3 的全部改动】
* ================================================================
*
* ────────────────────────────────────────────────────────────────
* [R1] 致命问题 1: EvolutionEngine::initImpl() 静默重建第 1 代
* ────────────────────────────────────────────────────────────────
* 现象: 断电后重新上电, 实验从第 1 代重新开始, 早期 CSV/基因记录/
*       帧日志仍在 SPIFFS 中, 但实验延续点被覆盖
* 根因: initImpl() 在 currentGeneration == 0 时无条件执行
*       currentGeneration = 1 + population[i].init() +
*       savePopulationForGeneration(1) + commitGeneration(1),
*       将"恢复失败"与"全新实验"混为一谈
* 修复:
*   1. initImpl() 在 currentGeneration == 0 时先调用
*      GeneStorage::findLatestGeneration()
*   2. 若 latest > 0, 尝试 loadPopulation(latest);
*      失败则 experimentReady = false, 绝不重建
*   3. 若 latest == 0, 再扫描 SPIFFS 是否存在任意 /pop_gen_* 文件;
*      若存在但不可加载, 同样 experimentReady = false
*   4. 只有确实无任何 /pop_gen_* 文件时, 才允许新建第 1 代
* 影响: SPIFFS 中存在任意 /pop_gen_*.bin 时, 断电后不再回第 1 代
* 位置: EvolutionEngine::initImpl()
*
* ────────────────────────────────────────────────────────────────
* [R2] 致命问题 2: 缺少实验就绪状态强校验
* ────────────────────────────────────────────────────────────────
* 现象: Web 端点击"启动测试"返回 {"status":"started"}, 但实际
*       运行的是空种群或第 1 代, 无任何错误提示
* 根因: EvolutionEngine 无"实验就绪"状态, handleEvolution() 与
*       startCurrentTestImpl() 均不检查种群文件与实验状态
* 修复:
*   1. EvolutionEngine 新增静态成员 experimentReady
*   2. initImpl() 成功加载/成功新建后设 true, 任何失败路径设 false
*   3. startCurrentTestImpl() 开头检查 experimentReady +
*      currentGeneration > 0 + /pop_gen_<gen>.bin 存在 +
*      loadPopulation() 成功, 任一失败则拒绝启动
*   4. handleEvolution("start") 在调用 startCurrentTest() 前
*      检查 isExperimentReady(), 失败返回 HTTP 500 +
*      {"status":"error","reason":"experiment_not_ready"}
* 影响: Web 端不再"假启动"; 未就绪时明确返回错误
* 位置: EvolutionEngine::experimentReady
*       EvolutionEngine::initImpl()
*       EvolutionEngine::startCurrentTestImpl()
*       CarWebServer::handleEvolution()
*
* ────────────────────────────────────────────────────────────────
* [R3] 高风险问题: loadExperimentState() 恢复链不完整
* ────────────────────────────────────────────────────────────────
* 现象: experiment_state.mrk 指向 N, 但 /pop_gen_N.bin 缺失且
*       N-1 也缺失时, experimentActive = false, 随后被
*       setup() 的 startNewExperiment() 接管; 若
*       findLatestGeneration() 返回 0, 则创建第 1 代
* 根因: loadExperimentState() 在 savedGen 和 savedGen-1 都失败后,
*       虽调用了 findLatestGeneration(), 但失败路径未阻止
*       setup() 后续的 startNewExperiment()
* 修复:
*   1. loadExperimentState() 在所有回退失败后, 再调一次
*      findLatestGeneration(); 若返回 > 0, 恢复该代并
*      experimentActive = true, 不再进入未激活分支
*   2. setup() 中 !isExperimentActive() 时, 先调
*      findLatestGeneration(); 若 > 0, 直接
*      setCurrentGeneration(latest, true) + forceActivate(),
*      不再调用 startNewExperiment()
*   3. 仅当 findLatestGeneration() == 0 时才调用
*      startNewExperiment()
* 影响: 恢复链补全, 避免因状态文件与种群文件不一致而回第 1 代
* 位置: GeneStorage::loadExperimentState()
*       setup()
*
* ────────────────────────────────────────────────────────────────
* [R4] 中风险问题: 缺少启动恢复审计日志
* ────────────────────────────────────────────────────────────────
* 现象: 上电后无法一眼看出恢复链在哪一步失败
* 根因: setup() 无恢复链审计输出
* 修复: setup() 在 GeneStorage::init() 与 EvolutionEngine::init()
*       之后打印完整恢复审计:
*         - SPIFFS ready
*         - experiment_state.mrk 是否存在及内容
*         - GeneStorage currentGeneration / experimentActive
*         - findLatestGeneration() 返回值
*         - populationFileExists(currentGeneration)
*         - EvolutionEngine experimentReady / generation
* 影响: 上电日志可直接定位恢复失败点
* 位置: setup()
*
* ────────────────────────────────────────────────────────────────
* [R5] 中风险问题: /status 缺少实验就绪字段
* ────────────────────────────────────────────────────────────────
* 现象: Web 端无法预知实验是否可启动
* 根因: /status JSON 未输出 experimentActive / experimentReady /
*       currentPopExists / latestPopGeneration
* 修复: handleStatus() 新增 4 个字段
* 影响: 前端可预检, 避免无效启动
* 位置: CarWebServer::handleStatus()
*
* ────────────────────────────────────────────────────────────────
* [v10.5 追加: SPIFFS 路径规范化]
* ────────────────────────────────────────────────────────────────
* 见文件顶端 "SPIFFS 路径规范化 强制规范" 区块。
* 共修改 13 处 File.name() 返回值处理, 全部经 normPath() 规范化。
* 影响: getStoredGenerations / findLatestGeneration / 所有目录扫描
*       函数在 Core 2.3.10 上恢复正常。
* 位置: 见顶端规范区块的 13 处清单。
*
* ────────────────────────────────────────────────────────────────
* [版本号变更]
* ────────────────────────────────────────────────────────────────
* FIRMWARE_VERSION: "v10.4-DataRecoveryFix" → "v10.5-SPIFFSNormFix"
* 二进制格式版本: 0x0009 保持不变 (数据向后兼容)
*
* ================================================================
* 修改日期: 2026-09-13
* 修改人: 系统优化
* ================================================================
*/