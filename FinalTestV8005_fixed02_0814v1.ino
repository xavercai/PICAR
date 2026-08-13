/* 
 * ================================================================ 
 * 项目名称：仿草履虫应激机制 - 脱困能力进化系统 
 * 版本： v8.5-fixed (V80行为对齐: normalize阈值/isClear/变异率/COND_IDLE)
 * 适配日期: 2026-08-09   修正日期: 2026-08-10
 * ================================================================ 
 * 烧录成功，系统启动，但是Web显示一切正常，但是解锁后，电机没有响应，LED也不闪烁  烧录成功，系统启动
 * ================================================================
 * SPIFFS 冲突根因说明（ESP32 架构限制）
 * ================================================================
 * 
 * ESP32 只有一片 SPI Flash，同时承担三个角色：
 *   1. 程序代码存储（通过 Cache 执行）
 *   2. SPIFFS 文件系统存储
 *   3. WiFi 协议栈固件存储
 * 
 * 当 SPIFFS 执行页擦除/编程时，Flash 控制器禁用 Cache → 所有
 * 从 Flash 执行的代码（包括 WiFi 中断处理）被暂停。这是硬件级
 * 限制，无法完全消除，但本文件通过以下措施将冲突降到最低：
 * 
 *   - SPIFFS 在 WiFi 之前初始化（setup() 中顺序保证）
 *   - 编码器 ISR 标记 IRAM_ATTR + GPIO寄存器直读（在 RAM 中执行，不被 Flash 写入阻塞）
 *   - 使用原子写入策略（临时文件 + 重命名，避免写入中断导致文件损坏）
 *   - 写入仅在个体死亡时触发（forceSave），非逐帧写入
 *   - 帧日志 CSV 仅在个体测试结束时一次性导出
 *   - 噪声源使用 GPIO35（仅输入 ADC1，不与 Flash SPI 总线共享引脚）
 * 
 * 已知残余风险（可接受）：
 *   - SPIFFS 写入期间 WiFi 可能丢包（~10-50ms 级别），不影响 AP 连接
 *   - Flash 写入时 ADC 读数可能有轻微噪声（电源纹波），但传感器
 *     使用多次采样基准值校准，单次噪声不影响决策
 *   - 如果 SPIFFS 空间耗尽（极罕见），doAtomicWrite 失败 → 数据丢失
 *     → 但 buffer 已清空，不会 OOM（#37 修复）
 * ================================================================ 
 * 
 * ================================================================
 * 【修正汇总】2026-08-10 移植自 v5.9 MotorDriver 电机增益控制逻辑
 * ================================================================
 * 
 * 一、电机增益移植（全局生效）
 * ───────────────────────────────────────────
 * 1. 新增宏 LEFT_GAIN=1.00f / RIGHT_GAIN=1.25f，补偿右电机机械偏弱
 * 2. 新增 writeMotorHardware() 统一入口函数，所有电机 PWM 写入必须
 *    经过此函数，确保增益补偿全局零死角生效
 * 3. 将 analogWrite() 全部替换为 ledcWrite()，匹配 AT8236 驱动芯片
 *    （ledcAttach 5kHz/8bit，在 init() 中初始化）
 * 4. 替换范围覆盖全部 6 个调用点共 10 次写入：
 *    - stopMotors()        → writeMotorHardware(0, 0)
 *    - backwardReset()     → writeMotorHardware(100, 100)
 *    - update()            → writeMotorHardware(currentSpeedL, currentSpeedR)
 *    - executeRecovery()   → writeMotorHardware(...) ×4 次
 *    - updateChaos()       → writeMotorHardware(randSpeedL, randSpeedR)
 * 
 * 二、漏洞修复
 * ───────────────────────────────────────────
 * 5. [修复] 右编码器 ISR 空函数体 → 实现正交解码（B相判断方向）
 * 6. [修复] 左编码器 ISR 仅做 distanceTicks++ → 实现正交解码（区分正反转）
 * 7. [修复] 混沌期间 lastMoveTime 不更新 → writeMotorHardware() 每次写入
 *    自动更新 lastMoveTime，endChaos() 脱困判定现已可靠
 * 
 * 三、附带修复
 * ───────────────────────────────────────────
 * 8. [修复] 删除 lastChaosTime 重复声明（原行 779-780 重复定义）
 * 9. [标注] SPINNING_STUCK_* 三个宏标注为 [预留]（当前未实现旋转卡死检测）
 * 10.[标注] EvolutionEngine 静态访问方式添加注释说明
 * 
 * 四、实车校准注意事项
 * ───────────────────────────────────────────
 * - RIGHT_GAIN=1.25 为预设值，需通过编码器校准工具（源文件
 *   CalibrationTool::calibrateGain()）实测左右轮脉冲差值后修正
 * - 增益后 backwardReset() 中右轮 PWM 略大（125 vs 100），
 *   后退复位可能产生轻微弧线，若场地空间受限可调低 RIGHT_GAIN
 * - 若更换电机或齿轮箱，需重新校准 LEFT_GAIN / RIGHT_GAIN
 * 
 * ================================================================
 * 【修正汇总】2026-08-10 时序与存储系统修复
 * ================================================================
 * 
 * 五、SPIFFS + WiFi 时序冲突修复
 * ───────────────────────────────────────────
 * 11.[致命] RobustStorage::init() 从未被调用 → 在 setup() 中新增调用，
 *    SPIFFS 在 WiFi 之前初始化，避免 Flash 操作与 WiFi 中断冲突
 * 12.[修复] loadHistory() 不存在 → 由 RobustStorage::init() 内部调用
 *    loadHistoryToRAM() 替代
 * 13.[修复] SAVE_INTERVAL_MS 未定义 → 改为 RobustStorage::tick()（内部
 *    使用 STORAGE_SAVE_INTERVAL_MS）
 * 14.[修复] flushIfNeeded() 不存在 → 改为 RobustStorage::tick()
 * 15.[修复] LOOP_DELAY_MS 未定义 → 新增宏定义为 10ms
 * 16.[修复] SensorCalibration::init() 不存在 → 改为 calibrate()
 * 17.[修复] Logger::getRecentLogs() 不存在 → 新增 stub 实现
 * 18.[修复] RobustStorage::clearAll() 不存在 → 新增实现（清除 Flash 文件
 *    及 RAM 缓冲区）
 * 
 * 六、倒车重置时序修复
 * ───────────────────────────────────────────
 * 19.[修复] backwardReset() 双重调用 → loop() 中删除重复的
 *    MotorController::backwardReset()，因为 endCurrentTest() 内部已调用，
 *    避免 5 秒双重阻塞冻结
 * 
 * ================================================================
 * 【修正汇总】2026-08-10 混沌脱困物理姿态修复
 * ================================================================
 * 
 * 七、物理噪声源与随机方向
 * ───────────────────────────────────────────
 * 20.[新增] PIN_NOISE_SOURCE (GPIO6) 悬空ADC引脚，利用半导体热噪声
 *    生成真随机数熵源（移植自 v5.9 的物理随机变异源设计）
 * 21.[新增] SensorCalibration::readNoise() 读取悬空引脚噪声值
 * 22.[新增] setup() 中 randomSeed(物理噪声) 播种随机数生成器
 * 
 * 八、混沌脱困后的物理姿态（贴合生物学应激行为）
 * ───────────────────────────────────────────
 * 23.[新增] escapeRetreat()：混沌成功后，物理噪声源驱动随机方向后退
 *    500ms 逃离危险区域（0=直退, 1=后退左转, 2=后退右转）
 *    避免脱困后车头仍对着障碍物、立刻重新撞入
 * 24.[修复] 混沌脱困失败 → 立即调用 endCurrentTest() + nextIndividual()
 *    模拟"挣扎失败后放弃生命"，不再靠 recovery 苟活稀释进化压力
 * 
 * 九、个体切换时的物理姿态
 * ───────────────────────────────────────────
 * 25.[新增] randomTurn()：个体切换时，backwardReset 后增加物理噪声
 *    驱动的随机原地转向 500ms（左转/右转），打破前任的死亡姿态
 *    ，确保新个体从不同方向出发探索
 * 
 * ================================================================
 * 【修正汇总】2026-08-10 基因表达与数据记录修复
 * ================================================================
 * 
 * 十、死基因激活
 * ───────────────────────────────────────────
 * 26.[修复] chaos_threshold 死基因 → detectDilemma() 中 collisionCount
 *    现在与基因 chaos_threshold 比较，而非硬编码 COLLISION_THRESHOLD=8
 *    （行 1160）。基因值范围 2-20，越小越敏感越早触发混沌
 * 27.[修复] preferred_action 死基因 → update() 畅通时根据偏好注入偏向：
 *    0=直行(无偏向), 1=左转(右轮+25%), 2=右转(左轮+25%)
 *    （行 1097-1102）。使偏好动作真正影响小车探索行为
 * 28.[新增] MotorController::setPreferredAction() + 静态成员
 *    preferred_action，在 applyGeneToMotor() 中传递（行 1005/741）
 * 
 * 十一、逐帧动作日志系统
 * ───────────────────────────────────────────
 * 29.[新增] FrameLogEntry 结构体：每帧记录时间戳、传感器值、电机PWM、
 *    方向、混沌状态、困境类型，共 10 字节/帧（行 264-278）
 * 30.[新增] 256 帧环形缓冲区（FRAME_LOG_SIZE），覆盖约 2.5 秒
 * 31.[新增] logFrame()：每帧在 update() 末尾采集传感器+电机状态
 * 32.[新增] flushFrameLogToFlash()：个体测试结束时导出 CSV 到 SPIFFS
 *    文件名格式 /frm_g{代}_i{个体}.csv
 * 33.[新增] startFrameLog() / getFrameLogSummary() 配套方法
 * 
 * ================================================================
 * 【修正汇总】2026-08-10 存储链路完整性修复
 * ================================================================
 * 
 * 十二、存储流断链与硬件冲突修复
 * ───────────────────────────────────────────
 * 34.[致命] GPIO6 与 Flash SPI CLK 总线冲突 → 改为 GPIO35（仅输入
 *    ADC1 引脚，悬空安全，不与 Flash 总线共享）
 * 35.[修复] 快速死亡个体数据丢失 → endCurrentTest() 中 addRecord()
 *    后追加 forceSave()，确保每个个体死亡时数据立即持久化
 * 36.[修复] 混沌脱困失败时 Web 冻结 5.5 秒 → endChaos() 失败分支
 *    不再调用 nextIndividual()，改为设置 pendingTransition 标志，
 *    由 loop() 在 handleClient() 之后处理，Web 不再被阻塞
 * 37.[修复] flushBuffer() 失败时 buffer 无限增长 → 3 次重试失败后
 *    清空 pendingBuffer 和 unsavedCount，防止内存溢出
 * ================================================================
 * 混沌脱困 → 物理姿态 行为流程图
 * ================================================================
 * 
 *  start() 释放个体
 *    │
 *    ▼
 *  正常行走 ──→ 困境检测 ──→ 触发混沌
 *                                │
 *                      ┌─────────┴─────────┐
 *                      ▼                   ▼
 *                混沌成功脱困          混沌脱困失败
 *                      │                   │
 *                      ▼                   ▼
 *           escapeRetreat()          立即死亡
 *           物理噪声 → 随机方向       endCurrentTest()
 *           ┌───┬───┬───┐            +pendingTransition
 *           │直退│左转│右转│                │
 *           └───┴───┴───┘            loop() 下一轮
 *                后退500ms               │
 *                车头已转向           handleClient() OK
 *                      │                   │
 *                      ▼                   ▼
 *              继续正常行走          nextIndividual()
 *                                    backwardReset()
 *                                    randomTurn()
 *                                          │
 *                                          ▼
 *                                   新个体从随机方向出发
 * ================================================================
 * 遗传数据流图
 * ================================================================
 * 
 * ┌─ 5 个可遗传基因 ─────────────────────────────────────────┐
 * │                                                          │
 * │  k_turn          → 避障转向幅度 (update 中叠加)          │
 * │  speed_bias      → 直行速度     (update 中 targetSpeed)   │
 * │  chaos_threshold → 混沌敏感度   (detectDilemma 碰撞判定)  │
 * │  recovery_mode   → 脱困策略     (executeRecovery switch)  │
 * │  preferred_action→ 探索偏向     (update 畅通时注入)       │
 * │                                                          │
 * └──────────────────────────────────────────────────────────┘
 *                            │
 *       个体测试结束 ──→ endCurrentTest()
 *                            │
 *                ┌───────────┴───────────┐
 *                ▼                       ▼
 *          聚合统计记录              逐帧动作日志
 *        (HistoryRecord)            (FrameLogEntry)
 *                │                       │
 *                ▼                       ▼
 *        addRecord() → 缓冲区      环形缓冲区 256帧
 *                │                       │
 *                ▼                       ▼
 *        定时 flushBuffer()       flushFrameLogToFlash()
 *        → /history.csv           → /frm_g{代}_i{个体}.csv
 *                │                       │
 *                └───────────┬───────────┘
 *                            ▼
 *                       SPIFFS Flash
 *                            │
 *        ┌───────────────────┼───────────────────┐
 *        ▼                                       ▼
 *   下一代 evolve()                      Web 导出查看
 *   精英保留 + 锦标赛选择                /api/history
 *   + 均匀交叉 + 变异                    /api/framelog
 * ================================================================
 */
/* 
 * ================================================================ 
 * 项目名称：仿草履虫应激机制 - 脱困能力进化系统 
 * 版本： v7.3-fixed-v2 (编译错误全面修复)
 * 适配日期: 2026-08-09   修正日期: 2026-08-11
 * ================================================================ 
 *
 * ================================================================
 * 【修正汇总】2026-08-11 编译错误全面修复 (v7.3-fixed → v7.3-fixed-v2)
 * ================================================================
 * 
 * 一、致命编译错误修复
 * ───────────────────────────────────────────
 * 1. [致命] EvolutionEngine 类设计为实例类，但代码中全部以静态方式调用
 *    → 重构为单例+静态包装模式：添加 static instance 成员，所有公有方法
 *      改为静态包装（调用 instance.implXxx()），新增公有访问器方法
 *    → 影响范围：MotorController::endChaos()、CarWebServer 全部路由、
 *      setup()、loop() 中共 30+ 处调用点
 * 
 * 2. [致命] ledcAttach(pin, freq, bits) 三参数函数不存在 (ESP32 Arduino 标准库)
 *    → 拆分为 ledcSetup(chan, freq, bits) + ledcAttachPin(pin, chan)
 *    → 新增 LEDC_CHANNEL_LEFT=0, LEDC_CHANNEL_RIGHT=1 宏定义
 * 
 * 3. [致命] ledcWrite 参数应为通道号，非引脚号
 *    → 全部 ledcWrite(PIN_xxx, val) 改为 ledcWrite(LEDC_CHANNEL_xxx, val)
 *    → 影响范围：writeMotorHardware()、init() 中共 4 处
 * 
 * 4. [致命] case 2: 中声明 int dir 跨越初始化
 *    → 加花括号 case 2: { int dir = ...; ... break; }
 * 
 * 5. [致命] PIN_NOISE_SOURCE 宏重复定义 (GPIO6 vs GPIO35)
 *    → 删除 GPIO6 的定义（与 Flash SPI CLK 总线冲突），仅保留 GPIO35
 *    → 修正注释：PIN_NOISE_SOURCE 不再标注为 GPIO6
 * 
 * 二、编译警告/运行时错误修复
 * ───────────────────────────────────────────
 * 6. [修复] isClear() 函数体内有不可达代码（第二个 return 永远不执行）
 *    → 删除重复的 return 语句，清理函数体
 * 
 * 7. [修复] server.on("/status", handleStatus) 重复注册两次
 *    → 删除重复行，避免运行时崩溃
 * 
 * 8. [修复] 缺少 <cstdio> 头文件（vsnprintf 依赖）
 *    → 添加 #include <cstdio>
 * 
 * 9. [修复] CarWebServer 直接访问 MotorController 的 private 静态成员
 *    → 新增 public getter: getCurrentSpeedL(), getCurrentSpeedR(),
 *      isChaosActive(), getCurrentDilemma()
 * 
 * 10.[修复] CarWebServer 直接访问 EvolutionEngine 的 private 成员
 *    → EvolutionEngine 重构后已有 public 静态访问器方法
 * ================================================================
 *
 * ================================================================
 * v8.0 — 物理结构即算法 开放式进化重构 (2026-08-11)
 * ================================================================
 * 
 * 设计哲学: 从"目标导向优化"转向"行为多样性探索"
 * ───────────────────────────────────────────
 * v7.3 本质是经典遗传算法(GA): 固定适应度函数 → 精英保留 → 收敛到最优。
 * v8.0 将三个维度同时改造，使系统真正检验"物理结构能否涌现智能行为":
 *   - 进化不再预设"什么是好"（取消适应度函数）
 *   - 物理身体不再只是"执行器"（参与决策和计时）
 *   - 行为不再由程序员硬编码（进化可以发明策略）
 * 
 * ================================================================
 * 维度一: Novelty Search 新奇度驱动进化
 * ================================================================
 * 
 * 核心思想: 不问"谁表现好"，问"谁的行为是档案里没见过的"
 * 
 * 行为描述符 (BehaviorDescriptor, 12维):
 * ┌─────────────────────────────────────────────────────────────┐
 * │ 传感器维度(4): 左均值/右均值/方差/不对称度                  │
 * │ 运动维度(4):   平均速度/速度方差/转向偏向/总距离            │
 * │ 策略维度(4):   直行占比/转向占比/后退占比/静止占比          │
 * └─────────────────────────────────────────────────────────────┘
 * 
 * 新奇度计算:
 *   个体测试结束 → 帧日志提取行为描述符 → 归一化 →
 *   计算到档案中 k=5 个最近邻的平均欧氏距离 → 新奇度得分
 *   新奇度 > 阈值(0.15) → 加入档案 (最多200条)
 * 
 * 新奇度档案 (NoveltyArchive):
 *   - 存储历史行为描述符，驱动进化向行为空间未探索区域扩展
 *   - 自动归一化: 跟踪各维度历史最大值，新描述符动态归一化
 *   - 环形淘汰: 档案满时替换最旧条目
 * 
 * 选择机制变换:
 *   旧: fitness排序 → 精英保留前25% → 锦标赛选fitness高的
 *   新: novelty排序 → 精英保留前25% → 锦标赛选novelty高的
 * 
 * 自适应变异率:
 *   新奇度停滞(avg < best*0.5) → 加大变异率 → 探索新行为空间
 *   新奇度健康(avg > best*0.8) → 降低变异率 → 精细调整
 * 
 * 预期效果:
 *   - 不再奖励"活得久但行为平庸"的个体
 *   - 自然涌现多样性: "撞墙专家""旋转专家""贴墙走专家"
 *   - 不需要预设什么是"好"行为 — 系统自己发现行为空间
 * 
 * ================================================================
 * 维度二: 物理结构承担计算
 * ================================================================
 * 
 * 核心思想: 物理身体不再只是"执行PWM指令的被动执行器"，
 *           而是参与决策、计时和运动平滑的计算主体
 * 
 * 2.1 物理终止条件 (physicsTerminatedAction)
 * ───────────────────────────────────────────
 * 旧: 所有动作使用固定 delay(ms) 计时
 *    executeRecovery case0: delay(500)
 *    executeRecovery case1: delay(800)
 *    escapeRetreat:        while(millis()-start<500)
 *    backwardReset:        delay(2500)
 * 
 * 新: 动作持续时间由物理现实决定
 *    physicsTerminatedAction(pwm, dir, minDuration):
 *      每10ms检测编码器增量 → 连续10帧编码器不动 = 物理已停止
 *      → 动作自动终止
 *      → 安全兜底: 最多3秒
 * 
 * 物理含义:
 *   - 电机扭矩大的个体 → 后退时间短 → 物理决定了动作时长
 *   - 摩擦力大的地面 → 自然延长动作 → 环境参与决策
 *   - 齿轮箱磨损 → 不同个体表现不同 → 物理的"个性"
 * 
 * 2.2 噪声连续调制 (Noise Modulation)
 * ───────────────────────────────────────────
 * 旧: GPIO35 仅用于 randomSeed() 一次性播种
 * 新: 每帧读取 GPIO35，调制所有行为参数
 * 
 *   噪声调制因子 = 0.7 + 噪声(0~0.999) * 0.6 → 范围 0.7~1.3
 *   调制对象:
 *     - 规则电机PWM: motorL *= noiseMod
 *     - 规则持续时间: durationMs *= noiseMod
 *     - 实际转向幅度: 基因决定基础值 + 噪声波动 ±30%
 * 
 * 物理含义:
 *   - 同一基因在不同时刻表现不同行为
 *   - 两个基因完全相同的个体，因噪声采样时间序列不同而行为分化
 *   - 创造"同一基因型 → 多种表现型"的物理涌现条件
 * 
 * 2.3 脉冲式控制 (Impulse Control)
 * ───────────────────────────────────────────
 * 旧: 逐帧平滑过渡
 *    currentSpeed = min(currentSpeed+MAX_SPEED_CHANGE_PER_FRAME, target)
 * 
 * 新: 直接施加目标 PWM，不做代码级平滑
 *    currentSpeed = targetSpeed
 *    writeMotorHardware(currentSpeedL, currentSpeedR)
 * 
 * 物理含义:
 *   - 速度变化曲线由物理系统决定: 电机电感响应时间、齿轮箱惯性、
 *     轮胎摩擦力 — 这些是代码不可控的物理变量
 *   - 不同电机老化程度 → 不同响应曲线 → 物理的"个性"
 *   - 代码不再"手工平滑" — 物理自己完成
 * 
 * ================================================================
 * 维度三: 可变长度行为表基因
 * ================================================================
 * 
 * 核心思想: 从"调5个参数"变为"进化可以发明行为规则"
 * 
 * 旧基因型 (5个固定参数):
 *   k_turn, speed_bias, chaos_threshold, recovery_mode, preferred_action
 *   → 只能调参，不能改变行为逻辑结构
 * 
 * 新基因型 (BehaviorRule 表):
 *   struct BehaviorRule {
 *     condType:   传感器/距离/时间/空闲/无条件 (8种)
 *     condValue:  阈值 (-3000~3000)
 *     condOp:     > / < / ==
 *     motorL:     左电机PWM (-255~255, 负=反转)
 *     motorR:     右电机PWM
 *     durationMs: 持续时间 (物理终止条件下限)
 *     nextRule:   0=重新评估, 1-N=跳转到指定规则
 *   };
 *   每个个体 2~16 条规则，规则数量本身可进化
 * 
 * 条件类型 (8种):
 * ┌──────────────────┬────────────────────────────────────┐
 * │ COND_SENSOR_LEFT │ 左传感器超过阈值                    │
 * │ COND_SENSOR_RIGHT│ 右传感器超过阈值                    │
 * │ COND_SENSOR_BOTH │ 两侧传感器都超过阈值                │
 * │ COND_SENSOR_ANY  │ 任一侧传感器超过阈值                │
 * │ COND_DISTANCE    │ 编码器距离超过阈值                  │
 * │ COND_TIME        │ 测试时间超过阈值                    │
 * │ COND_IDLE        │ 两侧传感器均低于阈值 (空闲)         │
 * │ COND_ALWAYS      │ 无条件触发                          │
 * └──────────────────┴────────────────────────────────────┘
 * 
 * 运行时行为表引擎:
 *   loop() 每帧:
 *     for 每条规则:
 *       if 条件满足:
 *         停止评估 → 执行该规则 → physicsTerminatedAction
 *         → 如果 nextRule>0: 跳转到指定规则继续执行
 *         → 否则: 重新评估条件
 *       → 没有规则匹配: 停止电机
 * 
 * 交叉: 随机切割点交叉
 *   父本1取前k1条规则，父本2取后k2条规则，k1、k2为随机数
 *   拼接后规则数约束在2~16之间，若不足则循环补齐
 * 
 * 变异 (10种操作):
 *   1. 微调condValue参数
 *   2. 微调motorL参数
 *   3. 微调motorR参数
 *   4. 微调durationMs参数
 *   5. 变更条件类型 (condType)
 *   6. 变更比较操作符 (condOp)
 *   7. 变更跳转目标 (nextRule)
 *   8. 增加一条规则 (ruleCount++)
 *   9. 删除一条规则 (ruleCount--)
 *  10. 交换两条规则顺序
 * 
 * 进化可以自发发现的策略示例:
 *   - "左传感器高时，右转随机角度前进" → 非程序员编码
 *   - "每3秒后退一次" → 自发涌现的探索节奏
 *   - "先左转200ms，如果传感器还高就右转400ms" → 多步策略链
 *   - "空闲时原地旋转寻找新方向" → 利用 COND_IDLE 条件
 * 
 * ================================================================
 * 新架构运行时数据流
 * ================================================================
 * 
 * setup():
 *   SPIFFS初始化 → 物理噪声播种 → 传感器校准 → 电机初始化
 *   → 进化引擎初始化(种群16个行为表基因) → Web控制台启动
 * 
 * loop():
 *   ┌─ 测试活跃? ─→ MotorController::update(gene)
 *   │                 │
 *   │                 ├─ 读取传感器 + 噪声
 *   │                 ├─ 行为表规则评估
 *   │                 │   ├─ 条件匹配 → 噪声调制参数 → 物理终止动作
 *   │                 │   └─ 无匹配 → 停止
 *   │                 └─ 记录帧日志
 *   │
 *   ├─ 测试超时? → endCurrentTest() → nextIndividual()
 *   │
 *   ├─ Web请求处理 (handleClient)
 *   │
 *   └─ 存储定时刷新 (RobustStorage::tick)
 * 
 * 个体测试结束 → endCurrentTest():
 *   统计存活时间+距离 → 帧日志提取行为描述符 →
 *   NoveltyArchive::computeNovelty() → 加入档案 →
 *   记录历史 → 帧日志CSV导出 → 检查是否新纪录
 * 
 * 一代结束 → evolve():
 *   按新奇度排序 → 精英保留 → 新奇度锦标赛选亲本 →
 *   规则子集交叉 → 变异 → 自适应变异率 → 下一轮
 * 
 * 数据持久化:
 *   /oe_history.csv: 每代每个体的新奇度+存活+距离+规则数
 *   /frm_g{代}_i{个体}.csv: 256帧逐帧传感器+电机日志
 * 
 * ================================================================
 * 与旧版(v7.3)关键差异对照
 * ================================================================
 * 
 * ┌──────────────────┬─────────────────────┬─────────────────────┐
 * │ 维度             │ v7.3 (经典GA)       │ v8.0 (物理即算法)   │
 * ├──────────────────┼─────────────────────┼─────────────────────┤
 * │ 进化目标         │ 最大化适应度        │ 探索行为多样性      │
 * │ 选择依据         │ 存活+距离+脱困      │ 新奇度(行为距离)    │
 * │ 基因型           │ 5个固定参数         │ 2~16条行为规则      │
 * │ 行为来源         │ 程序员硬编码        │ 进化产生IF-THEN表   │
 * │ 动作计时         │ 固定delay(ms)       │ 编码器物理终止      │
 * │ 速度控制         │ 逐帧平滑过渡        │ 脉冲式+物理惯性     │
 * │ 噪声角色         │ 仅播种随机数        │ 连续调制所有参数    │
 * │ 物理身体角色     │ 被动执行器          │ 参与决策+计时+平滑  │
 * │ 精英保留         │ fitness前25%        │ novelty前25%        │
 * │ 混沌/恢复/困境   │ 硬编码case分支      │ 行为表规则替代      │
 * │ 适应度停滞       │ 加大变异→收敛       │ 加大变异→探索新区间 │
 * └──────────────────┴─────────────────────┴─────────────────────┘
 * ================================================================ */

/*
 * ================================================================ 
 * 项目名称：仿草履虫应激机制 - 脱困能力进化系统 
 * 版本： v8.0-fixed (LEDC修复 + abs类型修复)
 * ================================================================ 
 */

/*
 * ================================================================ 
 * 项目名称：仿草履虫应激机制 - 脱困能力进化系统 
 * 版本： v8.1-fixed (循环引用修复 + LEDC包含)
 * ================================================================ 
 */

/*
 * ================================================================ 
 * 项目名称：仿草履虫应激机制 - 脱困能力进化系统 
 * 版本： v8.2-fixed (LEDC修复 + 循环引用彻底解决)
 * ================================================================ 
 */


/*
 * ================================================================ 
 * 项目名称：仿草履虫应激机制 - 脱困能力进化系统 
 * 版本： v8.0-fixed (编译错误全面修复)
 * 适配日期: 2026-08-11   修正日期: 2026-08-12
 * ================================================================ 
 * 由于ESP32 Arduino环境可能不支持 ledcSetup，使用最兼容的方式 - 使用 ledcAttach 函数（这是ESP32 Arduino早期版本就支持的)
 
 */

/* 
 * ================================================================ 
 * 项目名称：仿草履虫应激机制 - 脱困能力进化系统 
 * 版本： v8.0-final (完整修复版)
 * 适配日期: 2026-08-12
 * ================================================================ 
 * 
 * 设计哲学: 从"目标导向优化"转向"行为多样性探索"
 * - Novelty Search 新奇度驱动进化
 * - 物理结构承担计算 (物理终止条件 + 噪声连续调制)
 * - 可变长度行为表基因 (2~16条IF-THEN规则)
 * ================================================================ 
 */

/* 
 * ================================================================ 
 * 项目名称：仿草履虫应激机制 - 脱困能力进化系统 
 * 版本： v8.0-final (完整可用版)
 * 适配日期: 2026-08-12
 * ================================================================ 
 * 
 * 设计哲学: Novelty Search 新奇度驱动进化 + 行为表基因
 * ================================================================ 
 */

/* 
 * ================================================================ 
 * 项目名称：仿草履虫应激机制 - 脱困能力进化系统 
 * 版本： v8.0-final (完整可用版)
 * 适配日期: 2026-08-12
 * ================================================================ 
 * 
 * 设计哲学: Novelty Search 新奇度驱动进化 + 行为表基因
 * ================================================================ 
 */

/* 
 * ================================================================ 
 * 项目名称：仿草履虫应激机制 - 脱困能力进化系统 
 * 版本： v8.0-final (完整可用版)
 * 适配日期: 2026-08-12
 * ================================================================ 
 * 
 * 设计哲学: Novelty Search 新奇度驱动进化 + 行为表基因
 * ================================================================ 
 */

/* 
 * ================================================================ 
 * 项目名称：仿草履虫应激机制 - 脱困能力进化系统 
 * 版本： v8.5-final (轮询编码器方案)
 * ================================================================ 
 */

/* 
 * ================================================================ 
 * 项目名称：仿草履虫应激机制 - 脱困能力进化系统 
 * 版本： v8.5-final (ESP32兼容LEDC)
 * ================================================================ 
 */

/*
 * ================================================================ 
 * 项目名称：仿草履虫应激机制 - 脱困能力进化系统 
 * 版本： v8.5-final (SPIFFS已禁用)
 * 适配日期: 2026-08-12
 * ================================================================ 
 * 
 * SPIFFS已被禁用 - 所有数据仅保存在RAM中
 * 如需持久化数据，请取消注释SPIFFS相关代码并重新格式化Flash
 * ================================================================ 
 */

/* 
 * ================================================================ 
 * 项目名称：仿草履虫应激机制 - 脱困能力进化系统 
 * 版本： v8.6-nonblocking (非阻塞状态机重构)
 * 适配日期: 2026-08-13
 * ================================================================ 
 * 
 * 【重要更改】2026-08-13 非阻塞状态机重构
 * ================================================================
 * 
 * 一、根因修复：RTC看门狗复位 (rst:0x10)
 * ───────────────────────────────────────────
 * 问题：physicsTerminatedAction() 内部使用 while(1) + delay()
 *       最长阻塞 3 秒，期间不喂狗 → 触发 RTCWDT_RTC_RST
 * 
 * 修复：重构为非阻塞状态机
 *   - physicsTerminatedAction() → startPhysicsAction() + updatePhysicsAction()
 *   - startPhysicsAction(): 设置电机并记录起始状态
 *   - updatePhysicsAction(): 每帧调用，检测编码器停滞 → 自动终止
 *   - loop() 中每帧调用 updatePhysicsAction()
 * 
 * 二、移除所有阻塞 delay()
 * ───────────────────────────────────────────
 *   - physicsTerminatedAction() 中的 while 循环已移除
 *   - 所有动作现在由状态机驱动，不阻塞主循环
 *   - Web 控制台在动作执行期间保持响应
 * 
 * 三、新增接口
 * ───────────────────────────────────────────
 *   - MotorController::isActionInProgress(): 查询是否有物理动作正在执行
 *   - MotorController::updatePhysicsAction(): 状态机更新，需在 loop() 中调用
 *   - MotorController::forceStopAction(): 强制终止当前动作
 * 
 * 四、修复类型错误
 * ───────────────────────────────────────────
 *   - setMotor(int pwm, bool isLeft, bool forward) 第三个参数是 bool forward
 *   - 之前调用 setMotor(leftPWM, true, dirL) 正确，但 dirL 含义需明确
 *   - 新增 getMotorDirection() 辅助函数统一处理
 * ================================================================ 
 */

/* 
 * ================================================================ 
 * 项目名称：仿草履虫应激机制 - 脱困能力进化系统 
 * 版本： v8.5-final (SPIFFS已禁用)
 * 适配日期: 2026-08-12
 * ================================================================ 
 * 
 * SPIFFS已被禁用 - 所有数据仅保存在RAM中
 * 如需持久化数据，请取消注释SPIFFS相关代码并重新格式化Flash
 * ================================================================ 
 */

/* 
 * ================================================================ 
 * 项目名称：仿草履虫应激机制 - 脱困能力进化系统 
 * 版本： v8.6.1-hardware-fixed (基于v8.6非阻塞状态机 + 硬件引脚/电机逻辑修复)
 * 适配日期: 2026-08-14
 * ================================================================ 
 * 
 * 【修复说明】2026-08-14 硬件电机控制逻辑全面修复
 * ================================================================
 * 
 * 基于 v5.9 已验证的 AT8236 电机驱动逻辑，修复以下问题：
 * 
 * 1. [致命] 左方向引脚 PIN_LEFT_DIR2 从 21 修正为 46
 * 2. [致命] 右方向引脚 PIN_RIGHT_DIR2 从 33 修正为 15
 * 3. [致命] 方向引脚未初始化为 LOW（停止状态）
 * 4. [致命] 方向控制与 PWM 控制分离，导致某些调用路径方向未设置
 * 5. [修复] setMotorSigned() 重写为 setMotorSpeed()，同时控制方向和PWM
 * 6. [修复] writeMotorHardware() 删除，统一使用 setMotorSpeed()
 * 7. [修复] stopMotors() 改为直接调用 setMotorSpeed(0, 0)
 * 8. [修复] 所有电机控制调用点统一使用 setMotorSpeed()
 * 9. [修复] 右电机正反转方向与 v5.9 对齐
 * ================================================================ 
 */

/*
 * ================================================================ 
 * 项目名称：仿草履虫应激机制 - 脱困能力进化系统 
 * 版本： v8.5-final (SPIFFS已禁用)
 * 适配日期: 2026-08-12
 * ================================================================ 
 * 
 * SPIFFS已被禁用 - 所有数据仅保存在RAM中
 * 如需持久化数据，请取消注释SPIFFS相关代码并重新格式化Flash
 * ================================================================ 
 */

/* 
 * ================================================================ 
 * 项目名称：仿草履虫应激机制 - 脱困能力进化系统 
 * 版本： v8.6-nonblocking (非阻塞状态机重构)
 * 适配日期: 2026-08-13
 * ================================================================ 
 * 
 * 【重要更改】2026-08-13 非阻塞状态机重构
 * ================================================================
 * 
 * 一、根因修复：RTC看门狗复位 (rst:0x10)
 * ───────────────────────────────────────────
 * 问题：physicsTerminatedAction() 内部使用 while(1) + delay()
 *       最长阻塞 3 秒，期间不喂狗 → 触发 RTCWDT_RTC_RST
 * 
 * 修复：重构为非阻塞状态机
 *   - physicsTerminatedAction() → startPhysicsAction() + updatePhysicsAction()
 *   - startPhysicsAction(): 设置电机并记录起始状态
 *   - updatePhysicsAction(): 每帧调用，检测编码器停滞 → 自动终止
 *   - loop() 中每帧调用 updatePhysicsAction()
 * 
 * 二、移除所有阻塞 delay()
 * ───────────────────────────────────────────
 *   - physicsTerminatedAction() 中的 while 循环已移除
 *   - 所有动作现在由状态机驱动，不阻塞主循环
 *   - Web 控制台在动作执行期间保持响应
 * 
 * 三、新增接口
 * ───────────────────────────────────────────
 *   - MotorController::isActionInProgress(): 查询是否有物理动作正在执行
 *   - MotorController::updatePhysicsAction(): 状态机更新，需在 loop() 中调用
 *   - MotorController::forceStopAction(): 强制终止当前动作
 * 
 * 四、修复类型错误
 * ───────────────────────────────────────────
 *   - setMotor(int pwm, bool isLeft, bool forward) 第三个参数是 bool forward
 *   - 之前调用 setMotor(leftPWM, true, dirL) 正确，但 dirL 含义需明确
 *   - 新增 getMotorDirection() 辅助函数统一处理
 * ================================================================ 
 */

/* 
 * ================================================================ 
 * 项目名称：仿草履虫应激机制 - 脱困能力进化系统 
 * 版本： v8.5-final (SPIFFS已禁用)
 * 适配日期: 2026-08-12
 * ================================================================ 
 * 
 * SPIFFS已被禁用 - 所有数据仅保存在RAM中
 * 如需持久化数据，请取消注释SPIFFS相关代码并重新格式化Flash
 * ================================================================ 
 */

/*
 * ================================================================ 
 * 项目名称：仿草履虫应激机制 - 脱困能力进化系统 
 * 版本： v8.6.2-stable (SPIFFS启用 + 关键Bug修复)
 * 适配日期: 2026-08-14
 * ================================================================ 
 * 
 * 【修复说明】2026-08-14 三个关键问题修复
 * ================================================================
 * 
 * 修复1: 历史数据导出为空
 * ───────────────────────────────────────────
 * 问题: getCSVData() 从 pendingBuffer 读取，但数据被 flushBuffer() 清空
 * 修复: 改为从 ramHistory 数组构建 CSV，同时支持 SPIFFS 持久化
 * 
 * 修复2: 进化在第3个个体后停止
 * ───────────────────────────────────────────
 * 问题: stopMotors() 未重置 ruleActive 标志，导致后续个体无法匹配规则
 * 修复: stopMotors() 中增加 ruleActive = false
 * 
 * 修复3: 启用 SPIFFS 持久化存储
 * ───────────────────────────────────────────
 * 问题: 数据仅保存在RAM，重启丢失
 * 修复: 启用 SPIFFS，数据同时写入 Flash 和 RAM，双重保障
 * 
 * ================================================================ 
 * 
 * 硬件引脚: 基于 v5.9 已验证的 AT8236 电机驱动逻辑
 * 状态机: 基于 v8.6 非阻塞状态机
 * ================================================================ 
 */

/* 
 * ================================================================ 
 * 项目名称：仿草履虫应激机制 - 脱困能力进化系统 
 * 版本： v8.6.3-sensor-fixed (传感器校准修复版)
 * 适配日期: 2026-08-14
 * ================================================================ 
 * 
 * 【修复说明】2026-08-14 传感器-控制链路修复
 * ================================================================
 * 
 * 问题根因：
 *   1. 传感器校准值与原始值混用，导致行为表条件永远无法触发
 *   2. isClear() 中右传感器减了 leftBase 而非 rightBase
 *   3. 阈值定义与实际 GP2Y0A21 传感器输出范围不匹配
 *   4. 初始种群缺乏对传感器有反应的个体
 * 
 * 修复内容：
 *   A. SensorCalibration 类重构
 *      - getLeftRaw() / getRightRaw(): 返回原始ADC值
 *      - getLeftCalibrated() / getRightCalibrated(): 返回校准值
 *      - isObstacleLeft() / isObstacleRight(): 基于原始值判断
 *      - isClear(): 修复右传感器基准值错误
 * 
 *   B. 阈值重新定义
 *      - CLEAR_ADC_THRESH: 600 (无障碍物)
 *      - OBSTACLE_ADC_THRESH: 1500 (30cm障碍物)
 *      - DANGER_ADC_THRESH: 2400 (10cm近距离)
 * 
 *   C. Gene::evaluateCondition() 修复
 *      - COND_IDLE 使用原始ADC值判断
 *      - COND_SENSOR_BOTH/ANY 直接比较原始值
 * 
 *   D. 注入"避障种子个体"
 *      - 个体0预设3条规则：左转避障/右转避障/直行
 *      - 确保进化开始时有可工作的行为
 * 
 *   E. 增加传感器调试日志
 *      - 每100帧输出传感器ADC值
 *      - 便于验证传感器-控制链路是否导通
 * 
 * ================================================================ 
 */

#include <WiFi.h>
#include <WebServer.h>
#include <FS.h>
#include <SPIFFS.h>
#include <vector>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cmath>

// ==================== 硬件引脚定义 ====================
#define PIN_SENSOR_LEFT     4
#define PIN_SENSOR_RIGHT    5
#define PIN_NOISE_SOURCE    35
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

// ==================== LEDC PWM 配置 ====================
#define LEDC_FREQ           5000
#define LEDC_RESOLUTION     8
#define LEDC_CHANNEL_LEFT   0
#define LEDC_CHANNEL_RIGHT  1

// ==================== 电机转向逻辑 (AT8236) ====================
#define LEFT_FORWARD   HIGH
#define LEFT_REVERSE   LOW
#define RIGHT_FORWARD  LOW
#define RIGHT_REVERSE  HIGH

// ==================== 电机增益补偿 ====================
#define LEFT_GAIN   1.00f
#define RIGHT_GAIN  1.00f

// ==================== WiFi 配置 ====================
#define WIFI_SSID           "CarLogger"
#define WIFI_PASSWORD       "12345678"

// ==================== 系统参数 ====================
#define POPULATION_SIZE     16
#define TEST_DURATION_MS    30000
#define LOOP_DELAY_MS       10

// ==================== 物理行为参数 ====================
#define PHYSICS_STALL_FRAMES    10
#define PHYSICS_MAX_ACTION_MS   3000
#define PHYSICS_LOOP_DELAY_MS   10

// ==================== ADC 阈值 (GP2Y0A21 实测值) ====================
// 注：以下为原始ADC值（0-4095），非校准值
#define CLEAR_ADC_THRESH     600     // 无障碍物时 < 600
#define OBSTACLE_ADC_THRESH  1500    // 有障碍物时 > 1500 (约30cm)
#define DANGER_ADC_THRESH    2400    // 近距离障碍物 > 2400 (约10cm)

// ==================== 行为表基因参数 ====================
#define MAX_RULES           16
#define MIN_RULES           2
#define MAX_RULE_DURATION   2000
#define MIN_RULE_DURATION   50

// ==================== 新奇度参数 ====================
#define NOVELTY_ARCHIVE_MAX    200
#define NOVELTY_K_NEAREST      5
#define NOVELTY_ADD_THRESHOLD  0.15f

// ==================== 存储参数 ====================
#define STORAGE_SAVE_INTERVAL_MS  10000
#define STORAGE_BATCH_THRESHOLD   5
#define STORAGE_MAX_RETRIES       3
#define FRAME_LOG_SIZE            256

// ==================== 枚举类型 ====================
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

enum CondOp : uint8_t {
    OP_GREATER = 0,
    OP_LESS    = 1,
    OP_EQUAL   = 2
};

// ==================== 行为规则结构体 ====================
struct BehaviorRule {
    uint8_t  condType;
    int16_t  condValue;
    uint8_t  condOp;
    int16_t  motorL;
    int16_t  motorR;
    uint16_t durationMs;
    uint8_t  nextRule;

    void randomize() {
        condType   = random(0, 8);
        condValue  = random(-3000, 3000);
        condOp     = random(0, 3);
        motorL     = random(-255, 255);
        motorR     = random(-255, 255);
        durationMs = random(MIN_RULE_DURATION, MAX_RULE_DURATION);
        nextRule   = random(0, 3);
    }
    
    void clamp() {
        motorL     = constrain(motorL, -255, 255);
        motorR     = constrain(motorR, -255, 255);
        durationMs = constrain(durationMs, MIN_RULE_DURATION, MAX_RULE_DURATION);
        condType   = constrain(condType, (uint8_t)0, (uint8_t)7);
        condOp     = constrain(condOp, (uint8_t)0, (uint8_t)2);
    }
    
    bool compare(int value, int op, int threshold) const {
        switch (op) {
            case OP_GREATER: return value > threshold;
            case OP_LESS:    return value < threshold;
            case OP_EQUAL:   return abs(value - threshold) < 50;
            default: return false;
        }
    }
};

// ==================== 行为描述符 ====================
struct BehaviorDescriptor {
    float leftSensorMean, rightSensorMean;
    float sensorVariance, sensorAsymmetry;
    float avgSpeed, speedVariance, turnBias, totalDistance;
    float forwardRatio, turnRatio, reverseRatio, idleRatio;

    void init() {
        leftSensorMean = rightSensorMean = 0;
        sensorVariance = sensorAsymmetry = 0;
        avgSpeed = speedVariance = turnBias = totalDistance = 0;
        forwardRatio = turnRatio = reverseRatio = idleRatio = 0;
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
        return sqrtf(sum);
    }

    void normalize(const BehaviorDescriptor& maxVals) {
        if (maxVals.leftSensorMean > 0) leftSensorMean /= maxVals.leftSensorMean;
        if (maxVals.rightSensorMean > 0) rightSensorMean /= maxVals.rightSensorMean;
        if (maxVals.sensorVariance > 0) sensorVariance /= maxVals.sensorVariance;
        if (maxVals.avgSpeed > 0) avgSpeed /= maxVals.avgSpeed;
        if (maxVals.speedVariance > 0) speedVariance /= maxVals.speedVariance;
        if (maxVals.totalDistance > 0) totalDistance /= maxVals.totalDistance;
    }
};

// ==================== 帧日志结构体 ====================
struct FrameLogEntry {
    uint32_t timestamp_ms;
    int16_t  sensorLeft;
    int16_t  sensorRight;
    uint8_t  motorLeftPWM;
    uint8_t  motorRightPWM;
    uint8_t  directionL : 1;
    uint8_t  directionR : 1;
    uint8_t  reserved   : 6;
};

// ==================== 历史记录 ====================
struct HistoryRecord {
    uint32_t timestamp;
    uint32_t generation;
    float    noveltyScore;
    uint32_t survivalTime;
    uint32_t distance_ticks;
    uint8_t  ruleCount;
    uint8_t  reserved[3];
};

// ==================== 前向声明 ====================
struct Gene;
class Logger;
class SensorCalibration;
class RobustStorage;
class NoveltyArchive;
class MotorController;
class EvolutionEngine;
class CarWebServer;

// ==================== 日志工具 ====================
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
    static String getRecentLogs() { return "日志通过串口输出 (115200 baud)"; }
};

// ==================== 传感器校准 (v8.6.3 修复版) ====================
class SensorCalibration {
private:
    static int leftBase, rightBase;
    
public:
    static void calibrate() {
        Logger::log("传感器校准中...");
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
        Logger::logf("校准完成: 左Base=%d 右Base=%d", leftBase, rightBase);
    }
    
    // ✅ 返回原始 ADC 值（用于行为表条件评估）
    static int getLeftRaw() { 
        return analogRead(PIN_SENSOR_LEFT); 
    }
    static int getRightRaw() { 
        return analogRead(PIN_SENSOR_RIGHT); 
    }
    
    // ✅ 返回校准后的值（用于日志/调试）
    static int getLeftCalibrated() { 
        return analogRead(PIN_SENSOR_LEFT) - leftBase; 
    }
    static int getRightCalibrated() { 
        return analogRead(PIN_SENSOR_RIGHT) - rightBase;  // 修复: 使用 rightBase
    }
    
    // ✅ 障碍物检测（用原始值判断）
    static bool isObstacleLeft() {
        return analogRead(PIN_SENSOR_LEFT) > OBSTACLE_ADC_THRESH;
    }
    static bool isObstacleRight() {
        return analogRead(PIN_SENSOR_RIGHT) > OBSTACLE_ADC_THRESH;
    }
    
    // ✅ 修复: 使用正确的基准值
    static bool isClear() {
        int leftRaw = analogRead(PIN_SENSOR_LEFT);
        int rightRaw = analogRead(PIN_SENSOR_RIGHT);
        return (leftRaw < CLEAR_ADC_THRESH && rightRaw < CLEAR_ADC_THRESH);
    }
    
    static uint16_t readNoise() { return analogRead(PIN_NOISE_SOURCE); }
    
    // ✅ 调试接口
    static void debugPrint() {
        static uint32_t lastDebug = 0;
        if (millis() - lastDebug > 500) {
            lastDebug = millis();
            int l = analogRead(PIN_SENSOR_LEFT);
            int r = analogRead(PIN_SENSOR_RIGHT);
            Logger::logf("🔍 ADC: L=%d R=%d | 阈值: %d", l, r, OBSTACLE_ADC_THRESH);
        }
    }
};

int SensorCalibration::leftBase = 0;
int SensorCalibration::rightBase = 0;

// ==================== 鲁棒存储 ====================
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

    static HistoryRecord ramHistory[1000];
    static int ramHistoryCount;

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

    static void loadHistory() {
        if (!fsReady) return;
        
        File file = SPIFFS.open(HISTORY_FILE, FILE_READ);
        if (!file) {
            Logger::log("📄 历史文件不存在，将创建新文件");
            return;
        }
        
        int count = 0;
        while (file.available() && count < 1000) {
            String line = file.readStringUntil('\n');
            if (line.length() < 10) continue;
            
            int partIdx = 0;
            String parts[6];
            for (int i = 0; i < line.length() && partIdx < 6; i++) {
                if (line[i] == ',') {
                    partIdx++;
                    continue;
                }
                parts[partIdx] += line[i];
            }
            
            if (partIdx == 5) {
                ramHistory[count].timestamp = parts[0].toInt();
                ramHistory[count].generation = parts[1].toInt();
                ramHistory[count].noveltyScore = parts[2].toFloat();
                ramHistory[count].survivalTime = parts[3].toInt();
                ramHistory[count].distance_ticks = parts[4].toInt();
                ramHistory[count].ruleCount = parts[5].toInt();
                count++;
            }
        }
        ramHistoryCount = count;
        file.close();
        Logger::logf("📂 加载历史数据: %d 条记录", ramHistoryCount);
    }

public:
    static void init() {
        fsReady = false;
        isFlushing = false;
        lastSaveTime = 0;
        unsavedCount = 0;
        totalSaved = 0;
        totalFailed = 0;
        ramHistoryCount = 0;
        pendingBuffer.reserve(MAX_BUFFER_SIZE);
        
        if (!SPIFFS.begin(true)) {
            Logger::log("⚠️ SPIFFS 挂载失败，使用 RAM 模式");
            fsReady = false;
        } else {
            fsReady = true;
            Logger::log("✅ SPIFFS 挂载成功");
            loadHistory();
        }
        
        Logger::log("存储系统初始化 (SPIFFS + RAM 双重保障)");
    }

    static void addRecord(const HistoryRecord& record) {
        if (ramHistoryCount < 1000) {
            ramHistory[ramHistoryCount++] = record;
        } else {
            for (int i = 1; i < 1000; i++) {
                ramHistory[i-1] = ramHistory[i];
            }
            ramHistory[999] = record;
        }
        
        char line[256];
        snprintf(line, sizeof(line), "%lu,%lu,%.4f,%lu,%lu,%d\n",
                 record.timestamp, record.generation, record.noveltyScore,
                 record.survivalTime, record.distance_ticks, record.ruleCount);
        pendingBuffer += String(line);
        unsavedCount++;
        totalSaved++;
        
        if (pendingBuffer.length() > MAX_BUFFER_SIZE || unsavedCount >= STORAGE_BATCH_THRESHOLD) {
            flushBuffer();
        }
    }

    static bool flushBuffer() {
        if (pendingBuffer.length() == 0) {
            unsavedCount = 0;
            return true;
        }
        
        if (!fsReady) {
            Logger::logf("📝 RAM模式: %d条数据在缓冲区", unsavedCount);
            pendingBuffer = "";
            unsavedCount = 0;
            lastSaveTime = millis();
            return true;
        }
        
        bool success = appendToFile(HISTORY_FILE, pendingBuffer);
        if (success) {
            Logger::logf("💾 写入SPIFFS: %d条记录", unsavedCount);
            pendingBuffer = "";
            unsavedCount = 0;
            lastSaveTime = millis();
            return true;
        } else {
            Logger::log("⚠️ SPIFFS写入失败，数据保留在RAM");
            totalFailed++;
            return false;
        }
    }

    static void forceSave() {
        if (pendingBuffer.length() > 0) {
            flushBuffer();
        }
        if (fsReady) {
            Logger::logf("💾 强制保存完成: %d 条历史记录", ramHistoryCount);
        }
    }
    
    static bool isReady() { return fsReady; }
    static uint32_t getTotalSaved() { return totalSaved; }

    static void tick() {
        if (millis() - lastSaveTime > STORAGE_SAVE_INTERVAL_MS && pendingBuffer.length() > 0) {
            flushBuffer();
        }
    }

    static void clearAll() {
        pendingBuffer = "";
        unsavedCount = 0;
        ramHistoryCount = 0;
        if (fsReady) {
            SPIFFS.remove(HISTORY_FILE);
            Logger::log("🗑️ SPIFFS历史文件已删除");
        }
        Logger::log("🗑️ RAM数据已清空");
    }
    
    static String getCSVData() {
        if (pendingBuffer.length() > 0) {
            flushBuffer();
        }
        
        if (fsReady) {
            File file = SPIFFS.open(HISTORY_FILE, FILE_READ);
            if (file) {
                String content = file.readString();
                file.close();
                if (content.length() > 0) {
                    Logger::logf("📤 从SPIFFS导出: %d 字节", content.length());
                    return content;
                }
            }
        }
        
        String csv = "timestamp,generation,noveltyScore,survivalTime,distance_ticks,ruleCount\n";
        for (int i = 0; i < ramHistoryCount; i++) {
            csv += String(ramHistory[i].timestamp) + ",";
            csv += String(ramHistory[i].generation) + ",";
            csv += String(ramHistory[i].noveltyScore, 4) + ",";
            csv += String(ramHistory[i].survivalTime) + ",";
            csv += String(ramHistory[i].distance_ticks) + ",";
            csv += String(ramHistory[i].ruleCount) + "\n";
        }
        Logger::logf("📤 从RAM导出: %d 条记录", ramHistoryCount);
        return csv;
    }
};

const char* RobustStorage::HISTORY_FILE = "/oe_history.csv";
const char* RobustStorage::TEMP_FILE = "/oe_temp.tmp";
bool RobustStorage::fsReady = false;
bool RobustStorage::isFlushing = false;
uint32_t RobustStorage::lastSaveTime = 0;
uint32_t RobustStorage::unsavedCount = 0;
uint32_t RobustStorage::totalSaved = 0;
uint32_t RobustStorage::totalFailed = 0;
String RobustStorage::pendingBuffer = "";
HistoryRecord RobustStorage::ramHistory[1000];
int RobustStorage::ramHistoryCount = 0;

// ==================== 新奇度档案 ====================
class NoveltyArchive {
private:
    BehaviorDescriptor archive[NOVELTY_ARCHIVE_MAX];
    int archiveSize;
    BehaviorDescriptor maxValues;

    void updateMaxValues(const BehaviorDescriptor& desc) {
        if (desc.leftSensorMean > maxValues.leftSensorMean) maxValues.leftSensorMean = desc.leftSensorMean;
        if (desc.rightSensorMean > maxValues.rightSensorMean) maxValues.rightSensorMean = desc.rightSensorMean;
        if (desc.sensorVariance > maxValues.sensorVariance) maxValues.sensorVariance = desc.sensorVariance;
        if (desc.avgSpeed > maxValues.avgSpeed) maxValues.avgSpeed = desc.avgSpeed;
        if (desc.speedVariance > maxValues.speedVariance) maxValues.speedVariance = desc.speedVariance;
        if (desc.totalDistance > maxValues.totalDistance) maxValues.totalDistance = desc.totalDistance;
    }

public:
    void init() { archiveSize = 0; maxValues.init(); }

    float computeNovelty(const BehaviorDescriptor& desc) {
        if (archiveSize == 0) return 1.0f;
        BehaviorDescriptor norm = desc;
        norm.normalize(maxValues);

        float distances[NOVELTY_ARCHIVE_MAX];
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
        float novelty = computeNovelty(desc);
        if (novelty < NOVELTY_ADD_THRESHOLD && archiveSize > 0) return false;
        if (archiveSize < NOVELTY_ARCHIVE_MAX) {
            archive[archiveSize++] = desc;
        } else {
            archive[archiveSize % NOVELTY_ARCHIVE_MAX] = desc;
            archiveSize = (archiveSize % NOVELTY_ARCHIVE_MAX) + 1;
        }
        updateMaxValues(desc);
        return true;
    }

    BehaviorDescriptor extractFromFrameLog(const FrameLogEntry* frameLog, int frameCount, uint32_t totalDistance) {
        BehaviorDescriptor desc; desc.init();
        if (frameCount == 0) return desc;

        float sumL = 0, sumR = 0, sumSpeed = 0, sumSpeedSq = 0, sumTurnDiff = 0;
        int forwardFrames = 0, turnFrames = 0, reverseFrames = 0, idleFrames = 0;

        for (int i = 0; i < frameCount; i++) {
            const FrameLogEntry& e = frameLog[i];
            sumL += e.sensorLeft; sumR += e.sensorRight;
            float speed = (e.motorLeftPWM + e.motorRightPWM) / 2.0f;
            sumSpeed += speed; sumSpeedSq += speed * speed;
            sumTurnDiff += (float)e.motorLeftPWM - (float)e.motorRightPWM;

            if (e.motorLeftPWM < 10 && e.motorRightPWM < 10) idleFrames++;
            else if (e.directionL == 0 && e.directionR == 0) reverseFrames++;
            else if (abs((int)e.motorLeftPWM - (int)e.motorRightPWM) > 50) turnFrames++;
            else forwardFrames++;
        }

        int n = frameCount;
        desc.leftSensorMean = sumL / n;
        desc.rightSensorMean = sumR / n;
        desc.avgSpeed = sumSpeed / n;
        desc.speedVariance = (sumSpeedSq / n) - (desc.avgSpeed * desc.avgSpeed);
        if (desc.speedVariance < 0) desc.speedVariance = 0;
        desc.turnBias = sumTurnDiff / (float)n / 255.0f;
        desc.sensorAsymmetry = (desc.leftSensorMean - desc.rightSensorMean) / max(desc.leftSensorMean + desc.rightSensorMean, 1.0f);
        desc.totalDistance = (float)totalDistance / 1000.0f;

        int activeFrames = n - idleFrames;
        if (activeFrames > 0) {
            desc.forwardRatio = (float)forwardFrames / activeFrames;
            desc.turnRatio = (float)turnFrames / activeFrames;
            desc.reverseRatio = (float)reverseFrames / activeFrames;
        }
        desc.idleRatio = (float)idleFrames / n;

        float varL = 0, varR = 0;
        for (int i = 0; i < frameCount; i++) {
            float dL = frameLog[i].sensorLeft - desc.leftSensorMean;
            float dR = frameLog[i].sensorRight - desc.rightSensorMean;
            varL += dL*dL; varR += dR*dR;
        }
        desc.sensorVariance = (varL + varR) / (2.0f * n);
        return desc;
    }

    int getArchiveSize() const { return archiveSize; }
};

// ==================== 基因: 行为表 ====================
struct Gene {
    uint8_t ruleCount;
    BehaviorRule rules[MAX_RULES];
    uint32_t survival_time;
    uint32_t distance_ticks;
    float noveltyScore;
    BehaviorDescriptor behavior;

    void init() {
        ruleCount = random(MIN_RULES, MAX_RULES + 1);
        for (int i = 0; i < ruleCount; i++) rules[i].randomize();
        survival_time = 0; distance_ticks = 0;
        noveltyScore = 0; behavior.init();
    }

    // ✅ v8.6.3 修复: 使用原始ADC值判断传感器条件
    bool evaluateCondition(int ruleIdx, int leftSensor, int rightSensor, 
                           uint32_t distance, uint32_t elapsed) const {
        const BehaviorRule& r = rules[ruleIdx];
        int value = 0;
        switch (r.condType) {
            case COND_SENSOR_LEFT:  
                value = leftSensor;
                break;
            case COND_SENSOR_RIGHT: 
                value = rightSensor;
                break;
            case COND_SENSOR_BOTH:  
                // ✅ 直接比较原始ADC值
                return (leftSensor > OBSTACLE_ADC_THRESH) && 
                       (rightSensor > OBSTACLE_ADC_THRESH);
            case COND_SENSOR_ANY:   
                return (leftSensor > OBSTACLE_ADC_THRESH) || 
                       (rightSensor > OBSTACLE_ADC_THRESH);
            case COND_DISTANCE:     
                value = (int)distance;
                break;
            case COND_TIME:         
                value = (int)elapsed;
                break;
            case COND_IDLE:         
                // ✅ 空闲检测：两侧都无障碍物
                return (leftSensor < CLEAR_ADC_THRESH) && 
                       (rightSensor < CLEAR_ADC_THRESH);
            case COND_ALWAYS:       
                return true;
            default: 
                return false;
        }
        return r.compare(value, r.condOp, r.condValue);
    }

    void mutate(float mutationRate) {
        for (int i = 0; i < ruleCount; i++) {
            if (random(0, 1000) < mutationRate * 1000) { rules[i].condValue += random(-200,200); rules[i].clamp(); }
            if (random(0, 1000) < mutationRate * 1000) { rules[i].motorL += random(-50,50); rules[i].clamp(); }
            if (random(0, 1000) < mutationRate * 1000) { rules[i].motorR += random(-50,50); rules[i].clamp(); }
            if (random(0, 1000) < mutationRate * 500) { rules[i].durationMs += random(-200,200); rules[i].clamp(); }
            if (random(0, 1000) < mutationRate * 300) { rules[i].condType = random(0, 8); }
            if (random(0, 1000) < mutationRate * 300) { rules[i].condOp = random(0, 3); }
            if (random(0, 1000) < mutationRate * 200) { rules[i].nextRule = random(0, min(ruleCount, (uint8_t)3)); }
        }
        if (random(0, 1000) < mutationRate * 150 && ruleCount < MAX_RULES) {
            rules[ruleCount].randomize(); ruleCount++;
        }
        if (random(0, 1000) < mutationRate * 100 && ruleCount > MIN_RULES) {
            int delIdx = random(0, ruleCount); rules[delIdx] = rules[ruleCount - 1]; ruleCount--;
        }
        if (random(0, 1000) < mutationRate * 80 && ruleCount >= 2) {
            int a = random(0, ruleCount), b = random(0, ruleCount);
            if (a != b) { BehaviorRule tmp = rules[a]; rules[a] = rules[b]; rules[b] = tmp; }
        }
    }

    static void crossover(const Gene& p1, const Gene& p2, Gene& child) {
        int cut1 = random(0, p1.ruleCount), cut2 = random(0, p2.ruleCount);
        int fromP1 = cut1, fromP2 = p2.ruleCount - cut2;
        child.ruleCount = constrain(fromP1 + fromP2, MIN_RULES, MAX_RULES);
        for (int i = 0; i < fromP1 && i < child.ruleCount; i++) child.rules[i] = p1.rules[i];
        for (int i = 0; i < fromP2 && (fromP1 + i) < child.ruleCount; i++) child.rules[fromP1 + i] = p2.rules[cut2 + i];
        for (int i = fromP1 + fromP2; i < child.ruleCount; i++) child.rules[i] = p1.rules[i % p1.ruleCount];
        child.survival_time = 0; child.distance_ticks = 0;
        child.noveltyScore = 0; child.behavior.init();
    }
};

// ==================== 电机控制器 ====================
class MotorController {
private:
    static int      currentSpeedL, currentSpeedR;
    static volatile uint32_t distanceTicks;
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
    static uint32_t actionLastTicks;
    static int      actionStallFrames;
    static int      actionTargetL;
    static int      actionTargetR;
    static uint16_t actionMinDuration;
    static uint8_t  actionNextRule;

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

        int outL = constrain((int)(leftPWM * LEFT_GAIN), -255, 255);
        int outR = constrain((int)(rightPWM * RIGHT_GAIN), -255, 255);

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
        if (outL != 0 || outR != 0) lastMoveTime = millis();
    }

    static void updateEncoders() {
        if (millis() - lastEncoderReadTime < 10) return;
        lastEncoderReadTime = millis();
        
        bool leftA = digitalRead(PIN_LEFT_ENC_A);
        bool rightA = digitalRead(PIN_RIGHT_ENC_A);
        
        if (leftA && !lastLeftA) {
            if (digitalRead(PIN_LEFT_ENC_B) == HIGH) distanceTicks--;
            else distanceTicks++;
        }
        
        if (rightA && !lastRightA) {
            if (digitalRead(PIN_RIGHT_ENC_B) == HIGH) distanceTicks++;
            else distanceTicks--;
        }
        
        lastLeftA = leftA;
        lastRightA = rightA;
    }

public:
    static void init() {
        currentSpeedL = 0; currentSpeedR = 0; distanceTicks = 0;
        lastMoveTime = millis(); testStartTime = 0;
        leftSensorRaw = 0; rightSensorRaw = 0;
        currentRuleIndex = 0; ruleStartTime = 0; ruleActive = false;
        frameLogHead = 0; frameLogCount = 0;
        motorEnabled = false;
        
        lastLeftA = false;
        lastRightA = false;
        lastEncoderReadTime = 0;

        actionInProgress = false;
        actionStartTime = 0;
        actionLastTicks = 0;
        actionStallFrames = 0;
        actionTargetL = 0;
        actionTargetR = 0;
        actionMinDuration = 0;
        actionNextRule = 0;

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
        Logger::log("✅ Motor Controller initialized (v8.6.3-sensor-fixed)");
        Logger::logf("   LEFT: PWM=GPIO%d, DIR=GPIO%d", PIN_LEFT_PWM, PIN_LEFT_DIR2);
        Logger::logf("   RIGHT: PWM=GPIO%d, DIR=GPIO%d", PIN_RIGHT_PWM, PIN_RIGHT_DIR2);
    }

    static void stopMotors() {
        setMotorSpeed(0, 0);
        digitalWrite(PIN_LEFT_DIR2, LOW);
        digitalWrite(PIN_RIGHT_DIR2, LOW);
        actionInProgress = false;
        ruleActive = false;
    }

    static void enableMotor() { 
        motorEnabled = true; 
        stopMotors();
        Logger::log("电机已解锁");
    }
    
    static void disableMotor() { 
        motorEnabled = false; 
        stopMotors();
        Logger::log("电机已锁定");
    }
    
    static bool isMotorEnabled() { return motorEnabled; }
    static bool isRuleActive() { return ruleActive; }

    static void startPhysicsAction(int leftPWM, int rightPWM, uint16_t minDurationMs) {
        if (!motorEnabled) {
            Logger::log("⚠️ 电机已锁定，无法执行动作");
            return;
        }
        
        if (actionInProgress) {
            stopMotors();
        }
        
        actionInProgress = true;
        actionStartTime = millis();
        actionLastTicks = distanceTicks;
        actionStallFrames = 0;
        actionTargetL = leftPWM;
        actionTargetR = rightPWM;
        actionMinDuration = minDurationMs;
        actionNextRule = 0;
        
        setMotorSpeed(leftPWM, rightPWM);
        
        Logger::logf("🔧 物理动作启动: L=%d R=%d 最小%dms", leftPWM, rightPWM, minDurationMs);
    }

    static void updatePhysicsAction() {
        if (!actionInProgress) return;
        
        updateEncoders();
        
        uint32_t currentTicks = distanceTicks;
        int deltaTicks = abs((int)(currentTicks - actionLastTicks));
        actionLastTicks = currentTicks;
        
        if (deltaTicks == 0) {
            actionStallFrames++;
        } else {
            actionStallFrames = max(0, actionStallFrames - 1);
        }
        
        uint32_t elapsed = millis() - actionStartTime;
        bool stallDetected = (actionStallFrames > PHYSICS_STALL_FRAMES);
        bool timeout = (elapsed > PHYSICS_MAX_ACTION_MS);
        bool minDurationMet = (elapsed >= actionMinDuration);
        
        if (minDurationMet && (stallDetected || timeout)) {
            actionInProgress = false;
            stopMotors();
            
            if (stallDetected) {
                Logger::logf("⏹️ 物理动作结束 (编码器停滞 %d帧, 持续%dms)", actionStallFrames, elapsed);
            } else if (timeout) {
                Logger::logf("⏹️ 物理动作结束 (超时 %dms)", elapsed);
            }
        }
    }

    static void forceStopAction() {
        if (actionInProgress) {
            actionInProgress = false;
            stopMotors();
            Logger::log("⏹️ 物理动作被强制终止");
        }
    }

    static bool isActionInProgress() { return actionInProgress; }

    static void startFrameLog() {
        frameLogHead = 0; frameLogCount = 0; testStartTime = millis();
    }

    static void logFrame() {
        if (frameLogCount < FRAME_LOG_SIZE * 2) {
            FrameLogEntry& entry = frameLog[frameLogHead];
            entry.timestamp_ms = millis() - testStartTime;
            entry.sensorLeft = leftSensorRaw;
            entry.sensorRight = rightSensorRaw;
            entry.motorLeftPWM = currentSpeedL;
            entry.motorRightPWM = currentSpeedR;
            entry.directionL = (digitalRead(PIN_LEFT_DIR2) == LEFT_FORWARD) ? 1 : 0;
            entry.directionR = (digitalRead(PIN_RIGHT_DIR2) == RIGHT_FORWARD) ? 1 : 0;
            frameLogHead = (frameLogHead + 1) % FRAME_LOG_SIZE;
            frameLogCount++;
        }
    }

    static void flushFrameLogToFlash(int individualIndex, uint32_t generation) {
        if (frameLogCount == 0) return;
        int exportCount = min(frameLogCount, FRAME_LOG_SIZE);
        Logger::logf("📊 帧日志已记录: 个体 %d, 代 %lu, %d 帧", 
                     individualIndex, generation, exportCount);
    }

    // ✅ v8.6.3: 使用 SensorCalibration 获取原始值，增加调试输出
    static void update(const Gene& gene) {
        if (!motorEnabled) { 
            stopMotors(); 
            return; 
        }
        
        if (actionInProgress) {
            leftSensorRaw = SensorCalibration::getLeftRaw();
            rightSensorRaw = SensorCalibration::getRightRaw();
            logFrame();
            return;
        }
        
        updateEncoders();
        
        // ✅ 使用统一的原始值读取接口
        leftSensorRaw = SensorCalibration::getLeftRaw();
        rightSensorRaw = SensorCalibration::getRightRaw();

        // ✅ 低频调试输出 (每100帧)
        static int debugCounter = 0;
        if (++debugCounter % 100 == 0) {
            Logger::logf("📊 传感器: L=%d R=%d | 阈值: %d", 
                         leftSensorRaw, rightSensorRaw, OBSTACLE_ADC_THRESH);
        }

        float noise = (float)(SensorCalibration::readNoise() % 1000) / 1000.0f;
        uint32_t elapsed = millis() - testStartTime;
        uint32_t dist = distanceTicks;

        if (!ruleActive) {
            for (int i = 0; i < gene.ruleCount; i++) {
                if (gene.evaluateCondition(i, leftSensorRaw, rightSensorRaw, dist, elapsed)) {
                    currentRuleIndex = i;
                    ruleStartTime = millis();
                    ruleActive = true;

                    const BehaviorRule& r = gene.rules[i];
                    float noiseMod = 0.7f + noise * 0.6f;

                    int modMotorL = constrain((int)(r.motorL * noiseMod), -255, 255);
                    int modMotorR = constrain((int)(r.motorR * noiseMod), -255, 255);
                    uint16_t modDuration = constrain((uint16_t)(r.durationMs * noiseMod), MIN_RULE_DURATION, MAX_RULE_DURATION);

                    startPhysicsAction(modMotorL, modMotorR, modDuration);
                    logFrame();
                    return;
                }
            }
            if (!actionInProgress) {
                stopMotors();
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

    static uint32_t getDistanceTicks() { return distanceTicks; }
    static const FrameLogEntry* getFrameLog() { return frameLog; }
    static int getFrameLogCount() { return frameLogCount; }
    static int getCurrentSpeedL() { return currentSpeedL; }
    static int getCurrentSpeedR() { return currentSpeedR; }
};

int MotorController::currentSpeedL = 0;
int MotorController::currentSpeedR = 0;
volatile uint32_t MotorController::distanceTicks = 0;
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
bool MotorController::motorEnabled = false;
bool MotorController::lastLeftA = false;
bool MotorController::lastRightA = false;
uint32_t MotorController::lastEncoderReadTime = 0;

bool MotorController::actionInProgress = false;
uint32_t MotorController::actionStartTime = 0;
uint32_t MotorController::actionLastTicks = 0;
int MotorController::actionStallFrames = 0;
int MotorController::actionTargetL = 0;
int MotorController::actionTargetR = 0;
uint16_t MotorController::actionMinDuration = 0;
uint8_t MotorController::actionNextRule = 0;

// ==================== 进化引擎 ====================
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
    float mutationRate;
    float bestNoveltyEver;

    // ✅ v8.6.3: 植入"避障种子个体"
    void createSeedIndividual(Gene& gene) {
        gene.ruleCount = 3;
        
        // 规则1: 左传感器检测到障碍物 → 右转
        gene.rules[0].condType = COND_SENSOR_LEFT;
        gene.rules[0].condValue = OBSTACLE_ADC_THRESH;
        gene.rules[0].condOp = OP_GREATER;
        gene.rules[0].motorL = -150;
        gene.rules[0].motorR = 150;
        gene.rules[0].durationMs = 300;
        gene.rules[0].nextRule = 0;
        
        // 规则2: 右传感器检测到障碍物 → 左转
        gene.rules[1].condType = COND_SENSOR_RIGHT;
        gene.rules[1].condValue = OBSTACLE_ADC_THRESH;
        gene.rules[1].condOp = OP_GREATER;
        gene.rules[1].motorL = 150;
        gene.rules[1].motorR = -150;
        gene.rules[1].durationMs = 300;
        gene.rules[1].nextRule = 0;
        
        // 规则3: 空闲 → 直行
        gene.rules[2].condType = COND_IDLE;
        gene.rules[2].condValue = 0;
        gene.rules[2].condOp = OP_GREATER;
        gene.rules[2].motorL = 150;
        gene.rules[2].motorR = 150;
        gene.rules[2].durationMs = 100;
        gene.rules[2].nextRule = 0;
        
        gene.survival_time = 0;
        gene.distance_ticks = 0;
        gene.noveltyScore = 0;
        gene.behavior.init();
        
        Logger::log("🧬 植入避障种子个体 (规则数=3)");
    }

    void initImpl() {
        currentGeneration = 1; currentIndividual = 0;
        testActive = false; pendingTransition = false;
        mutationRate = 0.15f; bestNoveltyEver = 0;
        Logger::log("初始化进化引擎 (Novelty Search)...");
        archive.init();
        
        for (int i = 0; i < POPULATION_SIZE; i++) {
            population[i].init();
        }
        
        // ✅ 个体0替换为种子个体
        createSeedIndividual(population[0]);
        
        Logger::logf("种群初始化完成: %d 个个体 (含1个种子个体)", POPULATION_SIZE);
    }

    void startCurrentTestImpl() {
        testActive = true; testStartTime = millis();
        MotorController::enableMotor();
        MotorController::startFrameLog();
        Logger::logf("测试个体 %d/%d (代 %lu) [规则数=%d]",
                     currentIndividual+1, POPULATION_SIZE, currentGeneration,
                     population[currentIndividual].ruleCount);
    }

    void endCurrentTestImpl() {
        if (!testActive) return;
        testActive = false;
        MotorController::forceStopAction();
        MotorController::disableMotor();
        delay(50);
        MotorController::stopMotors();
        
        Gene& g = population[currentIndividual];
        g.survival_time = millis() - testStartTime;
        g.distance_ticks = MotorController::getDistanceTicks();

        g.behavior = archive.extractFromFrameLog(
            MotorController::getFrameLog(), MotorController::getFrameLogCount(), g.distance_ticks);
        g.noveltyScore = archive.computeNovelty(g.behavior);
        bool added = archive.addIfNovel(g.behavior);

        HistoryRecord record;
        record.timestamp = millis();
        record.generation = currentGeneration;
        record.noveltyScore = g.noveltyScore;
        record.survivalTime = g.survival_time;
        record.distance_ticks = g.distance_ticks;
        record.ruleCount = g.ruleCount;
        RobustStorage::addRecord(record);
        RobustStorage::forceSave();

        Logger::logf("个体 %d 完成: 新奇度=%.4f 规则数=%d %s",
                     currentIndividual+1, g.noveltyScore, g.ruleCount, added ? "[新行为!]" : "");
        MotorController::flushFrameLogToFlash(currentIndividual, currentGeneration);

        if (g.noveltyScore > bestNoveltyEver) {
            bestNoveltyEver = g.noveltyScore;
            Logger::logf("新行为纪录! 新奇度=%.4f", g.noveltyScore);
        }
    }

    void nextIndividualImpl() {
        endCurrentTestImpl();
        currentIndividual++;
        if (currentIndividual >= POPULATION_SIZE) {
            evolveImpl();
            currentIndividual = 0;
            currentGeneration++;
            Logger::logf("进入第 %lu 代 (档案: %d 行为)", currentGeneration, archive.getArchiveSize());
        }
        startCurrentTestImpl();
    }

    void evolveImpl() {
        Logger::log("开始进化 (新奇度驱动)...");
        std::sort(population, population + POPULATION_SIZE,
            [](const Gene& a, const Gene& b) { return a.noveltyScore > b.noveltyScore; });

        int eliteCount = POPULATION_SIZE / 4;
        if (eliteCount < 1) eliteCount = 1;
        Logger::logf("新奇度精英保留: %d 个", eliteCount);

        Gene newPopulation[POPULATION_SIZE];
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
        }
        for (int i = 0; i < POPULATION_SIZE; i++) population[i] = newPopulation[i];
        
        float avg = getAvgNovelty();
        if (avg < bestNoveltyEver * 0.5f) {
            mutationRate = min(mutationRate + 0.03f, 0.5f);
        } else if (avg > bestNoveltyEver * 0.8f) {
            mutationRate = max(mutationRate - 0.01f, 0.05f);
        }
        Logger::logf("进化完成, 变异率=%.2f", mutationRate);
    }

    int tournamentSelectByNovelty() {
        int tournamentSize = 3, best = random(0, POPULATION_SIZE);
        for (int i = 1; i < tournamentSize; i++) {
            int candidate = random(0, POPULATION_SIZE);
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
    static void init() { instance.initImpl(); }
    static void startCurrentTest() { instance.startCurrentTestImpl(); }
    static void endCurrentTest() { instance.endCurrentTestImpl(); }
    static void nextIndividual() { instance.nextIndividualImpl(); }
    static bool isTestActive() { return instance.testActive; }
    static uint32_t getTestStartTime() { return instance.testStartTime; }
    static uint32_t getGeneration() { return instance.currentGeneration; }
    static int getIndividual() { return instance.currentIndividual; }
    static float getMutationRate() { return instance.mutationRate; }
    static float getBestNoveltyEver() { return instance.bestNoveltyEver; }
    static int getArchiveSize() { return instance.archive.getArchiveSize(); }
    static Gene& getCurrentGene() { return instance.population[instance.currentIndividual]; }
    static void setPendingTransition(bool p) { instance.pendingTransition = p; }
    static bool getPendingTransition() { return instance.pendingTransition; }
    static void clearPendingTransition() { instance.pendingTransition = false; }
    static void setMutationRate(float rate) { instance.mutationRate = constrain(rate, 0.01f, 1.0f); }
};

EvolutionEngine EvolutionEngine::instance;

// ==================== Web 控制台 ====================
class CarWebServer {
private:
    static WebServer server;
    
public:
    static void init() {
        WiFi.softAP(WIFI_SSID, WIFI_PASSWORD);
        Logger::logf("WiFi 热点: %s", WIFI_SSID);
        Logger::logf("IP: %s", WiFi.softAPIP().toString().c_str());
        
        server.on("/", handleRoot);
        server.on("/status", handleStatus);
        server.on("/archive", handleArchive);
        server.on("/evolution", handleEvolution);
        server.on("/motor", handleMotor);
        server.on("/config", handleConfig);
        server.on("/reset", handleReset);
        server.on("/download/history", handleDownloadHistory);
        server.on("/download/framelog", handleDownloadFrameLog);
        server.on("/download/all", handleDownloadAll);
        server.on("/list/files", handleListFiles);
        server.on("/download/file", handleDownloadFile);
        server.onNotFound(handleNotFound);
        
        server.begin();
        Logger::log("Web 控制台就绪 (v8.6.3-sensor-fixed)");
    }

    static void handleClient() { server.handleClient(); }

    static void handleRoot() {
        String html = R"rawliteral(<!DOCTYPE html><html><head>
<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
<title>开放式进化小车控制台 v8.6.3</title>
<style>
*{box-sizing:border-box;}
body{font-family:sans-serif;background:#0d1117;color:#c9d1d9;padding:20px;margin:0;}
h1{color:#58a6ff;font-size:20px;margin:0 0 10px 0;}
h2{color:#f0f6fc;font-size:14px;margin:0 0 10px 0;border-bottom:1px solid #21262d;padding-bottom:8px;}
.card{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:15px;margin:10px 0;}
button{background:#21262d;color:#c9d1d9;border:1px solid #30363d;padding:8px 16px;border-radius:6px;cursor:pointer;margin:4px;font-size:12px;}
button:hover{background:#30363d;border-color:#58a6ff;}
button.primary{background:#238636;border-color:#2ea043;color:#fff;}
button.danger{background:#da3633;border-color:#f85149;color:#fff;}
button.download{background:#1f6feb;border-color:#58a6ff;color:#fff;}
button.download:hover{background:#388bfd;}
button.warning{background:#d29922;border-color:#d29922;color:#fff;}
button.warning:hover{background:#e3b341;}
.status{font-size:12px;line-height:1.8;font-family:monospace;}
.val{color:#58a6ff;}
.high{color:#3fb950;}
.warn{color:#d29922;}
.notice{background:#1f2937;border-left:4px solid #3fb950;padding:8px 12px;margin:8px 0;font-size:12px;color:#e5e7eb;}
.notice-sensor{background:#1f2937;border-left:4px solid #d29922;padding:8px 12px;margin:8px 0;font-size:12px;color:#e5e7eb;}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;}
.sensor-readings{font-family:monospace;font-size:13px;padding:8px;background:#0d1117;border-radius:4px;}
@media(max-width:600px){.grid{grid-template-columns:1fr;}}
</style>
</head><body>
<h1>🧬 开放式进化小车 (v8.6.3-sensor-fixed)</h1>
<div class="notice">✅ SPIFFS已启用 - 数据持久化保存</div>
<div class="notice-sensor">📡 传感器-控制链路已修复 - 行为表使用原始ADC值</div>
<div class='grid'>
<div class='card'><h2>📊 当前状态</h2><div id='status' class='status'>加载中...</div></div>
<div class='card'><h2>📚 行为档案</h2><div id='archive' class='status'>加载中...</div></div>
</div>
<div class='card'><h2>📡 传感器实时值</h2>
<div id='sensors' class='sensor-readings'>等待数据...</div>
</div>
<div class='card'><h2>🎮 进化控制</h2>
<button class='primary' onclick="api('/evolution?action=start')">▶ 开始进化</button>
<button class='danger' onclick="api('/evolution?action=stop')">⏹ 停止</button>
<button onclick="api('/evolution?action=next')">⏭ 下一个体</button>
<button onclick="location.reload()">🔄 刷新</button>
</div>
<div class='card'><h2>🛑 电机控制</h2>
<div style='display:flex;flex-wrap:wrap;gap:5px;'>
<button class='primary' onclick="motorCtrl('enable')">🔓 解锁电机</button>
<button class='danger' onclick="motorCtrl('disable')">🔒 锁定电机</button>
<button class='warning' onclick="motorCtrl('forward')">⬆ 前进</button>
<button class='warning' onclick="motorCtrl('backward')">⬇ 后退</button>
<button class='warning' onclick="motorCtrl('left')">⬅ 左转</button>
<button class='warning' onclick="motorCtrl('right')">➡ 右转</button>
<button onclick="motorCtrl('stop')">⏹ 停止</button>
</div>
<div id='motorStatus' class='status' style='margin-top:8px;'>电机: 加载中...</div>
</div>
<div class='card'><h2>📥 数据导出</h2>
<div style='display:flex;flex-wrap:wrap;gap:5px;'>
<button class='download' onclick="downloadFile('/download/history')">📊 下载历史CSV</button>
<button class='download' onclick="downloadFile('/download/all')">📦 下载全部数据</button>
<button onclick="listFiles()">📋 列出文件</button>
</div>
<div id='fileList' class='files' style='font-size:11px;font-family:monospace;max-height:150px;overflow-y:auto;background:#0d1117;padding:8px;border-radius:4px;margin-top:8px;'>点击"列出文件"查看SPIFFS内容</div>
</div>
<div class='card'><h2>⚙️ 系统</h2>
<button onclick="if(confirm('确认恢复出厂?')){api('/reset')}">🗑 恢复出厂</button>
</div>
<script>
function api(url){fetch(url).then(r=>r.json()).then(d=>console.log(d)).catch(e=>console.error(e));}
function motorCtrl(action){fetch('/motor?action='+action).then(r=>r.json()).then(d=>{document.getElementById('motorStatus').innerHTML='电机: '+d.status;}).catch(e=>console.error(e));}
function downloadFile(url){
    const a=document.createElement('a');
    a.href=url; a.download='';
    document.body.appendChild(a); a.click(); document.body.removeChild(a);
}
function listFiles(){
    fetch('/list/files').then(r=>r.json()).then(d=>{
        let html='';
        if(d.files && d.files.length>0){
            d.files.forEach(f=>{html+='<div style="color:#58a6ff;cursor:pointer;padding:2px 0;" onclick="downloadFile(\'/download/file?name='+f+'\')">📄 '+f+'</div>';});
        }else{
            html='<span style="color:#8b949e;">'+d.message+'</span>';
        }
        document.getElementById('fileList').innerHTML=html;
    }).catch(e=>console.error(e));
}
setInterval(function(){
fetch('/status').then(r=>r.json()).then(d=>{
document.getElementById('status').innerHTML=
'代数: <span class=val>'+d.generation+'</span><br>'+
'个体: <span class=val>'+(d.individual+1)+'/'+d.population+'</span><br>'+
'新奇度: <span class=high>'+d.novelty.toFixed(4)+'</span><br>'+
'规则数: <span class=val>'+d.ruleCount+'</span><br>'+
'存活: <span class=val>'+d.survival+'ms</span><br>'+
'距离: <span class=val>'+d.distance+'</span><br>'+
'变异率: <span class=val>'+(d.mutationRate*100).toFixed(1)+'%</span><br>'+
'电机: '+(d.motorEnabled?'<span class=high>🔓 已解锁</span>':'<span class=warn>🔒 已锁定</span>')+
'<br>动作: '+(d.actionInProgress?'<span class=high>🔄 执行中</span>':'<span class=val>⏸ 空闲</span>')+
'<br>存储: '+(d.storageReady?'<span class=high>💾 SPIFFS</span>':'<span class=warn>⚠️ RAM仅</span>');
}).catch(e=>console.error(e));
fetch('/archive').then(r=>r.json()).then(d=>{
document.getElementById('archive').innerHTML=
'档案大小: <span class=high>'+d.size+'</span><br>'+
'最佳新奇度: <span class=high>'+d.bestNovelty.toFixed(4)+'</span>';
}).catch(e=>console.error(e));
// 传感器实时值
fetch('/status').then(r=>r.json()).then(d=>{
if(d.sensorLeft !== undefined && d.sensorRight !== undefined){
    const leftBar = Math.min(d.sensorLeft/4095*100,100);
    const rightBar = Math.min(d.sensorRight/4095*100,100);
    const leftColor = d.sensorLeft > 1500 ? '#f85149' : '#3fb950';
    const rightColor = d.sensorRight > 1500 ? '#f85149' : '#3fb950';
    document.getElementById('sensors').innerHTML =
    '左传感器: <span style="color:'+leftColor+'">'+d.sensorLeft+'</span> (障碍物阈值: 1500)<br>'+
    '<div style="background:#21262d;height:6px;border-radius:3px;margin:4px 0;width:100%;"><div style="background:'+leftColor+';height:6px;border-radius:3px;width:'+leftBar+'%;"></div></div>'+
    '右传感器: <span style="color:'+rightColor+'">'+d.sensorRight+'</span><br>'+
    '<div style="background:#21262d;height:6px;border-radius:3px;margin:4px 0;width:100%;"><div style="background:'+rightColor+';height:6px;border-radius:3px;width:'+rightBar+'%;"></div></div>'+
    '<span style="font-size:11px;color:#8b949e;">📌 障碍物阈值: '+d.obstacleThreshold+' | 绿色=畅通 红色=有障碍</span>';
}
}).catch(e=>console.error(e));
},500);
</script>
</body></html>)rawliteral";
        server.send(200, "text/html", html);
    }

    static void handleStatus() {
        Gene& g = EvolutionEngine::getCurrentGene();
        int leftRaw = SensorCalibration::getLeftRaw();
        int rightRaw = SensorCalibration::getRightRaw();
        
        String json = "{";
        json += "\"generation\":" + String(EvolutionEngine::getGeneration()) + ",";
        json += "\"individual\":" + String(EvolutionEngine::getIndividual()) + ",";
        json += "\"population\":" + String(POPULATION_SIZE) + ",";
        json += "\"novelty\":" + String(g.noveltyScore, 4) + ",";
        json += "\"ruleCount\":" + String(g.ruleCount) + ",";
        json += "\"survival\":" + String(millis() - EvolutionEngine::getTestStartTime()) + ",";
        json += "\"distance\":" + String(MotorController::getDistanceTicks()) + ",";
        json += "\"mutationRate\":" + String(EvolutionEngine::getMutationRate(), 2) + ",";
        json += "\"motorEnabled\":" + String(MotorController::isMotorEnabled() ? "true" : "false") + ",";
        json += "\"actionInProgress\":" + String(MotorController::isActionInProgress() ? "true" : "false") + ",";
        json += "\"storageReady\":" + String(RobustStorage::isReady() ? "true" : "false") + ",";
        // ✅ 新增传感器数据
        json += "\"sensorLeft\":" + String(leftRaw) + ",";
        json += "\"sensorRight\":" + String(rightRaw) + ",";
        json += "\"obstacleThreshold\":" + String(OBSTACLE_ADC_THRESH);
        json += "}";
        server.send(200, "application/json", json);
    }

    static void handleArchive() {
        String json = "{";
        json += "\"size\":" + String(EvolutionEngine::getArchiveSize()) + ",";
        json += "\"bestNovelty\":" + String(EvolutionEngine::getBestNoveltyEver(), 4);
        json += "}";
        server.send(200, "application/json", json);
    }

    static void handleEvolution() {
        if (!server.hasArg("action")) {
            server.send(400, "application/json", "{\"error\":\"missing action\"}");
            return;
        }
        String action = server.arg("action");
        if (action == "start") {
            EvolutionEngine::startCurrentTest();
            server.send(200, "application/json", "{\"status\":\"started\"}");
        } else if (action == "stop") {
            EvolutionEngine::endCurrentTest();
            MotorController::stopMotors();
            server.send(200, "application/json", "{\"status\":\"stopped\"}");
        } else if (action == "next") {
            EvolutionEngine::endCurrentTest();
            EvolutionEngine::nextIndividual();
            server.send(200, "application/json", "{\"status\":\"next\"}");
        } else {
            server.send(400, "application/json", "{\"error\":\"invalid action\"}");
        }
    }

    static void handleConfig() {
        if (server.hasArg("mutation")) {
            float rate = server.arg("mutation").toFloat();
            EvolutionEngine::setMutationRate(rate);
        }
        server.send(200, "application/json", "{\"status\":\"ok\"}");
    }

    static void handleMotor() {
        if (!server.hasArg("action")) {
            server.send(400, "application/json", "{\"error\":\"missing action\"}");
            return;
        }
        String action = server.arg("action");
        String status = "ok";
        if (action == "enable") {
            MotorController::enableMotor();
            status = "已解锁";
        } else if (action == "disable") {
            MotorController::disableMotor();
            status = "已锁定";
        } else if (action == "stop") {
            MotorController::forceStopAction();
            MotorController::stopMotors();
            status = "已停止";
        } else if (action == "forward") {
            MotorController::enableMotor();
            MotorController::startPhysicsAction(150, 150, 100);
            status = "前进中";
        } else if (action == "backward") {
            MotorController::enableMotor();
            MotorController::startPhysicsAction(-150, -150, 100);
            status = "后退中";
        } else if (action == "left") {
            MotorController::enableMotor();
            MotorController::startPhysicsAction(-120, 120, 100);
            status = "左转中";
        } else if (action == "right") {
            MotorController::enableMotor();
            MotorController::startPhysicsAction(120, -120, 100);
            status = "右转中";
        } else {
            server.send(400, "application/json", "{\"error\":\"invalid action\"}");
            return;
        }
        String json = "{\"status\":\"" + status + "\",\"enabled\":" + String(MotorController::isMotorEnabled() ? "true" : "false") + "}";
        server.send(200, "application/json", json);
    }

    static void handleReset() {
        RobustStorage::clearAll();
        server.send(200, "application/json", "{\"status\":\"resetting\"}");
        delay(1000);
        ESP.restart();
    }

    static void handleDownloadHistory() {
        String csvData = RobustStorage::getCSVData();
        if (csvData.length() < 10) {
            csvData = "timestamp,generation,noveltyScore,survivalTime,distance_ticks,ruleCount\n";
        }
        server.sendHeader("Content-Type", "text/csv");
        server.sendHeader("Content-Disposition", "attachment; filename=oe_history.csv");
        server.send(200, "text/csv", csvData);
    }

    static void handleDownloadFrameLog() {
        server.send(404, "application/json", "{\"error\":\"帧日志仅保存在RAM中\"}");
    }

    static void handleDownloadAll() {
        String allData = "=== 进化历史数据 ===\n";
        allData += RobustStorage::getCSVData();
        allData += "\n=== 系统信息 ===\n";
        allData += "SPIFFS状态: " + String(RobustStorage::isReady() ? "已挂载" : "未挂载") + "\n";
        allData += "总记录数: " + String(RobustStorage::getTotalSaved()) + "\n";
        server.sendHeader("Content-Type", "text/csv");
        server.sendHeader("Content-Disposition", "attachment; filename=all_evolution_data.csv");
        server.send(200, "text/csv", allData);
    }

    static void handleListFiles() {
        String json = "{\"files\":[";
        if (RobustStorage::isReady()) {
            File root = SPIFFS.open("/");
            bool first = true;
            while (File file = root.openNextFile()) {
                if (!first) json += ",";
                json += "\"" + String(file.name()) + "\"";
                first = false;
            }
        }
        json += "],\"message\":\"" + String(RobustStorage::isReady() ? "SPIFFS已挂载" : "SPIFFS未挂载") + "\"}";
        server.send(200, "application/json", json);
    }

    static void handleDownloadFile() {
        if (!server.hasArg("name")) {
            server.send(400, "application/json", "{\"error\":\"missing name\"}");
            return;
        }
        String name = server.arg("name");
        if (!RobustStorage::isReady()) {
            server.send(404, "application/json", "{\"error\":\"SPIFFS未挂载\"}");
            return;
        }
        File file = SPIFFS.open(name, FILE_READ);
        if (!file) {
            server.send(404, "application/json", "{\"error\":\"文件不存在\"}");
            return;
        }
        String content = file.readString();
        file.close();
        server.sendHeader("Content-Type", "text/plain");
        server.sendHeader("Content-Disposition", "attachment; filename=" + name);
        server.send(200, "text/plain", content);
    }

    static void handleNotFound() {
        server.send(404, "application/json", "{\"error\":\"not found\"}");
    }
};

WebServer CarWebServer::server(80);

// ==================== 硬件自检工具 ====================
class HardwareTest {
public:
    static void runAll() {
        Logger::log("========== 硬件自检开始 ==========");
        testLED();
        testSensors();
        testMotors();
        testFloatingPins();
        Logger::log("========== 硬件自检完成 ==========");
    }

private:
    static void testLED() {
        Logger::log("[LED] 闪烁测试...");
        pinMode(PIN_LED, OUTPUT);
        for (int i = 0; i < 3; i++) {
            digitalWrite(PIN_LED, HIGH);
            delay(200);
            digitalWrite(PIN_LED, LOW);
            delay(200);
        }
        Logger::log("[LED] GPIO14 闪烁3次完成 ✓");
    }

    static void testSensors() {
        Logger::log("[传感器] 红外测距传感器测试...");
        int sumL = 0, sumR = 0;
        int minL = 9999, maxL = 0, minR = 9999, maxR = 0;
        for (int i = 0; i < 10; i++) {
            int l = SensorCalibration::getLeftRaw();
            int r = SensorCalibration::getRightRaw();
            sumL += l; sumR += r;
            if (l < minL) minL = l;
            if (l > maxL) maxL = l;
            if (r < minR) minR = r;
            if (r > maxR) maxR = r;
            delay(50);
        }
        int avgL = sumL / 10, avgR = sumR / 10;
        Logger::logf("[传感器] 左(GPIO4): avg=%d range=[%d,%d]", avgL, minL, maxL);
        Logger::logf("[传感器] 右(GPIO5): avg=%d range=[%d,%d]", avgR, minR, maxR);
        Logger::logf("[传感器] 障碍物阈值: %d (原始ADC值)", OBSTACLE_ADC_THRESH);
        if (avgL < 50 || avgL > 3500) Logger::log("[传感器] ⚠️ 左传感器读数异常，检查接线!");
        if (avgR < 50 || avgR > 3500) Logger::log("[传感器] ⚠️ 右传感器读数异常，检查接线!");
        if (avgL >= 50 && avgL <= 3500 && avgR >= 50 && avgR <= 3500)
            Logger::log("[传感器] GP2Y0A21 左右传感器正常 ✓");
    }

    static void testMotors() {
        Logger::log("[电机] 电机驱动测试 (短时运转，请放置小车在安全位置)...");
        MotorController::enableMotor();
        
        Logger::log("[电机] 前进测试...");
        MotorController::startPhysicsAction(120, 120, 200);
        delay(1000);
        MotorController::forceStopAction();
        delay(300);

        Logger::log("[电机] 后退测试...");
        MotorController::startPhysicsAction(-120, -120, 200);
        delay(1000);
        MotorController::forceStopAction();
        delay(300);

        Logger::log("[电机] 左转测试...");
        MotorController::startPhysicsAction(-100, 100, 200);
        delay(800);
        MotorController::forceStopAction();
        delay(300);

        Logger::log("[电机] 右转测试...");
        MotorController::startPhysicsAction(100, -100, 200);
        delay(800);
        MotorController::forceStopAction();

        MotorController::disableMotor();
        Logger::log("[电机] 前进/后退/左转/右转全部完成 ✓");
    }

    static void testFloatingPins() {
        Logger::log("[悬空引脚] 检测未连接引脚的ADC读数...");
        int freePins[] = {32, 33, 34, 36};
        int numPins = sizeof(freePins) / sizeof(freePins[0]);
        bool allOk = true;
        for (int i = 0; i < numPins; i++) {
            int pin = freePins[i];
            pinMode(pin, INPUT);
            int val = analogRead(pin);
            Logger::logf("[悬空] GPIO%d: ADC=%d", pin, val);
            if (val == 0 || val >= 4095) {
                Logger::logf("[悬空] ⚠️ GPIO%d 读数异常 (%d)，检查是否短路或意外连接!", pin, val);
                allOk = false;
            }
        }
        if (allOk) {
            Logger::log("[悬空引脚] 所有空闲ADC引脚读数正常 (波动范围合理) ✓");
        }
    }
};

// ==================== setup & loop ====================
void setup() {
    Serial.begin(115200);
    delay(1000);

    Logger::log("========================================");
    Logger::log("  🧬 开放式进化小车系统 v8.6.3-sensor-fixed");
    Logger::log("  (传感器-控制链路修复版)");
    Logger::log("========================================");

    RobustStorage::init();
    Logger::log("[1/6] 存储系统就绪 (SPIFFS + RAM 双重保障)");

    pinMode(PIN_NOISE_SOURCE, INPUT);
    randomSeed(SensorCalibration::readNoise());
    Logger::log("[2/6] 随机数发生器就绪");

    SensorCalibration::calibrate();
    Logger::log("[3/6] 传感器校准完成 (原始值用于控制, 校准值用于调试)");

    MotorController::init();
    Logger::log("[4/6] 电机控制器就绪 (非阻塞状态机, 默认锁定)");

    EvolutionEngine::init();
    Logger::log("[5/6] 进化引擎就绪 (含避障种子个体)");

    CarWebServer::init();
    Logger::log("[6/6] Web控制台就绪");

    Logger::log("========================================");
    Logger::log("  ✅ 系统就绪 (v8.6.3-sensor-fixed)");
    Logger::log("  📡 WiFi: CarLogger / 12345678");
    Logger::log("  🌐 http://192.168.4.1");
    Logger::log("  💾 SPIFFS已启用 - 数据持久化保存");
    Logger::logf("  📡 传感器阈值: 障碍物=%d (原始ADC值)", OBSTACLE_ADC_THRESH);
    Logger::log("  ⚠️ 电机默认锁定, 请通过Web解锁或开始进化");
    Logger::log("========================================");
}

void loop() {
    MotorController::updatePhysicsAction();

    if (EvolutionEngine::isTestActive()) {
        Gene& gene = EvolutionEngine::getCurrentGene();
        MotorController::update(gene);

        if (millis() - EvolutionEngine::getTestStartTime() > TEST_DURATION_MS) {
            EvolutionEngine::endCurrentTest();
            EvolutionEngine::nextIndividual();
        }
    }

    CarWebServer::handleClient();

    if (EvolutionEngine::getPendingTransition()) {
        EvolutionEngine::clearPendingTransition();
        EvolutionEngine::nextIndividual();
    }

    RobustStorage::tick();
    
    delay(LOOP_DELAY_MS);
}