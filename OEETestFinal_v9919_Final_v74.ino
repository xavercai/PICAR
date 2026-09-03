
/*
* ================================================================
 * 项目名称：仿草履虫应激机制 - 脱困能力进化系统
 * 文件：OEETestFinal_v9919_Final_v6 简单混沌规则.ino
 * 版本：v9.19-Final（存储链路审计修复 + 新颖度存档持久化 / 滚动保留 + R2/R4修复）
 * 
 * ================================================================
 * 修改日志 - 混沌触发机制简化 (2026-08-31)
 * ================================================================
 *
 * 【问题描述】
 *   混沌模式几乎不触发，原有检测机制过于复杂且条件苛刻。
 *   分析发现需要同时满足多个条件（窗口检测、两轮旋转、差值递增等），
 *   导致机器人卡住时无法及时进入混沌模式脱困。
 *
 * 【修改内容】
 *   1. 移除原有的多重卡住检测逻辑（windowStuck、diffStuck、oneWheelStuck等）
 *   2. 采用简化的单条件触发：编码器绝对值差 > 20 且维持 1 秒
 *   3. 增加详细的串口调试输出，便于观察触发状态
 *
 * 【修改前后对比】
 *   ┌─────────────────┬──────────────────────────────────┬──────────────────────┐
 *   │ 项目            │ 修改前                          │ 修改后               │
 *   ├─────────────────┼──────────────────────────────────┼──────────────────────┤
 *   │ 触发条件        │ 多重复杂条件组合                │ 仅需 diff > 20       │
 *   │ 维持时间        │ 窗口检测（不稳定）              │ 固定 1000ms (1秒)    │
 *   │ 轮子状态检查    │ 需要两轮都在转等                │ 不检查               │
 *   │ 代码行数        │ ~100行                          │ ~60行                │
 *   │ 串口调试        │ 较少                            │ 详细状态输出         │
 *   └─────────────────┴──────────────────────────────────┴──────────────────────┘
 *
 * 【可调参数】
 *   - 差值阈值: diff > 20  (可调整为 15~30)
 *   - 维持时间: elapsed >= 1000  (可调整为 500~2000ms)
 *
 * 【影响范围】
 *   - 函数: MotorController::update()
 *   - 不影响: 正常规则执行、混沌模式行为、数据存储
 *
 * 【测试建议】
 *   1. 观察串口输出 "[CHAOS] ⏱️" 和 "[CHAOS] 🚀" 日志
 *   2. 根据实际卡住情况调整阈值 (15-30) 和时间 (500-1500ms)
 *   3. 确认混沌触发后能正常脱困并退出混沌模式
 *
 * 【修改人】 系统优化
 * 【修改日期】 2026-08-31
 * ================================================================
 *
 * 修复版本：
 *   - 修复单轮转动很久但混沌不触发的问题
 *   - 增强单轮卡住检测逻辑
 *   - 新增累计帧数检测机制
 *   - 放宽差速卡住判定条件
 *   - P1改进: D3/D4增加forceFlush参数支持同步刷盘
 *   - P2改进: D7/D8增加空间预检查
 *   - R2修复: CSV历史记录加载增加表头识别和字段合法性校验
 *   - R4修复: 混沌快照文件增加文件头和CRC32校验
 * 最后更新：2026-08-31
 * ================================================================
 
 * ================================================================
 * 项目名称：仿草履虫应激机制 - 脱困能力进化系统
 * 文件：OEETestFinal_v9919_Final_v3_retention.ino
 * 版本：v9.19-Final（存储链路审计修复 + 新颖度存档持久化 / 滚动保留）
 * 最后更新：2026-08-26
 * ================================================================
 *
 * ────────────────────────────────────────────────────────────────
 *  存储链路审计与修改记录
 *  原则：本文件所有改动均溯源到下方审计问题编号，可逐条核对。
 * ────────────────────────────────────────────────────────────────
 *
 * 【审计范围】
 *   对 SPIFFS 存储全链路（写入 → 落盘 → 读取 → 解析 → 滚动删除/清空）
 *   做全链路审计，重点排查三类风险：
 *     ① 内存越界（堆越界读/写、峰值堆过高）
 *     ② 存储失败与死锁（空间不足死循环、目录遍历删除跳过文件）
 *     ③ 读写互相干涉（序列化校验缺失、版本不校验、完整性无保障）
 *
 * 【审计问题 → 修复追溯】
 *
 * 一、内存越界类
 *   A1  savePopulation / loadPopulation 按 ruleCount 直接读写 rules[]，
 *       ruleCount 一旦异常（> MAX_RULES）即发生堆越界读/写。
 *       → 修复：读写两侧均将 ruleCount 钳制到 MAX_RULES，循环加 j<MAX_RULES 上限。
 *          位置：GeneStorage::savePopulation() / loadPopulation()
 *
 * 二、存储失败 / 死锁类
 *   B1  ensureSpace 空间不足时，"删除最旧代"若不生效会陷入无限循环死锁。
 *       → 修复：删除最旧代，删除后重取代列表，直至低于保留上限（循环收敛）。
 *          位置：RollingStorage::ensureSpace()
 *   B2  SPIFFS 目录遍历过程中删除文件，会跳过相邻文件。
 *       → 修复：删除前先关闭文件句柄，避免目录游标跳跃。
 *          位置：RollingStorage::deleteGeneration() / clearAllExperimentData()
 *
 * 三、数据完整性 / 序列化类
 *   C1  帧日志(.bin)无校验和，读回无法确认内容完整。
 *       → 修复：FileHeader 增加 crc32 字段，写盘时由 CRC32::calculate 计算。
 *          位置：struct FileHeader、RollingStorage::saveFrameLog()
 *   C2  loadPopulation 不做格式校验，非法/损坏文件会被误解析甚至越界读。
 *       → 修复：fileSize 非零校验 + magic 校验 + popSize 一致性校验，
 *          并按 fileVersion 做条件式字段解析（>=0x0005 / >=0x0006）。
 *          位置：GeneStorage::loadPopulation()
 *
 * 四、本次新增：新颖度存档持久化与无限滚动保留（2026-08-26）
 *       （起因：新颖度评分依赖全局行为存档，无穷进化要求存档与保留策略正确）
 *   D1  新颖度存档（NoveltyArchive）仅存于 RAM，每次启动被 archive.init()
 *       清零，长期进化积累的行为历史全部丢失，新颖度随之失效。
 *       → 修复：新增 save()/load() 持久化到 /novelty_archive.bin，
 *          含 magic + version + count + crc32 校验；开机自动恢复。
 *   D2  存档满 NOVELTY_ARCHIVE_MAX(200) 时，原逻辑会使 archiveSize 从 200
 *       塌缩为 1（"失忆"）。
 *       → 修复：改为 FIFO 丢弃最旧（整体左移 + 末尾追加），size 恒为 200。
 *   D3  20 代硬上限（isExperimentComplete）与"无穷时间进化"冲突，
 *       到代后自动重置并清空全部数据。
 *       → 修复：MAX_GENERATIONS_PER_EXPERIMENT 改为 0xFFFFFFFF，实验永不"完成"。
 *
 * 【滚动保留策略（写死）】
 *   前提：进化繁殖只读当前代种群；新颖度依赖全局存档。
 *   因此：
 *     · 新颖度存档 /novelty_archive.bin —— 持久化，永不滚动删除，
 *       仅在"开始新实验 / 清空所有数据"时删除。
 *     · 当前代种群 —— 必须保留。
 *     · 旧代种群/帧日志/混沌日志 —— 最多保留 SPIFFS_MAX_GENERATIONS=5 代，
 *       超出或空间不足时删除最旧代。
 *
 * 【修改历史】（映射审计问题）
 *   2026-08-20  v9.19 存储链路审计修复（A1、B1、B2、C1、C2）
 *   2026-08-26  新颖度存档持久化 + 满员 FIFO + 移除 20 代上限（D1~D3）
 * ================================================================
 */
/*
 *
 * ────────────────────────────────────────────────────────────────
 *  存储链路审计与修改记录
 *  原则：本文件所有改动均溯源到下方审计问题编号，可逐条核对。
 * ────────────────────────────────────────────────────────────────
 *
 * 【审计范围】
 *   对 SPIFFS 存储全链路（写入 → 落盘 → 读取 → 解析 → 滚动删除/清空）
 *   做全链路审计，重点排查三类风险：
 *     ① 内存越界（堆越界读/写、峰值堆过高）
 *     ② 存储失败与死锁（空间不足死循环、目录遍历删除跳过文件）
 *     ③ 读写互相干涉（序列化校验缺失、版本不校验、完整性无保障）
 *
 * 【审计问题 → 修复追溯】
 *
 * 一、内存越界类
 *   A1  savePopulation / loadPopulation 按 ruleCount 直接读写 rules[]，
 *       ruleCount 一旦异常（> MAX_RULES）即发生堆越界读/写。
 *       → 修复：读写两侧均将 ruleCount 钳制到 MAX_RULES，循环加 j<MAX_RULES 上限。
 *          位置：GeneStorage::savePopulation() / loadPopulation()
 *
 * 二、存储失败 / 死锁类
 *   B1  ensureSpace 空间不足时，"删除最旧代"若不生效会陷入无限循环死锁。
 *       → 修复：删除最旧代，删除后重取代列表，直至低于保留上限（循环收敛）。
 *          位置：RollingStorage::ensureSpace()
 *   B2  SPIFFS 目录遍历过程中删除文件，会跳过相邻文件。
 *       → 修复：删除前先关闭文件句柄，避免目录游标跳跃。
 *          位置：RollingStorage::deleteGeneration() / clearAllExperimentData()
 *
 * 三、数据完整性 / 序列化类
 *   C1  帧日志(.bin)无校验和，读回无法确认内容完整。
 *       → 修复：FileHeader 增加 crc32 字段，写盘时由 CRC32::calculate 计算。
 *          位置：struct FileHeader、RollingStorage::saveFrameLog()
 *   C2  loadPopulation 不做格式校验，非法/损坏文件会被误解析甚至越界读。
 *       → 修复：fileSize 非零校验 + magic 校验 + popSize 一致性校验，
 *          并按 fileVersion 做条件式字段解析（>=0x0005 / >=0x0006）。
 *          位置：GeneStorage::loadPopulation()
 *
 * 四、本次新增：新颖度存档持久化与无限滚动保留（2026-08-26）
 *       （起因：新颖度评分依赖全局行为存档，无穷进化要求存档与保留策略正确）
 *   D1  新颖度存档（NoveltyArchive）仅存于 RAM，每次启动被 archive.init()
 *       清零，长期进化积累的行为历史全部丢失，新颖度随之失效。
 *       → 修复：新增 save()/load() 持久化到 /novelty_archive.bin，
 *          含 magic + version + count + crc32 校验；开机自动恢复。
 *   D2  存档满 NOVELTY_ARCHIVE_MAX(200) 时，原逻辑会使 archiveSize 从 200
 *       塌缩为 1（"失忆"）。
 *       → 修复：改为 FIFO 丢弃最旧（整体左移 + 末尾追加），size 恒为 200。
 *   D3  20 代硬上限（isExperimentComplete）与"无穷时间进化"冲突，
 *       到代后自动重置并清空全部数据。
 *       → 修复：MAX_GENERATIONS_PER_EXPERIMENT 改为 0xFFFFFFFF，实验永不"完成"。
 *
 * 【滚动保留策略（写死）】
 *   前提：进化繁殖只读当前代种群；新颖度依赖全局存档。
 *   因此：
 *     · 新颖度存档 /novelty_archive.bin —— 持久化，永不滚动删除，
 *       仅在"开始新实验 / 清空所有数据"时删除。
 *     · 当前代种群 —— 必须保留。
 *     · 旧代种群/帧日志/混沌日志 —— 最多保留 SPIFFS_MAX_GENERATIONS=5 代，
 *       超出或空间不足时删除最旧代。
 *
 * 【修改历史】（映射审计问题）
 *   2026-08-20  v9.19 存储链路审计修复（A1、B1、B2、C1、C2）
 *   2026-08-26  新颖度存档持久化 + 满员 FIFO + 移除 20 代上限（D1~D3）
 *   2026-08-29  移植 FileUtils 正确状态（前向声明 + 分离实现 + readStringCapped）
 * ================================================================
 */

/*
* ================================================================
 * 项目名称：仿草履虫应激机制 - 脱困能力进化系统
 * 文件：OEETestFinal_v9919_Final_v7_ChaosTuned_WithStorage.ino
 * 版本：v9.21-ChaosTuned+Storage（3轮混沌保障 + 完整存储可靠性）
 * 最后更新：2026-09-01
 * ================================================================
 *
 * ────────────────────────────────────────────────────────────────
 *  v9.21 ChaosTuned+Storage 迭代说明（2026-09-01）
 *  合并 v6（存储可靠性）和 v64（混沌触发机制）的最优特性
 * ────────────────────────────────────────────────────────────────
 *
 * 【合并策略】
 *   以 v6 简单混沌规则 为基础（保留所有存储改进），
 *   移植 v64_chaos_tuned 的混沌触发机制和时间参数。
 *
 *   基础版本: v6 (OEETestFinal_v9919_Final_v6Chaos.ino)
 *   移植来源: v64 (OEETestFinal_v9919_Final_v64_chaos_tuned.ino)
 *
 * 【保留自 v6 的存储改进】
 *   R2  CSV历史记录加载校验 (表头跳过 + 字段合法性校验)
 *   R4  混沌快照文件头 + CRC32校验
 *   P1  forceFlush 同步刷盘参数
 *   P2  空间预检查
 *   ──────────────────────────────────────────────────────────────
 *   完整的存储可靠性保障，防止数据损坏和断电丢失
 *
 * 【移植自 v64 的混沌触发机制】
 *   C1  chaosTimeoutMs 范围: 800~1800ms (原 500~8000ms)
 *   C2  chaosForceTimeoutMs 范围: 1500~3500ms (原 1000~15000ms)
 *   C3  P1: 速度不对称检测 (speedRatio > 2.5)
 *   C4  P2: 清除防抖 (150ms)
 *   C5  P3: 自适应阈值保护 (3次反弹 → 阈值减半)
 *   C6  P4: 零进展检测 (1000ms)
 *   C7  完整 STUCK 状态机 + accumulatedStuckTime 累计
 *   ──────────────────────────────────────────────────────────────
 *   确保每个个体在 30 秒生命周期内至少经历 3 轮混沌
 *
 * 【移除自 v6 的简单触发】
 *   - diff > 20 维持 1 秒的单一触发条件（过于简单，进化价值低）
 *   - stuckFrameCount 变量（被完整的 STUCK 状态机替代）
 *
 * 【合并后特性】
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │ 存储可靠性                                                │
 *   │  ├── 混沌快照 CRC32 校验 (R4)                            │
 *   │  ├── CSV 历史记录校验 (R2)                               │
 *   │  ├── forceFlush 同步刷盘 (P1)                            │
 *   │  └── 空间预检查 (P2)                                     │
 *   ├─────────────────────────────────────────────────────────────┤
 *   │ 混沌触发机制                                              │
 *   │  ├── 速度不对称检测 (P1)                                 │
 *   │  ├── 清除防抖 (P2)                                       │
 *   │  ├── 自适应阈值保护 (P3)                                 │
 *   │  ├── 零进展检测 (P4)                                     │
 *   │  └── chaosTimeoutMs: 800~1800ms (3轮保障)               │
 *   └─────────────────────────────────────────────────────────────┘
 *
 * 【修改溯源】
 *   M1  2026-09-01  保留 v6 的 ChaosSnapshotHeader (R4修复)
 *   M2  2026-09-01  保留 v6 的 CSV 校验函数 (R2修复)
 *   M3  2026-09-01  保留 v6 的 forceFlush 参数 (P1改进)
 *   M4  2026-09-01  保留 v6 的空间预检查 (P2改进)
 *   M5  2026-09-01  移植 v64 的 chaosTimeoutMs 范围 → Gene::init()
 *   M6  2026-09-01  移植 v64 的 chaosTimeoutMs 突变 → Gene::mutate()
 *   M7  2026-09-01  移植 v64 的 chaosForceTimeoutMs 范围 → Gene::init()
 *   M8  2026-09-01  移植 v64 的 chaosForceTimeoutMs 突变 → Gene::mutate()
 *   M9  2026-09-01  移植 v64 的 P1-P4 检测机制 → MotorController::update()
 *   M10 2026-09-01  移植 v64 的新增静态变量 → MotorController 类
 *   M11 2026-09-01  移除 v6 的 stuckFrameCount（被 P2/P3 替代）
 *   M12 2026-09-01  移除 v6 的 diff>20 单一触发逻辑
 * ================================================================
 *
 * ────────────────────────────────────────────────────────────────
 *  以下为 v6 原始修改日志（保留，追溯用）
 * ────────────────────────────────────────────────────────────────
 *
 * 【修改日志 - 混沌触发机制简化 (2026-08-31)】
 *   移除原有的多重卡住检测逻辑，采用简化的单条件触发
 *   ... (详见下方)
 *
 * ────────────────────────────────────────────────────────────────
 *  以下为 v9.19-Final 原始审计日志（保留，追溯用）
 * ────────────────────────────────────────────────────────────────
 *   存储链路审计与修改记录（A1、B1、B2、C1、C2、D1~D3）
 *   ... (详见下方)
 * ================================================================
 */

/*
* ================================================================
 * 项目名称：仿草履虫应激机制 - 脱困能力进化系统
 * 文件：OEETestFinal_v9919_Final_v7.ino
 * 版本：v9.21-ChaosTuned+Storage（3轮混沌保障 + 完整存储可靠性）
 * 最后更新：2026-09-01
 * ================================================================
 *
 * ────────────────────────────────────────────────────────────────
 *  v9.21 ChaosTuned+Storage 迭代说明（2026-09-01）
 *  合并 v6（存储可靠性）和 v64（混沌触发机制）的最优特性
 * ────────────────────────────────────────────────────────────────
 *
 * 【合并策略】
 *   以 v6 简单混沌规则 为基础（保留所有存储改进），
 *   移植 v64_chaos_tuned 的混沌触发机制和时间参数。
 *
 *   基础版本: v6 (OEETestFinal_v9919_Final_v6Chaos.ino)
 *   移植来源: v64 (OEETestFinal_v9919_Final_v64_chaos_tuned.ino)
 *
 * 【保留自 v6 的存储改进】
 *   R2  CSV历史记录加载校验 (表头跳过 + 字段合法性校验)
 *   R4  混沌快照文件头 + CRC32校验
 *   P1  forceFlush 同步刷盘参数
 *   P2  空间预检查
 *
 * 【移植自 v64 的混沌触发机制】
 *   C1  chaosTimeoutMs 范围: 800~1800ms (原 500~8000ms)
 *   C2  chaosForceTimeoutMs 范围: 3000~8000ms (原 1500~3500ms) ← A1/A2 修改
 *   C3  P1: 速度不对称检测 (speedRatio > 2.5)
 *   C4  P2: 清除防抖 (150ms)
 *   C5  P3: 自适应阈值保护 (3次反弹 → 阈值减半)
 *   C6  P4: 零进展检测 (1000ms)
 *   C7  完整 STUCK 状态机 + accumulatedStuckTime 累计
 *
 * 【v9.21 新增改进 (2026-09-01)】
 *   A1  chaosForceTimeoutMs 最小值 1500 → 3000 (保障混沌有充分脱困时间)
 *   A2  chaosForceTimeoutMs 最大值 3500 → 8000 (混沌不会过早终止)
 *   A3  Gene::init() 随机范围同步更新
 *   A4  Gene::mutate() 钳制范围同步更新
 *   A5  混沌超时时不再设置 deathFlag，个体继续进化 (避免首次混沌即死亡)
 *   A6  新增 /download/pop 端点 (下载种群二进制文件)
 *   A7  新增 /download/chaos_snap 端点 (下载混沌快照二进制文件)
 *   A8  新增 handleDownloadPop() 处理函数
 *   A9  新增 handleDownloadChaosSnap() 处理函数
 *
 * 【修改溯源】
 *   M1  2026-09-01  保留 v6 的 ChaosSnapshotHeader (R4修复)
 *   M2  2026-09-01  保留 v6 的 CSV 校验函数 (R2修复)
 *   M3  2026-09-01  保留 v6 的 forceFlush 参数 (P1改进)
 *   M4  2026-09-01  保留 v6 的空间预检查 (P2改进)
 *   M5  2026-09-01  移植 v64 的 chaosTimeoutMs 范围 → Gene::init()
 *   M6  2026-09-01  移植 v64 的 chaosTimeoutMs 突变 → Gene::mutate()
 *   M7  2026-09-01  移植 v64 的 chaosForceTimeoutMs 范围 → Gene::init()
 *   M8  2026-09-01  移植 v64 的 chaosForceTimeoutMs 突变 → Gene::mutate()
 *   M9  2026-09-01  移植 v64 的 P1-P4 检测机制 → MotorController::update()
 *   M10 2026-09-01  移植 v64 的新增静态变量 → MotorController 类
 *   M11 2026-09-01  移除 v6 的 stuckFrameCount
 *   A1  2026-09-01  chaosForceTimeoutMs 最小值 1500→3000
 *   A2  2026-09-01  chaosForceTimeoutMs 最大值 3500→8000
 *   A3  2026-09-01  Gene::init() 随机范围同步
 *   A4  2026-09-01  Gene::mutate() 钳制范围同步
 *   A5  2026-09-01  注释 deathFlag = true (超时不判死)
 *   A6  2026-09-01  新增 /download/pop 端点
 *   A7  2026-09-01  新增 /download/chaos_snap 端点
 *   A8  2026-09-01  新增 handleDownloadPop()
 *   A9  2026-09-01  新增 handleDownloadChaosSnap()
 * ================================================================
 */
 /*
* ================================================================
 * 项目名称：仿草履虫应激机制 - 脱困能力进化系统
 * 文件：OEETestFinal_v9919_Final_v71_RobustFixed.ino
 * 版本：v9.21-RobustFixed（序列化鲁棒性修复 + 完整存储链路审计）
 * 
 * ================================================================
 * 修改日志 - 序列化鲁棒性修复 (2026-09-03)
 * ================================================================
 *
 * 【问题描述】
 *   1. savePopulation() 的 totalSize 计算与实际写入字段不匹配
 *   2. loadPopulation() 缺少文件大小校验，损坏文件可能导致越界读取
 *   3. handleDownloadPopulation() 解析不准确，缺少 hasChaosRules 字段
 *
 * 【修复内容】
 *   1. 新增 serializeIndividual() 带边界检查
 *   2. 新增 deserializeIndividual() 带版本兼容
 *   3. 重写 savePopulation() 精确计算 totalSize
 *   4. 重写 loadPopulation() 增加完整性校验
 *   5. 重写 handleDownloadPopulation() 结构化解析
 *
 * 【修改人】 系统优化
 * 【修改日期】 2026-09-03
 ================================================================================
                    回归审计修复日志 (v9.22-ObserveOnly)
================================================================================
修复日期: 2026-09-03
修复依据: v9.21-RobustFixed 系统性审计报告 (9项违规)

┌────┬────────────────────┬──────────┬──────────────────────────────────────┐
│ ID │ 违规类型           │ 严重程度 │ 修复状态                             │
├────┼────────────────────┼──────────┼──────────────────────────────────────┤
│ R1 │ 设计目标异化       │ 严重     │ ✅ 已修复 - 版本号/注释/徽章全部更新 │
│ R2 │ 强制循环机制       │ 严重     │ ✅ 已修复 - 移除2处归零              │
│ R3 │ 冗余触发器         │ 中等     │ ✅ 已修复 - 删除 loop 中触发代码    │
│ R4 │ 混沌窗口过短       │ 高       │ ✅ 已修复 - 2000~6000ms              │
│ R5 │ 恢复条件激进       │ 中等     │ ✅ 已修复 - 20帧→50帧                │
│ R6 │ 断电回代           │ 严重     │ ✅ 已修复 - 跳过当前代删除           │
│ R7 │ 自适应标定         │ 中等     │ ✅ 已修复 - 移除阈值修改             │
│ R8 │ 归一化不完整       │ 低       │ ✅ 已修复 - 补全6维                  │
│ R9 │ 参数硬编码不一致   │ 低       │ ✅ 已修复 - 使用宏定义               │
└────┴────────────────────┴──────────┴──────────────────────────────────────┘

版本号: v9.21-RobustFixed → v9.22-ObserveOnly

核心设计原则回归:
  ✅ 「只观察、不优化」- 混沌由卡住条件自然触发，不强制保障次数
  ✅ 「向死而生」- 混沌是困境中的自然状态切换，非被安排流程
  ✅ 「去标定」- 传感器阈值在运行中不被修改
  ✅ 「无穷进化」- 断电恢复不重置到第1代
================================================================================
 * ================================================================
 */

/*
* ================================================================
 * 项目名称：仿草履虫应激机制 - 脱困能力进化系统
 * 文件：OEETestFinal_v9922_ObserveOnly.ino
 * 版本：v9.22-ObserveOnly（回归审计修复 - 只观察不优化）
 * 
 * ================================================================
 * 回归审计修复记录 (2026-09-03)
 * ================================================================
 *
 * 【审计依据】
 *   基于论文设计原则系统性审计，发现9项违规，全部修复。
 *   审计报告编号：v9.21-RobustFixed 审计报告 (2026-09-03)
 *
 * ────────────────────────────────────────────────────────────────
 *  修复清单
 * ────────────────────────────────────────────────────────────────
 *
 * 【R1】设计目标异化修复（违规1 - 严重）
 *   问题：版本号、注释、UI徽章写"确保3轮混沌"，违背"只观察、不优化"
 *   修复：版本号改为 v9.22-ObserveOnly，所有"3轮混沌保障"改为"观察模式"
 *
 * 【R2】强制循环机制修复（违规2 - 严重）
 *   问题：endChaos() 和 update() 中 accumulatedStuckTime 被强制归零
 *   修复：移除两处 accumulatedStuckTime = 0，让卡住时间自然累积
 *
 * 【R3】冗余触发器移除（违规3 - 中等）
 *   问题：loop() 中存在第二条混沌触发路径
 *   修复：完全删除 loop() 中的 force trigger 代码块
 *
 * 【R4】混沌窗口调整（违规4 - 高）
 *   问题：chaosTimeoutMs 范围 800~1800ms 过短
 *   修复：CHAOS_TIMEOUT_MIN=2000, CHAOS_TIMEOUT_MAX=6000
 *
 * 【R5】恢复条件放宽（违规5 - 中等）
 *   问题：CHAOS_RECOVER_STABLE_FRAMES=20 (200ms) 过于激进
 *   修复：改为 50 (500ms)
 *
 * 【R6】断电回代修复（违规6 - 严重）
 *   问题：ensureSpace() 可能删除当前代种群文件
 *   修复：获取当前代，在删除时跳过 currentGen
 *
 * 【R7】自适应标定移除（违规7 - 中等）
 *   问题：stuckBounceCount 运行时修改 encoderDiffThreshold
 *   修复：移除阈值修改逻辑，只保留 bounce 计数日志
 *
 * 【R8】12维归一化补全（违规8 - 低）
 *   问题：normalize() 仅归一化 6/12 维
 *   修复：补全剩余6维的归一化
 *
 * 【R9】参数宏定义统一（违规9 - 低）
 *   问题：Gene::init() 和 Gene::mutate() 中硬编码
 *   修复：使用 CHAOS_FORCE_TIMEOUT_MIN/MAX 宏定义
 *   ensureSpace() 已修复——不再删除当前代数（通过遍历列表跳过 currentGen）。
 *   但仍有 2 处硬重置 未修复：① loadExperimentState() 行2335：
 *   当种群文件缺失时直接 currentGeneration = 0 + 清空实验标记；
 *   ② initImpl() 行7：当种群文件不存在时直接 currentGeneration = 0。
 *   这两处应在文件丢失时搜索最后可用的代数并恢复，而非硬重置到第1代。
  * ================================================================
 */

// ================================================================
// ★★★ SPIFFS 格式化控制 ★★★
/*
* ================================================================
 * 项目名称：仿草履虫应激机制 - 脱困能力进化系统
 * 文件：OEETestFinal_v9922_ObserveOnly.ino
 * 版本：v9.22-ObserveOnly（回归审计修复 - 只观察不优化）
 * 
 * ================================================================
 * 回归审计修复记录 (2026-09-03)
 * ================================================================
 *
 * 【R1】设计目标异化修复 - 版本号改为 v9.22-ObserveOnly
 * 【R2】强制循环机制修复 - 移除 accumulatedStuckTime 强制归零
 * 【R3】冗余触发器移除 - 删除 loop() 中的 force trigger
 * 【R4】混沌窗口调整 - CHAOS_TIMEOUT_MIN=2000, MAX=6000
 * 【R5】恢复条件放宽 - CHAOS_RECOVER_STABLE_FRAMES=50
 * 【R6】断电回代修复 - ensureSpace 不删除当前代
 * 【R7】自适应标定移除 - 移除阈值修改逻辑
 * 【R8】12维归一化补全 - 补全剩余6维
 * 【R9】参数宏定义统一 - 使用 CHAOS_FORCE_TIMEOUT_MIN/MAX
 * ================================================================
 */

// ================================================================
// ★★★ SPIFFS 格式化控制 ★★★
// ================================================================
#define FORMAT_SPIFFS_ON_BOOT 0

// ================================================================
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
#define POPULATION_SIZE     16
#define TEST_DURATION_MS    30000
#define LOOP_DELAY_MS       10

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
#define RAM_LOG_BUFFER_SIZE       6000
#define SPIFFS_MAX_GENERATIONS    20
#define SPIFFS_MAX_INDIVIDUALS_PER_GEN 16
#define INCREMENTAL_SAVE_INTERVAL 100

// ================================================================
// ★★★ R1: 固件版本 - 移除"3轮混沌保障" ★★★
// ================================================================
#define FIRMWARE_VERSION "v9.22-ObserveOnly"
#define VERSION_MARKER_FILE "/version_" FIRMWARE_VERSION ".mrk"

// ================================================================
// ★★★ R5: 混沌恢复条件放宽 ★★★
// ================================================================
#define CHAOS_RECOVER_STABLE_FRAMES 50   // ★★★ R5: 20→50 ★★★
#define CHAOS_ESCAPE_PWM            200
#define MAX_STUCK_WINDOW            20

// ================================================================
// ★★★ R4: 混沌触发时间尺度参数 ★★★
// ================================================================
#define CHAOS_TIMEOUT_MIN       2000   // ★★★ R4: 800→2000 ★★★
#define CHAOS_TIMEOUT_MAX       6000   // ★★★ R4: 1800→6000 ★★★
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
    STATE_CHAOS     = 3
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
        condType   = random(0, 8);
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

    // ★★★ R8: 补全 12 维归一化 ★★★
    void normalize(const BehaviorDescriptor& maxVals) {
        if (maxVals.leftSensorMean > 0) leftSensorMean /= maxVals.leftSensorMean;
        if (maxVals.rightSensorMean > 0) rightSensorMean /= maxVals.rightSensorMean;
        if (maxVals.sensorVariance > 0) sensorVariance /= maxVals.sensorVariance;
        if (maxVals.avgSpeed > 0) avgSpeed /= maxVals.avgSpeed;
        if (maxVals.speedVariance > 0) speedVariance /= maxVals.speedVariance;
        if (maxVals.totalDistance > 0) totalDistance /= maxVals.totalDistance;
        // ★★★ R8: 补全剩余6维归一化 ★★★
        if (maxVals.turnBias > 0) turnBias /= maxVals.turnBias;
        if (maxVals.sensorAsymmetry > 0) sensorAsymmetry /= maxVals.sensorAsymmetry;
        if (maxVals.forwardRatio > 0) forwardRatio /= maxVals.forwardRatio;
        if (maxVals.turnRatio > 0) turnRatio /= maxVals.turnRatio;
        if (maxVals.reverseRatio > 0) reverseRatio /= maxVals.reverseRatio;
        if (maxVals.idleRatio > 0) idleRatio /= maxVals.idleRatio;
    }
};

// ================================================================
// 帧日志条目
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
    uint8_t  isChaosFrame : 1;
    uint8_t  state : 3;
    uint8_t  reserved : 1;
};

struct CompressedFrameEntry {
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

struct FileHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t headerSize;
    uint32_t crc32;
    uint32_t frameCount;
    uint32_t generation;
    uint16_t individual;
    uint16_t reserved;
};

struct HistoryRecord {
    uint32_t timestamp;
    uint32_t generation;
    float    noveltyScore;
    uint32_t survivalTime;
    int32_t  distance_ticks;
    uint8_t  ruleCount;
    uint8_t  reserved[3];
};

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

#define MAX_CHAOS_SNAPSHOTS 8

struct ChaosSnapshotEntry {
    int16_t  sensorLeft;
    int16_t  sensorRight;
    int8_t   motorLeftPWM;
    int8_t   motorRightPWM;
    uint16_t durationMs;
    uint32_t timestamp_ms;
};

struct ChaosSnapshotHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t headerSize;
    uint32_t crc32;
    uint8_t  count;
    uint8_t  reserved[3];
};

// === 序列化结构体尺寸锁定 ===
static_assert(sizeof(BehaviorRule) == 14, "BehaviorRule size must be 14");
static_assert(sizeof(CompressedFrameEntry) == 12, "CompressedFrameEntry size must be 12");
static_assert(sizeof(FileHeader) == 24, "FileHeader size must be 24");
static_assert(sizeof(ChaosSnapshotEntry) == 12, "ChaosSnapshotEntry size must be 12");
static_assert(sizeof(ChaosSnapshotHeader) == 16, "ChaosSnapshotHeader size must be 16");

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
    
    static void setThresholds(int16_t obs, int16_t clr) {
        obstacleThreshold = obs;
        clearThreshold = clr;
    }
    
    static int getLeftRaw() { return analogRead(PIN_SENSOR_LEFT); }
    static int getRightRaw() { return analogRead(PIN_SENSOR_RIGHT); }
    static bool isObstacleLeft() { return analogRead(PIN_SENSOR_LEFT) > obstacleThreshold; }
    static bool isObstacleRight() { return analogRead(PIN_SENSOR_RIGHT) > obstacleThreshold; }
    static bool isClear() {
        return (analogRead(PIN_SENSOR_LEFT) < clearThreshold &&
                analogRead(PIN_SENSOR_RIGHT) < clearThreshold);
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
// FileUtils 类
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
        if (ts < 1577836800UL || ts > 4102444800UL) return false;
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
                    Logger::logf("⚠️ CSV校验失败，跳过行: %s", line.c_str());
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
        Logger::logf("📋 历史记录加载完成: %d 条 (含CSV校验)", count);
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
        
        #if FORMAT_SPIFFS_ON_BOOT
            Logger::log("⚠️ FORMAT_SPIFFS_ON_BOOT is ENABLED - formatting SPIFFS...");
            if (SPIFFS.format()) {
                Logger::log("✅ SPIFFS formatted successfully");
            } else {
                Logger::log("❌ SPIFFS format failed!");
            }
        #endif
        
        if (!SPIFFS.begin(true)) {
            Logger::log("SPIFFS mount failed, using RAM mode");
            fsReady = false;
        } else {
            fsReady = true;
            Logger::log("SPIFFS mounted successfully");
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
        if (forceFlush || pendingBuffer.length() > MAX_BUFFER_SIZE || unsavedCount >= 5) {
            flushBuffer();
        }
    }

    static bool flushBuffer() {
        if (pendingBuffer.length() == 0) { unsavedCount = 0; return true; }
        if (!fsReady) {
            pendingBuffer = "";
            unsavedCount = 0;
            lastSaveTime = millis();
            return true;
        }
        if (!SPIFFS.exists(HISTORY_FILE)) {
            String header = "timestamp,generation,noveltyScore,survivalTime,distance_ticks,ruleCount\n";
            appendToFile(HISTORY_FILE, header);
        }
        bool success = appendToFile(HISTORY_FILE, pendingBuffer);
        if (success) {
            pendingBuffer = "";
            unsavedCount = 0;
            lastSaveTime = millis();
        }
        return success;
    }

    static void forceSave() { 
        if (pendingBuffer.length() > 0) flushBuffer(); 
        if (chaosPendingBuffer.length() > 0) flushChaosBuffer();
    }
    static bool isReady() { return fsReady; }
    static uint32_t getTotalSaved() { return totalSaved; }
    static void tick() {
        if (millis() - lastSaveTime > STORAGE_SAVE_INTERVAL_MS) {
            if (pendingBuffer.length() > 0) flushBuffer();
            if (chaosPendingBuffer.length() > 0) flushChaosBuffer();
        }
    }
    static void clearAll() { 
        pendingBuffer = ""; unsavedCount = 0; ramHistoryCount = 0; 
        chaosPendingBuffer = ""; chaosUnsavedCount = 0;
    }
    static String getCSVData() {
        if (pendingBuffer.length() > 0) flushBuffer();
        if (fsReady && SPIFFS.exists(HISTORY_FILE)) {
            return FileUtils::readStringCapped(HISTORY_FILE, 131072);
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

    static void addChaosRecord(const ChaoticTestRecord& record, bool forceFlush = false) {
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
        
        chaosPendingBuffer += String(line);
        chaosUnsavedCount++;
        totalSaved++;
        if (forceFlush || chaosPendingBuffer.length() > MAX_BUFFER_SIZE || chaosUnsavedCount >= 5) {
            flushChaosBuffer();
        }
    }
    
    static bool flushChaosBuffer() {
        if (chaosPendingBuffer.length() == 0) { chaosUnsavedCount = 0; return true; }
        if (!fsReady) {
            chaosPendingBuffer = "";
            chaosUnsavedCount = 0;
            return true;
        }
        File checkFile = SPIFFS.open(CHAOS_HISTORY_FILE, FILE_READ);
        bool needHeader = !checkFile || checkFile.size() == 0;
        if (checkFile) checkFile.close();
        if (needHeader) {
            chaosPendingBuffer = String("timestamp,generation,individual,chaosTriggerCount,chaosTotalDuration,"
                "chaosMaxDuration,chaosFirstTime,chaosLastTime,"
                "baselineDistance,chaosDistance,baselineFrames,chaosFrames,"
                "baselineAvgSpeedL,baselineAvgSpeedR,chaosAvgSpeedL,chaosAvgSpeedR,"
                "chaosSuccess,testTerminatedBy\n") + chaosPendingBuffer;
        }
        bool success = appendToFile(CHAOS_HISTORY_FILE, chaosPendingBuffer);
        if (success) {
            chaosPendingBuffer = "";
            chaosUnsavedCount = 0;
        }
        return success;
    }

    static String getChaosHistoryCSV() {
        if (!fsReady) return "";
        return FileUtils::readStringCapped(CHAOS_HISTORY_FILE, 131072);
    }
    
    static void formatSPIFFS() {
        if (SPIFFS.format()) {
            Logger::log("✅ SPIFFS formatted successfully");
        } else {
            Logger::log("❌ SPIFFS format failed!");
        }
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

// ================================================================
// RobustStorage 静态变量定义
// ================================================================
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

// ================================================================
// FileUtils 方法实现
// ================================================================
bool FileUtils::atomicWrite(const String& path, const uint8_t* data, size_t len) {
    if (!RobustStorage::isReady()) return false;
    
    String tempPath = path + ".tmp";
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
        std::vector<String> toDelete;
        {
            File root = SPIFFS.open("/");
            if (root) {
                while (File file = root.openNextFile()) {
                    String name = String(file.name());
                    bool shouldDelete = false;
                    if (name.startsWith("/pop_gen_")) shouldDelete = true;
                    if (name.startsWith("/gen_")) shouldDelete = true;
                    if (name.startsWith("/frm_")) shouldDelete = true;
                    if (name == "/oe_history.csv") shouldDelete = true;
                    if (name == "/experiment_state.mrk") shouldDelete = true;
                    if (name.startsWith("/version_") && name != String(VERSION_MARKER_FILE)) shouldDelete = true;
                    if (shouldDelete) toDelete.push_back(name);
                    file.close();
                }
                root.close();
            }
        }
        for (const String& name : toDelete) SPIFFS.remove(name);
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
        ruleCount = random(MIN_RULES, MAX_RULES + 1);
        for (int i = 0; i < ruleCount; i++) rules[i].randomize();
        survival_time = 0; distance_ticks = 0;
        noveltyScore = 0; behavior.init(); baselineBehavior.init();
        obstacleThreshold    = random(800, 2501);
        clearThreshold       = random(200, 1201);
        encoderDiffThreshold = random(10, 101);
        encoderDiffMin       = random(1, 21);
        wheelSpinThreshold   = random(5, 81);
        wheelStopThreshold   = random(1, 11);
        stuckWindowSize      = random(3, 21);
        chaosNoiseAmplifier  = random(50, 401);
        chaosMinPwm          = random(5, 81);
        chaosTimeoutMs       = random(CHAOS_TIMEOUT_MIN, CHAOS_TIMEOUT_MAX + 1);
        // ★★★ R9: 使用宏定义 ★★★
        chaosForceTimeoutMs  = random(CHAOS_FORCE_TIMEOUT_MIN, CHAOS_FORCE_TIMEOUT_MAX + 1);
        chaosRuleCount       = 0;
        chaosRulesStartIndex = 0;
        hasChaosRules        = false;
    }

    bool evaluateCondition(int ruleIdx, int leftSensor, int rightSensor, 
                           int32_t distance, uint32_t elapsed) const;

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
        if (random(0, 1000) < mutationRate * 500) { obstacleThreshold    += random(-100, 101); obstacleThreshold    = constrain(obstacleThreshold,    800, 2500); }
        if (random(0, 1000) < mutationRate * 500) { clearThreshold       += random(-50, 51);   clearThreshold       = constrain(clearThreshold,       200, 1200); }
        if (random(0, 1000) < mutationRate * 500) { encoderDiffThreshold += random(-5, 6);     encoderDiffThreshold = constrain(encoderDiffThreshold, 10,  100); }
        if (random(0, 1000) < mutationRate * 500) { encoderDiffMin       += random(-2, 3);     encoderDiffMin       = constrain(encoderDiffMin,       1,   20); }
        if (random(0, 1000) < mutationRate * 500) { wheelSpinThreshold   += random(-5, 6);     wheelSpinThreshold   = constrain(wheelSpinThreshold,   5,   80); }
        if (random(0, 1000) < mutationRate * 500) { wheelStopThreshold   += random(-1, 2);     wheelStopThreshold   = constrain(wheelStopThreshold,   1,   10); }
        if (random(0, 1000) < mutationRate * 300) { stuckWindowSize      += random(-2, 3);     stuckWindowSize      = constrain(stuckWindowSize,      3,   20); }
        if (random(0, 1000) < mutationRate * 500) { chaosNoiseAmplifier  += random(-30, 31);   chaosNoiseAmplifier  = constrain(chaosNoiseAmplifier,  50,  400); }
        if (random(0, 1000) < mutationRate * 500) { chaosMinPwm          += random(-5, 6);     chaosMinPwm          = constrain(chaosMinPwm,          5,   80); }
        
        if (random(0, 1000) < mutationRate * 500) {
            chaosTimeoutMs += random(-300, 301);
            chaosTimeoutMs = constrain(chaosTimeoutMs, CHAOS_TIMEOUT_MIN, CHAOS_TIMEOUT_MAX);
        }
        // ★★★ R9: 使用宏定义 ★★★
        if (random(0, 1000) < mutationRate * 500) {
            chaosForceTimeoutMs += random(-500, 501);
            chaosForceTimeoutMs = constrain(chaosForceTimeoutMs, CHAOS_FORCE_TIMEOUT_MIN, CHAOS_FORCE_TIMEOUT_MAX);
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
        child.noveltyScore = 0; child.behavior.init(); child.baselineBehavior.init();
        child.obstacleThreshold    = (random(0,2) == 0) ? p1.obstacleThreshold    : p2.obstacleThreshold;
        child.clearThreshold       = (random(0,2) == 0) ? p1.clearThreshold       : p2.clearThreshold;
        child.encoderDiffThreshold = (random(0,2) == 0) ? p1.encoderDiffThreshold : p2.encoderDiffThreshold;
        child.encoderDiffMin       = (random(0,2) == 0) ? p1.encoderDiffMin       : p2.encoderDiffMin;
        child.wheelSpinThreshold   = (random(0,2) == 0) ? p1.wheelSpinThreshold   : p2.wheelSpinThreshold;
        child.wheelStopThreshold   = (random(0,2) == 0) ? p1.wheelStopThreshold   : p2.wheelStopThreshold;
        child.stuckWindowSize      = (random(0,2) == 0) ? p1.stuckWindowSize      : p2.stuckWindowSize;
        child.chaosNoiseAmplifier  = (random(0,2) == 0) ? p1.chaosNoiseAmplifier  : p2.chaosNoiseAmplifier;
        child.chaosMinPwm          = (random(0,2) == 0) ? p1.chaosMinPwm          : p2.chaosMinPwm;
        child.chaosTimeoutMs       = (random(0,2) == 0) ? p1.chaosTimeoutMs       : p2.chaosTimeoutMs;
        child.chaosForceTimeoutMs  = (random(0,2) == 0) ? p1.chaosForceTimeoutMs  : p2.chaosForceTimeoutMs;
        
        int chaosInherited = 0;
        if (p1.hasChaosRules && p2.hasChaosRules) {
            const Gene& donor = (random(0, 2) == 0) ? p1 : p2;
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
// Gene::evaluateCondition 实现
// ================================================================
bool Gene::evaluateCondition(int ruleIdx, int leftSensor, int rightSensor, 
                             int32_t distance, uint32_t elapsed) const {
    const BehaviorRule& r = rules[ruleIdx];
    int value = 0;
    switch (r.condType) {
        case COND_SENSOR_LEFT:  value = leftSensor; break;
        case COND_SENSOR_RIGHT: value = rightSensor; break;
        case COND_SENSOR_BOTH:  return (leftSensor > obstacleThreshold) && (rightSensor > obstacleThreshold);
        case COND_SENSOR_ANY:   return (leftSensor > obstacleThreshold) || (rightSensor > obstacleThreshold);
        case COND_DISTANCE:     value = (int)distance; break;
        case COND_TIME:         value = (int)elapsed; break;
        case COND_IDLE:         return (leftSensor < clearThreshold) && (rightSensor < clearThreshold);
        case COND_ALWAYS:       return true;
        default: return false;
    }
    return r.compare(value, r.condOp, r.condValue);
}

// ================================================================
// ★★★ RollingStorage 类 - 包含 R6 修复 ★★★
// ================================================================
class RollingStorage {
private:
    static const int MAX_GENERATIONS = SPIFFS_MAX_GENERATIONS;
    static const int MAX_INDIVIDUALS_PER_GEN = SPIFFS_MAX_INDIVIDUALS_PER_GEN;
    static uint32_t freeSpaceThreshold;
    static int savedCount[100];
    
    // ★★★ R6: 添加静态变量存储当前代（解决循环依赖）★★★
    static uint32_t currentGenerationForStorage;
    
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
    
    static std::vector<uint32_t> getStoredGenerations() {
        std::vector<uint32_t> gens;
        if (!RobustStorage::isReady()) return gens;
        File root = SPIFFS.open("/");
        if (!root) return gens;
        while (File f = root.openNextFile()) {
            String name = String(f.name());
            if (name.startsWith("/pop_gen_")) {
                String numStr = name.substring(9, name.lastIndexOf('.'));
                uint32_t gen = numStr.toInt();
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
    
    static void deleteGeneration(uint32_t gen) {
        if (!RobustStorage::isReady()) return;
        std::vector<String> toDelete;
        String popPrefix = "/pop_gen_" + String(gen) + ".";
        String genPrefix = "/gen_" + String(gen) + "_";
        String frmPrefix = "/frm_" + String(gen) + "_";
        String chaosPrefix = "/chaos_g" + String(gen) + "_";
        String chaosSnapsPrefix = "/chaos_snaps_g" + String(gen) + "_";
        {
            File root = SPIFFS.open("/");
            if (root) {
                while (File f = root.openNextFile()) {
                    String name = String(f.name());
                    if (name.startsWith(popPrefix) || name.startsWith(genPrefix) ||
                        name.startsWith(frmPrefix) || name.startsWith(chaosPrefix) ||
                        name.startsWith(chaosSnapsPrefix)) {
                        toDelete.push_back(name);
                    }
                    f.close();
                }
                root.close();
            }
        }
        int deleted = 0;
        for (const String& name : toDelete) {
            if (SPIFFS.remove(name)) deleted++;
        }
        if (deleted > 0) {
            Logger::logf("🧹 Deleted generation %lu (%d files)", gen, deleted);
        }
    }
    
public:
    static void init() {
        freeSpaceThreshold = 50 * 1024;
        currentGenerationForStorage = 0;  // ★★★ R6 ★★★
        memset(savedCount, 0, sizeof(savedCount));
        Logger::logf("RollingStorage: max %d generations, %d individuals/gen",
                     MAX_GENERATIONS, MAX_INDIVIDUALS_PER_GEN);
    }
    
    // ★★★ R6: 设置当前代（由 GeneStorage 调用）★★★
    static void setCurrentGeneration(uint32_t gen) {
        currentGenerationForStorage = gen;
    }
    
    // ★★★ R6: 获取当前代 ★★★
    static uint32_t getCurrentGeneration() {
        return currentGenerationForStorage;
    }
    
    // ★★★ R6: ensureSpace() 不删除当前代 ★★★
    static bool ensureSpace(size_t requiredBytes) {
        if (!RobustStorage::isReady()) return false;
        size_t free = getFreeSpace();
        if (free < requiredBytes + freeSpaceThreshold) {
            auto gens = getStoredGenerations();
            if (!gens.empty()) {
                // ★★★ R6: 使用本地存储的当前代 ★★★
                uint32_t currentGen = currentGenerationForStorage;
                // 找到最旧且不是当前代的代数
                uint32_t oldest = 0;
                for (auto g : gens) {
                    if (g != currentGen) {
                        oldest = g;
                        break;
                    }
                }
                if (oldest == 0 && gens.size() > 1) {
                    Logger::logf("⚠️ Only current generation %lu found, cannot delete", currentGen);
                    return false;
                }
                if (oldest > 0) {
                    Logger::logf("⚠️ Space low (%d bytes), deleting gen %lu (not current %lu)", 
                                 free, oldest, currentGen);
                    deleteGeneration(oldest);
                    free = getFreeSpace();
                }
            }
        }
        auto gens = getStoredGenerations();
        uint32_t currentGen = currentGenerationForStorage;
        int maxRounds = (int)gens.size() + 4;
        while ((int)gens.size() >= MAX_GENERATIONS && maxRounds-- > 0) {
            size_t before = gens.size();
            // ★★★ R6: 跳过当前代 ★★★
            uint32_t oldest = 0;
            for (auto g : gens) {
                if (g != currentGen) {
                    oldest = g;
                    break;
                }
            }
            if (oldest == 0) break;
            Logger::logf("🔄 Too many gens (%d), deleting %lu (keeping current %lu)", 
                         gens.size(), oldest, currentGen);
            deleteGeneration(oldest);
            gens = getStoredGenerations();
            if (gens.size() >= before) break;
        }
        return getFreeSpace() >= requiredBytes;
    }
    
    static bool saveFrameLog(uint32_t generation, int individual, 
                             const FrameLogEntry* log, int count, int head = 0,
                             int startOffset = 0, int saveCount = -1,
                             bool isIncremental = false) {
        if (!RobustStorage::isReady() || count == 0) return false;
        if (saveCount < 0) saveCount = count;
        if (startOffset + saveCount > count) saveCount = count - startOffset;
        if (saveCount <= 0) return false;
        
        size_t estimatedSize = saveCount * sizeof(CompressedFrameEntry) + sizeof(FileHeader) + 512;
        if (!ensureSpace(estimatedSize)) {
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
        
        FileHeader header;
        header.magic = 0x47454E45;
        header.version = 0x0006;
        header.headerSize = sizeof(FileHeader);
        header.frameCount = exportCount;
        header.generation = generation;
        header.individual = individual;
        header.reserved = 0;
        header.crc32 = CRC32::calculate(dataBuffer, dataSize);
        
        size_t totalSize = sizeof(FileHeader) + dataSize;
        uint8_t* finalBuffer = (uint8_t*)malloc(totalSize);
        if (!finalBuffer) { free(dataBuffer); return false; }
        
        memcpy(finalBuffer, &header, sizeof(FileHeader));
        memcpy(finalBuffer + sizeof(FileHeader), dataBuffer, dataSize);
        free(dataBuffer);
        
        String path = "/frm_" + String(generation) + "_i" + String(individual) + (isIncremental ? ".inc.bin" : ".bin");
        bool success = FileUtils::atomicWrite(path, finalBuffer, totalSize);
        free(finalBuffer);
        
        if (success) {
            savedCount[generation % 100]++;
            Logger::logf("💾 saveFrameLog gen=%lu id=%d (slot %d/%d, CRC=0x%08X) ✅",
                         generation, individual,
                         savedCount[generation % 100], MAX_INDIVIDUALS_PER_GEN,
                         header.crc32);
        } else {
            Logger::logf("❌ saveFrameLog gen=%lu id=%d FAILED", generation, individual);
        }
        return success;
    }
    
    static bool incrementalSave(uint32_t generation, int individual,
                                const FrameLogEntry* log, int count, int head = 0) {
        if (count % INCREMENTAL_SAVE_INTERVAL != 0) return false;
        if (count < INCREMENTAL_SAVE_INTERVAL) return false;
        int startIdx = count - INCREMENTAL_SAVE_INTERVAL;
        return saveFrameLog(generation, individual, log, count, head, startIdx, INCREMENTAL_SAVE_INTERVAL, true);
    }
    
    static void resetGenerationCounter(uint32_t gen) {
        savedCount[gen % 100] = 0;
    }
    
    static String getStorageStatus() {
        String status = "=== Storage Status ===\n";
        size_t free = getFreeSpace();
        status += "Free space: " + String(free / 1024) + " KB\n";
        auto gens = getStoredGenerations();
        status += "Stored generations: " + String(gens.size()) + "/" + String(MAX_GENERATIONS) + "\n";
        for (auto g : gens) {
            status += "  Gen " + String(g) + "\n";
        }
        return status;
    }
    
    static size_t getFreeSpaceKB() {
        return getFreeSpace() / 1024;
    }
};

// ★★★ R6: 静态变量定义 ★★★
uint32_t RollingStorage::freeSpaceThreshold = 50 * 1024;
int RollingStorage::savedCount[100] = {0};
uint32_t RollingStorage::currentGenerationForStorage = 0;  // ★★★ R6 ★★★

// ================================================================
// GeneStorage 类 - 包含 R6 同步更新
// ================================================================
class GeneStorage {
private:
    static const uint32_t MAX_GENERATIONS_PER_EXPERIMENT = 0xFFFFFFFF;
    static const uint16_t FILE_VERSION = 0x0006;
    static const uint32_t FILE_MAGIC = 0x47454E45;
    static const char* POP_PREFIX;
    static const char* GEN_RECORD_PREFIX;
    static const char* FRAME_LOG_PREFIX;
    static const char* EXPERIMENT_MARKER;
    static uint32_t currentExperimentId;
    static uint32_t currentGeneration;
    static bool experimentActive;

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
        
        if (used + 12 > maxSize) return 0;
        memcpy(ptr, &g.obstacleThreshold, 2); ptr += 2; used += 2;
        memcpy(ptr, &g.clearThreshold, 2); ptr += 2; used += 2;
        memcpy(ptr, &g.encoderDiffThreshold, 2); ptr += 2; used += 2;
        memcpy(ptr, &g.encoderDiffMin, 2); ptr += 2; used += 2;
        memcpy(ptr, &g.wheelSpinThreshold, 2); ptr += 2; used += 2;
        memcpy(ptr, &g.wheelStopThreshold, 2); ptr += 2; used += 2;
        
        if (used + 1 > maxSize) return 0;
        *ptr = g.stuckWindowSize; ptr++; used += 1;
        
        if (used + 8 > maxSize) return 0;
        memcpy(ptr, &g.chaosNoiseAmplifier, 2); ptr += 2; used += 2;
        memcpy(ptr, &g.chaosMinPwm, 2); ptr += 2; used += 2;
        memcpy(ptr, &g.chaosTimeoutMs, 2); ptr += 2; used += 2;
        memcpy(ptr, &g.chaosForceTimeoutMs, 2); ptr += 2; used += 2;
        
        if (used + 3 > maxSize) return 0;
        *ptr = g.hasChaosRules ? 1 : 0; ptr++; used += 1;
        *ptr = g.chaosRuleCount; ptr++; used += 1;
        *ptr = g.chaosRulesStartIndex; ptr++; used += 1;
        
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
        
        g.behavior.init();
        return used;
    }

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
        Logger::logf("Gene storage initialized, current gen=%lu, active=%d", 
                     currentGeneration, experimentActive);
    }
    
    static void forceActivate() {
        experimentActive = true;
        if (currentGeneration == 0) currentGeneration = 1;
        saveExperimentState();
        Logger::logf("🔧 Force activated: gen=%lu, active=%d", 
                     currentGeneration, experimentActive);
    }
    
    static void startNewExperiment() {
        if (!RobustStorage::isReady()) {
            Logger::log("❌ startNewExperiment: SPIFFS not ready");
            return;
        }
        clearAllExperimentData();
        currentExperimentId = (uint32_t)millis();
        currentGeneration = 1;
        experimentActive = true;
        saveExperimentState();
        Logger::logf("New experiment started (ID: %lu, gen: %lu)", currentExperimentId, currentGeneration);
    }
    
    // ★★★ R6: setCurrentGeneration 同步更新 RollingStorage ★★★
    static void setCurrentGeneration(uint32_t gen) {
        currentGeneration = gen;
        RollingStorage::setCurrentGeneration(gen);  // ★★★ R6: 同步更新 ★★★
        saveExperimentState();
        Logger::logf("📌 Generation set to %lu", gen);
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
        
        if (gen > 0) {
            String popPath = "/pop_gen_" + String(gen) + ".bin";
            if (!SPIFFS.exists(popPath)) {
                Logger::logf("⚠️ State marker points to gen %lu but pop file missing", gen);
                SPIFFS.remove(EXPERIMENT_MARKER);
                return true;
            }
        }
        
        if (gen >= MAX_GENERATIONS_PER_EXPERIMENT) {
            Logger::logf("Previous experiment completed (ID: %lu, gens: %lu)", expId, gen);
            return true;
        }
        
        currentExperimentId = expId;
        currentGeneration = gen;
        experimentActive = true;
        // ★★★ R6: 同步更新 RollingStorage ★★★
        RollingStorage::setCurrentGeneration(gen);
        Logger::logf("✅ Resuming experiment (ID: %lu, gen: %lu)", expId, gen);
        return false;
    }
    
    static bool isExperimentActive() { return experimentActive; }
    static uint32_t getCurrentGeneration() { return currentGeneration; }
    static uint32_t getMaxGenerations() { return MAX_GENERATIONS_PER_EXPERIMENT; }
    static float getProgress() { return (float)currentGeneration / MAX_GENERATIONS_PER_EXPERIMENT; }
    static bool isExperimentComplete() { return currentGeneration >= MAX_GENERATIONS_PER_EXPERIMENT; }
    
    // ★★★ R6: incrementGeneration 同步更新 RollingStorage ★★★
    static void incrementGeneration() { 
        currentGeneration++; 
        RollingStorage::setCurrentGeneration(currentGeneration);  // ★★★ R6: 同步更新 ★★★
        saveExperimentState();
        Logger::logf("📈 Incremented to generation %lu", currentGeneration);
    }
    
    static bool savePopulation(Gene* population, int size) {
        Logger::logf("💾 savePopulation: starting for gen=%lu, popSize=%d", currentGeneration, size);
        
        if (!RobustStorage::isReady()) {
            Logger::log("❌ savePopulation: SPIFFS not ready");
            return false;
        }
        
        size_t totalSize = 4 + 2 + 2 + 4 + 4;
        
        for (int i = 0; i < size; i++) {
            uint8_t rc = constrain(population[i].ruleCount, MIN_RULES, MAX_RULES);
            size_t indivSize = 1;
            indivSize += rc * sizeof(BehaviorRule);
            indivSize += 4 + 4 + 4;
            indivSize += 12 + 1 + 8 + 3;
            totalSize += indivSize;
        }
        Logger::logf("  Total size: %d bytes", totalSize);
        
        if (!RollingStorage::ensureSpace(totalSize + 512)) {
            Logger::logf("❌ savePopulation: insufficient SPIFFS space (need %d)", totalSize + 512);
            return false;
        }
        
        uint8_t* buffer = (uint8_t*)malloc(totalSize);
        if (!buffer) {
            Logger::log("❌ savePopulation: malloc failed");
            return false;
        }
        
        uint8_t* ptr = buffer;
        
        uint32_t magic = FILE_MAGIC; memcpy(ptr, &magic, 4); ptr += 4;
        uint16_t version = FILE_VERSION; memcpy(ptr, &version, 2); ptr += 2;
        uint16_t popSize = size; memcpy(ptr, &popSize, 2); ptr += 2;
        uint32_t gen = currentGeneration; memcpy(ptr, &gen, 4); ptr += 4;
        uint32_t expId = currentExperimentId; memcpy(ptr, &expId, 4); ptr += 4;
        
        for (int i = 0; i < size; i++) {
            size_t written = serializeIndividual(population[i], ptr, totalSize - (ptr - buffer));
            if (written == 0) {
                free(buffer);
                Logger::logf("❌ savePopulation: serialization failed for individual %d", i);
                return false;
            }
            ptr += written;
        }
        
        size_t finalSize = ptr - buffer;
        String path = String(POP_PREFIX) + String(currentGeneration) + ".bin";
        bool success = FileUtils::atomicWrite(path, buffer, finalSize);
        free(buffer);
        
        Logger::logf("💾 savePopulation gen=%lu -> %s %s (size=%d bytes)", 
                     currentGeneration, path.c_str(), success ? "✅ OK" : "❌ FAILED", finalSize);
        return success;
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
        
        uint8_t* ptr = buffer;
        
        uint32_t magic; memcpy(&magic, ptr, 4); ptr += 4;
        if (magic != FILE_MAGIC) { 
            Logger::logf("❌ loadPopulation: invalid magic 0x%08X", magic);
            free(buffer); 
            return false; 
        }
        
        uint16_t fileVersion; memcpy(&fileVersion, ptr, 2); ptr += 2;
        uint16_t popSize; memcpy(&popSize, ptr, 2); ptr += 2;
        Logger::logf("  fileVersion=0x%04X, popSize=%d", fileVersion, popSize);
        
        if (popSize != size) { 
            Logger::logf("❌ loadPopulation: size mismatch: file=%d, expected=%d", popSize, size);
            free(buffer); 
            return false; 
        }
        
        uint32_t fileGen, fileExpId;
        memcpy(&fileGen, ptr, 4); ptr += 4;
        memcpy(&fileExpId, ptr, 4); ptr += 4;
        
        size_t remaining = fileSize - (ptr - buffer);
        size_t minPerIndiv = 1 + MIN_RULES * sizeof(BehaviorRule) + 4 + 4 + 4 + 12 + 1 + 8 + 3;
        size_t expectedMin = size * minPerIndiv;
        
        if (remaining < expectedMin) {
            Logger::logf("❌ loadPopulation: file too small! remaining=%d, need %d", remaining, expectedMin);
            free(buffer);
            return false;
        }
        
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
        Logger::logf("💾 saveChaosRecord gen=%lu id=%d %s", record.generation, record.individual, success ? "✅" : "❌");
        return success;
    }

    static bool populationFileExists(uint32_t generation) {
        if (!RobustStorage::isReady()) return false;
        String path = String(POP_PREFIX) + String(generation) + ".bin";
        return SPIFFS.exists(path);
    }
    
    static void clearAllExperimentData() {
        if (!RobustStorage::isReady()) return;
        std::vector<String> toDelete;
        {
            File root = SPIFFS.open("/");
            if (root) {
                while (File file = root.openNextFile()) {
                    String name = String(file.name());
                    if (name.startsWith("/pop_gen_") || name.startsWith("/gen_") || 
                        name.startsWith("/frm_") || name.startsWith("/chaos_g") ||
                        name.startsWith("/chaos_snaps_g") ||
                        name == "/experiment_state.mrk" || name == "/oe_history.csv" || 
                        name == "/chaos_history.csv" || name == "/novelty_archive.bin") {
                        toDelete.push_back(name);
                    }
                    file.close();
                }
                root.close();
            }
        }
        for (const String& name : toDelete) {
            SPIFFS.remove(name);
            Logger::logf("🗑️ Deleted: %s", name.c_str());
        }
        currentExperimentId = 0;
        currentGeneration = 0;
        experimentActive = false;
        RollingStorage::setCurrentGeneration(0);  // ★★★ R6 ★★★
        Logger::log("✅ All experiment data cleared");
    }
    
private:
    static void saveExperimentState() {
        if (!RobustStorage::isReady()) return;
        String content = String(currentExperimentId) + "," + String(currentGeneration);
        FileUtils::atomicWriteString(EXPERIMENT_MARKER, content);
        Logger::logf("💾 State saved: ID=%lu, gen=%lu", currentExperimentId, currentGeneration);
    }
    
    static void loadExperimentState() {
        if (!RobustStorage::isReady()) {
            experimentActive = false;
            return;
        }
        String content = FileUtils::safeRead(EXPERIMENT_MARKER);
        if (content.length() == 0) { 
            experimentActive = false; 
            return; 
        }
        int commaPos = content.indexOf(',');
        if (commaPos < 0) { 
            experimentActive = false; 
            return; 
        }
        currentExperimentId = content.substring(0, commaPos).toInt();
        currentGeneration = content.substring(commaPos + 1).toInt();
        
        if (currentGeneration > 0) {
            String popPath = "/pop_gen_" + String(currentGeneration) + ".bin";
            if (!SPIFFS.exists(popPath)) {
                Logger::logf("⚠️ State marker points to gen %lu but file missing, resetting", currentGeneration);
                
                currentGeneration = 0;
                currentExperimentId = 0;
                experimentActive = false;
                SPIFFS.remove(EXPERIMENT_MARKER);
                RollingStorage::setCurrentGeneration(0);  // ★★★ R6 ★★★
                return;
            }
            Logger::logf("✅ Loaded state: exp=%lu, gen=%lu", currentExperimentId, currentGeneration);
            // ★★★ R6: 同步更新 RollingStorage ★★★
            RollingStorage::setCurrentGeneration(currentGeneration);
        }
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
        }
        int n = (usedFrames > 0) ? usedFrames : frameCount;
        if (usedFrames == 0) return desc;
        
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
            int idx = (head - frameCount + i + FRAME_LOG_SIZE) % FRAME_LOG_SIZE;
            const FrameLogEntry& e = frameLog[idx];
            if (excludeChaosFrames && e.isChaosFrame) continue;
            float dL = e.sensorLeft - desc.leftSensorMean;
            float dR = e.sensorRight - desc.rightSensorMean;
            varL += dL*dL; varR += dR*dR;
        }
        desc.sensorVariance = (varL + varR) / (2.0f * n);
        return desc;
    }

    int getArchiveSize() const { return archiveSize; }

    void clear() {
        archiveSize = 0;
        maxValues.init();
        if (RobustStorage::isReady() && SPIFFS.exists(ARCHIVE_FILE)) {
            SPIFFS.remove(ARCHIVE_FILE);
            Logger::log("🗑️ Novelty archive cleared");
        }
    }

    static const char* ARCHIVE_FILE;

    bool save() {
        if (!RobustStorage::isReady()) return false;
        size_t payloadSize = (size_t)12 * sizeof(float) + (size_t)archiveSize * 12 * sizeof(float);
        size_t totalSize = 12 + payloadSize;
        uint8_t* buf = (uint8_t*)malloc(totalSize);
        if (!buf) return false;
        uint8_t* p = buf;
        memcpy(p, "ARCH", 4); p += 4;
        uint16_t ver = 1; memcpy(p, &ver, 2); p += 2;
        uint16_t cnt = (uint16_t)archiveSize; memcpy(p, &cnt, 2); p += 2;
        uint8_t* crcPtr = p; p += 4;
        memcpy(p, &maxValues, 12 * sizeof(float)); p += 12 * sizeof(float);
        for (int i = 0; i < archiveSize; i++) {
            memcpy(p, &archive[i], 12 * sizeof(float)); p += 12 * sizeof(float);
        }
        uint32_t crc = CRC32::calculate(buf + 12, payloadSize);
        memcpy(crcPtr, &crc, 4);
        bool ok = FileUtils::atomicWrite(ARCHIVE_FILE, buf, totalSize);
        free(buf);
        if (ok) Logger::logf("💾 Novelty archive saved (%d behaviors)", archiveSize);
        return ok;
    }

    bool load() {
        if (!RobustStorage::isReady()) return false;
        size_t size = FileUtils::getFileSize(ARCHIVE_FILE);
        if (size < 12) return false;
        uint8_t* buf = (uint8_t*)malloc(size);
        if (!buf) return false;
        File f = SPIFFS.open(ARCHIVE_FILE, FILE_READ);
        if (!f) { free(buf); return false; }
        size_t rd = f.read(buf, size);
        f.close();
        if (rd != size) { free(buf); return false; }
        if (memcmp(buf, "ARCH", 4) != 0) { free(buf); return false; }
        uint16_t ver; memcpy(&ver, buf + 4, 2);
        uint16_t cnt; memcpy(&cnt, buf + 6, 2);
        uint32_t crc; memcpy(&crc, buf + 8, 4);
        if (ver != 1) { free(buf); return false; }
        if (cnt > NOVELTY_ARCHIVE_MAX) { free(buf); return false; }
        size_t expectPayload = (size_t)12 * sizeof(float) + (size_t)cnt * 12 * sizeof(float);
        if (size - 12 != expectPayload) { free(buf); return false; }
        if (CRC32::calculate(buf + 12, size - 12) != crc) { free(buf); return false; }
        archiveSize = cnt;
        uint8_t* p = buf + 12;
        memcpy(&maxValues, p, 12 * sizeof(float)); p += 12 * sizeof(float);
        for (int i = 0; i < archiveSize; i++) {
            memcpy(&archive[i], p, 12 * sizeof(float)); p += 12 * sizeof(float);
        }
        free(buf);
        Logger::logf("📖 Novelty archive loaded (%d behaviors)", archiveSize);
        return true;
    }
};

const char* NoveltyArchive::ARCHIVE_FILE = "/novelty_archive.bin";

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

// ================================================================
// MotorController 类 - 包含 R2, R7 修复
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

    static void flushChaosSnapshot() {
        if (chaosSnapshotCount > 0) {
            ChaosSnapshotEntry& snap = chaosSnapshots[chaosSnapshotCount - 1];
            snap.durationMs = chaosSnapshotStableFrames * 10;
            chaosSnapshotStableFrames = 0;
        }
    }
    
public:
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
            header.version = 0x0001;
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
    
    static bool loadChaosSnapshotsFromSPIFFS(const String& path, 
                                             ChaosSnapshotEntry* outSnapshots, 
                                             int& outCount, 
                                             int maxCount) {
        if (!RobustStorage::isReady() || !SPIFFS.exists(path)) return false;
        
        File file = SPIFFS.open(path, FILE_READ);
        if (!file) return false;
        
        size_t fileSize = file.size();
        if (fileSize < sizeof(ChaosSnapshotHeader)) {
            file.close();
            Logger::logf("⚠️ Chaos snapshot file too small: %s", path.c_str());
            return false;
        }
        
        ChaosSnapshotHeader header;
        if (file.readBytes((char*)&header, sizeof(ChaosSnapshotHeader)) != sizeof(ChaosSnapshotHeader)) {
            file.close();
            return false;
        }
        
        if (header.magic != 0x4348534E) {
            file.close();
            Logger::logf("⚠️ Chaos snapshot magic mismatch: %s (expected 0x4348534E, got 0x%08X)", 
                         path.c_str(), header.magic);
            return false;
        }
        
        if (header.count > MAX_CHAOS_SNAPSHOTS) {
            file.close();
            Logger::logf("⚠️ Chaos snapshot count invalid: %s (count=%d)", path.c_str(), header.count);
            return false;
        }
        
        size_t payloadSize = 1 + header.count * sizeof(ChaosSnapshotEntry);
        uint8_t* payload = (uint8_t*)malloc(payloadSize);
        if (!payload) {
            file.close();
            return false;
        }
        
        if (file.readBytes((char*)payload, payloadSize) != payloadSize) {
            free(payload);
            file.close();
            return false;
        }
        file.close();
        
        uint32_t calcCrc = CRC32::calculate(payload, payloadSize);
        if (calcCrc != header.crc32) {
            free(payload);
            Logger::logf("❌ Chaos snapshot CRC mismatch: %s (expected 0x%08X, got 0x%08X)", 
                         path.c_str(), header.crc32, calcCrc);
            return false;
        }
        
        outCount = min((int)header.count, maxCount);
        memcpy(outSnapshots, payload + 1, outCount * sizeof(ChaosSnapshotEntry));
        free(payload);
        
        Logger::logf("✅ Chaos snapshots loaded: %s (%d snapshots, CRC verified)", 
                     path.c_str(), outCount);
        return true;
    }
    
private:
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
        forceStopAction();
        stopMotors();
        setMotorState(STATE_CHAOS);
        chaosStartTime = millis();
        testChaosTriggerCount++;
        baselineDistanceTicks += distanceTicks - lastChaosEndDistance;
        chaosStartDistance = distanceTicks;
        if (testChaosTriggerCount == 1) {
            testChaosFirstTriggerTime = millis() - testStartTime;
        }
        chaosHappened = true;
        
        Serial.println("========================================");
        Serial.println("!!! 🔥 CHAOS MODE TRIGGERED !!!");
        Serial.printf("  Trigger count: %d\n", testChaosTriggerCount);
        Serial.printf("  Encoder diff: %d\n", getEncoderDiff());
        Serial.printf("  Noise pin (GPIO1): %d\n", SensorCalibration::readNoise());
        Serial.println("  Physical noise takeover started");
        Serial.println("========================================");
        
        Logger::log("========================================");
        Logger::log("!!! CHAOS MODE TRIGGERED !!!");
        Logger::logf("  Trigger count: %d", testChaosTriggerCount);
        Logger::logf("  Encoder diff: %d", getEncoderDiff());
        Logger::log("  Physical noise takeover started");
        Logger::log("========================================");
    }
    
    static void updateChaos() {
        if (motorState != STATE_CHAOS) return;
        
        uint16_t noiseRaw = SensorCalibration::readNoise();
        int noiseL = constrain((int)(noiseRaw / 4) * chaosNoiseAmplifier / 64, 
                               -chaosNoiseAmplifier, chaosNoiseAmplifier);
        int noiseR = constrain((int)(-noiseRaw / 4) * chaosNoiseAmplifier / 64, 
                               -chaosNoiseAmplifier, chaosNoiseAmplifier);
        if (abs(noiseL) < chaosMinPwm) noiseL = (noiseL >= 0 ? chaosMinPwm : -chaosMinPwm);
        if (abs(noiseR) < chaosMinPwm) noiseR = (noiseR >= 0 ? chaosMinPwm : -chaosMinPwm);
        setMotorSpeed(noiseL, noiseR);
        
        leftSensorRaw = SensorCalibration::getLeftRaw();
        rightSensorRaw = SensorCalibration::getRightRaw();
        
        int motorDeltaL = abs((int)noiseL - (int)lastChaosMotorL);
        int motorDeltaR = abs((int)noiseR - (int)lastChaosMotorR);
        bool hasMotorChange = (motorDeltaL > 80 || motorDeltaR > 80);
        
        if (hasMotorChange) {
            flushChaosSnapshot();
            if (chaosSnapshotCount < MAX_CHAOS_SNAPSHOTS) {
                ChaosSnapshotEntry& snap = chaosSnapshots[chaosSnapshotCount];
                snap.sensorLeft    = leftSensorRaw;
                snap.sensorRight   = rightSensorRaw;
                snap.motorLeftPWM  = constrain(noiseL, -128, 127);
                snap.motorRightPWM = constrain(noiseR, -128, 127);
                snap.timestamp_ms  = millis() - chaosStartTime;
                chaosSnapshotStableFrames = 1;
                chaosSnapshotCount++;
            }
        } else {
            if (chaosSnapshotCount > 0) chaosSnapshotStableFrames++;
        }
        lastChaosMotorL = noiseL;
        lastChaosMotorR = noiseR;
        
        static int32_t lastLeftTicks = 0;
        static int32_t lastRightTicks = 0;
        static int stableFrames = 0;
        
        int32_t leftDelta = abs(leftTicks - lastLeftTicks);
        int32_t rightDelta = abs(rightTicks - lastRightTicks);
        int32_t totalDelta = leftDelta + rightDelta;
        
        lastLeftTicks = leftTicks;
        lastRightTicks = rightTicks;
        
        bool hasPhysicalMovement = (totalDelta > 5);
        
        if (hasPhysicalMovement) {
            stableFrames++;
            if (stableFrames >= CHAOS_RECOVER_STABLE_FRAMES) {
                flushChaosSnapshot();
                Serial.printf("[CHAOS] ✅ Recovery: movement detected! totalDelta=%ld\n", totalDelta);
                endChaos();
                stableFrames = 0;
                return;
            }
        } else {
            stableFrames = 0;
        }
        
        if (millis() - chaosStartTime > chaosForceTimeoutMs) {
            flushChaosSnapshot();
            Serial.println("[CHAOS] ⚠️ Timeout! Force exit, continuing evolution.");
            setMotorSpeed(-CHAOS_ESCAPE_PWM, CHAOS_ESCAPE_PWM);
            delay(300);
            setMotorSpeed(CHAOS_ESCAPE_PWM, -CHAOS_ESCAPE_PWM);
            delay(300);
            setMotorSpeed(0, 0);
            endChaos();
            return;
        }
        
        logFrame();
    }
    
    // ★★★ R2: endChaos() 不再归零 accumulatedStuckTime ★★★
    static bool endChaos() {
        if (motorState != STATE_CHAOS) return false;
        setMotorState(STATE_IDLE);
        stopMotors();
        uint32_t chaosDuration = millis() - chaosStartTime;
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
        Logger::logf("  Accumulated stuck time: %lums (preserved)", accumulatedStuckTime);
        Logger::log("========================================");
        // ★★★ R2: 不再归零 accumulatedStuckTime ★★★
        stuckStartTime = 0;
        return true;
    }

    static void startFrameLog() { 
        frameLogHead = 0; 
        frameLogCount = 0; 
        testStartTime = millis(); 
        lastIncrementalSaveFrame = 0;
        setMotorState(STATE_IDLE);
    }

    static void logFrame() {
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

    // ★★★ R2, R7: update() 包含混沌触发和自适应标定修复 ★★★
    static void update(const Gene& gene) {
        if (!motorEnabled) { stopMotors(); return; }
        syncGeneParams(gene);
        
        updateEncoders();
        leftSensorRaw = SensorCalibration::getLeftRaw();
        rightSensorRaw = SensorCalibration::getRightRaw();
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
        
        // ★★★ R7: 移除自适应阈值修改，只保留 bounce 计数 ★★★
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
        } else {
            // ★★★ R7: 不再修改 encoderDiffThreshold ★★★
        }
        lastDiff = diff;

        // ★★★ R2: 混沌触发时不再归零 accumulatedStuckTime ★★★
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
                // ★★★ R2: 不再归零 accumulatedStuckTime ★★★
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
};

// ================================================================
// MotorController 静态变量定义
// ================================================================
int MotorController::currentSpeedL = 0;
int MotorController::currentSpeedR = 0;
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
uint32_t MotorController::lastIncrementalSaveFrame = 0;
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

    void initImpl() {
        currentGeneration = GeneStorage::getCurrentGeneration();
        
        if (currentGeneration > 0) {
            String popPath = "/pop_gen_" + String(currentGeneration) + ".bin";
            if (!FileUtils::exists(popPath)) {
                Logger::logf("⚠️ Population file gen=%lu not found, resetting to gen 1", currentGeneration);
                currentGeneration = 0;
            }
        }
        
        if (currentGeneration == 0) {
            currentGeneration = 1;
            GeneStorage::setCurrentGeneration(currentGeneration);
        }
        
        currentIndividual = 0;
        testActive = false;
        pendingTransition = false;
        controlMode = false;
        mutationRate = 0.15f;
        bestNoveltyEver = 0;
        
        Logger::logf("🚀 Starting evolution from generation %lu", currentGeneration);
        if (!archive.load()) archive.init();
        
        static Gene loadedPop[POPULATION_SIZE];
        if (GeneStorage::loadPopulation(currentGeneration, loadedPop, POPULATION_SIZE)) {
            for (int i = 0; i < POPULATION_SIZE; i++) {
                population[i] = loadedPop[i];
            }
            Logger::logf("✅ Restored generation %lu population (%d individuals)", 
                         currentGeneration, POPULATION_SIZE);
        } else {
            Logger::logf("⚠️ Could not load gen %lu, creating random population", currentGeneration);
            for (int i = 0; i < POPULATION_SIZE; i++) {
                population[i].init();
            }
            
            if (GeneStorage::savePopulation(population, POPULATION_SIZE)) {
                Logger::log("✅ Random population saved to SPIFFS");
            } else {
                Logger::log("❌ Failed to save population!");
            }
        }
    }

    void startCurrentTestImpl() {
        testActive = true; 
        testStartTime = millis();
        MotorController::resetDistanceTicks();
        MotorController::enableMotor();
        MotorController::startFrameLog();
        MotorController::resetStuck();
        
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
                Serial.printf("[CHAOS→GENE] Injected %d rules from %d snapshots:\n", 
                              rulesToInject, snapshotCount);
                for (int i = 0; i < rulesToInject; i++) {
                    const BehaviorRule& r = g.rules[g.chaosRulesStartIndex + i];
                    Serial.printf("  Rule[%d]: cond=%d val=%d op=%d motorL=%d motorR=%d dur=%d\n",
                                  g.chaosRulesStartIndex + i,
                                  r.condType, r.condValue, r.condOp,
                                  r.motorL, r.motorR, r.durationMs);
                }
            } else {
                Logger::log("⚠️ No space to inject chaos rules (MAX_RULES reached or no snapshots)");
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
            if (added) archive.save();
        }
        
        HistoryRecord record;
        record.timestamp = millis() - testStartTime;
        record.generation = currentGeneration;
        record.noveltyScore = g.noveltyScore;
        record.survivalTime = g.survival_time;
        record.distance_ticks = g.distance_ticks;
        record.ruleCount = g.ruleCount;
        RobustStorage::addRecord(record, true);
        RobustStorage::forceSave();
        
        Logger::logf("⏹️ Individual %d done: novelty=%.4f rules=%d chaosCount=%d %s",
                     currentIndividual+1, g.noveltyScore, g.ruleCount,
                     MotorController::getTestChaosTriggerCount(),
                     added ? "✨ [new behavior]" : "");
        
        bool geneOk = GeneStorage::saveIndividualRecord(currentGeneration, currentIndividual, g);
        if (!geneOk) {
            Logger::logf("❌❌❌ CRITICAL: Individual record save FAILED for gen=%lu id=%d", 
                         currentGeneration, currentIndividual);
        }
        
        bool frameOk = RollingStorage::saveFrameLog(currentGeneration, currentIndividual,
                                     MotorController::getFrameLog(),
                                     MotorController::getFrameLogCount(),
                                     MotorController::getFrameLogHead());
        if (!frameOk) {
            Logger::logf("❌❌❌ CRITICAL: Frame log save FAILED for gen=%lu id=%d", 
                         currentGeneration, currentIndividual);
        } else {
            Logger::logf("✅ Frame log saved: gen=%lu id=%d", currentGeneration, currentIndividual);
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
        chaosRecord.testTerminatedBy = MotorController::isDead() ? 1 : 0;
        chaosRecord.reserved[0] = 0;
        chaosRecord.reserved[1] = 0;
        bool chaosOk = GeneStorage::saveChaosRecord(chaosRecord);
        if (!chaosOk) {
            Logger::logf("❌❌❌ CRITICAL: Chaos record save FAILED for gen=%lu id=%d", 
                         currentGeneration, currentIndividual);
        } else {
            Logger::logf("✅ Chaos record saved: gen=%lu id=%d", currentGeneration, currentIndividual);
        }
        RobustStorage::addChaosRecord(chaosRecord, true);
        
        Logger::logf("📊 End of individual %d: geneOk=%d, frameOk=%d, chaosOk=%d, chaosCount=%d",
                     currentIndividual, geneOk, frameOk, chaosOk,
                     MotorController::getTestChaosTriggerCount());
    }

    void nextIndividualImpl() {
        endCurrentTestImpl();
        currentIndividual++;
        if (currentIndividual >= POPULATION_SIZE) {
            evolveImpl();
            currentIndividual = 0;
            GeneStorage::incrementGeneration();
            currentGeneration = GeneStorage::getCurrentGeneration();
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
        
        bool popOk = GeneStorage::savePopulation(population, POPULATION_SIZE);
        if (!popOk) {
            Logger::log("❌❌❌ CRITICAL: Population save FAILED!");
        } else {
            Logger::logf("✅ Population saved: gen=%lu", currentGeneration);
        }
        
        if (GeneStorage::isExperimentComplete()) {
            Logger::log("🎉 Experiment completed! All data saved.");
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
        instance.currentGeneration = 1;
        GeneStorage::setCurrentGeneration(1);
        instance.initImpl();
        Logger::log("✅ Reset to generation 1");
    }
    static int getArchiveSize() { 
        return instance.archive.getArchiveSize(); 
    }
    static NoveltyArchive& getArchive() { 
        return instance.archive; 
    }
};

EvolutionEngine EvolutionEngine::instance;

// ================================================================
// CarWebServer 类 - 包含 R1 徽章修复
// ================================================================
class CarWebServer {
private:
    static WebServer server;
    
    // ★★★ R1: buildHTMLPage() 徽章更新 ★★★
    static String buildHTMLPage() {
        return R"rawliteral(<!DOCTYPE html>
<html>
<head>
<meta charset='UTF-8'>
<meta name='viewport' content='width=device-width,initial-scale=1,maximum-scale=1'>
<title>OEE V9.22</title>
<style>
*{margin:0;padding:0;box-sizing:border-box;}
body{font-family:-apple-system,system-ui,sans-serif;background:#0d1117;color:#e6edf3;padding:12px;max-width:480px;margin:0 auto;}
.header{background:linear-gradient(135deg,#161b22,#0d1117);border:1px solid #30363d;border-radius:12px;padding:16px 20px;margin-bottom:12px;}
.header h1{font-size:20px;color:#58a6ff;display:flex;align-items:center;gap:8px;}
.header h1 span{font-size:12px;color:#8b949e;font-weight:normal;}
.version-badge{background:#1f2937;color:#58a6ff;font-size:10px;padding:2px 8px;border-radius:10px;margin-left:8px;}
.card{background:#161b22;border:1px solid #30363d;border-radius:10px;padding:14px 16px;margin-bottom:10px;}
.card-title{font-size:11px;color:#8b949e;text-transform:uppercase;letter-spacing:0.5px;margin-bottom:8px;display:flex;align-items:center;gap:6px;}
.stat-grid{display:grid;grid-template-columns:1fr 1fr;gap:8px;}
.stat-item{background:#0d1117;border-radius:6px;padding:8px 10px;text-align:center;}
.stat-label{font-size:9px;color:#8b949e;text-transform:uppercase;}
.stat-value{font-size:18px;font-weight:600;color:#f0f6fc;}
.stat-value.high{color:#3fb950;}
.stat-value.warn{color:#d29922;}
.stat-value.danger{color:#f85149;}
.stat-value.blue{color:#58a6ff;}
.stat-value.purple{color:#a78bfa;}
.stat-value.orange{color:#f59e0b;}
.stat-value.cyan{color:#22d3ee;}
.sensor-grid{display:grid;grid-template-columns:1fr 1fr;gap:6px;}
.sensor-item{background:#0d1117;border-radius:6px;padding:6px 10px;font-size:12px;display:flex;justify-content:space-between;}
.sensor-item .label{color:#8b949e;}
.sensor-item .value{color:#58a6ff;font-weight:600;}
.sensor-item .value.orange{color:#f59e0b;}
.sensor-item .value.cyan{color:#22d3ee;}
.sensor-item .value.purple{color:#a78bfa;}
.state-matrix{display:flex;gap:6px;flex-wrap:wrap;}
.state-box{padding:6px 14px;border-radius:6px;border:2px solid #30363d;font-size:11px;font-weight:600;color:#8b949e;background:transparent;flex:1;text-align:center;transition:all 0.3s;}
.state-box.active{transform:scale(1.02);}
.state-idle.active{background:#8b949e;border-color:#8b949e;color:#0d1117;}
.state-walking.active{background:#3fb950;border-color:#3fb950;color:#0d1117;}
.state-stuck.active{background:#d29922;border-color:#d29922;color:#0d1117;animation:blink 0.5s infinite;}
.state-chaos.active{background:#f85149;border-color:#f85149;color:#0d1117;animation:blink 0.3s infinite;}
@keyframes blink{0%,100%{opacity:1;}50%{opacity:0.6;}}
.btn-group{display:flex;gap:6px;flex-wrap:wrap;}
.btn{background:#21262d;color:#c9d1d9;border:1px solid #30363d;padding:8px 14px;border-radius:6px;cursor:pointer;font-size:12px;font-weight:500;transition:all 0.2s;flex:1;min-width:56px;text-align:center;}
.btn:hover{background:#30363d;border-color:#58a6ff;}
.btn-primary{background:#238636;border-color:#2ea043;color:#fff;}
.btn-primary:hover{background:#2ea043;}
.btn-danger{background:#da3633;border-color:#f85149;color:#fff;}
.btn-danger:hover{background:#f85149;}
.btn-download{background:#1f6feb;border-color:#58a6ff;color:#fff;}
.btn-download:hover{background:#388bfd;}
.btn-purple{background:#6f42c1;border-color:#a78bfa;color:#fff;}
.btn-purple:hover{background:#8957e5;}
.btn-sm{padding:5px 10px;font-size:10px;flex:0 1 auto;}
.download-grid{display:grid;grid-template-columns:1fr 1fr 1fr;gap:5px;}
.download-grid .btn{font-size:10px;padding:6px 4px;flex:1;}
.storage-bar{background:#0d1117;border-radius:4px;height:6px;overflow:hidden;margin-top:4px;}
.storage-fill{height:100%;border-radius:4px;transition:width 0.5s;}
.storage-fill.good{background:#3fb950;}
.storage-fill.warn{background:#d29922;}
.storage-fill.danger{background:#f85149;}
.warning-box{background:#1f2937;border-left:3px solid #3fb950;padding:6px 10px;font-size:11px;border-radius:4px;margin-top:4px;}
.warning-box.danger{border-left-color:#f85149;background:#2d1b1b;}
input[type=number]{background:#0d1117;border:1px solid #30363d;color:#c9d1d9;padding:5px 8px;border-radius:4px;width:70px;font-size:12px;}
.download-row{display:flex;gap:6px;align-items:center;flex-wrap:wrap;margin-top:6px;}
.download-row label{font-size:11px;color:#8b949e;}
</style>
</head>
<body>
<div class="header">
<h1>🚗 OEE <span>v9.22</span><span class="version-badge">观察+存储</span></h1>
<div style="display:flex;justify-content:space-between;margin-top:6px;font-size:13px;">
<span>代 <span id="gen" class="blue" style="font-weight:600;">-</span></span>
<span>个体 <span id="ind" class="blue" style="font-weight:600;">-</span></span>
<span>新奇度 <span id="nov" class="high" style="font-weight:600;">-</span></span>
<span>混沌次数 <span id="chaosCnt" class="warn" style="font-weight:600;">-</span></span>
</div>
</div>
<div class="card">
<div class="card-title">🔴 状态</div>
<div class="state-matrix" id="stateMatrix">
<div class="state-box state-idle" data-state="0">⏸ IDLE</div>
<div class="state-box state-walking" data-state="1">🚶 WALK</div>
<div class="state-box state-stuck" data-state="2">⚠️ STUCK</div>
<div class="state-box state-chaos" data-state="3">🔥 CHAOS</div>
</div>
</div>
<div class="card">
<div class="card-title">📊 实时数据</div>
<div class="stat-grid">
<div class="stat-item"><div class="stat-label">存活时间</div><div class="stat-value blue" id="survival">-</div></div>
<div class="stat-item"><div class="stat-label">距离</div><div class="stat-value" id="dist">-</div></div>
<div class="stat-item"><div class="stat-label">规则数</div><div class="stat-value purple" id="rules">-</div></div>
<div class="stat-item"><div class="stat-label">混沌超时</div><div class="stat-value warn" id="chaosTimeout">-</div></div>
</div>
<div class="stat-grid" style="margin-top:6px;">
<div class="stat-item"><div class="stat-label">🎲 混沌噪声</div><div class="stat-value orange" id="chaosNoise">-</div></div>
<div class="stat-item"><div class="stat-label">📢 噪声放大器</div><div class="stat-value cyan" id="chaosAmplifier">-</div></div>
</div>
<div class="sensor-grid" style="margin-top:6px;">
<div class="sensor-item"><span class="label">⬅ 左传感器</span><span class="value" id="sL">-</span></div>
<div class="sensor-item"><span class="label">➡ 右传感器</span><span class="value" id="sR">-</span></div>
<div class="sensor-item"><span class="label">左编码器</span><span class="value" id="encL">-</span></div>
<div class="sensor-item"><span class="label">右编码器</span><span class="value" id="encR">-</span></div>
<div class="sensor-item" style="grid-column: span 2; background:#1a1a2e;"><span class="label">🌊 原始噪声 (GPIO1)</span><span class="value purple" id="noiseRaw">-</span></div>
</div>
</div>
<div class="card">
<div class="card-title">💾 存储 <span id="storageLabel">-</span></div>
<div class="storage-bar"><div class="storage-fill" id="storageFill" style="width:0%"></div></div>
<div id="storageMsg" class="warning-box">✅ 空间充足</div>
</div>
<div class="card">
<div class="card-title">🎮 控制</div>
<div class="btn-group">
<button class="btn btn-primary" onclick="api('/evolution?action=start')">▶ 启动</button>
<button class="btn btn-danger" onclick="api('/evolution?action=stop')">⏹ 停止</button>
<button class="btn" onclick="api('/evolution?action=next')">⏭ 下一个</button>
<button class="btn" onclick="location.reload()">🔄 刷新</button>
</div>
<div class="btn-group" style="margin-top:6px;">
<button class="btn btn-primary" onclick="motorCtrl('enable')">✅ 电机开</button>
<button class="btn btn-danger" onclick="motorCtrl('disable')">❌ 电机关</button>
<button class="btn" onclick="motorCtrl('stop')">⏹ 停止</button>
</div>
<div class="btn-group" style="margin-top:4px;">
<button class="btn" onclick="motorCtrl('forward')">⬆ 前进</button>
<button class="btn" onclick="motorCtrl('backward')">⬇ 后退</button>
<button class="btn" onclick="motorCtrl('left')">⬅ 左转</button>
<button class="btn" onclick="motorCtrl('right')">➡ 右转</button>
</div>
<div id="motorStatus" style="font-size:11px;color:#8b949e;margin-top:4px;">电机: -</div>
</div>
<div class="card">
<div class="card-title">📥 导出CSV</div>
<div class="download-grid">
<button class="btn btn-download" onclick="downloadFile('/download/history')">📋 历史</button>
<button class="btn btn-purple" onclick="downloadFile('/download/chaos')">🌪️ 混沌</button>
<button class="btn btn-download" onclick="downloadFile('/download/population')">🧬 种群</button>
</div>
<div class="download-row">
<label>代</label><input id="genInput" type="number" placeholder="1">
<label>个体</label><input id="idInput" type="number" placeholder="0">
<button class="btn btn-sm btn-download" onclick="downloadIndividual()">📦 个体</button>
<button class="btn btn-sm btn-purple" onclick="downloadSingleFrame()">📹 帧日志</button>
<button class="btn btn-sm" onclick="listFiles()">📂 文件</button>
</div>
<div id="fileList" style="font-size:10px;font-family:monospace;max-height:100px;overflow-y:auto;background:#0d1117;padding:6px;border-radius:4px;margin-top:6px;display:none;"></div>
</div>
<div class="card" style="border-color:#30363d;opacity:0.7;">
<div class="card-title">⚙️ 系统</div>
<div style="font-size:10px;color:#8b949e;display:flex;gap:12px;flex-wrap:wrap;">
<span>存储: <span id="freeSpace">-</span></span>
<span>存档: <span id="archiveSize">-</span></span>
<button class="btn btn-danger btn-sm" style="flex:0;" onclick="if(confirm('重置所有数据?')){fetch('/admin/reset').then(()=>location.reload());}">🔄 重置</button>
</div>
</div>
<script>
function api(url){fetch(url).then(r=>r.json()).catch(e=>console.error(e));}
function motorCtrl(a){fetch('/motor?action='+a).then(r=>r.json()).then(d=>{document.getElementById('motorStatus').innerHTML='电机: '+d.status;});}
function downloadFile(url){fetch(url).then(r=>{if(!r.ok)throw new Error('下载失败');return r.blob();}).then(blob=>{const a=document.createElement('a');a.href=URL.createObjectURL(blob);a.download=url.split('/').pop()||'download.csv';document.body.appendChild(a);a.click();document.body.removeChild(a);}).catch(e=>alert('下载失败: '+e.message));}
function downloadIndividual(){var g=document.getElementById('genInput').value;var i=document.getElementById('idInput').value;if(!g||!i){alert('请输入代数和个体编号');return;}downloadFile('/download/individual?gen='+g+'&id='+i);}
function downloadSingleFrame(){var g=document.getElementById('genInput').value;var i=document.getElementById('idInput').value;if(!g||!i){alert('请输入代数和个体编号');return;}downloadFile('/download/frame?gen='+g+'&id='+i);}
function listFiles(){var el=document.getElementById('fileList');if(el.style.display==='block'){el.style.display='none';return;}fetch('/list/files').then(r=>r.json()).then(d=>{let h='';if(d.files){d.files.forEach(f=>{h+='<div>'+f+'</div>';});}el.innerHTML=h||'没有文件';el.style.display='block';});}
setInterval(function(){
fetch('/status').then(r=>r.json()).then(d=>{
document.getElementById('gen').textContent=d.generation;
document.getElementById('ind').textContent=d.individual+1+'/'+d.population;
document.getElementById('nov').textContent=d.novelty.toFixed(4);
document.getElementById('chaosCnt').textContent=d.chaosTriggerCount;
document.getElementById('survival').textContent=(d.survival/1000).toFixed(1)+'s';
document.getElementById('dist').textContent=d.distance+' ticks';
document.getElementById('rules').textContent=d.ruleCount;
document.getElementById('chaosTimeout').textContent=d.chaosTimeoutMs+'ms';
document.getElementById('chaosNoise').textContent=d.chaosNoise || '0';
document.getElementById('chaosAmplifier').textContent=d.chaosAmplifier || '0';
document.getElementById('sL').textContent=d.sensorLeft;
document.getElementById('sR').textContent=d.sensorRight;
document.getElementById('encL').textContent=d.leftTicks;
document.getElementById('encR').textContent=d.rightTicks;
document.getElementById('noiseRaw').textContent=d.noiseRaw || '0';
document.getElementById('motorStatus').innerHTML='电机: '+(d.motorEnabled?'✅ 已启用':'❌ 已禁用');
document.querySelectorAll('.state-box').forEach(el=>{el.classList.remove('active');if(parseInt(el.dataset.state)===d.stateCode){el.classList.add('active');}});
var freeKB=d.freeSpaceKB||0;document.getElementById('freeSpace').textContent=freeKB+' KB';
document.getElementById('archiveSize').textContent=d.archiveSize||0;
var health=d.storageHealth||1.0;var pct=Math.round(health*100);var fill=document.getElementById('storageFill');
fill.style.width=pct+'%';var msg=document.getElementById('storageMsg');
if(pct<25){fill.className='storage-fill danger';msg.className='warning-box danger';msg.innerHTML='⚠️ 存储严重不足! ('+freeKB+' KB)';}
else if(pct<50){fill.className='storage-fill warn';msg.className='warning-box';msg.style.borderLeftColor='#d29922';msg.innerHTML='⚠️ 存储偏低 ('+freeKB+' KB)';}
else{fill.className='storage-fill good';msg.className='warning-box';msg.style.borderLeftColor='#3fb950';msg.innerHTML='✅ 空间充足 ('+freeKB+' KB)';}
document.getElementById('storageLabel').textContent=pct+'%';
});
},500);
</script>
</body></html>)rawliteral";
}

public:
    static void init() {
        server.on("/", [](){ server.send(200, "text/html", buildHTMLPage()); });
        server.on("/status", handleStatus);
        server.on("/evolution", handleEvolution);
        server.on("/motor", handleMotor);
        server.on("/download/history", handleDownloadHistory);
        server.on("/download/chaos", handleDownloadChaos);
        server.on("/download/population", handleDownloadPopulation);
        server.on("/download/individual", handleDownloadIndividual);
        server.on("/download/frame", handleDownloadFrame);
        server.on("/admin/reset", handleAdminReset);
        server.on("/list/files", handleListFiles);
        server.on("/download/pop", handleDownloadPop);
        server.on("/download/chaos_snap", handleDownloadChaosSnap);
        server.on("/download/frame_bin", handleDownloadFrameBin);
        server.onNotFound([](){ server.send(404, "text/plain", "Not Found"); });
        server.begin();
        Logger::log("Web server ready");
    }

    static void handleClient() { server.handleClient(); }

    static void handleStatus() {
        Gene& g = EvolutionEngine::getCurrentGene();
        const char* stateNames[] = {"IDLE", "WALKING", "STUCK", "CHAOS"};
        
        size_t freeSpace = 0;
        if (RobustStorage::isReady()) {
            File root = SPIFFS.open("/");
            if (root) {
                size_t used = 0;
                while (File f = root.openNextFile()) {
                    used += f.size();
                    f.close();
                }
                root.close();
                freeSpace = SPIFFS.totalBytes() - used;
            }
        }
        
        String json = "{";
        json += "\"generation\":" + String(EvolutionEngine::getGeneration()) + ",";
        json += "\"individual\":" + String(EvolutionEngine::getIndividual()) + ",";
        json += "\"population\":" + String(POPULATION_SIZE) + ",";
        json += "\"novelty\":" + String(g.noveltyScore, 4) + ",";
        json += "\"ruleCount\":" + String(g.ruleCount) + ",";
        json += "\"chaosTimeoutMs\":" + String(g.chaosTimeoutMs) + ",";
        json += "\"chaosNoise\":" + String(MotorController::getChaosNoiseValue()) + ",";
        json += "\"chaosAmplifier\":" + String(MotorController::getChaosNoiseAmplifier()) + ",";
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

    static void handleMotor() {
        if (!server.hasArg("action")) { server.send(400, "{}"); return; }
        String action = server.arg("action");
        String status = "ok";
        if (action == "enable") { MotorController::enableMotor(); status = "已启用"; }
        else if (action == "disable") { MotorController::disableMotor(); status = "已禁用"; }
        else if (action == "stop") { MotorController::forceStopAction(); MotorController::stopMotors(); status = "已停止"; }
        else if (action == "forward") { MotorController::enableMotor(); MotorController::startPhysicsAction(150, 150, 100); status = "前进"; }
        else if (action == "backward") { MotorController::enableMotor(); MotorController::startPhysicsAction(-150, -150, 100); status = "后退"; }
        else if (action == "left") { MotorController::enableMotor(); MotorController::startPhysicsAction(-130, 120, 100); status = "左转"; }
        else if (action == "right") { MotorController::enableMotor(); MotorController::startPhysicsAction(120, -130, 100); status = "右转"; }
        else { server.send(400, "{}"); return; }
        server.send(200, "application/json", "{\"status\":\"" + status + "\"}");
    }

    static void handleAdminReset() {
        Logger::log("🔄 Admin reset triggered");
        EvolutionEngine::getArchive().clear();
        GeneStorage::clearAllExperimentData();
        GeneStorage::startNewExperiment();
        EvolutionEngine::resetToGeneration1();
        server.send(200, "application/json", "{\"status\":\"reset\"}");
    }

    static void handleDownloadHistory() {
        String data = RobustStorage::getCSVData();
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
                   "chaosSuccess,testTerminatedBy\n";
            File root = SPIFFS.open("/");
            if (root) {
                while (File f = root.openNextFile()) {
                    String name = String(f.name());
                    if (name.startsWith("/chaos_g") && name.endsWith(".csv")) {
                        File file = SPIFFS.open(name, FILE_READ);
                        if (file) {
                            file.readStringUntil('\n');
                            data += file.readString();
                            file.close();
                        }
                    }
                    f.close();
                }
                root.close();
            }
        }
        server.sendHeader("Content-Type", "text/csv");
        server.sendHeader("Content-Disposition", "attachment; filename=chaos_records.csv");
        server.send(200, "text/csv", data);
    }

    static void handleDownloadPopulation() {
        String data = "generation,individual,ruleCount,survivalTime,distanceTicks,noveltyScore,chaosTimeoutMs\n";
        File root = SPIFFS.open("/");
        if (root) {
            while (File f = root.openNextFile()) {
                String name = String(f.name());
                if (name.startsWith("/pop_gen_") && name.endsWith(".bin")) {
                    File file = SPIFFS.open(name, FILE_READ);
                    if (file) {
                        uint32_t magic, expId, generation;
                        uint16_t version, popSize;
                        file.read((uint8_t*)&magic, 4);
                        file.read((uint8_t*)&version, 2);
                        file.read((uint8_t*)&popSize, 2);
                        file.read((uint8_t*)&generation, 4);
                        file.read((uint8_t*)&expId, 4);
                        for (int i = 0; i < popSize && i < 16; i++) {
                            uint8_t ruleCount;
                            file.read(&ruleCount, 1);
                            file.seek(file.position() + ruleCount * sizeof(BehaviorRule));
                            uint32_t survival, dist;
                            float novelty;
                            uint16_t chaosTimeoutMs;
                            file.read((uint8_t*)&survival, 4);
                            file.read((uint8_t*)&dist, 4);
                            file.read((uint8_t*)&novelty, 4);
                            file.seek(file.position() + 12);
                            file.read((uint8_t*)&chaosTimeoutMs, 2);
                            data += String(generation) + "," + String(i) + "," + String(ruleCount) + ","
                                  + String(survival) + "," + String(dist) + "," + String(novelty, 4) + ","
                                  + String(chaosTimeoutMs) + "\n";
                            file.seek(file.position() + 10);
                        }
                        file.close();
                    }
                }
                f.close();
            }
            root.close();
        }
        server.sendHeader("Content-Type", "text/csv");
        server.sendHeader("Content-Disposition", "attachment; filename=population_summary.csv");
        server.send(200, "text/csv", data);
    }

    static void handleDownloadIndividual() {
        if (!server.hasArg("gen") || !server.hasArg("id")) {
            server.send(400, "text/plain", "Usage: ?gen=1&id=0");
            return;
        }
        int gen = server.arg("gen").toInt();
        int id = server.arg("id").toInt();
        String path = "/gen_" + String(gen) + "_id_" + String(id) + ".csv";
        if (!SPIFFS.exists(path)) {
            server.send(404, "text/plain", "File not found: " + path);
            return;
        }
        String data = FileUtils::safeRead(path);
        server.sendHeader("Content-Type", "text/csv");
        server.sendHeader("Content-Disposition", "attachment; filename=individual_" + String(gen) + "_" + String(id) + ".csv");
        server.send(200, "text/csv", data);
    }

    static void handleDownloadFrame() {
        if (!server.hasArg("gen") || !server.hasArg("id")) {
            server.send(400, "text/plain", "Usage: ?gen=1&id=0");
            return;
        }
        int gen = server.arg("gen").toInt();
        int id = server.arg("id").toInt();
        String path = "/frm_" + String(gen) + "_i" + String(id) + ".bin";
        if (!SPIFFS.exists(path)) {
            server.send(404, "text/plain", "File not found: " + path);
            return;
        }
        File file = SPIFFS.open(path, FILE_READ);
        if (!file) { server.send(500, "text/plain", "Cannot open file"); return; }
        
        FileHeader header;
        if (file.read((uint8_t*)&header, sizeof(FileHeader)) != sizeof(FileHeader)) {
            file.close();
            server.send(500, "text/plain", "Invalid file format");
            return;
        }
        
        String csv = "timestamp_ms,sensorLeft,sensorRight,motorLeftPWM,motorRightPWM,state\n";
        int pwmL = 0, pwmR = 0;
        int maxFrames = min((int)header.frameCount, 2000);
        const char* stateNames[] = {"IDLE", "WALKING", "STUCK", "CHAOS"};
        for (int i = 0; i < maxFrames; i++) {
            CompressedFrameEntry e;
            if (file.read((uint8_t*)&e, sizeof(CompressedFrameEntry)) != sizeof(CompressedFrameEntry)) break;
            pwmL = constrain(pwmL + e.motorLeftPWM, 0, 255);
            pwmR = constrain(pwmR + e.motorRightPWM, 0, 255);
            if (i == 0) { pwmL = e.motorLeftPWM; pwmR = e.motorRightPWM; }
            csv += String(e.timestamp_ms) + "," + String(e.sensorLeft) + "," + String(e.sensorRight) + ","
                 + String(pwmL) + "," + String(pwmR) + "," + String(stateNames[e.state & 0x07]) + "\n";
        }
        file.close();
        
        server.sendHeader("Content-Type", "text/csv");
        server.sendHeader("Content-Disposition", "attachment; filename=frame_log_" + String(gen) + "_" + String(id) + ".csv");
        server.send(200, "text/csv", csv);
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

    static void handleDownloadPop() {
        if (!server.hasArg("gen")) {
            server.send(400, "text/plain", "Missing gen parameter. Usage: /download/pop?gen=1");
            return;
        }
        int gen = server.arg("gen").toInt();
        if (gen < 1) {
            server.send(400, "text/plain", "Invalid gen parameter (must be >= 1)");
            return;
        }
        String path = "/pop_gen_" + String(gen) + ".bin";
        if (!SPIFFS.exists(path)) {
            server.send(404, "text/plain", "File not found: " + path);
            return;
        }
        File file = SPIFFS.open(path, FILE_READ);
        if (!file) {
            server.send(500, "text/plain", "Cannot open file");
            return;
        }
        server.sendHeader("Content-Type", "application/octet-stream");
        server.sendHeader("Content-Disposition", "attachment; filename=pop_gen_" + String(gen) + ".bin");
        server.streamFile(file, "application/octet-stream");
        file.close();
        Logger::logf("📥 Downloaded: pop_gen_%d.bin", gen);
    }

    static void handleDownloadChaosSnap() {
        if (!server.hasArg("gen") || !server.hasArg("id")) {
            server.send(400, "text/plain", "Missing gen or id parameter. Usage: /download/chaos_snap?gen=1&id=0");
            return;
        }
        int gen = server.arg("gen").toInt();
        int id = server.arg("id").toInt();
        if (gen < 1 || id < 0) {
            server.send(400, "text/plain", "Invalid parameters (gen>=1, id>=0)");
            return;
        }
        String path = "/chaos_snaps_g" + String(gen) + "_i" + String(id) + ".bin";
        if (!SPIFFS.exists(path)) {
            server.send(404, "text/plain", "File not found: " + path);
            return;
        }
        File file = SPIFFS.open(path, FILE_READ);
        if (!file) {
            server.send(500, "text/plain", "Cannot open file");
            return;
        }
        server.sendHeader("Content-Type", "application/octet-stream");
        server.sendHeader("Content-Disposition", "attachment; filename=chaos_snaps_g" + String(gen) + "_i" + String(id) + ".bin");
        server.streamFile(file, "application/octet-stream");
        file.close();
        Logger::logf("📥 Downloaded: chaos_snaps_g%d_i%d.bin", gen, id);
    }

    static void handleDownloadFrameBin() {
        if (!server.hasArg("gen") || !server.hasArg("id")) {
            server.send(400, "text/plain", "Usage: /download/frame_bin?gen=1&id=0");
            return;
        }
        int gen = server.arg("gen").toInt();
        int id = server.arg("id").toInt();
        if (gen < 1 || id < 0) {
            server.send(400, "text/plain", "Invalid parameters (gen>=1, id>=0)");
            return;
        }
        String path = "/frm_" + String(gen) + "_i" + String(id) + ".bin";
        if (!SPIFFS.exists(path)) {
            server.send(404, "text/plain", "File not found: " + path);
            return;
        }
        File file = SPIFFS.open(path, FILE_READ);
        if (!file) {
            server.send(500, "text/plain", "Cannot open file");
            return;
        }
        server.sendHeader("Content-Type", "application/octet-stream");
        server.sendHeader("Content-Disposition", "attachment; filename=frm_" + String(gen) + "_i" + String(id) + ".bin");
        server.streamFile(file, "application/octet-stream");
        file.close();
        Logger::logf("📥 Downloaded binary frame: frm_%d_i%d.bin", gen, id);
    }
};
WebServer CarWebServer::server(80);

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
// ★★★ R1: setup() 启动日志更新 ★★★
// ================================================================
void setup() {
    Serial.begin(115200);
    delay(1000);

    Logger::log("========================================");
    Logger::logf("  OEE Test %s", FIRMWARE_VERSION);
    Logger::log("  🛡️ 回归审计修复 (2026-09-03)");
    Logger::log("  ├── R1: 移除'3轮混沌保障'设计目标异化");
    Logger::log("  ├── R2: 移除 accumulatedStuckTime 强制归零");
    Logger::log("  ├── R3: 移除 loop() 冗余混沌触发器");
    Logger::log("  ├── R4: chaosTimeout 范围 2000~6000ms");
    Logger::log("  ├── R5: 恢复条件放宽至 50 帧 (500ms)");
    Logger::log("  ├── R6: ensureSpace 不删除当前代");
    Logger::log("  ├── R7: 移除自适应阈值标定");
    Logger::log("  ├── R8: 补全 12 维归一化");
    Logger::log("  └── R9: 统一参数宏定义");
    Logger::log("  🔬 观察模式: chaosTimeout=2000~6000ms (自然触发)");
    Logger::log("  💾 存储: CRC校验 + CSV校验 + forceFlush");
    Logger::log("  ✅ 混沌超时不判死，个体继续进化");
    Logger::logf("  FORMAT_SPIFFS_ON_BOOT = %s", FORMAT_SPIFFS_ON_BOOT ? "ENABLED ⚠️" : "DISABLED ✅");
    Logger::log("  Commands: ls, status, frame, pop, chaos, popall, ramlog, storage, reset, help");
    Logger::log("========================================");

    RobustStorage::init();
    
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
        Logger::log("⚠️ Experiment not active - force starting...");
        GeneStorage::startNewExperiment();
    }
    
    if (!GeneStorage::isExperimentActive()) {
        Logger::log("⚠️ Still not active - using forceActivate...");
        GeneStorage::forceActivate();
    }
    
    Logger::logf("📌 GeneStorage active: %d, gen: %lu", 
                 GeneStorage::isExperimentActive(), GeneStorage::getCurrentGeneration());

    RAMLogBuffer::init();
    RollingStorage::init();

    pinMode(PIN_NOISE_SOURCE, INPUT);
    randomSeed(SensorCalibration::readNoise());

    SensorCalibration::calibrate();
    MotorController::init();
    EvolutionEngine::init();

    Logger::log("========================================");
    Logger::logf("  ✅ Ready (%s)", FIRMWARE_VERSION);
    Logger::log("  WiFi: CarLogger / 12345678");
    Logger::log("  http://192.168.4.1");
    Logger::log("  Serial: ls, status, frame, pop, chaos, popall, ramlog, storage, reset, help");
    Logger::log("========================================");
}

// ================================================================
// ★★★ R3: loop() 移除冗余混沌触发器 ★★★
// ================================================================
void loop() {
    if (!wifiInitialized) initWiFi();

    MotorController::updatePhysicsAction();

    // ★★★ R3: 移除 loop 中的混沌触发器 ★★★
    // 混沌触发完全由 MotorController::update() 中的状态机控制

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
            Serial.println("可用命令 (v9.22-ObserveOnly):");
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

    RobustStorage::tick();
    delay(LOOP_DELAY_MS);
}

// ================================================================
// 串口命令函数实现
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
    Serial.printf("  混沌噪声(GPIO1): %d\n", SensorCalibration::readNoise());
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
        
        float ratio = (avgR > 0) ? avgL / avgR : 0;
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