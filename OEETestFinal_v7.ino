/* ================================================================
 * 项目名称：仿草履虫应激机制 - 脱困能力进化系统
 * 文件：OEETestFinal.ino (v9.7-DataAuditFix)
 * 最后更新：2026-08-16
 * 
 * ================================================================
 * 【数据审计修复】v9.5-DataAuditFix (2026-08-16)
 * ================================================================
 * 
 * 一、修复背景
 * ------------
 * v9.4 发布后，对系统数据链路进行完整审计，发现以下问题：
 * 
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │ 问题1：帧日志容量不足                                       │
 *   │   FRAME_LOG_SIZE = 256 帧 ≈ 2.56秒 (100fps)               │
 *   │   30秒测试中丢失 97% 的帧数据                              │
 *   │   自身对照实验的混沌前后对比数据不完整                      │
 *   ├─────────────────────────────────────────────────────────────┤
 *   │ 问题2：历史记录重复写入                                     │
 *   │   GeneStorage::saveHistoryRecord() 和                      │
 *   │   RobustStorage::addRecord() 写入同一文件                  │
 *   │   造成存储空间浪费和数据冗余                                │
 *   ├─────────────────────────────────────────────────────────────┤
 *   │ 问题3：帧日志无独立下载端点                                 │
 *   │   帧日志只能通过 /download/population 间接获取             │
 *   │   且该端点混合了种群快照和基因记录，组织混乱                │
 *   ├─────────────────────────────────────────────────────────────┤
 *   │ 问题4：种群快照和基因记录混在一起                           │
 *   │   /download/population 同时包含 pop_gen_*.bin 和 gen_*.csv │
 *   │   数据组织不清晰，不方便事后分析                            │
 *   ├─────────────────────────────────────────────────────────────┤
 *   │ 问题5：无法下载单个体完整数据                               │
 *   │   基因、帧日志、混沌记录分散在不同文件中                    │
 *   │   分析某个个体时需要手动拼接多个文件                        │
 *   └─────────────────────────────────────────────────────────────┘
 * 
 * 
 * 二、修复方案
 * ------------
 * 
 * 修复1：增加帧日志容量
 *   #define FRAME_LOG_SIZE 4096  // 256 → 4096 (可存储40秒数据)
 *   影响：帧日志可覆盖完整30秒测试周期，自身对照实验数据完整
 * 
 * 修复2：删除重复写入
 *   endCurrentTestImpl() 中删除 GeneStorage::saveHistoryRecord()
 *   仅保留 RobustStorage::addRecord()
 *   影响：每次个体测试减少一次文件写入操作
 * 
 * 修复3：新增独立下载端点
 *   /download/genes     → 仅下载 gen_*.csv 基因记录
 *   /download/frames    → 下载帧日志 (支持 ?gen=N&id=M 单个体)
 *   /download/individual → 下载单个体完整数据包
 *   影响：数据下载更加精细化和灵活
 * 
 * 修复4：分离种群快照和基因记录
 *   /download/population → 仅 pop_gen_*.bin
 *   /download/genes      → 仅 gen_*.csv
 *   影响：数据组织清晰，便于分类分析
 * 
 * 修复5：单个体完整数据包
 *   /download/individual?gen=N&id=M
 *   返回：基因记录 + 帧日志(解析) + 混沌记录 的完整数据包
 *   影响：分析单个个体时无需手动拼接多个文件
 * 
 * 
 * 三、修复前后对比
 * ----------------
 *   ┌──────────────────────┬─────────────────┬─────────────────┐
 *   │ 维度                 │ 修复前 (v9.4)   │ 修复后 (v9.5)   │
 *   ├──────────────────────┼─────────────────┼─────────────────┤
 *   │ 帧日志容量           │ 256帧 (2.56秒)  │ 4096帧 (40秒)   │
 *   │ 历史记录写入次数     │ 2次/个体        │ 1次/个体        │
 *   │ 下载端点数量         │ 4个             │ 7个             │
 *   │ 种群快照下载         │ 混杂基因记录    │ 仅种群快照      │
 *   │ 基因记录下载         │ 无              │ 独立端点        │
 *   │ 帧日志下载           │ 无              │ 独立端点        │
 *   │ 单个体完整下载       │ 无              │ 独立端点        │
 *   └──────────────────────┴─────────────────┴─────────────────┘
 * 
 * 
 * 四、新增下载端点说明
 * --------------------
 * 
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │ 端点                              │ 说明                   │
 *   ├───────────────────────────────────┼────────────────────────┤
 *   │ /download/history                 │ 历史记录 CSV           │
 *   │ /download/evolution               │ 进化数据 CSV           │
 *   │ /download/chaos                   │ 所有混沌数据打包       │
 *   │ /download/population              │ 种群快照摘要           │
 *   │ /download/genes                   │ ★ 所有基因记录 CSV    │
 *   │ /download/frames                  │ ★ 帧日志摘要/二进制   │
 *   │ /download/individual?gen=N&id=M   │ ★ 单个体完整数据包    │
 *   └───────────────────────────────────┴────────────────────────┘
 * 
 * 
 * 五、修改文件清单
 * ----------------
 *   ┌───────────────┬────────────────────────────────────────────┐
 *   │ 行号          │ 修改内容                                   │
 *   ├───────────────┼────────────────────────────────────────────┤
 *   │ 第69行        │ FRAME_LOG_SIZE 256 → 4096                 │
 *   │ 第72行        │ FIRMWARE_VERSION v9.4 → v9.5-DataAuditFix │
 *   │ 第1160-1161行 │ 删除 GeneStorage::saveHistoryRecord()     │
 *   │ 第1545-1547行 │ CarWebServer::init() 新增3个路由          │
 *   │ 第1698行      │ handleDownloadPopulation() 仅处理pop_gen  │
 *   │ 第1739-1776行 │ 新增 handleDownloadGenes()                │
 *   │ 第1782-1860行 │ 新增 handleDownloadFrames()               │
 *   │ 第1866-1946行 │ 新增 handleDownloadIndividual()           │
 *   │ 第1245-1530行 │ buildHTMLPage() 更新Web界面               │
 *   └───────────────┴────────────────────────────────────────────┘
 * 
 * 
 * 六、Web界面新增按钮
 * -------------------
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │ 按钮                      │ 功能                          │
 *   ├───────────────────────────┼───────────────────────────────┤
 *   │ [🧬 基因记录]             │ 下载所有 gen_*.csv            │
 *   │ [📹 所有帧日志]           │ 下载帧日志摘要                │
 *   │ [📦 下载个体]             │ 下载单个体完整数据包          │
 *   │ [📹 下载单帧日志]         │ 下载单个帧日志二进制文件      │
 *   │ [📂 列出文件]             │ 列出SPIFFS所有文件            │
 *   └───────────────────────────┴───────────────────────────────┘
 * 
 * 
 * 七、数据完整性保证
 * ------------------
 *   - 所有文件写入使用 FileUtils::atomicWrite (临时文件+重命名)
 *   - 种群快照包含 Magic Number (0x47454E45) 和版本号
 *   - 帧日志包含 chaosActive 标记，支持自身对照实验
 *   - 混沌数据双重备份 (个体级 + 全局汇总)
 * 
 * ================================================================ */
/* ================================================================
 * 项目名称：仿草履虫应激机制 - 脱困能力进化系统
 * 文件：OEETestFinal.ino (v9.5-OEETestV4)
 * 最后更新：2026-08-16
 * 
 * 本文件记录了从 v7.3 到 v8.9-chaos-v6 的完整演进历程。
 * 以下按时间顺序整理各版本的设计思路、问题发现与修复路径。
 * ================================================================ */

/* ================================================================
 * 【第一阶段】v7.3-fixed-v2 — 经典遗传算法基线 (2026-08-09 ~ 2026-08-11)
 * ================================================================
 * 
 * 设计哲学：
 *   经典遗传算法(GA)：固定适应度函数 → 精英保留 → 收敛到最优。
 *   基因型：5个固定参数 (k_turn, speed_bias, chaos_threshold,
 *           recovery_mode, preferred_action)
 * 
 * 编译错误修复 (v7.3-fixed → v7.3-fixed-v2)：
 *   1. EvolutionEngine 重构为单例+静态包装模式，解决实例类被静态调用问题
 *   2. ledcAttach(pin, freq, bits) → ledcSetup + ledcAttachPin 拆分
 *   3. ledcWrite 参数从引脚号改为通道号
 *   4. case 2 中声明 int dir 跨越初始化 → 加花括号
 *   5. PIN_NOISE_SOURCE 从 GPIO6（与Flash冲突）改为 GPIO35
 *   6. isClear() 删除不可达代码
 *   7. 删除重复的 server.on("/status") 注册
 *   8. 添加 #include <cstdio>
 *   9. 新增 MotorController public getter 方法
 * 
 * ================================================================ */

/* ================================================================
 * 【第二阶段】v8.0 — 开放式进化重构 "物理结构即算法" (2026-08-11)
 * ================================================================
 * 
 * 设计哲学转变：
 *   从"目标导向优化"转向"行为多样性探索"。
 *   核心思想：不问"谁表现好"，问"谁的行为是档案里没见过的"。
 * 
 * 维度一：Novelty Search 新奇度驱动进化
 *   - 行为描述符 (12维)：传感器4维 + 运动4维 + 策略4维
 *   - 新奇度 = 到档案中 k=5 个最近邻的平均欧氏距离
 *   - 新奇度 > 0.15 → 加入档案 (最多200条)
 *   - 选择：新奇度排序 → 精英保留前25% → 锦标赛选新奇度高的
 *   - 自适应变异率：新奇度停滞 → 加大变异；新奇度健康 → 降低变异
 * 
 * 维度二：物理结构承担计算
 *   - 物理终止条件：physicsTerminatedAction() 检测编码器停滞
 *   - 噪声连续调制：GPIO35每帧读取，调制所有行为参数 (0.7~1.3倍)
 *   - 脉冲式控制：删除逐帧平滑，速度变化由物理系统决定
 * 
 * 维度三：可变长度行为表基因
 *   - BehaviorRule 结构体：8种条件类型 + 电机PWM + 持续时间 + 跳转
 *   - 每个个体 2~16 条规则，规则数量本身可进化
 *   - 交叉：随机切割点交叉 (父本1前k1 + 父本2后k2)
 *   - 变异：10种操作 (参数微调、条件变更、增删规则、交换顺序等)
 * 
 * 与v7.3关键差异：
 *   ┌──────────────┬─────────────────┬─────────────────┐
 *   │ 维度         │ v7.3 (经典GA)   │ v8.0 (物理即算法)│
 *   ├──────────────┼─────────────────┼─────────────────┤
 *   │ 进化目标     │ 最大化适应度    │ 探索行为多样性  │
 *   │ 选择依据     │ 存活+距离+脱困  │ 新奇度(行为距离)│
 *   │ 基因型       │ 5个固定参数     │ 2~16条行为规则  │
 *   │ 动作计时     │ 固定delay(ms)   │ 编码器物理终止  │
 *   │ 速度控制     │ 逐帧平滑过渡    │ 脉冲式+物理惯性 │
 *   │ 物理身体角色 │ 被动执行器      │ 参与决策+计时+平滑│
 *   └──────────────┴─────────────────┴─────────────────┘
 * 
 * ================================================================ */

/* ================================================================
 * 【第三阶段】v8.5-final — 硬件适配与编译修复 (2026-08-12)
 * ================================================================
 * 
 * 修复内容：
 *   1. 方向引脚修正：PIN_LEFT_DIR2 21→46，PIN_RIGHT_DIR2 33→15
 *   2. setMotorSigned() 重写为 setMotorSpeed()，同时控制方向和PWM
 *   3. 删除 writeMotorHardware()，统一使用 setMotorSpeed()
 *   4. stopMotors() 直接调用 setMotorSpeed(0, 0)
 *   5. 右电机正反转方向与 v5.9 对齐
 * 
 * SPIFFS状态：
 *   此时SPIFFS被禁用，数据仅保存在RAM中。
 * 
 * ================================================================ */

/* ================================================================
 * 【第四阶段】v8.6 — 非阻塞状态机重构 (2026-08-13 ~ 2026-08-14)
 * ================================================================
 * 
 * 问题根因：RTC看门狗复位 (rst:0x10)
 *   physicsTerminatedAction() 内部使用 while(1) + delay()
 *   最长阻塞3秒，期间不喂狗 → 触发 RTCWDT_RTC_RST
 * 
 * 修复方案：
 *   - physicsTerminatedAction() → startPhysicsAction() + updatePhysicsAction()
 *   - startPhysicsAction(): 设置电机并记录起始状态
 *   - updatePhysicsAction(): 每帧调用，检测编码器停滞 → 自动终止
 *   - 移除所有阻塞 delay()
 *   - Web控制台在动作执行期间保持响应
 * 
 * ================================================================ */

/* ================================================================
 * 【第五阶段】v8.6.4 — 实机运行数据分析与关键Bug修复 (2026-08-14)
 * ================================================================
 * 
 * 实机运行数据 (mmexport截图)：
 *   distanceTicks 显示为 4294967257, 4294967220, 4294967204
 *   对应有符号值：-39, -76, -92 (逐个体累积恶化)
 * 
 * 问题1（致命）：distanceTicks 的 uint32_t 下溢溢出
 *   编码器ISR在电机后退时执行 distanceTicks--
 *   当 distanceTicks 从0递减 → 绕回 UINT32_MAX (~43亿)
 *   修复：volatile uint32_t → volatile int32_t
 * 
 * 问题2（致命）：distanceTicks 在个体之间从未重置
 *   distanceTicks 仅在 MotorController::init() 中初始化为0
 *   startCurrentTestImpl() 没有重置
 *   修复：新增 resetDistanceTicks() 并在 startCurrentTestImpl() 中调用
 * 
 * 问题3（致命）：下溢值污染行为描述符，破坏新奇度计算
 *   totalDistance ≈ 430万 → 经归一化后所有个体该维度都接近1.0
 *   12维空间中有一个维度被完全污染，新奇度计算无法区分不同行为
 *   修复：改用 int32_t + abs()，确保 totalDistance 反映真实位移
 * 
 * 问题4（中等）：种子个体可能未被首先测试
 *   handleEvolution 的 action="next" 中 endCurrentTest() 重复调用
 *   导致 currentIndividual 从0跳到1，跳过种子个体
 *   修复：移除重复的 endCurrentTest() 调用
 * 
 * 修复清单 (共12处类型修复)：
 *   - distanceTicks: uint32_t → int32_t
 *   - actionLastTicks: uint32_t → int32_t
 *   - HistoryRecord::distance_ticks: uint32_t → int32_t
 *   - Gene::distance_ticks: uint32_t → int32_t
 *   - 新增 MotorController::resetDistanceTicks()
 *   - startCurrentTestImpl() 中调用 resetDistanceTicks()
 *   - handleEvolution action="next" 移除重复的 endCurrentTest()
 * 
 * 进化能否发生？修复前无法有效进化：
 *   distanceTicks下溢 → totalDistance≈430万
 *   → 行为描述符被假维度主导
 *   → 所有个体行为看似高度相似
 *   → 新奇度计算失效 → 进化退化为随机游走
 * 
 * ================================================================ */

/* ================================================================
 * 【第六阶段】v8.7 — 完整基因存储 + 二进制帧日志 (2026-08-15)
 * ================================================================
 * 
 * 改动原因：
 *   1. 完整基因（每条规则的具体内容）从未被持久化
 *   2. 帧日志仅保存在RAM环形缓冲区，测试结束后丢失
 *   3. 数据写入无原子性保证
 *   4. 实验状态无法恢复，重启后所有RAM数据丢失
 * 
 * 新增功能：
 *   1. GeneStorage 类：完整基因的原子保存与加载
 *      - 种群快照：/pop_gen_N.bin (二进制，含所有个体的完整规则)
 *      - 个体记录：/gen_N_id_X.csv (CSV，人类可读)
 *      - 帧日志：/frm_N_iX.bin (二进制，每帧11字节)
 * 
 *   2. 原子写入策略 (FileUtils::atomicWrite)
 *      - 先写临时文件 → 验证大小 → 重命名为正式文件
 *      - 任何步骤失败都不会留下损坏的文件
 * 
 *   3. 实验生命周期管理
 *      - 每轮固定20代，自动检测新实验并清理旧数据
 *      - 实验状态持久化到 /experiment_state.mrk
 *      - 重启后自动恢复实验进度
 * 
 *   4. 数据完整性保证
 *      - 文件头包含魔数 (0x47454E45) 和版本号 (0x0002)
 * 
 * 存储空间需求：
 *   - 种群快照：~3KB/代 × 20 = 60KB
 *   - 个体记录：~1KB/代 × 20 = 20KB
 *   - 帧日志：~45KB/代 × 20 = 900KB
 *   - 总计：~1MB (适合1.5-2MB SPIFFS)
 * 
 * ================================================================ */

/* ================================================================
 * 【第七阶段】v8.8 — 卡死感知 + 向死而生混沌机制 (2026-08-15)
 * ================================================================
 * 
 * 问题背景：
 *   v8.0-v8.7 重构时，原有的混沌/卡死检测逻辑被"行为表规则替代"，
 *   但没有等价功能补上。卡死后编码器仍计数，所有个体都活满30秒，
 *   进化没有选择压力，退化为随机游走。
 * 
 * 新增功能：
 *   1. 卡死检测（硬编码基础设施）
 *      - 检测条件：轮子在转（速度>50）但编码器差值持续增大 (>50)
 *      - 持续1秒 → stuckFlag = true
 * 
 *   2. COND_STUCK 和 COND_ESCAPE 条件类型
 *      - 行为表规则可以检测卡死状态并执行脱困动作
 * 
 *   3. 种子个体包含卡死脱困规则
 *      - 规则3: COND_STUCK → 后退右转脱困 (nextRule=4)
 *      - 规则4: COND_ESCAPE → 清除状态
 * 
 * 设计哲学：
 *   - 卡死检测 = 硬编码基础设施（生存保障）
 *   - 脱困行为 = 基因中的规则控制（可进化）
 *   - 阈值 = 固定（后续版本可进化为基因参数）
 * 
 * ================================================================ */

/* ================================================================
 * 【第八阶段】v8.9-robust-stable — 鲁棒性增强 (2026-08-15)
 * ================================================================
 * 
 * 问题现象：烧录后 Web 显示正常，解锁后电机没有任何响应，LED也不闪烁
 * 
 * 根因1（致命）：状态机死锁
 *   ruleActive=true 但 actionInProgress=false 时，update() 进入 else 分支
 *   如果 nextRule=0，ruleActive 永远不会被重置为 false
 *   修复：stopMotors() 中强制重置 ruleActive=false，重置 currentRuleIndex
 * 
 * 根因2（致命）：种群数据损坏或无有效个体
 *   loadPopulation() 加载的种群可能 ruleCount=0 或全零规则
 *   修复：增加数据完整性验证，自动修复损坏个体
 * 
 * 根因3（致命）：种子个体被覆盖或未正确创建
 *   修复：initImpl() 末尾强制验证并重建种子个体
 * 
 * 根因4（严重）：startPhysicsAction() 未检查 motorEnabled
 *   修复：返回 bool 表示是否成功启动
 * 
 * 根因5（严重）：update() 中无规则匹配时没有 logFrame()
 *   修复：所有状态都记录帧日志
 * 
 * ================================================================ */

/* ================================================================
 * 【第九阶段】v8.9-chaos — 混沌机制完整实现 (2026-08-15)
 * ================================================================
 * 
 * 问题背景：
 *   v8.8 中加入的 COND_STUCK 和 COND_ESCAPE 条件类型，
 *   在 evaluateCondition() 中从未实现判断逻辑。
 *   卡死检测变量未在 MotorController 中正确定义和初始化。
 * 
 * 修复内容：
 *   1. CondType 枚举增加 COND_STUCK = 8, COND_ESCAPE = 9
 *   2. MotorController 新增静态成员：stuckFlag, stuckCounter, 
 *      escapeActive, lastSensorSum, lastSpeedSum
 *   3. 静态变量在类外定义并初始化为 0/false
 *   4. Gene::evaluateCondition() 增加 case COND_STUCK 和 COND_ESCAPE
 *   5. 规则匹配时管理 escapeActive 状态
 *   6. Web界面增加卡死/脱困状态显示
 * 
 * ================================================================ */

/* ================================================================
 * 【第十阶段】v8.9-chaos-v2 ~ v8.9-chaos-v6 — 向死而生混沌机制演进 (2026-08-15)
 * ================================================================
 * 
 * 核心设计哲学：
 *   当基因驱动的脱困策略在 CHAOS_TIMEOUT_MS (2秒) 内未能解除卡死，
 *   系统判定"物理结构陷入了基因也无法破解的绝境"。
 *   此时代码彻底退场，GPIO35 热噪声直接驱动电机，
 *   物理世界自主寻找脱困路径（各态历经搜索 + 对称性破缺）。
 * 
 * 双模式卡死检测 (v8.9-chaos-v4)：
 *   模式1：编码器差值卡死
 *     条件：diff > 15 且持续增大，两轮都在转
 *     持续1秒 → 触发卡死
 * 
 *   模式2：单轮卡死
 *     条件：一个轮子转(>20)，另一个轮子停(<3)
 *     持续1秒 → 触发卡死
 * 
 *   模式3：大差值兜底
 *     条件：diff > 30，任意轮子在转
 *     持续1秒 → 触发卡死
 * 
 * 向死而生混沌机制 (v8.9-chaos-v5)：
 *   1. 卡死持续 2 秒后，基因脱困超时 → 启动混沌
 *   2. 读取 GPIO35 热噪声，放大后映射到左右电机 PWM
 *   3. 不经过任何行为表规则、不经过任何基因解码
 *   4. 编码器检测到物理运动恢复 → 自动退出混沌，代码重新接管
 * 
 * 完整闭环：
 *   物理结构"死寂" → 代码感知 → 接通物理随机源
 *   → 物理世界自主决策 (各态历经搜索)
 *   → 物理运动恢复 → 代码重新接管
 * 
 * ================================================================ */

/* ================================================================
 * 【第十一阶段】v8.9-chaos-v6 — 自身对照实验设计 (2026-08-16)
 * ================================================================
 * 
 * 论文背景：
 *   论文实验设计章节提出了"自身对照"方法论——以混沌触发为时间分界线，
 *   将同一硬件上的行为数据划分为基线组（Chaos-Free）和实验组（Chaos-Intervention），
 *   通过差分分析消除硬件偏差与环境噪声干扰。
 * 
 * 问题：
 *   混沌触发机制虽已工程实现，但数据记录层缺少混沌状态标记，
 *   导致事后无法区分混沌前后的帧数据，自身对照差分分析无从实施。
 * 
 * 本次修改 (14处)：
 * 
 *   1. FrameLogEntry 结构体
 *      - 将 reserved:6 拆分为 chaosActive:1 + reserved:4
 *      - 每帧可标记是否处于混沌模式
 * 
 *   2. 新增 ChaoticTestRecord 结构体 (56字节/条)
 *      - 记录：混沌触发次数、累计/最长持续时间、首次/末次触发时间
 *      - 混沌前后距离/帧数/平均速度、脱困结果、终止原因
 * 
 *   3. MotorController 类 (5处)
 *      - init(): 初始化15个静态追踪变量
 *      - startChaos(): 记录混沌起始距离快照和首次触发时间
 *      - endChaos(): 计算持续时长、累计距离、更新最大值
 *      - logFrame(): 根据 chaosActive 分流统计到基线/实验组
 *      - 新增14个公开访问器 + resetChaosStatistics()
 * 
 *   4. EvolutionEngine 类 (2处)
 *      - startCurrentTestImpl(): 调用 resetChaosStatistics()
 *      - endCurrentTestImpl(): 收集统计数据并保存
 * 
 *   5. GeneStorage + RobustStorage + Web (5处)
 *      - saveChaosRecord(): 原子写入个体级 CSV
 *      - addChaosRecord(): 追加到全局汇总 CSV
 *      - getChaosHistoryCSV(): 读取汇总文件
 *      - Web 新增 /download/chaos 端点
 *      - 路由注册
 * 
 * 自身对照数据流：
 *   运行时: 每帧 → logFrame() → 按 chaosActive 分流到基线/实验组
 *   混沌触发 → startChaos() → 记录起始快照
 *   混沌退出 → endChaos() → 计算持续时长和距离
 *   死亡时: endCurrentTestImpl() → 收集全部统计 → 写入个体CSV + 汇总CSV
 *   事后分析: Web下载 → pandas加载 → 基线组vs实验组差分计算
 * 
 * ================================================================ */

/* ================================================================
 * 【当前状态】v9.3-OEETestV3 (2026-08-16)
 * ================================================================
 * 
 * 完整功能清单：
 *   ✅ Novelty Search 新奇度驱动进化
 *   ✅ 可变长度行为表基因 (2~16条IF-THEN规则)
 *   ✅ 物理终止条件 (编码器检测)
 *   ✅ 噪声连续调制 (GPIO35)
 *   ✅ 双模式卡死检测 (差值+单轮)
 *   ✅ 向死而生混沌机制 (物理噪声直接驱动)
 *   ✅ 完整基因存储 (种群快照+个体记录)
 *   ✅ 二进制帧日志 (每帧11字节)
 *   ✅ 原子写入策略 (临时文件+重命名)
 *   ✅ 实验生命周期管理 (20代自动检测)
 *   ✅ 自身对照实验设计 (混沌前后数据标记)
 *   ✅ 完整Web下载端点 (历史/基因/帧日志/混沌记录)
 *   ✅ SPIFFS数据持久化
 * 
 * 架构特点：
 *   - 代码退场哲学：混沌期间代码不决策，物理世界自主探索
 *   - 物理结构即算法：身体参与决策、计时和运动平滑
 *   - 进化不预设目标：新奇度驱动行为多样性探索
 * 
 * ================================================================ */

/* ================================================================
 * 【数据流总览】
 * ================================================================
 * 
 * ┌─ 运行时数据流 ───────────────────────────────────────────────┐
 * │                                                             │
 * │  loop() 每帧:                                               │
 * │    ├─ 混沌模式? → updateChaos() (物理噪声驱动)              │
 * │    ├─ 物理动作? → updatePhysicsAction() (编码器检测终止)    │
 * │    ├─ 规则评估 → 匹配条件 → startPhysicsAction()            │
 * │    ├─ logFrame() → 标记 chaosActive → 分流统计              │
 * │    └─ 卡死检测 → 超时2秒 → startChaos()                    │
 * │                                                             │
 * └─────────────────────────────────────────────────────────────┘
 * 
 * ┌─ 持久化数据流 ───────────────────────────────────────────────┐
 * │                                                             │
 * │  个体死亡 → endCurrentTestImpl():                           │
 * │    ├─ 新奇度计算 → archive.addIfNovel()                    │
 * │    ├─ HistoryRecord → /oe_history.csv                      │
 * │    ├─ ChaoticTestRecord → /chaos_g{N}_i{X}.csv             │
 * │    ├─ 基因记录 → /gen_{N}_id_{X}.csv                       │
 * │    ├─ 帧日志 → /frm_{N}_i{X}.bin (11字节/帧)              │
 * │    └─ 种群快照 → /pop_gen_{N}.bin (原子写入)              │
 * │                                                             │
 * └─────────────────────────────────────────────────────────────┘
 * 
 * ================================================================ 
 */
/* ================================================================
 * 项目名称：仿草履虫应激机制 - 脱困能力进化系统
 * 文件：OEETestFinal.ino (v9.4-ChaosFix)
 * 最后更新：2026-08-16
 * 
 * ================================================================
 * 【混沌触发修复】v9.4-ChaosFix (2026-08-16)
 * ================================================================
 * 
 * 问题现象：
 *   即使编码器差值超过100甚至200，小车仍然只执行初始动作，
 *   混沌机制从未被触发，向死而生程序形同虚设。
 * 
 * 问题根因：
 *   MotorController::update() 中，卡死检测和混沌触发逻辑被放在
 *   if (!ruleActive) 代码块内部。
 * 
 *   实际执行路径：
 *   1. 小车启动 → 种子个体规则开始执行 → ruleActive = true
 *   2. 小车撞墙 → 编码器差增大 (diff > 15)
 *   3. 卡死检测执行 → stuckFlag = true ✓
 *   4. 但触发混沌的代码在 if (!ruleActive) 块内
 *   5. 因为 ruleActive 一直为 true，永远跳不到触发混沌的代码
 *   6. 结果：stuckFlag 被设置，但混沌永不触发 ✗
 * 
 * 修复方案：
 *   1. 将卡死检测逻辑从 if (!ruleActive) 块中移出
 *       → 每帧都执行，与 ruleActive 状态解耦
 * 
 *   2. 将混沌触发判断移到 if (!ruleActive) 块外部
 *       → 无论 ruleActive 是 true 还是 false，都检查是否触发混沌
 * 
 *   3. 混沌触发后立即 return，避免与规则执行冲突
 * 
 *   4. 增加详细调试日志，便于确认各阶段是否正常
 * 
 * ================================================================
 * 修复后混沌触发流程（新动图）
 * ================================================================
 * 
 *                         ┌────────────────────────┐
 *                         │    每帧 loop 开始      │
 *                         └────────────┬───────────┘
 *                                      ▼
 *                         ┌────────────────────────┐
 *                         │  读取传感器 & 编码器   │
 *                         │  leftTicks, rightTicks │
 *                         └────────────┬───────────┘
 *                                      ▼
 *                         ┌────────────────────────┐
 *                         │  计算 diff = |L - R|   │
 *                         └────────────┬───────────┘
 *                                      ▼
 *                    ┌─────────────────────────────────┐
 *                    │         卡死检测 (3种模式)       │
 *                    │  → 设置 stuckFlag               │
 *                    │  (现在在 if(!ruleActive) 外部)  │
 *                    └────────────┬────────────────────┘
 *                                 ▼
 *                    ┌─────────────────────────────────┐
 *                    │   ✓ 混沌触发判断 (每帧执行)    │
 *                    │   stuckFlag && !chaosActive    │
 *                    │   && duration >= 2000ms        │
 *                    │   (现在在 if(!ruleActive) 外部) │
 *                    └────────────┬────────────────────┘
 *                                 │
 *                          ┌──────┴──────┐
 *                          ▼             ▼
 *                   ┌───────────┐  ┌───────────┐
 *                   │  触发混沌  │  │  继续执行  │
 *                   │ startChaos│  │  规则评估  │
 *                   │ 立即return│  │  (原逻辑)  │
 *                   └───────────┘  └───────────┘
 *                          │             │
 *                          └──────┬──────┘
 *                                 ▼
 *                    ┌─────────────────────────────────┐
 *                    │       混沌模式执行               │
 *                    │  GPIO35 热噪声直接驱动电机       │
 *                    │  代码退场，物理世界自主搜索       │
 *                    └────────────┬────────────────────┘
 *                                 ▼
 *                    ┌─────────────────────────────────┐
 *                    │     检测物理运动恢复             │
 *                    │  编码器差值变化 > 20             │
 *                    │  或速度 > WHEEL_STOP_THRESHOLD   │
 *                    │  连续20帧稳定                   │
 *                    └────────────┬────────────────────┘
 *                                 │
 *                          ┌──────┴──────┐
 *                          ▼             ▼
 *                   ┌───────────┐  ┌───────────┐
 *                   │  退出混沌  │  │  继续混沌  │
 *                   │  endChaos │  │  等待恢复  │
 *                   └───────────┘  └───────────┘
 * 
 * 修复前后对比：
 *   ┌────────────────────┬─────────────────┬─────────────────┐
 *   │ 维度               │ 修复前 (v9.3)   │ 修复后 (v9.4)   │
 *   ├────────────────────┼─────────────────┼─────────────────┤
 *   │ 卡死检测位置       │ if(!ruleActive) │ 每帧独立执行    │
 *   │                    │ 块内部          │                 │
 *   │ 混沌触发判断位置   │ if(!ruleActive) │ 每帧独立执行    │
 *   │                    │ 块内部          │                 │
 *   │ ruleActive=true时  │ 卡死检测跳过    │ 卡死检测正常    │
 *   │ 混沌是否可触发     │ ✗ 永不触发     │ ✓ 正常触发     │
 *   │ 调试可见性         │ 低              │ 高 (详细日志)   │
 *   └────────────────────┴─────────────────┴─────────────────┘
 * 
 * ================================================================ */

/* ================================================================
 * 项目名称：仿草履虫应激机制 - 脱困能力进化系统
 * 文件：OEETestFinal_v6.ino (v9.5-DataAuditFix)
 * 最后更新：2026-08-16
 * 
 * 【v9.5 修复内容】
 *   - FRAME_LOG_SIZE: 256 → 4096 (可存40秒数据)
 *   - 删除重复写入
 *   - 新增下载端点: /download/genes, /download/frames, /download/individual
 *   - 新增串口命令: ls, status, frame, pop, chaos, help
 * ================================================================ */

/* ================================================================
 * 项目名称：仿草履虫应激机制 - 脱困能力进化系统
 * 文件：OEETestFinal_v7.ino (v9.5-DataAuditFix)
 * 最后更新：2026-08-16
 * 
 * 【功能清单】
 *   ✅ FRAME_LOG_SIZE: 4096 (可存40秒数据)
 *   ✅ 完整Web下载界面 (历史/进化/混沌/种群/基因/帧日志/个体)
 *   ✅ 串口命令: ls, status, frame, pop, chaos, popall, help
 *   ✅ 一键查看一代所有个体数据
 * ================================================================ */

/* ================================================================
 * 项目名称：仿草履虫应激机制 - 脱困能力进化系统
 * 文件：OEETestFinal_v9.7.ino
 * 版本：v9.7-MotorBalanceFix
 * 最后更新：2026-08-16
 * 
 * 【v9.7 修复内容】
 *   1. 电机PWM不平衡补偿 - 分方向增益 + 死区补偿
 *   2. LEFT_FWD_GAIN=1.00, RIGHT_FWD_GAIN=1.25 唯一直行组合
 *   3. 左转/右转 Web 控制 PWM 调整 (-130/120)
 *   4. 完整串口命令: ls, status, frame, pop, chaos, popall, help
 *   5. FRAME_LOG_SIZE=4096 (可存40秒数据)
 * ================================================================ */

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

// ================================================================
// 硬件引脚定义
// ================================================================
#define PIN_SENSOR_LEFT     4
#define PIN_SENSOR_RIGHT    5
#define PIN_NOISE_SOURCE    1
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
// ★ v9.7 电机增益补偿 ★
// 校准基准: LEFT_FWD=1.00, RIGHT_FWD=1.25 是唯一直行组合
// ================================================================
#define LEFT_FWD_GAIN       1.10f
#define RIGHT_FWD_GAIN      1.25f
#define LEFT_REV_GAIN       1.35f   // 左电机反转增益（左转关键参数，可调）
#define RIGHT_REV_GAIN      1.00f   // 右电机反转增益（默认不变）
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
#define POPULATION_SIZE     8
#define TEST_DURATION_MS    30000
#define LOOP_DELAY_MS       10

// ================================================================
// 物理动作参数
// ================================================================
#define PHYSICS_STALL_FRAMES    10
#define PHYSICS_MAX_ACTION_MS   3000

// ================================================================
// 传感器阈值
// ================================================================
#define CLEAR_ADC_THRESH     600
#define OBSTACLE_ADC_THRESH  1500

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
#define NOVELTY_ARCHIVE_MAX    200
#define NOVELTY_K_NEAREST      5
#define NOVELTY_ADD_THRESHOLD  0.15f

// ================================================================
// 存储参数
// ================================================================
#define STORAGE_SAVE_INTERVAL_MS  10000
#define FRAME_LOG_SIZE            4096

// ================================================================
// 固件版本
// ================================================================
#define FIRMWARE_VERSION "v9.7-MotorBalanceFix"
#define VERSION_MARKER_FILE "/version_" FIRMWARE_VERSION ".mrk"

// ================================================================
// 卡死检测参数
// ================================================================
#define ENCODER_DIFF_THRESHOLD   50
#define ENCODER_STUCK_DURATION   1000
#define ENCODER_DIFF_MIN         6
#define WHEEL_SPIN_THRESHOLD     30
#define WHEEL_STOP_THRESHOLD     3

// ================================================================
// 混沌机制参数
// ================================================================
#define CHAOS_TIMEOUT_MS        2000
#define CHAOS_NOISE_AMPLIFIER   180
#define CHAOS_MIN_PWM           20
#define CHAOS_RECOVER_STABLE_FRAMES 20

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
    COND_ALWAYS       = 7,
    COND_STUCK        = 8,
    COND_ESCAPE       = 9
};

enum CondOp : uint8_t {
    OP_GREATER = 0,
    OP_LESS    = 1,
    OP_EQUAL   = 2
};

// ================================================================
// 行为规则结构体
// ================================================================
struct BehaviorRule {
    uint8_t  condType;
    int16_t  condValue;
    uint8_t  condOp;
    int16_t  motorL;
    int16_t  motorR;
    uint16_t durationMs;
    uint8_t  nextRule;
    uint8_t  _padding;

    void randomize() {
        condType   = random(0, 10);
        condValue  = random(-3000, 3000);
        condOp     = random(0, 3);
        motorL     = random(-255, 255);
        motorR     = random(-255, 255);
        durationMs = random(MIN_RULE_DURATION, MAX_RULE_DURATION);
        nextRule   = random(0, 3);
        _padding   = 0;
    }
    
    void clamp() {
        motorL     = constrain(motorL, -255, 255);
        motorR     = constrain(motorR, -255, 255);
        durationMs = constrain(durationMs, MIN_RULE_DURATION, MAX_RULE_DURATION);
        condType   = constrain(condType, (uint8_t)0, (uint8_t)9);
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

// ================================================================
// 行为描述符
// ================================================================
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

// ================================================================
// 帧日志条目 (11 bytes)
// ================================================================
struct FrameLogEntry {
    uint32_t timestamp_ms;
    int16_t  sensorLeft;
    int16_t  sensorRight;
    uint8_t  motorLeftPWM;
    uint8_t  motorRightPWM;
    uint8_t  directionL : 1;
    uint8_t  directionR : 1;
    uint8_t  chaosActive : 1;
    uint8_t  reserved : 4;
};

// ================================================================
// 历史记录
// ================================================================
struct HistoryRecord {
    uint32_t timestamp;
    uint32_t generation;
    float    noveltyScore;
    uint32_t survivalTime;
    int32_t  distance_ticks;
    uint8_t  ruleCount;
    uint8_t  reserved[3];
};

// ================================================================
// 混沌实验记录
// ================================================================
struct ChaoticTestRecord {
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
    uint8_t  chaosSuccess;
    uint8_t  testTerminatedBy;
    uint8_t  reserved[2];
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
// SensorCalibration 类
// ================================================================
class SensorCalibration {
private:
    static int leftBase, rightBase;
    
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
        Logger::logf("Calibration done: L=%d R=%d", leftBase, rightBase);
    }
    
    static int getLeftRaw() { return analogRead(PIN_SENSOR_LEFT); }
    static int getRightRaw() { return analogRead(PIN_SENSOR_RIGHT); }
    static bool isObstacleLeft() { return analogRead(PIN_SENSOR_LEFT) > OBSTACLE_ADC_THRESH; }
    static bool isObstacleRight() { return analogRead(PIN_SENSOR_RIGHT) > OBSTACLE_ADC_THRESH; }
    static bool isClear() {
        return (analogRead(PIN_SENSOR_LEFT) < CLEAR_ADC_THRESH && 
                analogRead(PIN_SENSOR_RIGHT) < CLEAR_ADC_THRESH);
    }
    static uint16_t readNoise() { return analogRead(PIN_NOISE_SOURCE); }
};

int SensorCalibration::leftBase = 0;
int SensorCalibration::rightBase = 0;

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
        if (!file) return;
        int count = 0;
        while (file.available() && count < 1000) {
            String line = file.readStringUntil('\n');
            if (line.length() < 10) continue;
            int partIdx = 0;
            String parts[6];
            for (int i = 0; i < line.length() && partIdx < 6; i++) {
                if (line[i] == ',') { partIdx++; continue; }
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
    }

public:
    static void init() {
        fsReady = false;
        lastSaveTime = 0;
        unsavedCount = 0;
        totalSaved = 0;
        ramHistoryCount = 0;
        pendingBuffer.reserve(MAX_BUFFER_SIZE);
        if (!SPIFFS.begin(true)) {
            Logger::log("SPIFFS mount failed, using RAM mode");
            fsReady = false;
        } else {
            fsReady = true;
            Logger::log("SPIFFS mounted successfully");
            loadHistory();
        }
    }

    static void addRecord(const HistoryRecord& record) {
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
        if (pendingBuffer.length() > MAX_BUFFER_SIZE || unsavedCount >= 5) flushBuffer();
    }

    static bool flushBuffer() {
        if (pendingBuffer.length() == 0) { unsavedCount = 0; return true; }
        if (!fsReady) {
            pendingBuffer = "";
            unsavedCount = 0;
            lastSaveTime = millis();
            return true;
        }
        bool success = appendToFile(HISTORY_FILE, pendingBuffer);
        if (success) {
            pendingBuffer = "";
            unsavedCount = 0;
            lastSaveTime = millis();
        }
        return success;
    }

    static void forceSave() { if (pendingBuffer.length() > 0) flushBuffer(); }
    static bool isReady() { return fsReady; }
    static uint32_t getTotalSaved() { return totalSaved; }
    static void tick() {
        if (millis() - lastSaveTime > STORAGE_SAVE_INTERVAL_MS && pendingBuffer.length() > 0) flushBuffer();
    }
    static void clearAll() { pendingBuffer = ""; unsavedCount = 0; ramHistoryCount = 0; }
    static String getCSVData() {
        if (pendingBuffer.length() > 0) flushBuffer();
        if (fsReady) {
            File file = SPIFFS.open(HISTORY_FILE, FILE_READ);
            if (file) { String content = file.readString(); file.close(); return content; }
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
        return csv;
    }

    static const char* CHAOS_HISTORY_FILE;

    static void addChaosRecord(const ChaoticTestRecord& record) {
        if (!fsReady) return;
        char line[512];
        snprintf(line, sizeof(line),
            "%lu,%lu,%d,%d,%lu,%lu,%lu,%lu,%ld,%ld,%d,%d,%.2f,%.2f,%.2f,%.2f,%d,%d\n",
            record.timestamp, record.generation, record.individual,
            record.chaosTriggerCount, record.chaosTotalDuration,
            record.chaosMaxDuration, record.chaosFirstTime,
            record.chaosLastTime,
            (long)record.baselineDistance, (long)record.chaosDistance,
            record.baselineFrames, record.chaosFrames,
            record.baselineAvgSpeedL, record.baselineAvgSpeedR,
            record.chaosAvgSpeedL, record.chaosAvgSpeedR,
            record.chaosSuccess, record.testTerminatedBy);
        File file = SPIFFS.open(CHAOS_HISTORY_FILE, FILE_APPEND);
        if (!file) {
            file = SPIFFS.open(CHAOS_HISTORY_FILE, FILE_WRITE);
            if (!file) return;
            file.print("timestamp,generation,individual,chaosTriggerCount,chaosTotalDuration,"
                       "chaosMaxDuration,chaosFirstTime,chaosLastTime,"
                       "baselineDistance,chaosDistance,baselineFrames,chaosFrames,"
                       "baselineAvgSpeedL,baselineAvgSpeedR,chaosAvgSpeedL,chaosAvgSpeedR,"
                       "chaosSuccess,testTerminatedBy\n");
        }
        if (file) { file.print(line); file.close(); }
    }

    static String getChaosHistoryCSV() {
        if (!fsReady) return "";
        File file = SPIFFS.open(CHAOS_HISTORY_FILE, FILE_READ);
        if (!file) return "";
        String content = file.readString();
        file.close();
        return content;
    }
};

const char* RobustStorage::HISTORY_FILE = "/oe_history.csv";
const char* RobustStorage::CHAOS_HISTORY_FILE = "/chaos_history.csv";
bool RobustStorage::fsReady = false;
uint32_t RobustStorage::lastSaveTime = 0;
uint32_t RobustStorage::unsavedCount = 0;
uint32_t RobustStorage::totalSaved = 0;
String RobustStorage::pendingBuffer = "";
HistoryRecord RobustStorage::ramHistory[1000];
int RobustStorage::ramHistoryCount = 0;

// ================================================================
// FirmwareVersionManager 类
// ================================================================
class FirmwareVersionManager {
public:
    static bool isNewVersion() {
        if (!RobustStorage::isReady()) return true;
        if (!SPIFFS.exists(VERSION_MARKER_FILE)) return true;
        File marker = SPIFFS.open(VERSION_MARKER_FILE, FILE_READ);
        if (!marker) return true;
        String savedVersion = marker.readString();
        marker.close();
        savedVersion.trim();
        return savedVersion != String(FIRMWARE_VERSION);
    }
    
    static void cleanAllData() {
        if (!RobustStorage::isReady()) return;
        File root = SPIFFS.open("/");
        if (!root) return;
        while (File file = root.openNextFile()) {
            String name = String(file.name());
            bool shouldDelete = false;
            if (name.startsWith("/pop_gen_")) shouldDelete = true;
            if (name.startsWith("/gen_")) shouldDelete = true;
            if (name.startsWith("/frm_")) shouldDelete = true;
            if (name == "/oe_history.csv") shouldDelete = true;
            if (name == "/experiment_state.mrk") shouldDelete = true;
            if (name.startsWith("/version_") && name != String(VERSION_MARKER_FILE)) shouldDelete = true;
            if (shouldDelete) { file.close(); SPIFFS.remove(name); }
        }
        root.close();
        writeVersionMarker();
    }
    
    static void writeVersionMarker() {
        if (!RobustStorage::isReady()) return;
        if (SPIFFS.exists(VERSION_MARKER_FILE)) SPIFFS.remove(VERSION_MARKER_FILE);
        File marker = SPIFFS.open(VERSION_MARKER_FILE, FILE_WRITE);
        if (marker) { marker.println(FIRMWARE_VERSION); marker.close(); }
    }
};

// ================================================================
// FileUtils 类
// ================================================================
class FileUtils {
public:
    static bool atomicWrite(const String& path, const uint8_t* data, size_t len) {
        if (!RobustStorage::isReady()) return false;
        String tempPath = path + ".tmp";
        if (SPIFFS.exists(tempPath)) SPIFFS.remove(tempPath);
        File tempFile = SPIFFS.open(tempPath, FILE_WRITE);
        if (!tempFile) return false;
        size_t written = tempFile.write(data, len);
        tempFile.close();
        if (written != len) { SPIFFS.remove(tempPath); return false; }
        if (SPIFFS.exists(path)) SPIFFS.remove(path);
        if (!SPIFFS.rename(tempPath, path)) { SPIFFS.remove(tempPath); return false; }
        return true;
    }
    
    static bool atomicWriteString(const String& path, const String& content) {
        return atomicWrite(path, (const uint8_t*)content.c_str(), content.length());
    }
    
    static String safeRead(const String& path) {
        if (!RobustStorage::isReady()) return "";
        if (!SPIFFS.exists(path)) return "";
        File file = SPIFFS.open(path, FILE_READ);
        if (!file) return "";
        String content = file.readString();
        file.close();
        return content;
    }
    
    static size_t getFileSize(const String& path) {
        if (!RobustStorage::isReady()) return 0;
        if (!SPIFFS.exists(path)) return 0;
        File file = SPIFFS.open(path, FILE_READ);
        if (!file) return 0;
        size_t size = file.size();
        file.close();
        return size;
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

    void init() {
        ruleCount = random(MIN_RULES, MAX_RULES + 1);
        for (int i = 0; i < ruleCount; i++) rules[i].randomize();
        survival_time = 0; distance_ticks = 0;
        noveltyScore = 0; behavior.init();
    }

    bool evaluateCondition(int ruleIdx, int leftSensor, int rightSensor, 
                           int32_t distance, uint32_t elapsed) const;

    void mutate(float mutationRate) {
        for (int i = 0; i < ruleCount; i++) {
            if (random(0, 1000) < mutationRate * 1000) { rules[i].condValue += random(-200,200); rules[i].clamp(); }
            if (random(0, 1000) < mutationRate * 1000) { rules[i].motorL += random(-50,50); rules[i].clamp(); }
            if (random(0, 1000) < mutationRate * 1000) { rules[i].motorR += random(-50,50); rules[i].clamp(); }
            if (random(0, 1000) < mutationRate * 500) { rules[i].durationMs += random(-200,200); rules[i].clamp(); }
            if (random(0, 1000) < mutationRate * 300) { rules[i].condType = random(0, 10); }
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

// ================================================================
// GeneStorage 类
// ================================================================
class GeneStorage {
private:
    static const uint32_t MAX_GENERATIONS_PER_EXPERIMENT = 20;
    static const uint16_t FILE_VERSION = 0x0002;
    static const uint32_t FILE_MAGIC = 0x47454E45;
    static const char* POP_PREFIX;
    static const char* GEN_RECORD_PREFIX;
    static const char* FRAME_LOG_PREFIX;
    static const char* EXPERIMENT_MARKER;
    static uint32_t currentExperimentId;
    static uint32_t currentGeneration;
    static bool experimentActive;
    
public:
    static void init() {
        currentExperimentId = 0;
        currentGeneration = 0;
        experimentActive = false;
        if (!RobustStorage::isReady()) {
            Logger::log("SPIFFS unavailable, using RAM mode");
            return;
        }
        loadExperimentState();
        Logger::logf("Gene storage initialized");
    }
    
    static void startNewExperiment() {
        if (!RobustStorage::isReady()) return;
        clearAllExperimentData();
        currentExperimentId = (uint32_t)millis();
        currentGeneration = 0;
        experimentActive = true;
        saveExperimentState();
        Logger::logf("New experiment started (ID: %lu)", currentExperimentId);
    }
    
    static bool shouldStartNewExperiment() {
        if (!RobustStorage::isReady()) return true;
        if (!SPIFFS.exists(EXPERIMENT_MARKER)) return true;
        String content = FileUtils::safeRead(EXPERIMENT_MARKER);
        if (content.length() == 0) return true;
        int commaPos = content.indexOf(',');
        if (commaPos < 0) return true;
        uint32_t expId = content.substring(0, commaPos).toInt();
        uint32_t gen = content.substring(commaPos + 1).toInt();
        if (gen >= MAX_GENERATIONS_PER_EXPERIMENT) {
            Logger::logf("Previous experiment completed (ID: %lu, gens: %lu)", expId, gen);
            return true;
        }
        currentExperimentId = expId;
        currentGeneration = gen;
        experimentActive = true;
        Logger::logf("Resuming experiment (ID: %lu, gen: %lu)", expId, gen);
        return false;
    }
    
    static bool isExperimentActive() { return experimentActive; }
    static uint32_t getCurrentGeneration() { return currentGeneration; }
    static uint32_t getMaxGenerations() { return MAX_GENERATIONS_PER_EXPERIMENT; }
    static float getProgress() { return (float)currentGeneration / MAX_GENERATIONS_PER_EXPERIMENT; }
    static bool isExperimentComplete() { return currentGeneration >= MAX_GENERATIONS_PER_EXPERIMENT; }
    static void incrementGeneration() { currentGeneration++; saveExperimentState(); }
    
    static bool savePopulation(Gene* population, int size) {
        if (!RobustStorage::isReady() || !experimentActive) return false;
        size_t totalSize = 4 + 2 + 2 + 4 + 4;
        for (int i = 0; i < size; i++) {
            totalSize += 1 + population[i].ruleCount * sizeof(BehaviorRule) + 12;
        }
        uint8_t* buffer = (uint8_t*)malloc(totalSize);
        if (!buffer) return false;
        uint8_t* ptr = buffer;
        uint32_t magic = FILE_MAGIC; memcpy(ptr, &magic, 4); ptr += 4;
        uint16_t version = FILE_VERSION; memcpy(ptr, &version, 2); ptr += 2;
        uint16_t popSize = size; memcpy(ptr, &popSize, 2); ptr += 2;
        uint32_t gen = currentGeneration; memcpy(ptr, &gen, 4); ptr += 4;
        uint32_t expId = currentExperimentId; memcpy(ptr, &expId, 4); ptr += 4;
        for (int i = 0; i < size; i++) {
            *ptr = population[i].ruleCount; ptr += 1;
            for (int j = 0; j < population[i].ruleCount; j++) {
                memcpy(ptr, &population[i].rules[j], sizeof(BehaviorRule));
                ptr += sizeof(BehaviorRule);
            }
            memcpy(ptr, &population[i].survival_time, 4); ptr += 4;
            memcpy(ptr, &population[i].distance_ticks, 4); ptr += 4;
            memcpy(ptr, &population[i].noveltyScore, 4); ptr += 4;
        }
        String path = String(POP_PREFIX) + String(currentGeneration) + ".bin";
        bool success = FileUtils::atomicWrite(path, buffer, totalSize);
        free(buffer);
        Logger::logf("💾 savePopulation gen=%lu -> %s %s", currentGeneration, path.c_str(), success ? "✅ OK" : "❌ FAILED");
        return success;
    }
    
    static bool saveIndividualRecord(uint32_t generation, int individual, const Gene& gene) {
        if (!RobustStorage::isReady() || !experimentActive) {
            Logger::logf("⚠️ saveIndividualRecord: SPIFFS not ready, skipping gen=%lu id=%d", generation, individual);
            return false;
        }
        String path = String(GEN_RECORD_PREFIX) + String(generation) + "_id_" + String(individual) + ".csv";
        String content = "ruleIndex,condType,condValue,condOp,motorL,motorR,durationMs,nextRule,survival_time,distance_ticks,noveltyScore\n";
        for (int i = 0; i < gene.ruleCount; i++) {
            const BehaviorRule& r = gene.rules[i];
            content += String(i) + "," + String(r.condType) + "," + String(r.condValue) + ",";
            content += String(r.condOp) + "," + String(r.motorL) + "," + String(r.motorR) + ",";
            content += String(r.durationMs) + "," + String(r.nextRule) + ",";
            content += String(gene.survival_time) + "," + String(gene.distance_ticks) + ",";
            content += String(gene.noveltyScore, 6) + "\n";
        }
        bool success = FileUtils::atomicWriteString(path, content);
        Logger::logf("💾 saveIndividualRecord gen=%lu id=%d -> %s %s", generation, individual, path.c_str(), success ? "✅ OK" : "❌ FAILED");
        return success;
    }
    
    static bool saveFrameLog(uint32_t generation, int individual, const FrameLogEntry* log, int count) {
        if (!RobustStorage::isReady() || !experimentActive || count == 0) {
            Logger::logf("⚠️ saveFrameLog: skipping gen=%lu id=%d (ready=%d active=%d count=%d)", 
                         generation, individual, RobustStorage::isReady(), experimentActive, count);
            return false;
        }
        int exportCount = min(count, FRAME_LOG_SIZE);
        size_t totalSize = exportCount * 11;
        uint8_t* buffer = (uint8_t*)malloc(totalSize);
        if (!buffer) return false;
        uint8_t* ptr = buffer;
        for (int i = 0; i < exportCount; i++) {
            const FrameLogEntry& e = log[i];
            memcpy(ptr, &e.timestamp_ms, 4); ptr += 4;
            memcpy(ptr, &e.sensorLeft, 2); ptr += 2;
            memcpy(ptr, &e.sensorRight, 2); ptr += 2;
            *ptr = e.motorLeftPWM; ptr += 1;
            *ptr = e.motorRightPWM; ptr += 1;
            uint8_t dir = (e.directionL ? 0x01 : 0x00) | (e.directionR ? 0x02 : 0x00);
            *ptr = dir; ptr += 1;
        }
        String path = String(FRAME_LOG_PREFIX) + String(generation) + "_i" + String(individual) + ".bin";
        bool success = FileUtils::atomicWrite(path, buffer, totalSize);
        free(buffer);
        Logger::logf("💾 saveFrameLog gen=%lu id=%d -> %s (%d frames) %s", 
                     generation, individual, path.c_str(), exportCount, success ? "✅ OK" : "❌ FAILED");
        return success;
    }

    static bool saveChaosRecord(const ChaoticTestRecord& record) {
        if (!RobustStorage::isReady()) return false;
        String path = "/chaos_g" + String(record.generation) + "_i" + String(record.individual) + ".csv";
        String csv = "timestamp,generation,individual,chaosTriggerCount,chaosTotalDuration,"
                     "chaosMaxDuration,chaosFirstTime,chaosLastTime,"
                     "baselineDistance,chaosDistance,baselineFrames,chaosFrames,"
                     "baselineAvgSpeedL,baselineAvgSpeedR,chaosAvgSpeedL,chaosAvgSpeedR,"
                     "chaosSuccess,testTerminatedBy\n";
        char line[512];
        snprintf(line, sizeof(line),
            "%lu,%lu,%d,%d,%lu,%lu,%lu,%lu,%ld,%ld,%d,%d,%.2f,%.2f,%.2f,%.2f,%d,%d\n",
            record.timestamp, record.generation, record.individual,
            record.chaosTriggerCount, record.chaosTotalDuration,
            record.chaosMaxDuration, record.chaosFirstTime,
            record.chaosLastTime,
            (long)record.baselineDistance, (long)record.chaosDistance,
            record.baselineFrames, record.chaosFrames,
            record.baselineAvgSpeedL, record.baselineAvgSpeedR,
            record.chaosAvgSpeedL, record.chaosAvgSpeedR,
            record.chaosSuccess, record.testTerminatedBy);
        csv += line;
        bool success = FileUtils::atomicWriteString(path, csv);
        Logger::logf("💾 saveChaosRecord gen=%lu id=%d -> %s %s", 
                     record.generation, record.individual, path.c_str(), success ? "✅ OK" : "❌ FAILED");
        return success;
    }

    static bool loadPopulation(uint32_t generation, Gene* population, int size) {
        if (!RobustStorage::isReady()) return false;
        String path = String(POP_PREFIX) + String(generation) + ".bin";
        size_t fileSize = FileUtils::getFileSize(path);
        if (fileSize == 0) return false;
        File file = SPIFFS.open(path, FILE_READ);
        if (!file) return false;
        uint8_t* buffer = (uint8_t*)malloc(fileSize);
        if (!buffer) { file.close(); return false; }
        file.read(buffer, fileSize);
        file.close();
        uint8_t* ptr = buffer;
        uint32_t magic; memcpy(&magic, ptr, 4); ptr += 4;
        if (magic != FILE_MAGIC) { free(buffer); return false; }
        ptr += 2;
        uint16_t popSize; memcpy(&popSize, ptr, 2); ptr += 2;
        if (popSize != size) { free(buffer); return false; }
        ptr += 4; ptr += 4;
        for (int i = 0; i < size; i++) {
            population[i].ruleCount = *ptr; ptr += 1;
            for (int j = 0; j < population[i].ruleCount && j < MAX_RULES; j++) {
                memcpy(&population[i].rules[j], ptr, sizeof(BehaviorRule));
                ptr += sizeof(BehaviorRule);
            }
            memcpy(&population[i].survival_time, ptr, 4); ptr += 4;
            memcpy(&population[i].distance_ticks, ptr, 4); ptr += 4;
            memcpy(&population[i].noveltyScore, ptr, 4); ptr += 4;
            population[i].behavior.init();
        }
        free(buffer);
        return true;
    }
    
    static void clearAllExperimentData() {
        if (!RobustStorage::isReady()) return;
        File root = SPIFFS.open("/");
        if (!root) return;
        while (File file = root.openNextFile()) {
            String name = String(file.name());
            if (name.startsWith("/pop_gen_") || name.startsWith("/gen_") || 
                name.startsWith("/frm_") || name == "/experiment_state.mrk" || name == "/oe_history.csv") {
                file.close();
                SPIFFS.remove(name);
            }
        }
        root.close();
        currentExperimentId = 0;
        currentGeneration = 0;
        experimentActive = false;
    }
    
private:
    static void saveExperimentState() {
        if (!RobustStorage::isReady()) return;
        String content = String(currentExperimentId) + "," + String(currentGeneration);
        FileUtils::atomicWriteString(EXPERIMENT_MARKER, content);
    }
    
    static void loadExperimentState() {
        if (!RobustStorage::isReady()) return;
        String content = FileUtils::safeRead(EXPERIMENT_MARKER);
        if (content.length() == 0) { experimentActive = false; return; }
        int commaPos = content.indexOf(',');
        if (commaPos < 0) { experimentActive = false; return; }
        currentExperimentId = content.substring(0, commaPos).toInt();
        currentGeneration = content.substring(commaPos + 1).toInt();
        experimentActive = true;
    }
};

const char* GeneStorage::POP_PREFIX = "/pop_gen_";
const char* GeneStorage::GEN_RECORD_PREFIX = "/gen_";
const char* GeneStorage::FRAME_LOG_PREFIX = "/frm_";
const char* GeneStorage::EXPERIMENT_MARKER = "/experiment_state.mrk";
uint32_t GeneStorage::currentExperimentId = 0;
uint32_t GeneStorage::currentGeneration = 0;
bool GeneStorage::experimentActive = false;

// ================================================================
// NoveltyArchive 类
// ================================================================
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

    BehaviorDescriptor extractFromFrameLog(const FrameLogEntry* frameLog, int frameCount, int32_t totalDistance) {
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
            float dL = frameLog[i].sensorLeft - desc.leftSensorMean;
            float dR = frameLog[i].sensorRight - desc.rightSensorMean;
            varL += dL*dL; varR += dR*dR;
        }
        desc.sensorVariance = (varL + varR) / (2.0f * n);
        return desc;
    }

    int getArchiveSize() const { return archiveSize; }
};

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
    static int32_t  actionLastTicks;
    static int      actionStallFrames;

    static volatile int32_t leftTicks;
    static volatile int32_t rightTicks;
    static bool stuckFlag;
    static uint32_t stuckStartTime;
    static int32_t lastDiff;
    static bool escapeActive;

    static bool chaosActive;
    static uint32_t chaosStartTime;

    static int32_t  testChaosTriggerCount;
    static uint32_t testChaosTotalDuration;
    static uint32_t testChaosMaxDuration;
    static uint32_t testChaosFirstTriggerTime;
    static uint32_t testChaosLastTriggerTime;
    static int32_t  baselineDistanceTicks;
    static int32_t  chaosDistanceTicks;
    static int      baselineFrameCount;
    static int      chaosFrameCount;
    static float    baselineSpeedLSum;
    static float    baselineSpeedRSum;
    static float    chaosSpeedLSum;
    static float    chaosSpeedRSum;
    static bool     chaosHappened;
    static int32_t  chaosStartDistance;

    // ★ v9.7 ★ 新的 setMotorSpeed 实现 (分方向增益 + 死区补偿)
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

        // --- 左电机：分方向增益 + 死区补偿 ---
        int outL;
        if (leftPWM > 0) {
            // 正转：使用 LEFT_FWD_GAIN（直行基准，1.00 不变）
            outL = constrain((int)(leftPWM * LEFT_FWD_GAIN), 0, MOTOR_PWM_MAX);
            if (outL > 0 && outL < LEFT_DEADZONE) outL = LEFT_DEADZONE;
        } else if (leftPWM < 0) {
            // 反转：使用 LEFT_FWD_GAIN × LEFT_REV_GAIN（补偿反转无力）
            outL = constrain((int)(-leftPWM * LEFT_FWD_GAIN * LEFT_REV_GAIN), 0, MOTOR_PWM_MAX);
            if (outL > 0 && outL < LEFT_DEADZONE) outL = LEFT_DEADZONE;
            outL = -outL;
        } else {
            outL = 0;
        }

        // --- 右电机：分方向增益 + 死区补偿 ---
        int outR;
        if (rightPWM > 0) {
            // 正转：使用 RIGHT_FWD_GAIN（直行基准，1.25 不变）
            outR = constrain((int)(rightPWM * RIGHT_FWD_GAIN), 0, MOTOR_PWM_MAX);
            if (outR > 0 && outR < RIGHT_DEADZONE) outR = RIGHT_DEADZONE;
        } else if (rightPWM < 0) {
            // 反转：使用 RIGHT_FWD_GAIN × RIGHT_REV_GAIN
            outR = constrain((int)(-rightPWM * RIGHT_FWD_GAIN * RIGHT_REV_GAIN), 0, MOTOR_PWM_MAX);
            if (outR > 0 && outR < RIGHT_DEADZONE) outR = RIGHT_DEADZONE;
            outR = -outR;
        } else {
            outR = 0;
        }

        // --- 写入硬件：先方向后PWM ---
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

public:
    static bool isStuck() { return stuckFlag; }
    static bool isEscapeActive() { return escapeActive; }
    static bool isChaosActive() { return chaosActive; }
    static uint32_t getChaosDuration() {
        if (!chaosActive) return 0;
        return millis() - chaosStartTime;
    }
    static int32_t getTestChaosTriggerCount() { return testChaosTriggerCount; }
    static uint32_t getTestChaosTotalDuration() { return testChaosTotalDuration; }
    static uint32_t getTestChaosMaxDuration() { return testChaosMaxDuration; }
    static uint32_t getTestChaosFirstTriggerTime() { return testChaosFirstTriggerTime; }
    static uint32_t getTestChaosLastTriggerTime() { return testChaosLastTriggerTime; }
    static int32_t getBaselineDistanceTicks() { return baselineDistanceTicks; }
    static int32_t getChaosDistanceTicks() { return chaosDistanceTicks; }
    static int getBaselineFrameCount() { return baselineFrameCount; }
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
    
    static void resetChaosStatistics() {
        testChaosTriggerCount = 0;
        testChaosTotalDuration = 0;
        testChaosMaxDuration = 0;
        testChaosFirstTriggerTime = 0;
        testChaosLastTriggerTime = 0;
        baselineDistanceTicks = 0;
        chaosDistanceTicks = 0;
        baselineFrameCount = 0;
        chaosFrameCount = 0;
        baselineSpeedLSum = 0;
        baselineSpeedRSum = 0;
        chaosSpeedLSum = 0;
        chaosSpeedRSum = 0;
        chaosHappened = false;
        chaosStartDistance = 0;
    }

    static void setEscapeActive(bool val) { escapeActive = val; }
    static void resetStuck() { stuckFlag = false; stuckStartTime = 0; }
    static int32_t getLeftTicks() { return leftTicks; }
    static int32_t getRightTicks() { return rightTicks; }
    static int32_t getEncoderDiff() { return abs(leftTicks - rightTicks); }

    static void init() {
        currentSpeedL = 0; currentSpeedR = 0;
        distanceTicks = 0;
        leftTicks = 0;
        rightTicks = 0;
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
        stuckFlag = false;
        stuckStartTime = 0;
        lastDiff = 0;
        escapeActive = false;
        chaosActive = false;
        chaosStartTime = 0;
        testChaosTriggerCount = 0;
        testChaosTotalDuration = 0;
        testChaosMaxDuration = 0;
        testChaosFirstTriggerTime = 0;
        testChaosLastTriggerTime = 0;
        baselineDistanceTicks = 0;
        chaosDistanceTicks = 0;
        baselineFrameCount = 0;
        chaosFrameCount = 0;
        baselineSpeedLSum = 0;
        baselineSpeedRSum = 0;
        chaosSpeedLSum = 0;
        chaosSpeedRSum = 0;
        chaosHappened = false;
        chaosStartDistance = 0;

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
        Logger::log("Motor Controller initialized (v9.7 Balance Fix)");
        Logger::logf("  LEFT_FWD=%.2f RIGHT_FWD=%.2f LEFT_REV=%.2f RIGHT_REV=%.2f",
                     LEFT_FWD_GAIN, RIGHT_FWD_GAIN, LEFT_REV_GAIN, RIGHT_REV_GAIN);
    }

    static void stopMotors() {
        setMotorSpeed(0, 0);
        digitalWrite(PIN_LEFT_DIR2, LOW);
        digitalWrite(PIN_RIGHT_DIR2, LOW);
        actionInProgress = false;
        ruleActive = false;
        stuckFlag = false;
        stuckStartTime = 0;
    }

    static void enableMotor() { motorEnabled = true; stopMotors(); Logger::log("Motor enabled"); }
    static void disableMotor() { motorEnabled = false; stopMotors(); Logger::log("Motor disabled"); }
    static bool isMotorEnabled() { return motorEnabled; }

    static void startPhysicsAction(int leftPWM, int rightPWM, uint16_t minDurationMs) {
        if (!motorEnabled) { Logger::log("Motor locked"); return; }
        if (actionInProgress) stopMotors();
        actionInProgress = true;
        actionStartTime = millis();
        actionLastTicks = distanceTicks;
        actionStallFrames = 0;
        setMotorSpeed(leftPWM, rightPWM);
    }

    static void updatePhysicsAction() {
        if (!actionInProgress) return;
        updateEncoders();
        int32_t currentTicks = distanceTicks;
        int deltaTicks = abs((int)(currentTicks - actionLastTicks));
        actionLastTicks = currentTicks;
        if (deltaTicks == 0) actionStallFrames++;
        else actionStallFrames = max(0, actionStallFrames - 1);
        uint32_t elapsed = millis() - actionStartTime;
        if (elapsed >= 100 && (actionStallFrames > PHYSICS_STALL_FRAMES || elapsed > PHYSICS_MAX_ACTION_MS)) {
            actionInProgress = false;
            stopMotors();
        }
    }

    static void forceStopAction() {
        if (actionInProgress) { actionInProgress = false; stopMotors(); }
    }
    static bool isActionInProgress() { return actionInProgress; }

    static void startChaos() {
        if (!motorEnabled) { Logger::log("Motor locked, cannot start chaos"); return; }
        if (chaosActive) return;
        forceStopAction();
        stopMotors();
        chaosActive = true;
        chaosStartTime = millis();
        testChaosTriggerCount++;
        chaosStartDistance = distanceTicks;
        if (testChaosTriggerCount == 1) {
            testChaosFirstTriggerTime = millis() - testStartTime;
        }
        chaosHappened = true;
        Logger::log("========================================");
        Logger::log("!!! CHAOS MODE TRIGGERED !!!");
        Logger::logf("  Trigger count: %d", testChaosTriggerCount);
        Logger::logf("  Encoder diff: %d", getEncoderDiff());
        Logger::log("  Physical noise takeover started");
        Logger::log("========================================");
    }
    
    static void updateChaos() {
        if (!chaosActive) return;
        uint16_t noiseRaw = SensorCalibration::readNoise();
        int noiseL = constrain((int)(noiseRaw / 4) * CHAOS_NOISE_AMPLIFIER / 64, 
                               -CHAOS_NOISE_AMPLIFIER, CHAOS_NOISE_AMPLIFIER);
        int noiseR = constrain((int)(-noiseRaw / 4) * CHAOS_NOISE_AMPLIFIER / 64, 
                               -CHAOS_NOISE_AMPLIFIER, CHAOS_NOISE_AMPLIFIER);
        if (abs(noiseL) < CHAOS_MIN_PWM) noiseL = (noiseL >= 0 ? CHAOS_MIN_PWM : -CHAOS_MIN_PWM);
        if (abs(noiseR) < CHAOS_MIN_PWM) noiseR = (noiseR >= 0 ? CHAOS_MIN_PWM : -CHAOS_MIN_PWM);
        setMotorSpeed(noiseL, noiseR);
        leftSensorRaw = SensorCalibration::getLeftRaw();
        rightSensorRaw = SensorCalibration::getRightRaw();
        logFrame();
        static int recoverStableFrames = 0;
        int32_t currentDiff = abs(leftTicks - rightTicks);
        int32_t currentSpeedL_check = abs(currentSpeedL);
        int32_t currentSpeedR_check = abs(currentSpeedR);
        bool physicalRecovery = (abs(currentDiff - lastDiff) > 20) ||
                               (currentSpeedL_check > WHEEL_STOP_THRESHOLD) ||
                               (currentSpeedR_check > WHEEL_STOP_THRESHOLD);
        if (physicalRecovery) {
            recoverStableFrames++;
            lastDiff = currentDiff;
            if (recoverStableFrames >= CHAOS_RECOVER_STABLE_FRAMES) {
                Logger::log("Physical recovery confirmed (encoder movement detected)");
                endChaos();
                recoverStableFrames = 0;
            }
        } else {
            recoverStableFrames = 0;
        }
    }
    
    static bool endChaos() {
        if (!chaosActive) return false;
        chaosActive = false;
        stopMotors();
        uint32_t chaosDuration = millis() - chaosStartTime;
        testChaosTotalDuration += chaosDuration;
        if (chaosDuration > testChaosMaxDuration) testChaosMaxDuration = chaosDuration;
        testChaosLastTriggerTime = millis() - testStartTime;
        int32_t currentDist = distanceTicks;
        chaosDistanceTicks += currentDist - chaosStartDistance;
        Logger::log("========================================");
        Logger::log("!!! CHAOS MODE ENDED !!!");
        Logger::logf("  Duration: %lums", chaosDuration);
        Logger::logf("  Total triggers: %d", testChaosTriggerCount);
        Logger::log("  Code resumed control");
        Logger::log("========================================");
        stuckFlag = false;
        stuckStartTime = 0;
        escapeActive = false;
        return true;
    }

    static void startFrameLog() { frameLogHead = 0; frameLogCount = 0; testStartTime = millis(); }

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
            entry.chaosActive = chaosActive ? 1 : 0;
            if (chaosActive) {
                chaosFrameCount++;
                chaosSpeedLSum += currentSpeedL;
                chaosSpeedRSum += currentSpeedR;
            } else {
                baselineFrameCount++;
                baselineSpeedLSum += currentSpeedL;
                baselineSpeedRSum += currentSpeedR;
            }
            frameLogHead = (frameLogHead + 1) % FRAME_LOG_SIZE;
            frameLogCount++;
        }
    }

    static void update(const Gene& gene) {
        if (!motorEnabled) { stopMotors(); return; }
        updateEncoders();
        leftSensorRaw = SensorCalibration::getLeftRaw();
        rightSensorRaw = SensorCalibration::getRightRaw();
        int32_t diff = abs(leftTicks - rightTicks);
        bool diffIncreasing = (diff > lastDiff + ENCODER_DIFF_MIN);
        bool leftSpinning = (currentSpeedL > WHEEL_SPIN_THRESHOLD);
        bool rightSpinning = (currentSpeedR > WHEEL_SPIN_THRESHOLD);
        bool leftStopped = (currentSpeedL < WHEEL_STOP_THRESHOLD);
        bool rightStopped = (currentSpeedR < WHEEL_STOP_THRESHOLD);
        bool anySpinning = leftSpinning || rightSpinning;
        bool diffStuck = (diff > ENCODER_DIFF_THRESHOLD && diffIncreasing && 
                          leftSpinning && rightSpinning && !escapeActive);
        bool oneWheelStuck = ((leftSpinning && rightStopped) || 
                              (rightSpinning && leftStopped)) && !escapeActive;
        bool largeDiffStuck = (diff > ENCODER_DIFF_THRESHOLD * 2 && 
                               anySpinning && !escapeActive);
        bool isStuckCondition = diffStuck || oneWheelStuck || largeDiffStuck;
        if (isStuckCondition) {
            if (!stuckFlag) {
                stuckStartTime = millis();
                stuckFlag = true;
                Logger::logf("[STUCK] Detected! diff=%ld, L=%d, R=%d", diff, currentSpeedL, currentSpeedR);
            }
        } else {
            if (stuckFlag) {
                bool shouldClear = (diff < ENCODER_DIFF_THRESHOLD || !diffIncreasing);
                if (shouldClear || (!diffStuck && !oneWheelStuck && !largeDiffStuck)) {
                    stuckFlag = false;
                    stuckStartTime = 0;
                    Logger::log("[STUCK] Cleared");
                }
            }
        }
        lastDiff = diff;

        if (stuckFlag && !chaosActive) {
            uint32_t stuckDuration = millis() - stuckStartTime;
            if (stuckDuration >= CHAOS_TIMEOUT_MS) {
                startChaos();
                logFrame();
                return;
            }
        }

        if (chaosActive) {
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

        float noise = (float)(SensorCalibration::readNoise() % 1000) / 1000.0f;
        uint32_t elapsed = millis() - testStartTime;
        int32_t dist = distanceTicks;

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
                    uint16_t modDuration = constrain((uint16_t)(r.durationMs * noiseMod), 
                                                     MIN_RULE_DURATION, MAX_RULE_DURATION);
                    if (r.condType == COND_STUCK) {
                        escapeActive = true;
                        stuckFlag = false;
                        Logger::log("Escape rule triggered!");
                    }
                    if (r.condType == COND_ESCAPE) {
                        escapeActive = false;
                        Logger::log("Escape completed");
                    }
                    startPhysicsAction(modMotorL, modMotorR, modDuration);
                    logFrame();
                    return;
                }
            }
            if (!actionInProgress) stopMotors();
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
    static int getFrameLogCount() { return frameLogCount; }
    static int getCurrentSpeedL() { return currentSpeedL; }
    static int getCurrentSpeedR() { return currentSpeedR; }
};

// ================================================================
// MotorController 静态变量定义
// ================================================================
int MotorController::currentSpeedL = 0;
int MotorController::currentSpeedR = 0;
volatile int32_t MotorController::distanceTicks = 0;
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
int32_t MotorController::actionLastTicks = 0;
int MotorController::actionStallFrames = 0;
volatile int32_t MotorController::leftTicks = 0;
volatile int32_t MotorController::rightTicks = 0;
bool MotorController::stuckFlag = false;
uint32_t MotorController::stuckStartTime = 0;
int32_t MotorController::lastDiff = 0;
bool MotorController::escapeActive = false;
bool MotorController::chaosActive = false;
uint32_t MotorController::chaosStartTime = 0;
int32_t MotorController::testChaosTriggerCount = 0;
uint32_t MotorController::testChaosTotalDuration = 0;
uint32_t MotorController::testChaosMaxDuration = 0;
uint32_t MotorController::testChaosFirstTriggerTime = 0;
uint32_t MotorController::testChaosLastTriggerTime = 0;
int32_t MotorController::baselineDistanceTicks = 0;
int32_t MotorController::chaosDistanceTicks = 0;
int MotorController::baselineFrameCount = 0;
int MotorController::chaosFrameCount = 0;
float MotorController::baselineSpeedLSum = 0;
float MotorController::baselineSpeedRSum = 0;
float MotorController::chaosSpeedLSum = 0;
float MotorController::chaosSpeedRSum = 0;
bool MotorController::chaosHappened = false;
int32_t MotorController::chaosStartDistance = 0;

// ================================================================
// Gene::evaluateCondition 实现
// ================================================================
bool Gene::evaluateCondition(int ruleIdx, int leftSensor, int rightSensor, 
                             int32_t distance, uint32_t elapsed) const {
    const BehaviorRule& r = rules[ruleIdx];
    int value = 0;
    switch (r.condType) {
        case COND_SENSOR_LEFT:  value = leftSensor; break;
        case COND_SENSOR_RIGHT: value = rightSensor; break;
        case COND_SENSOR_BOTH:  return (leftSensor > OBSTACLE_ADC_THRESH) && (rightSensor > OBSTACLE_ADC_THRESH);
        case COND_SENSOR_ANY:   return (leftSensor > OBSTACLE_ADC_THRESH) || (rightSensor > OBSTACLE_ADC_THRESH);
        case COND_DISTANCE:     value = (int)distance; break;
        case COND_TIME:         value = (int)elapsed; break;
        case COND_IDLE:         return (leftSensor < CLEAR_ADC_THRESH) && (rightSensor < CLEAR_ADC_THRESH);
        case COND_ALWAYS:       return true;
        case COND_STUCK:        return MotorController::isStuck() && !MotorController::isEscapeActive();
        case COND_ESCAPE:       return MotorController::isEscapeActive();
        default: return false;
    }
    return r.compare(value, r.condOp, r.condValue);
}

// ================================================================
// EvolutionEngine 类
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

    void createSeedIndividual(Gene& gene) {
        gene.ruleCount = 6;
        gene.rules[0] = {COND_SENSOR_LEFT, OBSTACLE_ADC_THRESH, OP_GREATER, -150, 150, 300, 0, 0};
        gene.rules[1] = {COND_SENSOR_RIGHT, OBSTACLE_ADC_THRESH, OP_GREATER, 150, -150, 300, 0, 0};
        gene.rules[2] = {COND_IDLE, 0, OP_GREATER, 150, 150, 100, 0, 0};
        gene.rules[3] = {COND_STUCK, 0, OP_GREATER, -180, 100, 500, 4, 0};
        gene.rules[4] = {COND_ESCAPE, 0, OP_GREATER, 0, 0, 50, 0, 0};
        gene.rules[5] = {COND_ALWAYS, 0, OP_GREATER, 120, 120, 100, 0, 0};
        gene.survival_time = 0; gene.distance_ticks = 0;
        gene.noveltyScore = 0; gene.behavior.init();
        Logger::log("Seed individual created (6 rules)");
    }

    void initImpl() {
        currentGeneration = GeneStorage::getCurrentGeneration();
        if (currentGeneration == 0) currentGeneration = 1;
        currentIndividual = 0;
        testActive = false; pendingTransition = false; controlMode = false;
        mutationRate = 0.15f; bestNoveltyEver = 0;
        Logger::log("Initializing evolution engine...");
        archive.init();
        Gene loadedPop[POPULATION_SIZE];
        if (GeneStorage::loadPopulation(currentGeneration, loadedPop, POPULATION_SIZE)) {
            for (int i = 0; i < POPULATION_SIZE; i++) population[i] = loadedPop[i];
            Logger::logf("Restored generation %lu population", currentGeneration);
        } else {
            for (int i = 0; i < POPULATION_SIZE; i++) population[i].init();
            createSeedIndividual(population[0]);
            createSeedIndividual(population[1]);
            population[1].rules[3].motorL = 100;
            population[1].rules[3].motorR = -180;
            Logger::logf("Population initialized: %d individuals", POPULATION_SIZE);
            GeneStorage::savePopulation(population, POPULATION_SIZE);
        }
    }

    void startCurrentTestImpl() {
        testActive = true; testStartTime = millis();
        MotorController::resetDistanceTicks();
        MotorController::enableMotor();
        MotorController::startFrameLog();
        MotorController::resetStuck();
        MotorController::setEscapeActive(false);
        Logger::logf("Testing individual %d/%d (gen %lu) [rules=%d]",
                     currentIndividual+1, POPULATION_SIZE, currentGeneration,
                     population[currentIndividual].ruleCount);
        MotorController::resetChaosStatistics();
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
        Logger::logf("Individual %d done: novelty=%.4f rules=%d %s",
                     currentIndividual+1, g.noveltyScore, g.ruleCount, added ? "[new behavior]" : "");
        GeneStorage::saveIndividualRecord(currentGeneration, currentIndividual, g);
        GeneStorage::saveFrameLog(currentGeneration, currentIndividual,
                                  MotorController::getFrameLog(),
                                  MotorController::getFrameLogCount());
        if (g.noveltyScore > bestNoveltyEver) {
            bestNoveltyEver = g.noveltyScore;
            Logger::logf("New behavior record! novelty=%.4f", g.noveltyScore);
        }
        ChaoticTestRecord chaosRecord;
        chaosRecord.timestamp = millis();
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
        chaosRecord.testTerminatedBy = 0;
        chaosRecord.reserved[0] = 0;
        chaosRecord.reserved[1] = 0;
        GeneStorage::saveChaosRecord(chaosRecord);
        RobustStorage::addChaosRecord(chaosRecord);
        Logger::logf("[Self-control] Chaos stats: triggers=%d total=%lums max=%lums",
                     chaosRecord.chaosTriggerCount,
                     (unsigned long)chaosRecord.chaosTotalDuration,
                     (unsigned long)chaosRecord.chaosMaxDuration);
    }

    void nextIndividualImpl() {
        endCurrentTestImpl();
        currentIndividual++;
        if (currentIndividual >= POPULATION_SIZE) {
            evolveImpl();
            currentIndividual = 0;
            GeneStorage::incrementGeneration();
            currentGeneration = GeneStorage::getCurrentGeneration();
            Logger::logf("Entering generation %lu (archive: %d behaviors)", currentGeneration, archive.getArchiveSize());
        }
        startCurrentTestImpl();
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
        if (avg < bestNoveltyEver * 0.5f) mutationRate = min(mutationRate + 0.03f, 0.5f);
        else if (avg > bestNoveltyEver * 0.8f) mutationRate = max(mutationRate - 0.01f, 0.05f);
        Logger::logf("Evolution done, mutation rate=%.2f", mutationRate);
        GeneStorage::savePopulation(population, POPULATION_SIZE);
        if (GeneStorage::isExperimentComplete()) {
            Logger::log("20-generation experiment completed! All data saved.");
        }
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
    static void setControlMode(bool enable) { 
        instance.controlMode = enable; 
        Logger::logf("Control mode: %s", enable ? "ON" : "OFF");
    }
};

EvolutionEngine EvolutionEngine::instance;

// ================================================================
// CarWebServer 类 (完整Web界面)
// ================================================================
class CarWebServer {
private:
    static WebServer server;
    
    static String buildHTMLPage() {
        return R"rawliteral(<!DOCTYPE html><html><head>
<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
<title>进化小车 v9.7</title>
<style>
*{box-sizing:border-box;}body{font-family:sans-serif;background:#0d1117;color:#c9d1d9;padding:20px;margin:0;}
h1{color:#58a6ff;font-size:20px;margin:0 0 10px 0;}
h2{color:#f0f6fc;font-size:14px;margin:0 0 10px 0;border-bottom:1px solid #21262d;padding-bottom:8px;}
.card{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:15px;margin:10px 0;}
button{background:#21262d;color:#c9d1d9;border:1px solid #30363d;padding:8px 16px;border-radius:6px;cursor:pointer;margin:4px;font-size:12px;}
button:hover{background:#30363d;border-color:#58a6ff;}
button.primary{background:#238636;border-color:#2ea043;color:#fff;}
button.danger{background:#da3633;border-color:#f85149;color:#fff;}
button.download{background:#1f6feb;border-color:#58a6ff;color:#fff;}
button.chaos{background:#8b5cf6;border-color:#a78bfa;color:#fff;}
button.gene{background:#d97706;border-color:#f59e0b;color:#fff;}
button.frame{background:#059669;border-color:#34d399;color:#fff;}
button.individual{background:#dc2626;border-color:#f87171;color:#fff;}
.status{font-size:12px;line-height:1.8;font-family:monospace;}
.val{color:#58a6ff;}.high{color:#3fb950;}.warn{color:#d29922;}
.notice{background:#1f2937;border-left:4px solid #3fb950;padding:8px 12px;margin:8px 0;font-size:12px;}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;}
.download-grid{display:grid;grid-template-columns:1fr 1fr 1fr;gap:6px;}
.download-grid button{width:100%;font-size:11px;padding:6px;}
@media(max-width:600px){.grid{grid-template-columns:1fr;}.download-grid{grid-template-columns:1fr 1fr;}}
</style>
</head><body>
<h1>🚗 进化小车 <span style="font-size:14px;color:#8b949e;">v9.7-MotorBalanceFix</span></h1>
<div class="notice">✅ 电机平衡补偿 | 串口输入 help 查看命令 | 输入 popall 1 查看一代数据</div>
<div class="grid">
<div class='card'><h2>📊 状态</h2><div id='status' class='status'>加载中...</div></div>
<div class='card'><h2>📚 档案库</h2><div id='archive' class='status'>加载中...</div></div>
</div>
<div class='card'><h2>🧪 实验</h2><div id='experiment' class='status'>加载中...</div></div>
<div class='card'><h2>🎮 控制</h2>
<button class='primary' onclick="api('/evolution?action=start')">▶ 启动</button>
<button class='danger' onclick="api('/evolution?action=stop')">⏹ 停止</button>
<button onclick="api('/evolution?action=next')">⏭ 下一个</button>
<button onclick="location.reload()">🔄 刷新</button>
</div>
<div class='card'><h2>⚙️ 电机</h2>
<button class='primary' onclick="motorCtrl('enable')">✅ 启用</button>
<button class='danger' onclick="motorCtrl('disable')">❌ 禁用</button>
<button onclick="motorCtrl('forward')">⬆ 前进</button>
<button onclick="motorCtrl('backward')">⬇ 后退</button>
<button onclick="motorCtrl('left')">⬅ 左转</button>
<button onclick="motorCtrl('right')">➡ 右转</button>
<button onclick="motorCtrl('stop')">⏹ 停止</button>
<div id='motorStatus' class='status' style='margin-top:8px;'>电机: 加载中...</div>
</div>
<div class='card'><h2>📥 数据导出</h2>
<div class='download-grid'>
<button class='download' onclick="downloadFile('/download/history')">📋 历史</button>
<button class='download' onclick="downloadFile('/download/evolution')">📈 进化</button>
<button class='chaos' onclick="downloadFile('/download/chaos')">🌪️ 混沌</button>
<button class='download' onclick="downloadFile('/download/population')">🧬 种群</button>
<button class='gene' onclick="downloadFile('/download/genes')">🧬 基因</button>
<button class='frame' onclick="downloadFile('/download/frames')">📹 帧日志</button>
</div>
<div style="margin-top:10px;display:flex;gap:6px;flex-wrap:wrap;align-items:center;">
<input id='genInput' type='number' placeholder='代数' style='background:#0d1117;border:1px solid #30363d;color:#c9d1d9;padding:6px;border-radius:4px;width:80px;'>
<input id='idInput' type='number' placeholder='个体' style='background:#0d1117;border:1px solid #30363d;color:#c9d1d9;padding:6px;border-radius:4px;width:80px;'>
<button class='individual' onclick="downloadIndividual()">📦 下载个体</button>
<button class='frame' onclick="downloadSingleFrame()">📹 单帧日志</button>
</div>
<button onclick="listFiles()" style="margin-top:8px;">📂 列出文件</button>
<div id='fileList' style='font-size:11px;font-family:monospace;max-height:150px;overflow-y:auto;background:#0d1117;padding:8px;border-radius:4px;margin-top:8px;'></div>
</div>
<script>
function api(url){fetch(url).then(r=>r.json()).catch(e=>console.error(e));}
function motorCtrl(a){fetch('/motor?action='+a).then(r=>r.json()).then(d=>{document.getElementById('motorStatus').innerHTML='电机: '+d.status;});}
function downloadFile(url){
    fetch(url).then(r=>{if(!r.ok)throw new Error('下载失败: '+r.status);return r.blob();})
    .then(blob=>{const a=document.createElement('a');a.href=URL.createObjectURL(blob);a.download=url.split('/').pop()||'download';document.body.appendChild(a);a.click();document.body.removeChild(a);})
    .catch(e=>alert('下载失败: '+e.message));
}
function downloadIndividual(){
    var gen=document.getElementById('genInput').value;
    var id=document.getElementById('idInput').value;
    if(!gen||!id){alert('请输入代数和个体编号');return;}
    downloadFile('/download/individual?gen='+gen+'&id='+id);
}
function downloadSingleFrame(){
    var gen=document.getElementById('genInput').value;
    var id=document.getElementById('idInput').value;
    if(!gen||!id){alert('请输入代数和个体编号');return;}
    downloadFile('/download/frames?gen='+gen+'&id='+id);
}
function listFiles(){
    fetch('/list/files').then(r=>r.json()).then(d=>{
        let h='';if(d.files){d.files.forEach(f=>{h+='<div>'+f+'</div>';});}
        document.getElementById('fileList').innerHTML=h||'没有文件';
    });
}
setInterval(function(){
fetch('/status').then(r=>r.json()).then(d=>{
document.getElementById('status').innerHTML=
'代数: <span class=val>'+d.generation+'</span><br>'+
'个体: <span class=val>'+(d.individual+1)+'/'+d.population+'</span><br>'+
'新奇度: <span class=high>'+d.novelty.toFixed(4)+'</span><br>'+
'规则数: <span class=val>'+d.ruleCount+'</span><br>'+
'卡死: '+(d.stuckFlag?'<span class=warn>⚠ 卡死!</span>':'<span class=high>✅ 正常</span>')+
'<br>混沌: '+(d.chaosActive?'<span class=warn>🔥 激活</span>':'<span class=val>⏸ 空闲</span>')+
'<br>编码器差: <span class=val>'+d.encoderDiff+'</span>'+
'<br>左: <span class=val>'+d.leftTicks+'</span> 右: <span class=val>'+d.rightTicks+'</span>'+
'<br>存活: <span class=val>'+d.survival+'ms</span><br>'+
'距离: <span class=val>'+d.distance+'</span><br>'+
'电机: '+(d.motorEnabled?'<span class=high>✅ 已启用</span>':'<span class=warn>❌ 已禁用</span>');
});
fetch('/archive').then(r=>r.json()).then(d=>{
document.getElementById('archive').innerHTML='档案大小: <span class=high>'+d.size+'</span><br>最佳新奇度: <span class=high>'+d.bestNovelty.toFixed(4)+'</span>';
});
fetch('/experiment/status').then(r=>r.json()).then(d=>{
document.getElementById('experiment').innerHTML=
'实验: '+(d.experimentActive?'<span class=high>▶ 运行中</span>':'<span class=warn>⏹ 已停止</span>')+
'<br>当前代数: <span class=val>'+d.currentGeneration+'/'+d.maxGenerations+'</span>'+
'<br>进度: <span class=high>'+(d.progress*100).toFixed(1)+'%</span>';
});
},500);
</script></body></html>)rawliteral";
    }

public:
    static void init() {
        server.on("/", [](){ server.send(200, "text/html", buildHTMLPage()); });
        server.on("/status", handleStatus);
        server.on("/archive", handleArchive);
        server.on("/evolution", handleEvolution);
        server.on("/motor", handleMotor);
        server.on("/reset", handleReset);
        server.on("/download/history", handleDownloadHistory);
        server.on("/download/evolution", handleDownloadEvolution);
        server.on("/download/chaos", handleDownloadChaos);
        server.on("/download/population", handleDownloadPopulation);
        server.on("/download/genes", handleDownloadGenes);
        server.on("/download/frames", handleDownloadFrames);
        server.on("/download/individual", handleDownloadIndividual);
        server.on("/list/files", handleListFiles);
        server.on("/experiment/status", handleExperimentStatus);
        server.on("/version", handleVersion);
        server.on("/admin/clean", handleAdminClean);
        server.onNotFound([](){ server.send(404, "text/plain", "Not Found"); });
        server.begin();
        Logger::log("Web server ready");
    }

    static void handleClient() { server.handleClient(); }

    static void handleStatus() {
        Gene& g = EvolutionEngine::getCurrentGene();
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
        json += "\"sensorLeft\":" + String(SensorCalibration::getLeftRaw()) + ",";
        json += "\"sensorRight\":" + String(SensorCalibration::getRightRaw()) + ",";
        json += "\"leftTicks\":" + String(MotorController::getLeftTicks()) + ",";
        json += "\"rightTicks\":" + String(MotorController::getRightTicks()) + ",";
        json += "\"encoderDiff\":" + String(MotorController::getEncoderDiff()) + ",";
        json += "\"stuckFlag\":" + String(MotorController::isStuck() ? "true" : "false") + ",";
        json += "\"escapeActive\":" + String(MotorController::isEscapeActive() ? "true" : "false") + ",";
        json += "\"chaosActive\":" + String(MotorController::isChaosActive() ? "true" : "false") + ",";
        json += "\"chaosDuration\":" + String(MotorController::getChaosDuration());
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
        if (!server.hasArg("action")) { server.send(400, "{}"); return; }
        String action = server.arg("action");
        if (action == "start") {
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
            EvolutionEngine::nextIndividual();
            server.send(200, "application/json", "{\"status\":\"next\"}");
        } else {
            server.send(400, "{}");
        }
    }

    // ★ v9.7 ★ Web 电机控制 (左转/右转使用 -130/120 补偿)
    static void handleMotor() {
        if (!server.hasArg("action")) { server.send(400, "{}"); return; }
        String action = server.arg("action");
        String status = "ok";
        if (action == "enable") { MotorController::enableMotor(); status = "已启用"; }
        else if (action == "disable") { MotorController::disableMotor(); status = "已禁用"; }
        else if (action == "stop") { MotorController::forceStopAction(); MotorController::stopMotors(); status = "已停止"; }
        else if (action == "forward") { MotorController::enableMotor(); MotorController::startPhysicsAction(150, 150, 100); status = "前进"; }
        else if (action == "backward") { MotorController::enableMotor(); MotorController::startPhysicsAction(-150, -150, 100); status = "后退"; }
        else if (action == "left") {
            MotorController::enableMotor();
            // ★ v9.7 ★ 左转: 左电机反转(需更高PWM补偿-130), 右电机正转(120)
            MotorController::startPhysicsAction(-130, 120, 100);
            status = "左转";
        }
        else if (action == "right") {
            MotorController::enableMotor();
            // ★ v9.7 ★ 右转: 右电机反转(需更高PWM补偿-130), 左电机正转(120)
            MotorController::startPhysicsAction(120, -130, 100);
            status = "右转";
        }
        else { server.send(400, "{}"); return; }
        server.send(200, "application/json", "{\"status\":\"" + status + "\"}");
    }

    static void handleReset() { server.send(200, "{}"); delay(500); ESP.restart(); }
    static void handleAdminClean() { FirmwareVersionManager::cleanAllData(); server.send(200, "{}"); }
    
    static void handleExperimentStatus() {
        String json = "{";
        json += "\"experimentActive\":" + String(GeneStorage::isExperimentActive() ? "true" : "false") + ",";
        json += "\"currentGeneration\":" + String(GeneStorage::getCurrentGeneration()) + ",";
        json += "\"maxGenerations\":" + String(GeneStorage::getMaxGenerations()) + ",";
        json += "\"progress\":" + String(GeneStorage::getProgress(), 3) + ",";
        json += "\"complete\":" + String(GeneStorage::isExperimentComplete() ? "true" : "false");
        json += "}";
        server.send(200, "application/json", json);
    }

    static void handleVersion() {
        String json = "{\"firmwareVersion\":\"" + String(FIRMWARE_VERSION) + "\"}";
        server.send(200, "application/json", json);
    }

    static void handleDownloadHistory() {
        String data = RobustStorage::getCSVData();
        server.sendHeader("Content-Type", "text/csv");
        server.sendHeader("Content-Disposition", "attachment; filename=history_data.csv");
        server.send(200, "text/csv", data);
    }

    static void handleDownloadEvolution() {
        String data = "=== 进化历史 ===\n" + RobustStorage::getCSVData();
        server.sendHeader("Content-Type", "text/csv");
        server.sendHeader("Content-Disposition", "attachment; filename=evolution_data.csv");
        server.send(200, "text/csv", data);
    }

    static void handleDownloadChaos() {
        if (!RobustStorage::isReady()) {
            server.send(500, "text/plain", "SPIFFS 不可用");
            return;
        }
        String allChaos = "";
        File root = SPIFFS.open("/");
        if (!root) { server.send(500, "text/plain", "无法打开根目录"); return; }
        while (File f = root.openNextFile()) {
            String name = String(f.name());
            if (name.startsWith("/chaos_") || name == "/chaos_history.csv") {
                allChaos += "=== " + name + " ===\n";
                File file = SPIFFS.open(name, FILE_READ);
                if (file) { allChaos += file.readString(); file.close(); }
                allChaos += "\n";
            }
            f.close();
        }
        root.close();
        if (allChaos.length() == 0) allChaos = "没有混沌数据文件\n";
        server.sendHeader("Content-Type", "text/plain");
        server.sendHeader("Content-Disposition", "attachment; filename=chaos_data.txt");
        server.send(200, "text/plain", allChaos);
    }

    static void handleDownloadPopulation() {
        if (!RobustStorage::isReady()) {
            server.send(500, "text/plain", "SPIFFS 不可用");
            return;
        }
        String allData = "";
        File root = SPIFFS.open("/");
        if (!root) { server.send(500, "text/plain", "无法打开根目录"); return; }
        while (File f = root.openNextFile()) {
            String name = String(f.name());
            if (name.startsWith("/pop_gen_") && name.endsWith(".bin")) {
                allData += "=== " + name + " ===\n";
                allData += "文件大小: " + String(f.size()) + " bytes\n";
                File file = SPIFFS.open(name, FILE_READ);
                if (file) {
                    uint32_t magic, expId, gen;
                    uint16_t version, popSize;
                    file.read((uint8_t*)&magic, 4);
                    file.read((uint8_t*)&version, 2);
                    file.read((uint8_t*)&popSize, 2);
                    file.read((uint8_t*)&gen, 4);
                    file.read((uint8_t*)&expId, 4);
                    allData += "Magic: 0x" + String(magic, HEX) + "\n";
                    allData += "Version: 0x" + String(version, HEX) + "\n";
                    allData += "Population Size: " + String(popSize) + "\n";
                    allData += "Generation: " + String(gen) + "\n";
                    for (int i = 0; i < popSize && i < 16; i++) {
                        uint8_t ruleCount;
                        file.read(&ruleCount, 1);
                        allData += "  Individual " + String(i) + ": " + String(ruleCount) + " rules\n";
                        file.seek(file.position() + ruleCount * sizeof(BehaviorRule) + 12);
                    }
                    file.close();
                }
                allData += "\n";
            }
            f.close();
        }
        root.close();
        if (allData.length() == 0) allData = "没有种群快照文件\n";
        server.sendHeader("Content-Type", "text/plain");
        server.sendHeader("Content-Disposition", "attachment; filename=population_snapshots.txt");
        server.send(200, "text/plain", allData);
    }

    static void handleDownloadGenes() {
        if (!RobustStorage::isReady()) {
            server.send(500, "text/plain", "SPIFFS 不可用");
            return;
        }
        String allGenes = "";
        File root = SPIFFS.open("/");
        if (!root) { server.send(500, "text/plain", "无法打开根目录"); return; }
        int count = 0;
        while (File f = root.openNextFile()) {
            String name = String(f.name());
            if (name.startsWith("/gen_") && name.endsWith(".csv")) {
                allGenes += "=== " + name + " ===\n";
                File file = SPIFFS.open(name, FILE_READ);
                if (file) { allGenes += file.readString(); file.close(); }
                allGenes += "\n";
                count++;
            }
            f.close();
        }
        root.close();
        if (count == 0) allGenes = "没有基因记录文件 (gen_*.csv)\n";
        server.sendHeader("Content-Type", "text/plain");
        server.sendHeader("Content-Disposition", "attachment; filename=genetic_records.txt");
        server.send(200, "text/plain", allGenes);
    }

    static void handleDownloadFrames() {
        if (!RobustStorage::isReady()) {
            server.send(500, "text/plain", "SPIFFS 不可用");
            return;
        }
        if (server.hasArg("gen") && server.hasArg("id")) {
            int gen = server.arg("gen").toInt();
            int id = server.arg("id").toInt();
            String path = "/frm_" + String(gen) + "_i" + String(id) + ".bin";
            if (SPIFFS.exists(path)) {
                File file = SPIFFS.open(path, FILE_READ);
                if (file) {
                    size_t size = file.size();
                    uint8_t* buffer = (uint8_t*)malloc(size);
                    if (buffer) {
                        file.read(buffer, size);
                        file.close();
                        server.sendHeader("Content-Type", "application/octet-stream");
                        server.sendHeader("Content-Disposition", "attachment; filename=" + path.substring(1));
                        server.send(200, "application/octet-stream", String((char*)buffer, size));
                        free(buffer);
                        return;
                    }
                    file.close();
                }
            }
            server.send(404, "text/plain", "帧日志不存在: " + path);
            return;
        }
        String allFrames = "";
        File root = SPIFFS.open("/");
        if (!root) { server.send(500, "text/plain", "无法打开根目录"); return; }
        int count = 0;
        while (File f = root.openNextFile()) {
            String name = String(f.name());
            if (name.startsWith("/frm_") && name.endsWith(".bin")) {
                size_t size = f.size();
                int frames = size / 11;
                allFrames += "=== " + name + " ===\n";
                allFrames += "总帧数: " + String(frames) + "\n";
                allFrames += "文件大小: " + String(size) + " bytes\n\n";
                count++;
                f.close();
            }
            f.close();
        }
        root.close();
        if (count == 0) allFrames = "没有帧日志文件 (frm_*.bin)\n";
        server.sendHeader("Content-Type", "text/plain");
        server.sendHeader("Content-Disposition", "attachment; filename=frame_logs_summary.txt");
        server.send(200, "text/plain", allFrames);
    }

    static void handleDownloadIndividual() {
        if (!server.hasArg("gen") || !server.hasArg("id")) {
            server.send(400, "text/plain", "需要 gen 和 id 参数");
            return;
        }
        if (!RobustStorage::isReady()) {
            server.send(500, "text/plain", "SPIFFS 不可用");
            return;
        }
        int gen = server.arg("gen").toInt();
        int id = server.arg("id").toInt();
        String output = "=== 个体数据包 ===\n";
        output += "代数: " + String(gen) + "\n";
        output += "个体: " + String(id) + "\n\n";
        String genePath = "/gen_" + String(gen) + "_id_" + String(id) + ".csv";
        if (SPIFFS.exists(genePath)) {
            output += "--- 基因数据 ---\n";
            File f = SPIFFS.open(genePath, FILE_READ);
            if (f) { output += f.readString(); f.close(); }
            output += "\n";
        } else {
            output += "--- 基因数据: 未找到 ---\n\n";
        }
        String framePath = "/frm_" + String(gen) + "_i" + String(id) + ".bin";
        if (SPIFFS.exists(framePath)) {
            File f = SPIFFS.open(framePath, FILE_READ);
            if (f) {
                int frames = f.size() / 11;
                output += "--- 帧日志 ---\n";
                output += "总帧数: " + String(frames) + "\n";
                output += "文件大小: " + String(f.size()) + " bytes\n\n";
                f.close();
            }
        } else {
            output += "--- 帧日志: 未找到 ---\n\n";
        }
        String chaosPath = "/chaos_g" + String(gen) + "_i" + String(id) + ".csv";
        if (SPIFFS.exists(chaosPath)) {
            output += "--- 混沌数据 ---\n";
            File f = SPIFFS.open(chaosPath, FILE_READ);
            if (f) { output += f.readString(); f.close(); }
            output += "\n";
        } else {
            output += "--- 混沌数据: 未找到 ---\n\n";
        }
        server.sendHeader("Content-Type", "text/plain");
        server.sendHeader("Content-Disposition", "attachment; filename=individual_g" + String(gen) + "_i" + String(id) + ".txt");
        server.send(200, "text/plain", output);
    }

    static void handleListFiles() {
        String json = "{\"files\":[";
        if (RobustStorage::isReady()) {
            File root = SPIFFS.open("/");
            bool first = true;
            while (File f = root.openNextFile()) {
                if (!first) json += ",";
                json += "\"" + String(f.name()) + "\"";
                first = false;
                f.close();
            }
            root.close();
        }
        json += "]}";
        server.send(200, "application/json", json);
    }
};

WebServer CarWebServer::server(80);

// ================================================================
// ================================================================
// 串口命令函数
// ================================================================
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
        String name = String(file.name());
        size_t size = file.size();
        totalFiles++;
        totalBytes += size;
        if (name.startsWith("/pop_gen_")) Serial.printf("  🧬 %s (%d bytes)\n", name.c_str(), size);
        else if (name.startsWith("/gen_")) Serial.printf("  🧬 %s (%d bytes)\n", name.c_str(), size);
        else if (name.startsWith("/frm_")) Serial.printf("  📹 %s (%d bytes, %d frames)\n", name.c_str(), size, size / 11);
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
    Serial.println("========================================");
    Serial.println("📊 系统状态:");
    Serial.printf("  版本: %s\n", FIRMWARE_VERSION);
    Serial.printf("  代数: %d\n", EvolutionEngine::getGeneration());
    Serial.printf("  个体: %d/%d\n", EvolutionEngine::getIndividual() + 1, POPULATION_SIZE);
    Serial.printf("  新奇度: %.4f\n", EvolutionEngine::getCurrentGene().noveltyScore);
    Serial.printf("  规则数: %d\n", EvolutionEngine::getCurrentGene().ruleCount);
    Serial.printf("  距离: %d ticks\n", MotorController::getDistanceTicks());
    Serial.printf("  卡死: %s\n", MotorController::isStuck() ? "⚠️ 是" : "✅ 否");
    Serial.printf("  混沌: %s\n", MotorController::isChaosActive() ? "🔥 激活" : "⏸ 空闲");
    Serial.printf("  SPIFFS: %s\n", RobustStorage::isReady() ? "✅ 可用" : "❌ 不可用");
    Serial.println("========================================");
}

void parseFrameLog(int gen, int id) {
    String path = "/frm_" + String(gen) + "_i" + String(id) + ".bin";
    if (!SPIFFS.exists(path)) { Serial.println("❌ 文件不存在: " + path); return; }
    File file = SPIFFS.open(path, FILE_READ);
    if (!file) { Serial.println("❌ 无法打开文件"); return; }
    size_t size = file.size();
    int frameCount = size / 11;
    Serial.println("========================================");
    Serial.printf("📹 帧日志: %s\n", path.c_str());
    Serial.printf("文件大小: %d bytes\n", size);
    Serial.printf("总帧数: %d\n", frameCount);
    Serial.println("========================================");
    Serial.println("Time(ms) | SensorL | SensorR | PWM_L | PWM_R | Dir | Chaos");
    Serial.println("---------|---------|---------|-------|-------|-----|-------");
    int maxShow = min(50, frameCount);
    for (int i = 0; i < maxShow; i++) {
        FrameLogEntry e;
        file.read((uint8_t*)&e, 11);
        Serial.printf("%8d | %7d | %7d | %5d | %5d | %3d%3d | %5d\n",
            e.timestamp_ms, e.sensorLeft, e.sensorRight,
            e.motorLeftPWM, e.motorRightPWM, e.directionL, e.directionR, e.chaosActive);
    }
    if (frameCount > maxShow) Serial.printf("... (省略 %d 帧)\n", frameCount - maxShow);
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
    const char* condNames[] = {"L", "R", "BOTH", "ANY", "DIST", "TIME", "IDLE", "ALWAYS", "STUCK", "ESCAPE"};
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
        file.seek(file.position() + 12);
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

// ================================================================
// ★★★ 核心功能：显示一代所有个体数据 ★★★
// ================================================================
void parseAllIndividuals(int gen) {
    Serial.println("========================================");
    Serial.printf("📊 第 %d 代 所有个体数据汇总\n", gen);
    Serial.println("========================================");
    Serial.println("ID | 规则数 | 新奇度 | 混沌次数 | 左轮速度 | 右轮速度 | 速度比 | 评价");
    Serial.println("---|--------|--------|---------|---------|---------|--------|------");
    
    for (int id = 0; id < POPULATION_SIZE; id++) {
        String genePath = "/gen_" + String(gen) + "_id_" + String(id) + ".csv";
        if (!SPIFFS.exists(genePath)) {
            Serial.printf("%2d | 无数据\n", id);
            continue;
        }
        
        String chaosPath = "/chaos_g" + String(gen) + "_i" + String(id) + ".csv";
        int chaosCount = 0;
        float avgL = 0, avgR = 0;
        int ruleCount = 0;
        float novelty = 0;
        
        File geneFile = SPIFFS.open(genePath, FILE_READ);
        if (geneFile) {
            String content = geneFile.readString();
            geneFile.close();
            int lines = 0;
            for (int i = 0; i < content.length(); i++) if (content[i] == '\n') lines++;
            ruleCount = lines - 1;
            if (ruleCount < 0) ruleCount = 0;
            int lastComma = content.lastIndexOf(',');
            int prevComma = content.lastIndexOf(',', lastComma - 1);
            if (prevComma > 0 && lastComma > prevComma) {
                novelty = content.substring(prevComma + 1, lastComma).toFloat();
            }
        }
        
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
        
        float ratio = (avgR > 0) ? avgL / avgR : 0;
        String eval;
        if (chaosCount > 0) eval = "⚠️ 混沌救回";
        else if (avgL > 0 && avgR > 0 && ratio > 0.5 && ratio < 2.0) eval = "✅ 良好";
        else if (avgL == 0 && avgR == 0) eval = "💀 死亡";
        else eval = "⚠️ 失衡";
        
        Serial.printf("%2d | %6d | %6.3f | %7d | %7.1f | %7.1f | %6.2f | %s\n",
            id, ruleCount, novelty, chaosCount, avgL, avgR, ratio, eval.c_str());
    }
    
    Serial.println("========================================");
    
    int totalChaos = 0, goodCount = 0, deathCount = 0;
    float totalNovelty = 0;
    for (int id = 0; id < POPULATION_SIZE; id++) {
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
                    int start = 0, fieldIdx = 0, chaosCount = 0;
                    float l = 0, r = 0;
                    for (int i = 0; i < lastLine.length(); i++) {
                        if (lastLine[i] == ',') {
                            fieldIdx++;
                            if (fieldIdx == 3) chaosCount = lastLine.substring(start, i).toInt();
                            else if (fieldIdx == 12) l = lastLine.substring(start, i).toFloat();
                            else if (fieldIdx == 13) { r = lastLine.substring(start, i).toFloat(); break; }
                            start = i + 1;
                        }
                    }
                    totalChaos += chaosCount;
                    if (l > 0 && r > 0 && (l/r) > 0.5 && (l/r) < 2.0) goodCount++;
                    if (l == 0 && r == 0) deathCount++;
                }
            }
        }
        String genePath = "/gen_" + String(gen) + "_id_" + String(id) + ".csv";
        if (SPIFFS.exists(genePath)) {
            File geneFile = SPIFFS.open(genePath, FILE_READ);
            if (geneFile) {
                String content = geneFile.readString();
                geneFile.close();
                int lastComma = content.lastIndexOf(',');
                int prevComma = content.lastIndexOf(',', lastComma - 1);
                if (prevComma > 0 && lastComma > prevComma) {
                    totalNovelty += content.substring(prevComma + 1, lastComma).toFloat();
                }
            }
        }
    }
    
    Serial.printf("📊 统计: 总混沌触发=%d次, 良好个体=%d个, 死亡个体=%d个, 平均新奇度=%.3f\n",
        totalChaos, goodCount, deathCount, totalNovelty / POPULATION_SIZE);
    Serial.println("========================================");
}

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
// setup()
// ================================================================
void setup() {
    Serial.begin(115200);
    delay(1000);

    Logger::log("========================================");
    Logger::logf("  Evolution Car %s", FIRMWARE_VERSION);
    Logger::log("  Motor Balance Fix - v9.7");
    Logger::log("  LEFT_FWD=1.00 RIGHT_FWD=1.25 LEFT_REV=1.20");
    Logger::log("  Commands: ls, status, frame, pop, chaos, popall, help");
    Logger::log("========================================");

    RobustStorage::init();
    if (FirmwareVersionManager::isNewVersion()) {
        FirmwareVersionManager::cleanAllData();
    }

    GeneStorage::init();
    if (GeneStorage::shouldStartNewExperiment()) {
        GeneStorage::startNewExperiment();
    }

    pinMode(PIN_NOISE_SOURCE, INPUT);
    randomSeed(SensorCalibration::readNoise());

    SensorCalibration::calibrate();
    MotorController::init();
    EvolutionEngine::init();

    Logger::log("========================================");
    Logger::logf("  Ready (%s)", FIRMWARE_VERSION);
    Logger::log("  WiFi: CarLogger / 12345678");
    Logger::log("  http://192.168.4.1");
    Logger::log("  Serial: ls, status, frame, pop, chaos, popall, help");
    Logger::log("========================================");

    if (SPIFFS.begin(true)) {
        Serial.println("SPIFFS formatted!");
        SPIFFS.format();
    }

    
}

// ================================================================
// loop()
// ================================================================
void loop() {
    if (!wifiInitialized) initWiFi();

    MotorController::updatePhysicsAction();

//    if (MotorController::isChaosActive()) {
//        MotorController::updateChaos();
//    }

    if (EvolutionEngine::isTestActive()) {
        Gene& gene = EvolutionEngine::getCurrentGene();
        MotorController::update(gene);
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

    // ============================================================
    // 串口命令处理
    // ============================================================
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
            Serial.println("可用命令:");
            Serial.println("  ls / list          - 列出 SPIFFS 所有文件");
            Serial.println("  status             - 显示系统状态");
            Serial.println("  frame <代> <个体>  - 解析帧日志，例如: frame 2 0");
            Serial.println("  pop <代>           - 解析种群快照，例如: pop 2");
            Serial.println("  chaos <代> <个体>  - 解析混沌记录，例如: chaos 2 0");
            Serial.println("  popall <代>        - ★ 显示一代所有个体数据，例如: popall 1");
            Serial.println("  help               - 显示帮助");
            Serial.println("========================================");
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
        }
    }

    RobustStorage::tick();
    delay(LOOP_DELAY_MS);
}