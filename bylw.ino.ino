/*
 * ============================================================
 * 仿草履虫应激机制 - 小车避障进化验证系统
 * 版本 2.0 - 向死而生
 * 
 * 核心设计思想：
 * 1. 物理差动驱动：left - right 直接控制转向（无PID、无滤波、无标定）
 * 2. 进化机制：基因（k_turn, speed_bias）通过物理噪声变异 + 生存时间选择
 * 3. 混沌注入器：卡住时随机激荡，尝试脱困（向死而生）
 * 4. 内部对照：chaos_count == 0 的个体可作为基线对照组
 * ============================================================
 */

// 引入EEPROM库，用于在Flash中持久化存储基因数据（掉电不丢失）
#include <EEPROM.h>

// ===================== 模式选择 =====================
// AUTO_START: 1=上电自动启动进化（脱机运行），0=等待串口输入（调试模式）
#define AUTO_START 1

// ===================== 引脚定义 =====================
// 定义所有硬件连接到ESP32的GPIO引脚编号

#define LEFT_SENSOR   34  // 左侧红外传感器（模拟输入，ADC通道）
#define RIGHT_SENSOR  35  // 右侧红外传感器（模拟输入，ADC通道）
#define NOISE_PIN     36  // 悬空ADC引脚，用于读取物理热噪声（变异源）
#define LED_PIN       2   // 板载LED指示灯

// 左轮电机控制引脚
#define LEFT_PWM      21  // 左轮PWM调速信号（接L298N的ENA）
#define LEFT_DIR1     12  // 左轮方向控制1（接L298N的IN1）
#define LEFT_DIR2     14  // 左轮方向控制2（接L298N的IN2）

// 右轮电机控制引脚
#define RIGHT_PWM     22  // 右轮PWM调速信号（接L298N的ENB）
#define RIGHT_DIR1    16  // 右轮方向控制1（接L298N的IN3）
#define RIGHT_DIR2    17  // 右轮方向控制2（接L298N的IN4）

// 编码器和碰撞检测引脚
#define LEFT_ENC      4   // 左轮编码器A相信号（外部中断）
#define RIGHT_ENC     5   // 右轮编码器A相信号（外部中断）
#define COLLISION_PIN 18  // 碰撞检测开关（外部中断，下降沿触发）

// ===================== 全局参数 =====================
// 这些参数控制进化实验的规模和行为

#define POPULATION_SIZE  32   // 每代个体数量（建议32或48，越大统计越可靠但耗时更长）
#define BASE_SPEED       150  // 基准速度（已废弃，保留作为参考）
#define TEST_DURATION_MS 60000 // 每个个体最长测试时间（毫秒），60秒
#define ELITE_COUNT      4    // 每代保留的精英数量（前4名直接进入下一代）
#define EEPROM_START     0    // EEPROM存储起始地址

// 混沌注入器参数
#define CHAOS_SAMPLE_COUNT 30   // 混沌模式下随机动作的次数（每次100ms，共3秒）
#define CHAOS_STEP_MS      100  // 每个混沌动作持续的时间（毫秒）
#define STUCK_THRESHOLD_MS 3000 // 判定为“卡住”的持续时间阈值（3秒）

// ===================== 基因结构 =====================
// 定义每个个体的“基因”包含哪些属性
// 每个个体对应一个Gene_t结构体，存储在EEPROM中
typedef struct {
    uint16_t id;                // 个体编号（0 ~ POPULATION_SIZE-1）
    int8_t   k_turn;            // 转向灵敏度（-50 ~ +50），决定转向幅度
    uint8_t  speed_bias;        // 速度偏置（0-255），影响基础速度
    uint32_t generation;        // 该个体所属的代数
    uint32_t survival_ms;       // 生存时间（毫秒），核心适应度指标
    uint32_t distance_ticks;    // 轮速脉冲计数（代表移动距离）
    uint16_t obstacle_count;    // 避障触发次数
    uint8_t  chaos_count;       // 该个体一生中进入混沌的次数
    uint8_t  chaos_escape_count; // 该个体从混沌中成功脱困的次数
} Gene_t;

// ===================== 全局变量 =====================
// 程序运行时使用的全局状态

Gene_t population[POPULATION_SIZE];  // 种群数组，存储所有个体的基因
Gene_t g_runtime;                    // 当前正在测试的个体的基因副本
uint8_t current_idx = 0;             // 当前测试的个体在种群中的索引（0 ~ POPULATION_SIZE-1）

// 硬件测量的易失变量（在中断中更新）
volatile uint32_t pulse_count = 0;       // 轮速脉冲累计值（左右轮合并）
volatile uint32_t obstacle_count = 0;    // 障碍物触发次数
volatile bool collision_detected = false; // 碰撞标志（由中断设置）

// 时间与状态管理
uint32_t test_start_time = 0;        // 当前个体测试开始的时间戳
uint32_t current_generation = 0;     // 当前代数
uint32_t last_pulse_count = 0;       // 上一次检测时的脉冲数（用于判断是否卡住）
uint32_t stuck_start_time = 0;       // 开始卡住的时间戳
bool is_in_chaos = false;            // 是否正在执行混沌模式
bool chaos_escaped = false;          // 混沌是否成功脱困

// 进化状态机枚举
typedef enum {
    STATE_INIT,        // 初始化：检查EEPROM，加载或创建种群
    STATE_LOAD_GENE,   // 加载指定个体的基因到运行时变量
    STATE_RUN_TEST,    // 运行测试：执行物理差动控制，监控碰撞和卡住
    STATE_CALC_SCORE,  // 计算当前个体的生存分数
    STATE_NEXT_IND,    // 移动到下一个个体
    STATE_EVALUATE,    // 评估整代：按分数排序
    STATE_REPRODUCE,   // 繁殖：精英保留 + 杂交变异生成下一代
    STATE_SLEEP        // 休眠：进化完成或等待状态
} EvoState_t;

EvoState_t evo_state = STATE_INIT;  // 当前状态机状态

// ===================== 电机控制函数 =====================
// 功能：停止所有电机
// 原理：将所有方向引脚拉低，PWM输出置0
void motor_stop() {
    digitalWrite(LEFT_DIR1, LOW);   // 左轮方向A置低
    digitalWrite(LEFT_DIR2, LOW);   // 左轮方向B置低
    digitalWrite(RIGHT_DIR1, LOW);  // 右轮方向A置低
    digitalWrite(RIGHT_DIR2, LOW);  // 右轮方向B置低
    ledcWrite(LEFT_PWM, 0);         // 左轮PWM输出0
    ledcWrite(RIGHT_PWM, 0);        // 右轮PWM输出0
}

// 功能：设置左右轮电机速度和方向
// 参数：left_speed, right_speed 范围 -255 ~ +255
//       正值为正转，负值为反转，0为停止
void set_motors(int left_speed, int right_speed) {
    // 限制输入值在有效范围内
    left_speed = constrain(left_speed, -255, 255);
    right_speed = constrain(right_speed, -255, 255);
    
    // ---- 左轮控制 ----
    if (left_speed >= 0) {
        // 正转：IN1高，IN2低
        digitalWrite(LEFT_DIR1, HIGH);
        digitalWrite(LEFT_DIR2, LOW);
        ledcWrite(LEFT_PWM, left_speed);  // 输出PWM调速
    } else {
        // 反转：IN1低，IN2高
        digitalWrite(LEFT_DIR1, LOW);
        digitalWrite(LEFT_DIR2, HIGH);
        ledcWrite(LEFT_PWM, -left_speed); // 取绝对值作为PWM值
    }
    
    // ---- 右轮控制（逻辑同上） ----
    if (right_speed >= 0) {
        digitalWrite(RIGHT_DIR1, HIGH);
        digitalWrite(RIGHT_DIR2, LOW);
        ledcWrite(RIGHT_PWM, right_speed);
    } else {
        digitalWrite(RIGHT_DIR1, LOW);
        digitalWrite(RIGHT_DIR2, HIGH);
        ledcWrite(RIGHT_PWM, -right_speed);
    }
}

// ===================== 死局检测函数 =====================
// 功能：判断小车是否卡住（轮速停滞 或 前方障碍物持续过近）
// 返回：true表示已卡住，false表示正常
bool is_stuck() {
    // 检测条件1：轮速脉冲变化量小于5（几乎没动）
    uint32_t pulse_delta = pulse_count - last_pulse_count;
    if (pulse_delta < 5) {
        // 如果卡住计时器还没开始，记录当前时间
        if (stuck_start_time == 0) {
            stuck_start_time = millis();
        }
        // 如果卡住持续时间超过阈值，判定为卡住
        if (millis() - stuck_start_time > STUCK_THRESHOLD_MS) {
            return true;
        }
    } else {
        // 轮子在动，重置卡住计时器
        stuck_start_time = 0;
    }
    
    // 检测条件2：左右传感器读数同时大于3000（障碍物极近）
    int left = analogRead(LEFT_SENSOR);
    int right = analogRead(RIGHT_SENSOR);
    if (left > 3000 && right > 3000) {
        if (stuck_start_time == 0) {
            stuck_start_time = millis();
        }
        if (millis() - stuck_start_time > STUCK_THRESHOLD_MS) {
            return true;
        }
    } else {
        // 障碍物消失，重置卡住计时器
        stuck_start_time = 0;
    }
    
    return false;  // 未卡住
}

// ===================== 混沌注入器 =====================
// 功能：当小车卡住时，执行随机激荡动作，尝试脱困
// 原理：连续执行30次随机PWM组合，每次100ms
//       期间如果检测到脱困（轮速增加或障碍物消失），立即退出
void enter_chaos_mode() {
    // 如果已经在混沌模式中，避免重复进入
    if (is_in_chaos) return;
    
    // 标记进入混沌模式
    is_in_chaos = true;
    chaos_escaped = false;
    g_runtime.chaos_count++;  // 该个体混沌次数+1
    
    Serial.println("=== CHAOS INJECTED ===");  // 串口调试输出
    
    // 执行混沌激荡
    for (int i = 0; i < CHAOS_SAMPLE_COUNT; i++) {
        // 随机生成左右轮PWM值（-255 ~ 255）
        int left_pwm = random(-255, 256);
        int right_pwm = random(-255, 256);
        set_motors(left_pwm, right_pwm);  // 执行随机动作
        digitalWrite(LED_PIN, (i % 2 == 0));  // LED闪烁指示混沌模式
        delay(CHAOS_STEP_MS);  // 保持动作100ms
        
        // 检测是否脱困
        int left = analogRead(LEFT_SENSOR);
        int right = analogRead(RIGHT_SENSOR);
        uint32_t pulse_delta = pulse_count - last_pulse_count;
        
        // 脱困条件：障碍物消失（读数<1500）或轮速明显增加（>20脉冲）
        if ((left < 1500 && right < 1500) || pulse_delta > 20) {
            chaos_escaped = true;
            g_runtime.chaos_escape_count++;  // 脱困次数+1
            Serial.println("=== CHAOS ESCAPED! ===");
            break;  // 立即退出混沌
        }
        last_pulse_count = pulse_count;  // 更新脉冲计数基准
    }
    
    // 混沌结束，停止电机
    motor_stop();
    is_in_chaos = false;
    stuck_start_time = 0;
    
    // 如果混沌未能脱困，记录当前个体为“死亡”，保存生存数据
    if (!chaos_escaped) {
        Serial.println("=== CHAOS FAILED ===");
        g_runtime.survival_ms = millis() - test_start_time;
        g_runtime.distance_ticks = pulse_count;
        g_runtime.obstacle_count = obstacle_count;
        gene_save(&g_runtime, current_idx);  // 保存基因到EEPROM
    }
}

// ===================== EEPROM读写函数 =====================
// 功能：将基因数据保存到EEPROM（Flash），掉电不丢失
// 参数：g - 基因指针，idx - 个体索引
void gene_save(Gene_t *g, uint8_t idx) {
    uint32_t addr = EEPROM_START + idx * sizeof(Gene_t);  // 计算存储地址
    EEPROM.put(addr, *g);   // 写入数据
    EEPROM.commit();        // 提交到Flash（关键！否则断电丢失）
}

// 功能：从EEPROM读取基因数据
void gene_load(Gene_t *g, uint8_t idx) {
    uint32_t addr = EEPROM_START + idx * sizeof(Gene_t);
    EEPROM.get(addr, *g);   // 读取数据
}

// 功能：保存整个种群到EEPROM
void gene_pool_save() {
    for (int i = 0; i < POPULATION_SIZE; i++) {
        gene_save(&population[i], i);
    }
}

// 功能：从EEPROM加载整个种群
void gene_pool_load() {
    for (int i = 0; i < POPULATION_SIZE; i++) {
        gene_load(&population[i], i);
    }
}

// ===================== 基因变异函数 =====================
// 功能：对基因施加微小随机扰动（物理噪声驱动）
// 原理：读取悬空ADC引脚的热噪声，作为变异幅度
void gene_mutate(Gene_t *g) {
    // 读取物理噪声（0~4095之间的随机值）
    uint16_t noise = analogRead(NOISE_PIN);
    
    // 对k_turn施加±2的随机扰动
    int8_t delta = (int8_t)(noise & 0x03) - 1;  // 取低2位，映射到-1,0,1,2
    g->k_turn += delta;
    g->k_turn = constrain(g->k_turn, -50, 50);   // 限制范围
    
    // 对speed_bias施加±2的随机扰动
    delta = (int8_t)((noise >> 2) & 0x03) - 1;
    g->speed_bias += delta;
    g->speed_bias = constrain(g->speed_bias, 80, 200);  // 限制范围
}

// ===================== 生存分数计算 =====================
// 功能：根据个体的生存数据计算适应度分数
// 分数越高，表示该个体越适应环境
uint32_t compute_survival_score(Gene_t *g) {
    // 距离得分：每2个脉冲得1分
    uint32_t distance_score = g->distance_ticks / 2;
    
    // 避障得分：每个障碍物50分
    uint32_t obstacle_score = g->obstacle_count * 50;
    
    // 混沌惩罚：每次进入混沌扣20分（过度依赖混沌说明策略不佳）
    uint32_t chaos_penalty = g->chaos_count * 20;
    
    // 防作弊：如果距离太短（<200脉冲），避障得分无效
    if (g->distance_ticks < 200) {
        obstacle_score = 0;
    }
    
    // 基础分数 = 距离 + 避障 - 混沌惩罚
    uint32_t score = distance_score + obstacle_score - chaos_penalty;
    
    // 奖励：如果混沌脱困率高于50%，额外加分
    if (g->chaos_count > 0 && g->chaos_escape_count > g->chaos_count / 2) {
        score += g->chaos_escape_count * 30;
    }
    
    return score;
}

// ===================== 中断服务程序 =====================
// 功能：碰撞检测中断（下降沿触发）
// 注意：中断中只设置标志位，不做复杂计算
void IRAM_ATTR collision_isr() {
    collision_detected = true;  // 设置碰撞标志
}

// 功能：左轮编码器中断（下降沿触发）
// 每次轮子转过一个齿，pulse_count累加1
void IRAM_ATTR left_encoder_isr() {
    pulse_count++;
}

// 功能：右轮编码器中断（下降沿触发）
void IRAM_ATTR right_encoder_isr() {
    pulse_count++;
}

// ===================== 种群初始化 =====================
// 功能：创建初始种群（第0代），所有个体使用保守基因值
void init_population() {
    for (int i = 0; i < POPULATION_SIZE; i++) {
        population[i].id = i;
        population[i].k_turn = 10;       // 中等转向灵敏度
        population[i].speed_bias = 150;  // 中等速度
        population[i].generation = 0;
        population[i].survival_ms = 0;
        population[i].distance_ticks = 0;
        population[i].obstacle_count = 0;
        population[i].chaos_count = 0;
        population[i].chaos_escape_count = 0;
        gene_save(&population[i], i);    // 保存到EEPROM
    }
    Serial.println("Population initialized");
}

// ===================== 打印函数 =====================
// 功能：打印单个个体的基因信息到串口
void print_gene(Gene_t *g) {
    Serial.printf("ID:%d Gen:%lu Score:%lu k:%d speed:%d Dist:%lu Obst:%d Chaos:%d/%d\n",
        g->id, g->generation, compute_survival_score(g),
        g->k_turn, g->speed_bias,
        g->distance_ticks, g->obstacle_count,
        g->chaos_count, g->chaos_escape_count);
}

// 功能：打印整个种群的基因信息到串口
void print_population() {
    Serial.println("=== Population ===");
    for (int i = 0; i < POPULATION_SIZE; i++) {
        gene_load(&population[i], i);
        print_gene(&population[i]);
    }
    Serial.println("=================");
}

// ===================== 进化状态机 =====================
// 功能：核心进化逻辑，由loop()反复调用
void evolution_loop() {
    // 静态变量：在多次调用间保持状态
    static bool test_running = false;
    static bool chaos_triggered_this_run = false;
    uint32_t score = 0;  // 临时存储分数
    
    // 状态机切换
    switch(evo_state) {
        
        // ---- 状态：初始化 ----
        case STATE_INIT: {
            Serial.println("=== EVO: INIT ===");
            EEPROM.begin(4096);  // 初始化EEPROM（4KB）
            
            // 检查第0个个体是否为有效数据（generation==0且id==0表示空EEPROM）
            gene_load(&population[0], 0);
            if (population[0].generation == 0 && population[0].id == 0) {
                // 首次运行：创建初始种群
                init_population();
                current_generation = 0;
            } else {
                // 已有数据：加载现有种群
                gene_pool_load();
                current_generation = population[0].generation;
            }
            current_idx = 0;  // 从第0个个体开始
            evo_state = STATE_LOAD_GENE;
            break;
        }
        
        // ---- 状态：加载基因 ----
        case STATE_LOAD_GENE: {
            Serial.printf("=== EVO: LOAD GENE (idx=%d) ===\n", current_idx);
            // 从种群中加载指定个体的基因到运行时变量
            gene_load(&g_runtime, current_idx);
            // 重置所有测量变量
            pulse_count = 0;
            obstacle_count = 0;
            collision_detected = false;
            last_pulse_count = 0;
            stuck_start_time = 0;
            is_in_chaos = false;
            chaos_triggered_this_run = false;
            test_start_time = millis();
            evo_state = STATE_RUN_TEST;
            test_running = false;
            break;
        }
        
        // ---- 状态：运行测试 ----
        // 这是整个系统最核心的状态：执行物理差动控制
        case STATE_RUN_TEST: {
            // 如果是首次进入，启动计时器
            if (!test_running) {
                test_running = true;
                test_start_time = millis();
                Serial.printf("=== EVO: RUN TEST (k=%d, speed=%d) ===\n", 
                    g_runtime.k_turn, g_runtime.speed_bias);
            }
            
            // ---- 碰撞检测 ----
            if (collision_detected) {
                motor_stop();
                g_runtime.survival_ms = millis() - test_start_time;
                g_runtime.distance_ticks = pulse_count;
                g_runtime.obstacle_count = obstacle_count;
                gene_save(&g_runtime, current_idx);
                Serial.printf("=== COLLISION! survived: %lu ms ===\n", g_runtime.survival_ms);
                collision_detected = false;
                evo_state = STATE_CALC_SCORE;
                break;
            }
            
            // ---- 超时检测 ----
            if (millis() - test_start_time >= TEST_DURATION_MS) {
                motor_stop();
                g_runtime.survival_ms = TEST_DURATION_MS;
                g_runtime.distance_ticks = pulse_count;
                g_runtime.obstacle_count = obstacle_count;
                gene_save(&g_runtime, current_idx);
                Serial.printf("=== TIMEOUT! survived: %lu ms ===\n", g_runtime.survival_ms);
                evo_state = STATE_CALC_SCORE;
                break;
            }
            
            // ---- 死局检测 + 混沌注入 ----
            // 如果小车卡住且不在混沌模式，触发混沌注入
            if (is_stuck() && !is_in_chaos) {
                chaos_triggered_this_run = true;
                enter_chaos_mode();
                // 如果混沌失败，evo_state已被设为STATE_CALC_SCORE
                if (evo_state == STATE_CALC_SCORE) break;
            }
            
            // ---- 物理差动控制（核心！） ----
            // 如果不处于混沌模式，执行正常的物理差动驱动
            if (!is_in_chaos) {
                // 读取左右传感器原始ADC值（不滤波、不标定）
                int left = analogRead(LEFT_SENSOR);
                int right = analogRead(RIGHT_SENSOR);
                int steer = left - right;  // 物理差动！！！
                
                // 计算PWM输出
                // speed_bias（0-255）映射到有符号速度（-128~127）
                int base_speed = g_runtime.speed_bias - 128;
                int pwm_left = base_speed + steer * g_runtime.k_turn / 10;
                int pwm_right = base_speed - steer * g_runtime.k_turn / 10;
                
                // 限制PWM范围
                pwm_left = constrain(pwm_left, -255, 255);
                pwm_right = constrain(pwm_right, -255, 255);
                
                // 执行电机控制
                set_motors(pwm_left, pwm_right);
            }
            
            // ---- LED心跳 ----
            // 每200ms翻转一次LED状态，指示程序正在运行
            if ((millis() / 200) % 2 == 0) {
                digitalWrite(LED_PIN, HIGH);
            } else {
                digitalWrite(LED_PIN, LOW);
            }
            break;
        }
        
        // ---- 状态：计算分数 ----
        case STATE_CALC_SCORE: {
            Serial.printf("=== EVO: CALC SCORE idx=%d ===\n", current_idx);
            gene_load(&g_runtime, current_idx);
            score = compute_survival_score(&g_runtime);
            Serial.printf("Score: %lu (surv:%lu, dist:%lu, obs:%d, chaos:%d/%d)\n", 
                score, g_runtime.survival_ms, g_runtime.distance_ticks, 
                g_runtime.obstacle_count, g_runtime.chaos_count, g_runtime.chaos_escape_count);
            evo_state = STATE_NEXT_IND;
            break;
        }
        
        // ---- 状态：下一个个体 ----
        case STATE_NEXT_IND: {
            current_idx++;
            if (current_idx >= POPULATION_SIZE) {
                // 所有个体测试完毕，进入评估阶段
                current_idx = 0;
                evo_state = STATE_EVALUATE;
            } else {
                // 继续加载下一个个体
                evo_state = STATE_LOAD_GENE;
            }
            break;
        }
        
        // ---- 状态：评估整代 ----
        case STATE_EVALUATE: {
            Serial.println("=== EVO: EVALUATE ===");
            gene_pool_load();
            // 冒泡排序：按生存分数降序排列（分数高的在前）
            for (int i = 0; i < POPULATION_SIZE - 1; i++) {
                for (int j = i + 1; j < POPULATION_SIZE; j++) {
                    uint32_t score_i = compute_survival_score(&population[i]);
                    uint32_t score_j = compute_survival_score(&population[j]);
                    if (score_i < score_j) {
                        // 交换位置
                        Gene_t tmp = population[i];
                        population[i] = population[j];
                        population[j] = tmp;
                    }
                }
            }
            print_population();  // 打印排序后的种群
            evo_state = STATE_REPRODUCE;
            break;
        }
        
        // ---- 状态：繁殖 ----
        case STATE_REPRODUCE: {
            Serial.println("=== EVO: REPRODUCE ===");
            Gene_t next_population[POPULATION_SIZE];  // 临时存储下一代
            
            // 精英保留：前ELITE_COUNT名直接进入下一代
            for (int i = 0; i < ELITE_COUNT; i++) {
                next_population[i] = population[i];
                next_population[i].generation = population[i].generation + 1;
            }
            
            // 杂交变异：剩余个体从精英中随机选取亲本
            for (int i = ELITE_COUNT; i < POPULATION_SIZE; i++) {
                // 用物理噪声随机选择两个亲本
                uint16_t noise = analogRead(NOISE_PIN);
                int p1 = noise & 0x03;          // 取低2位 -> 0-3
                int p2 = (noise >> 2) & 0x03;   // 取次2位 -> 0-3
                if (p1 >= ELITE_COUNT) p1 = 0;
                if (p2 >= ELITE_COUNT) p2 = 1;
                
                // 继承亲本1的k_turn，亲本2的speed_bias
                next_population[i] = population[p1];
                next_population[i].speed_bias = population[p2].speed_bias;
                next_population[i].generation = population[p1].generation + 1;
                next_population[i].survival_ms = 0;
                next_population[i].distance_ticks = 0;
                next_population[i].obstacle_count = 0;
                next_population[i].chaos_count = 0;
                next_population[i].chaos_escape_count = 0;
                gene_mutate(&next_population[i]);  // 施加变异
                next_population[i].id = i;
            }
            
            // 将下一代写入EEPROM
            for (int i = 0; i < POPULATION_SIZE; i++) {
                population[i] = next_population[i];
                gene_save(&population[i], i);
            }
            current_generation = population[0].generation;
            current_idx = 0;
            Serial.printf("=== New Generation: %lu ===\n", current_generation);
            print_population();
            evo_state = STATE_LOAD_GENE;  // 开始测试新一代
            break;
        }
        
        // ---- 状态：休眠 ----
        case STATE_SLEEP: {
            // LED闪烁显示代数（模10）
            for (int i = 0; i < (current_generation % 10); i++) {
                digitalWrite(LED_PIN, HIGH);
                delay(100);
                digitalWrite(LED_PIN, LOW);
                delay(100);
            }
            delay(1000);
            break;
        }
    }
}

// ===================== 主程序 setup() =====================
// 功能：Arduino启动时执行一次，初始化所有硬件和软件
void setup() {
    // 初始化串口通信，波特率115200（与串口监视器匹配）
    Serial.begin(115200);
    delay(1000);  // 等待串口稳定
    Serial.println("\n========================================");
    Serial.println(" 仿草履虫应激机制 - 小车避障进化系统");
    Serial.println(" 版本 2.0 - 向死而生");
    Serial.println("========================================");
    
    // 初始化LED引脚为输出
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    
    // 初始化传感器引脚为输入（ADC模式）
    pinMode(LEFT_SENSOR, INPUT);
    pinMode(RIGHT_SENSOR, INPUT);
    pinMode(NOISE_PIN, INPUT);  // 悬空，读取热噪声
    
    // 初始化电机控制引脚为输出
    pinMode(LEFT_PWM, OUTPUT);
    pinMode(RIGHT_PWM, OUTPUT);
    pinMode(LEFT_DIR1, OUTPUT);
    pinMode(LEFT_DIR2, OUTPUT);
    pinMode(RIGHT_DIR1, OUTPUT);
    pinMode(RIGHT_DIR2, OUTPUT);
    
    // 初始化编码器引脚为输入上拉（防止浮动）
    pinMode(LEFT_ENC, INPUT_PULLUP);
    pinMode(RIGHT_ENC, INPUT_PULLUP);
    pinMode(COLLISION_PIN, INPUT_PULLUP);
    
    // 附加中断服务程序
    attachInterrupt(digitalPinToInterrupt(COLLISION_PIN), collision_isr, FALLING);
    attachInterrupt(digitalPinToInterrupt(LEFT_ENC), left_encoder_isr, FALLING);
    attachInterrupt(digitalPinToInterrupt(RIGHT_ENC), right_encoder_isr, FALLING);
    
    // 初始化PWM通道（ESP32 LEDC）
    // 参数：引脚，频率(5kHz)，分辨率(8位，0-255)
    ledcAttach(LEFT_PWM, 5000, 8);
    ledcAttach(RIGHT_PWM, 5000, 8);
    
    // 启动时LED闪烁3次，表示系统就绪
    for (int i = 0; i < 3; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(200);
        digitalWrite(LED_PIN, LOW);
        delay(200);
    }
    
    // 初始化EEPROM（4KB空间）
    EEPROM.begin(4096);
    
    // ===== 如需重置基因库（调试用），取消下行注释 =====
    // 警告：取消注释后会清空所有进化数据！
    // init_population(); gene_pool_save();
    
    // ---- 启动模式选择 ----
    #if AUTO_START == 1
        // 脱机模式：上电后自动启动进化
        Serial.println("System ready. Auto-starting evolution...");
        Serial.println("Starting evolution...");
    #else
        // 调试模式：等待串口输入字符后才启动
        Serial.println("System ready. Press any key to start...");
        while (!Serial.available()) {
            delay(100);
        }
        Serial.read();
        Serial.println("Starting evolution...");
    #endif
}

// ===================== 主程序 loop() =====================
// 功能：Arduino主循环，反复调用进化状态机
void loop() {
    evolution_loop();  // 执行进化状态机
    delay(10);         // 小延时防止看门狗复位
}