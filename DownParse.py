#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
================================================================
OEE 数据下载与转换工具 - 完整版 v3.0
仿草履虫应激机制 - 脱困能力进化系统 数据导出工具

对应固件: OEETestFinal_v9919_Final_v6Chaos.ino (v9.19-Final-v5)
================================================================

【功能】
1. 下载所有 SPIFFS 数据 (bin/csv/mrk 文件)
2. 将二进制文件转换为可读 CSV
3. 生成数据汇总报告

【数据结构 (与 Arduino 代码同步)】
┌────────────────────────────────────────────────────────────────────┐
│ 1. 帧日志: /frm_<gen>_i<id>.bin                                    │
│    文件头: FileHeader (24 bytes)                                   │
│    数据:   CompressedFrameEntry (12 bytes/帧)                      │
│    Magic:  0x47454E45 ('GENE')                                    │
├────────────────────────────────────────────────────────────────────┤
│ 2. 混沌快照: /chaos_snaps_g<gen>_i<id>.bin                        │
│    文件头: ChaosSnapshotHeader (16 bytes) [R4修复]                 │
│    数据:   ChaosSnapshotEntry (12 bytes/快照)                      │
│    Magic:  0x4348534E ('CHSN')                                    │
├────────────────────────────────────────────────────────────────────┤
│ 3. 种群文件: /pop_gen_<gen>.bin                                    │
│    文件头: magic(4) + version(2) + popSize(2) + generation(4)      │
│    数据:   Gene 结构体数组                                         │
│    Magic:  0x47454E45 ('GENE')                                    │
├────────────────────────────────────────────────────────────────────┤
│ 4. 新颖度存档: /novelty_archive.bin                                │
│    文件头: magic(4) + version(2) + count(2) + crc32(4)            │
│    Magic:  0x41524348 ('ARCH')                                    │
├────────────────────────────────────────────────────────────────────┤
│ 5. 个体CSV: /gen_<gen>_id_<id>.csv                                │
│    格式: 元数据行 + 规则表 (含 isChaosRule 列)                    │
├────────────────────────────────────────────────────────────────────┤
│ 6. 历史CSV: /oe_history.csv                                        │
│    格式: timestamp,generation,noveltyScore,survivalTime,          │
│           distance_ticks,ruleCount                                 │
└────────────────────────────────────────────────────────────────────┘

【用法】
    python oee_downloader.py --ip 192.168.4.1 --output ./oee_data

【依赖】
    pip install requests
================================================================
"""

#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
================================================================
OEE 数据下载与转换工具 - 完整版 v3.1
仿草履虫应激机制 - 脱困能力进化系统 数据导出工具

对应固件: OEETestFinal_v9919_Final_v7.ino (v9.21-ChaosTuned+Storage)
================================================================

【功能】
1. 下载所有 SPIFFS 数据 (bin/csv/mrk 文件)
2. 将二进制文件转换为可读 CSV
3. 生成数据汇总报告

【数据结构 (与 Arduino 代码同步)】
┌────────────────────────────────────────────────────────────────────┐
│ 1. 帧日志: /frm_<gen>_i<id>.bin                                    │
│    文件头: FileHeader (24 bytes)                                   │
│    数据:   CompressedFrameEntry (12 bytes/帧)                      │
│    Magic:  0x47454E45 ('GENE')                                    │
├────────────────────────────────────────────────────────────────────┤
│ 2. 混沌快照: /chaos_snaps_g<gen>_i<id>.bin                        │
│    文件头: ChaosSnapshotHeader (16 bytes) [R4修复]                 │
│    数据:   ChaosSnapshotEntry (12 bytes/快照)                      │
│    Magic:  0x4348534E ('CHSN')                                    │
├────────────────────────────────────────────────────────────────────┤
│ 3. 种群文件: /pop_gen_<gen>.bin                                    │
│    文件头: magic(4) + version(2) + popSize(2) + generation(4)      │
│    数据:   Gene 结构体数组                                         │
│    Magic:  0x47454E45 ('GENE')                                    │
├────────────────────────────────────────────────────────────────────┤
│ 4. 新颖度存档: /novelty_archive.bin                                │
│    文件头: magic(4) + version(2) + count(2) + crc32(4)            │
│    Magic:  0x41524348 ('ARCH')                                    │
├────────────────────────────────────────────────────────────────────┤
│ 5. 个体CSV: /gen_<gen>_id_<id>.csv                                │
│    格式: 元数据行 + 规则表 (含 isChaosRule 列)                    │
├────────────────────────────────────────────────────────────────────┤
│ 6. 历史CSV: /oe_history.csv                                        │
│    格式: timestamp,generation,noveltyScore,survivalTime,          │
│           distance_ticks,ruleCount                                 │
└────────────────────────────────────────────────────────────────────┘

【用法】
    python oee_downloader.py --ip 192.168.4.1 --output ./oee_data

【依赖】
    pip install requests
================================================================
"""

#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
OEE 数据下载与转换工具 v3.2 - 修复版
"""

#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
OEE 数据下载与转换工具 v3.2 - 修复版
修复: struct.unpack 格式错误导致 28/24 字节不匹配
"""

#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
================================================================
OEE 数据下载与转换工具 v4.0 - 适配 v9.21 固件
仿草履虫应激机制 - 脱困能力进化系统 数据导出工具

对应固件: OEETestFinal_v9919_Final_v7.ino (v9.21-ChaosTuned+Storage)
================================================================

【功能】
1. 下载所有 SPIFFS 数据 (bin/csv/mrk 文件)
2. 将二进制文件转换为可读 CSV
3. 生成数据汇总报告

【数据结构 (与 Arduino 代码同步)】
┌────────────────────────────────────────────────────────────────────┐
│ 1. 帧日志: /frm_<gen>_i<id>.bin                                    │
│    文件头: FileHeader (24 bytes)                                   │
│    数据:   CompressedFrameEntry (12 bytes/帧)                      │
│    Magic:  0x47454E45 ('GENE')                                    │
│    端点:   /download/frame (CSV) 或 /download/frame_bin (BIN)     │
├────────────────────────────────────────────────────────────────────┤
│ 2. 混沌快照: /chaos_snaps_g<gen>_i<id>.bin                        │
│    文件头: ChaosSnapshotHeader (16 bytes)                          │
│    数据:   ChaosSnapshotEntry (12 bytes/快照)                      │
│    Magic:  0x4348534E ('CHSN')                                    │
│    端点:   /download/chaos_snap?gen=X&id=Y                        │
├────────────────────────────────────────────────────────────────────┤
│ 3. 种群文件: /pop_gen_<gen>.bin                                    │
│    文件头: magic(4) + version(2) + popSize(2) + generation(4)      │
│    数据:   Gene 结构体数组                                         │
│    Magic:  0x47454E45 ('GENE')                                    │
│    端点:   /download/pop?gen=X                                    │
├────────────────────────────────────────────────────────────────────┤
│ 4. 新颖度存档: /novelty_archive.bin                                │
│    文件头: magic(4) + version(2) + count(2) + crc32(4)            │
│    Magic:  0x41524348 ('ARCH')                                    │
├────────────────────────────────────────────────────────────────────┤
│ 5. 个体CSV: /gen_<gen>_id_<id>.csv                                │
│    格式: 元数据行 + 规则表 (含 isChaosRule 列)                    │
│    端点:   /download/individual?gen=X&id=Y                        │
├────────────────────────────────────────────────────────────────────┤
│ 6. 历史CSV: /oe_history.csv                                        │
│    端点:   /download/history                                       │
└────────────────────────────────────────────────────────────────────┘

【用法】
    python oee_downloader.py --ip 192.168.4.1 --output ./oee_data

【依赖】
    pip install requests

【v4.0 更新说明】
    - 新增 /download/frame_bin 端点支持 (二进制帧日志下载)
    - 新增 /download/pop 端点支持 (种群二进制下载)
    - 新增 /download/chaos_snap 端点支持 (混沌快照二进制下载)
    - 修复帧日志魔数不匹配问题 (区分 CSV 和 BIN 下载)
================================================================
"""

#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
================================================================
OEE 数据下载与转换工具 v4.0 - 新固件兼容版
仿草履虫应激机制 - 脱困能力进化系统 数据导出工具

对应固件: OEETestFinal_v9919_Final_v71_RobustFixed.ino (v9.21-RobustFixed)
================================================================

【功能】
1. 下载所有 SPIFFS 数据 (bin/csv/mrk 文件)
2. 将二进制文件转换为可读 CSV
3. 生成数据汇总报告

【数据结构 (与 Arduino 代码同步)】
┌────────────────────────────────────────────────────────────────────┐
│ 1. 帧日志: /frm_<gen>_i<id>.bin                                    │
│    下载: /download/frame_bin?gen=X&id=Y                           │
│    文件头: FileHeader (24 bytes)                                   │
│    数据:   CompressedFrameEntry (12 bytes/帧)                      │
│    Magic:  0x47454E45 ('GENE')                                    │
├────────────────────────────────────────────────────────────────────┤
│ 2. 混沌快照: /chaos_snaps_g<gen>_i<id>.bin                        │
│    下载: /download/chaos_snap?gen=X&id=Y                          │
│    文件头: ChaosSnapshotHeader (16 bytes) [R4修复]                 │
│    数据:   ChaosSnapshotEntry (12 bytes/快照)                      │
│    Magic:  0x4348534E ('CHSN')                                    │
├────────────────────────────────────────────────────────────────────┤
│ 3. 种群文件: /pop_gen_<gen>.bin                                    │
│    下载: /download/pop?gen=X                                      │
│    文件头: magic(4) + version(2) + popSize(2) + generation(4) +  │
│            experimentId(4)                                        │
│    数据:   Gene 结构体数组 (使用 serializeIndividual 序列化)       │
│    Magic:  0x47454E45 ('GENE')                                    │
├────────────────────────────────────────────────────────────────────┤
│ 4. 新颖度存档: /novelty_archive.bin                                │
│    下载: /download/novelty_archive                                │
│    文件头: magic(4) + version(2) + count(2) + crc32(4)            │
│    Magic:  0x41524348 ('ARCH')                                    │
├────────────────────────────────────────────────────────────────────┤
│ 5. 个体CSV: /gen_<gen>_id_<id>.csv                                │
│    下载: /download/individual?gen=X&id=Y                          │
│    格式: 元数据行 + 规则表 (含 isChaosRule 列)                    │
├────────────────────────────────────────────────────────────────────┤
│ 6. 历史CSV: /oe_history.csv                                        │
│    下载: /download/history                                         │
│    格式: timestamp,generation,noveltyScore,survivalTime,          │
│           distance_ticks,ruleCount                                 │
├────────────────────────────────────────────────────────────────────┤
│ 7. 混沌历史CSV: /chaos_history.csv                                 │
│    下载: /download/chaos                                           │
└────────────────────────────────────────────────────────────────────┘

【用法】
    python oee_downloader_v4.py --ip 192.168.4.1 --output ./oee_data

【依赖】
    pip install requests
================================================================
"""

import os
import sys
import struct
import argparse
import requests
import json
import re
from pathlib import Path
from datetime import datetime
from typing import List, Dict, Tuple, Optional

# ================================================================
# 常量定义 - 与固件结构体严格对齐
# ================================================================

# Magic Numbers
MAGIC_FILE_HEADER = 0x47454E45      # 'GENE'
MAGIC_CHAOS_SNAPSHOT = 0x4348534E   # 'CHSN'
MAGIC_NOVELTY_ARCHIVE = 0x41524348  # 'ARCH'

# 结构体大小 (bytes)
SIZE_FILE_HEADER = 24               # FileHeader
SIZE_COMPRESSED_FRAME = 12          # CompressedFrameEntry
SIZE_BEHAVIOR_RULE = 14             # BehaviorRule
SIZE_CHAOS_SNAPSHOT_HEADER = 16     # ChaosSnapshotHeader
SIZE_CHAOS_SNAPSHOT_ENTRY = 12      # ChaosSnapshotEntry

# 固定参数
POPULATION_SIZE = 16
MAX_RULES = 16
MIN_RULES = 2

# 状态名称
STATE_NAMES = ['IDLE', 'WALKING', 'STUCK', 'CHAOS']

# 条件类型名称
COND_NAMES = ['L', 'R', 'BOTH', 'ANY', 'DIST', 'TIME', 'IDLE', 'ALWAYS']

# 端点映射 - 新固件 v9.21-RobustFixed
ENDPOINTS = {
    'list_files': '/list/files',
    'status': '/status',
    'history': '/download/history',
    'chaos_history': '/download/chaos',
    'population_summary': '/download/population',
    'individual': '/download/individual?gen={gen}&id={id}',
    'frame_csv': '/download/frame?gen={gen}&id={id}',
    'pop_bin': '/download/pop?gen={gen}',
    'frame_bin': '/download/frame_bin?gen={gen}&id={id}',
    'chaos_snap_bin': '/download/chaos_snap?gen={gen}&id={id}',
    'novelty_archive': '/download/novelty_archive',
}


def crc32_calculate(data: bytes) -> int:
    """计算 CRC32 校验值 (与固件算法一致)"""
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xEDB88320
            else:
                crc >>= 1
    return ~crc & 0xFFFFFFFF


def parse_filename(name: str) -> Dict[str, any]:
    """解析文件名提取 gen 和 id"""
    result = {'gen': None, 'id': None, 'type': None}
    
    # pop_gen_1.bin
    match = re.match(r'pop_gen_(\d+)\.bin', name)
    if match:
        result['type'] = 'pop_bin'
        result['gen'] = int(match.group(1))
        return result
    
    # frm_1_i0.bin 或 frm_1_i0.inc.bin
    match = re.match(r'frm_(\d+)_i(\d+)\.(?:inc\.)?bin', name)
    if match:
        result['type'] = 'frame_bin'
        result['gen'] = int(match.group(1))
        result['id'] = int(match.group(2))
        return result
    
    # chaos_snaps_g1_i0.bin
    match = re.match(r'chaos_snaps_g(\d+)_i(\d+)\.bin', name)
    if match:
        result['type'] = 'chaos_snap_bin'
        result['gen'] = int(match.group(1))
        result['id'] = int(match.group(2))
        return result
    
    # gen_1_id_0.csv
    match = re.match(r'gen_(\d+)_id_(\d+)\.csv', name)
    if match:
        result['type'] = 'individual_csv'
        result['gen'] = int(match.group(1))
        result['id'] = int(match.group(2))
        return result
    
    # chaos_g1_i0.csv
    match = re.match(r'chaos_g(\d+)_i(\d+)\.csv', name)
    if match:
        result['type'] = 'chaos_record_csv'
        result['gen'] = int(match.group(1))
        result['id'] = int(match.group(2))
        return result
    
    # 特殊文件
    if name == 'oe_history.csv':
        result['type'] = 'oe_history'
    elif name == 'chaos_history.csv':
        result['type'] = 'chaos_history'
    elif name == 'population_summary.csv':
        result['type'] = 'population_summary'
    elif name == 'novelty_archive.bin':
        result['type'] = 'novelty_archive'
    elif name == 'experiment_state.mrk':
        result['type'] = 'experiment_state'
    elif name.startswith('version_') and name.endswith('.mrk'):
        result['type'] = 'version_marker'
    else:
        result['type'] = 'unknown'
    
    return result


# ================================================================
# 任务1: 下载 SPIFFS 数据
# ================================================================

class SPIFFSDownloader:
    def __init__(self, ip: str, output_dir: str):
        self.base_url = f"http://{ip}"
        self.output_dir = Path(output_dir) / "spiffs_raw"
        self.output_dir.mkdir(parents=True, exist_ok=True)
        self.stats = {'success': 0, 'failed': 0, 'total_bytes': 0}
        
        # 子目录
        self.subdirs = {
            'pop_bin': self.output_dir / 'pop_bin',
            'frame_bin': self.output_dir / 'frame_bin',
            'chaos_snap_bin': self.output_dir / 'chaos_snap_bin',
            'individual_csv': self.output_dir / 'individual_csv',
            'chaos_record_csv': self.output_dir / 'chaos_record_csv',
        }
        for d in self.subdirs.values():
            d.mkdir(parents=True, exist_ok=True)
    
    def list_files(self) -> List[str]:
        """获取 SPIFFS 文件列表"""
        try:
            resp = requests.get(f"{self.base_url}{ENDPOINTS['list_files']}", timeout=10)
            if resp.status_code == 200:
                data = resp.json()
                return data.get('files', [])
        except Exception as e:
            print(f"  ❌ 获取文件列表失败: {e}")
        return []
    
    def download_file(self, url_path: str, save_name: str, subdir: str = "") -> bool:
        """下载单个文件"""
        url = f"{self.base_url}{url_path}"
        save_dir = self.output_dir / subdir if subdir else self.output_dir
        save_dir.mkdir(parents=True, exist_ok=True)
        save_path = save_dir / save_name
        
        try:
            resp = requests.get(url, timeout=30, stream=True)
            if resp.status_code == 200 and len(resp.content) > 0:
                with open(save_path, 'wb') as f:
                    f.write(resp.content)
                self.stats['success'] += 1
                self.stats['total_bytes'] += len(resp.content)
                print(f"  ✅ {save_name} ({len(resp.content):,} bytes)")
                return True
            else:
                print(f"  ⚠️ {save_name} - HTTP {resp.status_code}")
                self.stats['failed'] += 1
                return False
        except Exception as e:
            print(f"  ❌ {save_name} - {e}")
            self.stats['failed'] += 1
            return False
    
    def download_all(self) -> Dict:
        """下载所有 SPIFFS 数据"""
        print("\n" + "=" * 70)
        print("【任务1】下载 SPIFFS 数据")
        print("=" * 70)
        
        print("\n[1/4] 获取文件列表...")
        files = self.list_files()
        if not files:
            print("  ❌ 无法获取文件列表")
            return self.stats
        
        # 分类文件
        categories = {
            'oe_history': [],
            'chaos_history': [],
            'population_summary': [],
            'pop_bin': [],
            'frame_bin': [],
            'individual_csv': [],
            'chaos_record_csv': [],
            'chaos_snap_bin': [],
            'novelty_archive': [],
            'experiment_state': [],
            'version_marker': [],
            'unknown': []
        }
        
        for f in files:
            name = f.lstrip('/')
            info = parse_filename(name)
            ftype = info.get('type', 'unknown')
            
            if ftype in categories:
                categories[ftype].append((name, info))
            else:
                categories['unknown'].append((name, info))
        
        print(f"\n  历史记录: {len(categories['oe_history'])}")
        print(f"  混沌历史: {len(categories['chaos_history'])}")
        print(f"  种群汇总: {len(categories['population_summary'])}")
        print(f"  种群BIN: {len(categories['pop_bin'])}")
        print(f"  帧日志BIN: {len(categories['frame_bin'])}")
        print(f"  个体CSV: {len(categories['individual_csv'])}")
        print(f"  混沌记录CSV: {len(categories['chaos_record_csv'])}")
        print(f"  混沌快照BIN: {len(categories['chaos_snap_bin'])}")
        print(f"  新颖度存档: {len(categories['novelty_archive'])}")
        
        print("\n[2/4] 下载历史与摘要...")
        if categories['oe_history']:
            self.download_file(ENDPOINTS['history'], "oe_history.csv")
        if categories['chaos_history']:
            self.download_file(ENDPOINTS['chaos_history'], "chaos_history.csv")
        if categories['population_summary']:
            self.download_file(ENDPOINTS['population_summary'], "population_summary.csv")
        if categories['novelty_archive']:
            self.download_file(ENDPOINTS['novelty_archive'], "novelty_archive.bin")
        
        print("\n[3/4] 下载种群BIN...")
        for name, info in categories['pop_bin']:
            gen = info['gen']
            url = ENDPOINTS['pop_bin'].format(gen=gen)
            self.download_file(url, name, subdir="pop_bin")
        
        print("\n[4/4] 下载帧日志、个体CSV、混沌快照...")
        
        # 帧日志BIN
        for name, info in categories['frame_bin']:
            gen = info['gen']
            id_ = info['id']
            url = ENDPOINTS['frame_bin'].format(gen=gen, id=id_)
            self.download_file(url, name, subdir="frame_bin")
        
        # 个体CSV
        for name, info in categories['individual_csv']:
            gen = info['gen']
            id_ = info['id']
            url = ENDPOINTS['individual'].format(gen=gen, id=id_)
            self.download_file(url, name, subdir="individual_csv")
        
        # 混沌记录CSV
        for name, info in categories['chaos_record_csv']:
            gen = info['gen']
            id_ = info['id']
            url = ENDPOINTS['individual'].format(gen=gen, id=id_)
            self.download_file(url, name, subdir="chaos_record_csv")
        
        # 混沌快照BIN
        for name, info in categories['chaos_snap_bin']:
            gen = info['gen']
            id_ = info['id']
            url = ENDPOINTS['chaos_snap_bin'].format(gen=gen, id=id_)
            self.download_file(url, name, subdir="chaos_snap_bin")
        
        # 其他文件 (experiment_state, version_marker)
        other_files = categories['experiment_state'] + categories['version_marker']
        if other_files:
            print("\n  下载其他文件...")
            for name, info in other_files:
                self.download_file(f"/{name}", name)
        
        print(f"\n📁 保存到: {self.output_dir}")
        print(f"   成功: {self.stats['success']}, 失败: {self.stats['failed']}, 总大小: {self.stats['total_bytes']:,} bytes")
        return self.stats


# ================================================================
# 任务2: RAM 数据下载
# ================================================================

class RAMDataDownloader:
    def __init__(self, ip: str, output_dir: str):
        self.base_url = f"http://{ip}"
        self.output_dir = Path(output_dir) / "ram_data"
        self.output_dir.mkdir(parents=True, exist_ok=True)
        self.stats = {'success': 0, 'failed': 0}
    
    def download_all(self) -> Dict:
        print("\n" + "=" * 70)
        print("【任务2】下载 RAM 数据")
        print("=" * 70)
        
        endpoints = [
            (ENDPOINTS['status'], "status.json"),
            (ENDPOINTS['list_files'], "file_list.json"),
            (ENDPOINTS['history'], "history_snapshot.csv"),
            (ENDPOINTS['chaos_history'], "chaos_snapshot.csv"),
            (ENDPOINTS['population_summary'], "population_snapshot.csv"),
        ]
        
        for endpoint, name in endpoints:
            try:
                resp = requests.get(f"{self.base_url}{endpoint}", timeout=30)
                if resp.status_code == 200 and len(resp.content) > 0:
                    with open(self.output_dir / name, 'wb') as f:
                        f.write(resp.content)
                    print(f"  ✅ {name}")
                    self.stats['success'] += 1
                else:
                    print(f"  ⚠️ {name} - HTTP {resp.status_code}")
                    self.stats['failed'] += 1
            except Exception as e:
                print(f"  ❌ {name} - {e}")
                self.stats['failed'] += 1
        
        return self.stats


# ================================================================
# 任务3: BIN 转 CSV (与固件 v9.21-RobustFixed 严格对齐)
# ================================================================

class BinToCsvConverter:
    def __init__(self, input_dir: str, output_dir: str):
        self.input_dir = Path(input_dir)
        self.output_dir = Path(output_dir) / "csv_converted"
        self.output_dir.mkdir(parents=True, exist_ok=True)
        
        self.subdirs = {
            'frame_bin': self.output_dir / "frame_logs",
            'chaos_snap_bin': self.output_dir / "chaos_snapshots",
            'pop_bin': self.output_dir / "populations",
        }
        for d in self.subdirs.values():
            d.mkdir(parents=True, exist_ok=True)
        
        self.stats = {'success': 0, 'failed': 0, 'skipped': 0}
    
    def convert_all(self) -> Dict:
        print("\n" + "=" * 70)
        print("【任务3】BIN 转 CSV")
        print("=" * 70)
        
        bin_files = list(self.input_dir.rglob("*.bin"))
        print(f"\n找到 {len(bin_files)} 个 bin 文件")
        
        for bin_file in bin_files:
            name = bin_file.name
            try:
                if name.startswith('frm_'):
                    self._convert_frame_log(bin_file)
                elif name.startswith('chaos_snaps_'):
                    self._convert_chaos_snapshot(bin_file)
                elif name.startswith('pop_gen_'):
                    self._convert_population(bin_file)
                elif name == 'novelty_archive.bin':
                    self._convert_novelty_archive(bin_file)
                else:
                    self.stats['skipped'] += 1
            except Exception as e:
                print(f"  ❌ {name} - {e}")
                self.stats['failed'] += 1
        
        print(f"\n   成功: {self.stats['success']}, 失败: {self.stats['failed']}, 跳过: {self.stats['skipped']}")
        return self.stats
    
    # ================================================================
    # 帧日志转换 - 完全手动解析
    # ================================================================
    def _convert_frame_log(self, bin_file: Path):
        """转换帧日志 (FileHeader + CompressedFrameEntry)"""
        with open(bin_file, 'rb') as f:
            data = f.read()
        
        if len(data) < 24:
            raise ValueError(f"文件太小: {len(data)} < 24")
        
        # === 解析 FileHeader (24 bytes) ===
        magic = int.from_bytes(data[0:4], 'little')
        version = int.from_bytes(data[4:6], 'little')
        header_size = int.from_bytes(data[6:8], 'little')
        crc32_val = int.from_bytes(data[8:12], 'little')
        frame_count = int.from_bytes(data[12:16], 'little')
        gen = int.from_bytes(data[16:20], 'little')
        ind = int.from_bytes(data[20:22], 'little')
        reserved = int.from_bytes(data[22:24], 'little')
        
        if magic != MAGIC_FILE_HEADER:
            print(f"  ⚠️ {bin_file.name} - 魔数不匹配: 0x{magic:08X} (预期 0x{MAGIC_FILE_HEADER:08X})")
        
        frames_data = data[24:]
        actual_count = len(frames_data) // 12
        if frame_count != actual_count and actual_count > 0:
            print(f"  ⚠️ {bin_file.name} - 帧数: 期望 {frame_count}, 实际 {actual_count}")
            frame_count = min(frame_count, actual_count)
        
        # CRC 校验
        if frame_count > 0:
            calc_crc = crc32_calculate(frames_data[:frame_count * 12])
            if calc_crc != crc32_val:
                print(f"  ⚠️ {bin_file.name} - CRC: 期望 0x{crc32_val:08X}, 计算 0x{calc_crc:08X}")
        
        csv_path = self.subdirs['frame_bin'] / f"{bin_file.stem}.csv"
        with open(csv_path, 'w', encoding='utf-8') as f:
            f.write("timestamp_ms,sensorLeft,sensorRight,motorLeftPWM,motorRightPWM,state,stateName\n")
            pwmL, pwmR = 0, 0
            for i in range(frame_count):
                off = i * 12
                if off + 12 > len(frames_data):
                    break
                
                ts = int.from_bytes(frames_data[off:off+4], 'little')
                sL = int.from_bytes(frames_data[off+4:off+6], 'little', signed=True)
                sR = int.from_bytes(frames_data[off+6:off+8], 'little', signed=True)
                mL = int.from_bytes(frames_data[off+8:off+9], 'little', signed=True)
                mR = int.from_bytes(frames_data[off+9:off+10], 'little', signed=True)
                packed = frames_data[off+10]
                
                state = (packed >> 4) & 0x07
                stateName = STATE_NAMES[state] if state < 4 else f"UNKNOWN({state})"
                
                if i == 0:
                    pwmL, pwmR = mL & 0xFF, mR & 0xFF
                else:
                    pwmL = max(0, min(255, pwmL + mL))
                    pwmR = max(0, min(255, pwmR + mR))
                
                f.write(f"{ts},{sL},{sR},{pwmL},{pwmR},{state},{stateName}\n")
        
        print(f"  ✅ {bin_file.name} → {csv_path.name} ({frame_count} 帧)")
        self.stats['success'] += 1
    
    # ================================================================
    # 混沌快照转换
    # ================================================================
    def _convert_chaos_snapshot(self, bin_file: Path):
        """转换混沌快照 (ChaosSnapshotHeader + ChaosSnapshotEntry)"""
        with open(bin_file, 'rb') as f:
            data = f.read()
        
        if len(data) < 16:
            raise ValueError(f"文件太小: {len(data)} < 16")
        
        # === 解析 ChaosSnapshotHeader (16 bytes) ===
        magic = int.from_bytes(data[0:4], 'little')
        version = int.from_bytes(data[4:6], 'little')
        header_size = int.from_bytes(data[6:8], 'little')
        crc32_val = int.from_bytes(data[8:12], 'little')
        count = data[12]
        
        if magic != MAGIC_CHAOS_SNAPSHOT:
            print(f"  ⚠️ {bin_file.name} - 魔数不匹配: 0x{magic:08X} (预期 0x{MAGIC_CHAOS_SNAPSHOT:08X})")
        
        payload = data[16:]
        actual_count = (len(payload) - 1) // 12 if len(payload) > 1 else 0
        if count != actual_count and actual_count > 0:
            print(f"  ⚠️ {bin_file.name} - 快照数: 期望 {count}, 实际 {actual_count}")
            count = min(count, actual_count)
        
        csv_path = self.subdirs['chaos_snap_bin'] / f"{bin_file.stem}.csv"
        with open(csv_path, 'w', encoding='utf-8') as f:
            f.write("index,sensorLeft,sensorRight,motorLeftPWM,motorRightPWM,durationMs,timestamp_ms\n")
            for i in range(count):
                off = 1 + i * 12
                if off + 12 > len(payload):
                    break
                sL = int.from_bytes(payload[off:off+2], 'little', signed=True)
                sR = int.from_bytes(payload[off+2:off+4], 'little', signed=True)
                mL = int.from_bytes(payload[off+4:off+5], 'little', signed=True)
                mR = int.from_bytes(payload[off+5:off+6], 'little', signed=True)
                dur = int.from_bytes(payload[off+6:off+8], 'little')
                ts = int.from_bytes(payload[off+8:off+12], 'little')
                f.write(f"{i},{sL},{sR},{mL},{mR},{dur},{ts}\n")
        
        print(f"  ✅ {bin_file.name} → {csv_path.name} ({count} 条)")
        self.stats['success'] += 1
    
    # ================================================================
    # ★★★ 种群转换 - 修复版 (与 serializeIndividual 严格对齐) ★★★
    # ================================================================
    def _convert_population(self, bin_file: Path):
        """
        转换种群BIN文件
        使用 serializeIndividual 的字段顺序:
        1. ruleCount (1)
        2. rules (ruleCount * 14)
        3. survival_time (4)
        4. distance_ticks (4)
        5. noveltyScore (4)
        6. obstacleThreshold (2)
        7. clearThreshold (2)
        8. encoderDiffThreshold (2)
        9. encoderDiffMin (2)
        10. wheelSpinThreshold (2)
        11. wheelStopThreshold (2)
        12. stuckWindowSize (1)
        13. chaosNoiseAmplifier (2)
        14. chaosMinPwm (2)
        15. chaosTimeoutMs (2)
        16. chaosForceTimeoutMs (2)
        17. hasChaosRules (1)
        18. chaosRuleCount (1)
        19. chaosRulesStartIndex (1)
        """
        with open(bin_file, 'rb') as f:
            data = f.read()
        
        if len(data) < 16:
            raise ValueError(f"文件太小: {len(data)} < 16")
        
        # === 解析文件头 ===
        magic = int.from_bytes(data[0:4], 'little')
        version = int.from_bytes(data[4:6], 'little')
        pop_size = int.from_bytes(data[6:8], 'little')
        gen = int.from_bytes(data[8:12], 'little')
        exp_id = int.from_bytes(data[12:16], 'little')
        
        if magic != MAGIC_FILE_HEADER:
            print(f"  ⚠️ {bin_file.name} - 魔数不匹配: 0x{magic:08X}")
        
        print(f"  📊 {bin_file.name}: gen={gen}, popSize={pop_size}, version=0x{version:04X}")
        
        csv_path = self.subdirs['pop_bin'] / f"{bin_file.stem}.csv"
        with open(csv_path, 'w', encoding='utf-8') as f:
            f.write(f"# Population: version=0x{version:04X}, popSize={pop_size}, gen={gen}, expId={exp_id}\n")
            f.write("individual,ruleCount,survivalTime_ms,distanceTicks,noveltyScore,"
                    "obstacleThreshold,clearThreshold,encoderDiffThreshold,encoderDiffMin,"
                    "wheelSpinThreshold,wheelStopThreshold,stuckWindowSize,"
                    "chaosNoiseAmplifier,chaosMinPwm,chaosTimeoutMs,chaosForceTimeoutMs,"
                    "hasChaosRules,chaosRuleCount,chaosRulesStartIndex\n")
            
            offset = 16  # 跳过文件头
            
            for i in range(min(pop_size, 16)):
                if offset >= len(data):
                    print(f"  ⚠️ 个体 {i}: 数据不足")
                    break
                
                # 1. ruleCount (1 byte)
                rule_count = data[offset]
                offset += 1
                if rule_count > MAX_RULES:
                    rule_count = MAX_RULES
                
                # 2. rules (ruleCount * 14 bytes) - 跳过
                offset += rule_count * 14
                
                # 3. survival_time (4)
                if offset + 4 > len(data): break
                survival = int.from_bytes(data[offset:offset+4], 'little')
                offset += 4
                
                # 4. distance_ticks (4)
                if offset + 4 > len(data): break
                distance = int.from_bytes(data[offset:offset+4], 'little', signed=True)
                offset += 4
                
                # 5. noveltyScore (4)
                if offset + 4 > len(data): break
                novelty = struct.unpack_from('<f', data, offset)[0]
                offset += 4
                
                # 6-11. 阈值参数 (2+2+2+2+2+2 = 12)
                if offset + 12 > len(data): break
                obstacle_threshold = int.from_bytes(data[offset:offset+2], 'little', signed=True)
                clear_threshold = int.from_bytes(data[offset+2:offset+4], 'little', signed=True)
                encoder_diff_threshold = int.from_bytes(data[offset+4:offset+6], 'little', signed=True)
                encoder_diff_min = int.from_bytes(data[offset+6:offset+8], 'little', signed=True)
                wheel_spin_threshold = int.from_bytes(data[offset+8:offset+10], 'little', signed=True)
                wheel_stop_threshold = int.from_bytes(data[offset+10:offset+12], 'little', signed=True)
                offset += 12
                
                # 12. stuckWindowSize (1)
                if offset + 1 > len(data): break
                stuck_window_size = data[offset]
                offset += 1
                
                # 13-16. chaos参数 (2+2+2+2 = 8)
                if offset + 8 > len(data): break
                chaos_noise_amplifier = int.from_bytes(data[offset:offset+2], 'little', signed=True)
                chaos_min_pwm = int.from_bytes(data[offset+2:offset+4], 'little', signed=True)
                chaos_timeout_ms = int.from_bytes(data[offset+4:offset+6], 'little')
                chaos_force_timeout_ms = int.from_bytes(data[offset+6:offset+8], 'little')
                offset += 8
                
                # 17-19. chaos规则信息 (1+1+1 = 3)
                if offset + 3 > len(data): break
                has_chaos_rules = data[offset]
                chaos_rule_count = data[offset+1]
                chaos_rules_start_index = data[offset+2]
                offset += 3
                
                f.write(f"{i},{rule_count},{survival},{distance},{novelty:.6f},"
                        f"{obstacle_threshold},{clear_threshold},{encoder_diff_threshold},{encoder_diff_min},"
                        f"{wheel_spin_threshold},{wheel_stop_threshold},{stuck_window_size},"
                        f"{chaos_noise_amplifier},{chaos_min_pwm},{chaos_timeout_ms},{chaos_force_timeout_ms},"
                        f"{has_chaos_rules},{chaos_rule_count},{chaos_rules_start_index}\n")
        
        print(f"  ✅ {bin_file.name} → {csv_path.name} ({pop_size} 个个体)")
        self.stats['success'] += 1
    
    # ================================================================
    # 新颖度存档转换
    # ================================================================
    def _convert_novelty_archive(self, bin_file: Path):
        """转换新颖度存档 (ARCH + float数组)"""
        with open(bin_file, 'rb') as f:
            data = f.read()
        
        if len(data) < 12:
            raise ValueError(f"文件太小: {len(data)} < 12")
        
        magic = int.from_bytes(data[0:4], 'little')
        version = int.from_bytes(data[4:6], 'little')
        count = int.from_bytes(data[6:8], 'little')
        crc32_val = int.from_bytes(data[8:12], 'little')
        
        if magic != MAGIC_NOVELTY_ARCHIVE:
            print(f"  ⚠️ {bin_file.name} - 魔数不匹配: 0x{magic:08X}")
        
        csv_path = self.output_dir / "novelty_archive.csv"
        with open(csv_path, 'w', encoding='utf-8') as f:
            f.write(f"# Novelty Archive: count={count}, version={version}, crc32=0x{crc32_val:08X}\n")
            f.write("index,leftSensorMean,rightSensorMean,sensorVariance,sensorAsymmetry,"
                    "avgSpeed,speedVariance,turnBias,totalDistance,"
                    "forwardRatio,turnRatio,reverseRatio,idleRatio\n")
            
            if count > 0:
                # maxValues 占 48 字节 (12 个 float)
                offset = 12 + 48
                for i in range(count):
                    if offset + 48 > len(data):
                        break
                    values = struct.unpack_from('<ffffffffffff', data, offset)
                    offset += 48
                    f.write(f"{i}," + ",".join(f"{v:.6f}" for v in values) + "\n")
        
        print(f"  ✅ {bin_file.name} → {csv_path.name} ({count} 条)")
        self.stats['success'] += 1


# ================================================================
# 任务4: 生成汇总报告
# ================================================================

class ReportGenerator:
    def __init__(self, output_dir: str):
        self.output_dir = Path(output_dir)
        self.report_path = self.output_dir / "data_summary.txt"
    
    def generate(self, spiffs_stats: Dict, ram_stats: Dict, convert_stats: Dict) -> None:
        """生成数据汇总报告"""
        lines = []
        lines.append("=" * 70)
        lines.append("OEE 数据汇总报告")
        lines.append(f"生成时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
        lines.append("=" * 70)
        
        # SPIFFS 下载统计
        lines.append("\n【SPIFFS 下载】")
        lines.append(f"  成功: {spiffs_stats.get('success', 0)}")
        lines.append(f"  失败: {spiffs_stats.get('failed', 0)}")
        lines.append(f"  总大小: {spiffs_stats.get('total_bytes', 0):,} bytes")
        
        # RAM 下载统计
        lines.append("\n【RAM 下载】")
        lines.append(f"  成功: {ram_stats.get('success', 0)}")
        lines.append(f"  失败: {ram_stats.get('failed', 0)}")
        
        # BIN 转换统计
        lines.append("\n【BIN 转换】")
        lines.append(f"  成功: {convert_stats.get('success', 0)}")
        lines.append(f"  失败: {convert_stats.get('failed', 0)}")
        lines.append(f"  跳过: {convert_stats.get('skipped', 0)}")
        
        # 文件清单
        lines.append("\n【文件清单】")
        spiffs_dir = self.output_dir / "spiffs_raw"
        if spiffs_dir.exists():
            for root, dirs, files in os.walk(spiffs_dir):
                rel_path = Path(root).relative_to(spiffs_dir)
                if rel_path == Path('.'):
                    rel_path = Path('')
                for f in files:
                    size = os.path.getsize(os.path.join(root, f))
                    lines.append(f"  {rel_path}/{f} ({size:,} bytes)")
        
        with open(self.report_path, 'w', encoding='utf-8') as f:
            f.write("\n".join(lines))
        
        print(f"\n📄 汇总报告: {self.report_path}")


# ================================================================
# 主程序
# ================================================================

def main():
    parser = argparse.ArgumentParser(
        description='OEE 数据工具 v4.0 - 新固件兼容版',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  python oee_downloader_v4.py --ip 192.168.4.1 --output ./oee_data
  python oee_downloader_v4.py --task convert --local-dir ./oee_data/spiffs_raw
  python oee_downloader_v4.py --task spiffs --ip 192.168.4.1
        """
    )
    parser.add_argument('--ip', type=str, default='192.168.4.1', help='ESP32 IP地址')
    parser.add_argument('--output', type=str, default='./oee_data', help='输出目录')
    parser.add_argument('--task', choices=['all', 'spiffs', 'ram', 'convert', 'report'], 
                        default='all', help='执行的任务')
    parser.add_argument('--local-dir', type=str, default=None, 
                        help='本地SPIFFS目录 (用于convert/report)')
    args = parser.parse_args()
    
    print("\n" + "=" * 70)
    print("OEE 数据下载与转换工具 v4.0")
    print("对应固件: v9.21-RobustFixed (2026-09-03)")
    print("=" * 70)
    
    spiffs_dir = None
    spiffs_stats = {'success': 0, 'failed': 0, 'total_bytes': 0}
    ram_stats = {'success': 0, 'failed': 0}
    convert_stats = {'success': 0, 'failed': 0, 'skipped': 0}
    
    # 执行任务
    if args.task in ['all', 'spiffs']:
        downloader = SPIFFSDownloader(args.ip, args.output)
        spiffs_stats = downloader.download_all()
        spiffs_dir = downloader.output_dir
    else:
        spiffs_dir = Path(args.local_dir) if args.local_dir else Path(args.output) / "spiffs_raw"
    
    if args.task in ['all', 'ram']:
        ram_downloader = RAMDataDownloader(args.ip, args.output)
        ram_stats = ram_downloader.download_all()
    
    if args.task in ['all', 'convert']:
        if spiffs_dir and spiffs_dir.exists():
            converter = BinToCsvConverter(str(spiffs_dir), args.output)
            convert_stats = converter.convert_all()
        else:
            print("\n⚠️ SPIFFS 目录不存在，跳过转换: {spiffs_dir}")
    
    if args.task in ['all', 'report']:
        reporter = ReportGenerator(args.output)
        reporter.generate(spiffs_stats, ram_stats, convert_stats)
    
    print("\n" + "=" * 70)
    print("✅ 完成!")
    print(f"📁 输出: {Path(args.output).absolute()}")
    print("=" * 70)


if __name__ == '__main__':
    sys.exit(main())