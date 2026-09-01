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

import os
import sys
import struct
import argparse
import requests
import json
from pathlib import Path
from datetime import datetime
from typing import List, Dict, Optional

# ================================================================
# 常量
# ================================================================

MAGIC_FILE_HEADER = 0x47454E45      # 'GENE'
MAGIC_CHAOS_SNAPSHOT = 0x4348534E   # 'CHSN'
MAGIC_POPULATION = 0x47454E45       # 'GENE'
MAGIC_NOVELTY_ARCHIVE = 0x41524348  # 'ARCH'

SIZE_FILE_HEADER = 24
SIZE_COMPRESSED_FRAME = 12
SIZE_BEHAVIOR_RULE = 14
SIZE_CHAOS_SNAPSHOT_HEADER = 16
SIZE_CHAOS_SNAPSHOT_ENTRY = 12

STATE_NAMES = ['IDLE', 'WALKING', 'STUCK', 'CHAOS']


def crc32_calculate(data: bytes) -> int:
    """计算 CRC32"""
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xEDB88320
            else:
                crc >>= 1
    return ~crc & 0xFFFFFFFF


# ================================================================
# 任务1: 下载 SPIFFS 数据 (支持 v7 新端点)
# ================================================================

class SPIFFSDownloader:
    def __init__(self, ip: str, output_dir: str):
        self.base_url = f"http://{ip}"
        self.output_dir = Path(output_dir) / "spiffs_raw"
        self.output_dir.mkdir(parents=True, exist_ok=True)
        self.stats = {'success': 0, 'failed': 0, 'total_bytes': 0}
    
    def list_files(self) -> List[str]:
        """获取文件列表"""
        try:
            resp = requests.get(f"{self.base_url}/list/files", timeout=10)
            if resp.status_code == 200:
                return resp.json().get('files', [])
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
            resp = requests.get(url, timeout=30)
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
        except requests.exceptions.ConnectionError as e:
            print(f"  ❌ {save_name} - 连接错误: {e}")
            self.stats['failed'] += 1
            return False
        except Exception as e:
            print(f"  ❌ {save_name} - {e}")
            self.stats['failed'] += 1
            return False
    
    def download_frame_bin(self, gen: int, ind: int) -> bool:
        """下载二进制帧日志 (使用 /download/frame_bin 端点)"""
        url_path = f"/download/frame_bin?gen={gen}&id={ind}"
        save_name = f"frm_{gen}_i{ind}.bin"
        return self.download_file(url_path, save_name, subdir="frame_logs_bin")
    
    def download_frame_csv(self, gen: int, ind: int) -> bool:
        """下载 CSV 帧日志 (使用 /download/frame 端点)"""
        url_path = f"/download/frame?gen={gen}&id={ind}"
        save_name = f"frm_{gen}_i{ind}.csv"
        save_dir = self.output_dir / "frame_logs_csv"
        save_dir.mkdir(parents=True, exist_ok=True)
        save_path = save_dir / save_name
        try:
            resp = requests.get(f"{self.base_url}{url_path}", timeout=30)
            if resp.status_code == 200 and len(resp.text) > 0:
                with open(save_path, 'w', encoding='utf-8') as f:
                    f.write(resp.text)
                self.stats['success'] += 1
                self.stats['total_bytes'] += len(resp.text.encode('utf-8'))
                print(f"  ✅ {save_name} ({len(resp.text):,} bytes, CSV)")
                return True
            else:
                print(f"  ⚠️ {save_name} - HTTP {resp.status_code}")
                self.stats['failed'] += 1
                return False
        except Exception as e:
            print(f"  ❌ {save_name} - {e}")
            self.stats['failed'] += 1
            return False
    
    def download_pop(self, gen: int) -> bool:
        """下载种群文件 (使用 /download/pop 端点)"""
        url_path = f"/download/pop?gen={gen}"
        save_name = f"pop_gen_{gen}.bin"
        return self.download_file(url_path, save_name, subdir="pop_bin")
    
    def download_chaos_snap(self, gen: int, ind: int) -> bool:
        """下载混沌快照 (使用 /download/chaos_snap 端点)"""
        url_path = f"/download/chaos_snap?gen={gen}&id={ind}"
        save_name = f"chaos_snaps_g{gen}_i{ind}.bin"
        return self.download_file(url_path, save_name, subdir="chaos_snapshots")
    
    def download_individual(self, gen: int, ind: int) -> bool:
        """下载个体记录 (使用 /download/individual 端点)"""
        url_path = f"/download/individual?gen={gen}&id={ind}"
        save_name = f"gen_{gen}_id_{ind}.csv"
        save_dir = self.output_dir / "individuals"
        save_dir.mkdir(parents=True, exist_ok=True)
        save_path = save_dir / save_name
        try:
            resp = requests.get(f"{self.base_url}{url_path}", timeout=30)
            if resp.status_code == 200 and len(resp.text) > 0:
                with open(save_path, 'w', encoding='utf-8') as f:
                    f.write(resp.text)
                self.stats['success'] += 1
                self.stats['total_bytes'] += len(resp.text.encode('utf-8'))
                print(f"  ✅ {save_name} ({len(resp.text):,} bytes)")
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
        """下载所有数据"""
        print("\n" + "=" * 60)
        print("【任务1】下载 SPIFFS 数据")
        print("=" * 60)
        
        print("\n[1/5] 获取文件列表...")
        files = self.list_files()
        if not files:
            print("  ❌ 无法获取文件列表")
            return self.stats
        
        # 分类文件
        categories = {
            'history': [], 'chaos': [], 'pop_bin': [],
            'frame_logs': [], 'individuals': [], 'chaos_snapshots': [], 'other': []
        }
        
        for f in files:
            name = f.lstrip('/')
            if name == 'oe_history.csv' or name == 'history.csv':
                categories['history'].append(name)
            elif name.startswith('chaos_g') and name.endswith('.csv'):
                categories['chaos'].append(name)
            elif name == 'chaos_history.csv':
                categories['chaos'].append(name)
            elif name.startswith('pop_gen_') and name.endswith('.bin'):
                categories['pop_bin'].append(name)
            elif name.startswith('frm_') and name.endswith('.bin'):
                categories['frame_logs'].append(name)
            elif name.startswith('gen_') and name.endswith('.csv'):
                categories['individuals'].append(name)
            elif name.startswith('chaos_snaps_') and name.endswith('.bin'):
                categories['chaos_snapshots'].append(name)
            else:
                categories['other'].append(name)
        
        print(f"\n  历史记录: {len(categories['history'])}")
        print(f"  混沌记录: {len(categories['chaos'])}")
        print(f"  种群快照: {len(categories['pop_bin'])}")
        print(f"  帧日志: {len(categories['frame_logs'])}")
        print(f"  个体记录: {len(categories['individuals'])}")
        print(f"  混沌快照: {len(categories['chaos_snapshots'])}")
        
        print("\n[2/5] 下载历史与混沌记录...")
        if categories['history']:
            self.download_file("/download/history", "oe_history.csv")
        if categories['chaos']:
            self.download_file("/download/chaos", "chaos_history.csv")
        
        print("\n[3/5] 下载种群快照 (使用 /download/pop)...")
        for f in categories['pop_bin']:
            # 从文件名提取代数: pop_gen_1.bin → 1
            try:
                gen = int(f.replace('pop_gen_', '').replace('.bin', ''))
                self.download_pop(gen)
            except ValueError:
                self.download_file(f"/{f}", f, subdir="pop_bin")
        
        print("\n[4/5] 下载帧日志...")
        for f in categories['frame_logs']:
            parts = f.replace('.bin', '').split('_')
            if len(parts) >= 3:
                try:
                    gen = int(parts[1])
                    ind = int(parts[2].lstrip('i'))
                    # 优先下载二进制 (用于解析)
                    self.download_frame_bin(gen, ind)
                except ValueError:
                    self.download_file(f"/{f}", f, subdir="frame_logs_bin")
        
        print("\n[5/5] 下载个体记录和混沌快照...")
        for f in categories['individuals']:
            parts = f.replace('.csv', '').split('_')
            if len(parts) >= 4:
                try:
                    gen = int(parts[1])
                    ind = int(parts[3])
                    self.download_individual(gen, ind)
                except ValueError:
                    self.download_file(f"/{f}", f, subdir="individuals")
        
        for f in categories['chaos_snapshots']:
            # 解析: chaos_snaps_g1_i0.bin
            parts = f.replace('.bin', '').split('_')
            if len(parts) >= 3:
                try:
                    gen = int(parts[2].lstrip('g'))
                    ind = int(parts[3].lstrip('i'))
                    self.download_chaos_snap(gen, ind)
                except (ValueError, IndexError):
                    self.download_file(f"/{f}", f, subdir="chaos_snapshots")
        
        # 下载其他文件
        for f in categories['other']:
            if f.endswith('.mrk') or f == 'experiment_state.mrk':
                self.download_file(f"/{f}", f)
        
        print(f"\n📁 保存到: {self.output_dir}")
        print(f"   成功: {self.stats['success']}, 失败: {self.stats['failed']}")
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
        print("\n" + "=" * 60)
        print("【任务2】下载 RAM 数据")
        print("=" * 60)
        
        endpoints = [
            ("/status", "status.json"),
            ("/list/files", "file_list.json"),
            ("/download/history", "history_snapshot.csv"),
            ("/download/chaos", "chaos_snapshot.csv"),
            ("/download/population", "population_snapshot.csv"),
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
# 任务3: BIN 转 CSV (支持 v7 新格式)
# ================================================================

class BinToCsvConverter:
    def __init__(self, input_dir: str, output_dir: str):
        self.input_dir = Path(input_dir)
        self.output_dir = Path(output_dir) / "csv_converted"
        self.output_dir.mkdir(parents=True, exist_ok=True)
        
        self.frame_dir = self.output_dir / "frame_logs"
        self.chaos_dir = self.output_dir / "chaos_snapshots"
        self.pop_dir = self.output_dir / "populations"
        for d in [self.frame_dir, self.chaos_dir, self.pop_dir]:
            d.mkdir(parents=True, exist_ok=True)
        
        self.stats = {'success': 0, 'failed': 0, 'skipped': 0}
    
    def convert_all(self) -> Dict:
        print("\n" + "=" * 60)
        print("【任务3】BIN 转 CSV")
        print("=" * 60)
        
        # 查找所有 bin 文件 (包括子目录)
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
    
    def _convert_frame_log(self, bin_file: Path):
        """转换帧日志 - 手动解析"""
        with open(bin_file, 'rb') as f:
            data = f.read()
        
        if len(data) < 24:
            raise ValueError(f"文件太小: {len(data)} < 24")
        
        # 解析 FileHeader (24 bytes)
        magic = int.from_bytes(data[0:4], 'little')
        version = int.from_bytes(data[4:6], 'little')
        header_size = int.from_bytes(data[6:8], 'little')
        crc32_val = int.from_bytes(data[8:12], 'little')
        frame_count = int.from_bytes(data[12:16], 'little')
        gen = int.from_bytes(data[16:20], 'little')
        ind = int.from_bytes(data[20:22], 'little')
        reserved = int.from_bytes(data[22:24], 'little')
        
        if magic != 0x47454E45:
            print(f"  ⚠️ {bin_file.name} - 魔数不匹配: 0x{magic:08X} (期望 0x47454E45)")
        
        frames_data = data[24:]
        actual_count = len(frames_data) // 12
        if frame_count != actual_count and actual_count > 0:
            print(f"  ⚠️ {bin_file.name} - 帧数: 期望 {frame_count}, 实际 {actual_count}")
            frame_count = min(frame_count, actual_count)
        
        # CRC校验
        if frame_count > 0:
            calc_crc = crc32_calculate(frames_data[:frame_count * 12])
            if calc_crc != crc32_val:
                print(f"  ⚠️ {bin_file.name} - CRC: 期望 0x{crc32_val:08X}, 计算 0x{calc_crc:08X}")
        
        csv_path = self.frame_dir / f"{bin_file.stem}.csv"
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
    
    def _convert_chaos_snapshot(self, bin_file: Path):
        """转换混沌快照"""
        with open(bin_file, 'rb') as f:
            data = f.read()
        
        if len(data) < 16:
            raise ValueError(f"文件太小: {len(data)} < 16")
        
        magic = int.from_bytes(data[0:4], 'little')
        version = int.from_bytes(data[4:6], 'little')
        header_size = int.from_bytes(data[6:8], 'little')
        crc32_val = int.from_bytes(data[8:12], 'little')
        count = data[12]
        
        if magic != 0x4348534E:
            print(f"  ⚠️ {bin_file.name} - 魔数不匹配: 0x{magic:08X} (期望 0x4348534E)")
        
        payload = data[16:]
        actual_count = (len(payload) - 1) // 12 if len(payload) > 1 else 0
        if count != actual_count and actual_count > 0:
            print(f"  ⚠️ {bin_file.name} - 快照数: 期望 {count}, 实际 {actual_count}")
            count = min(count, actual_count)
        
        csv_path = self.chaos_dir / f"{bin_file.stem}.csv"
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
    
    def _convert_population(self, bin_file: Path):
        """转换种群文件"""
        with open(bin_file, 'rb') as f:
            data = f.read()
        
        if len(data) < 16:
            raise ValueError(f"文件太小: {len(data)} < 16")
        
        magic = int.from_bytes(data[0:4], 'little')
        version = int.from_bytes(data[4:6], 'little')
        pop_size = int.from_bytes(data[6:8], 'little')
        gen = int.from_bytes(data[8:12], 'little')
        exp_id = int.from_bytes(data[12:16], 'little')
        
        csv_path = self.pop_dir / f"{bin_file.stem}.csv"
        with open(csv_path, 'w', encoding='utf-8') as f:
            f.write(f"# Population: version=0x{version:04X}, popSize={pop_size}, gen={gen}, expId={exp_id}\n")
            f.write("individual,ruleCount,survivalTime_ms,distanceTicks,noveltyScore,chaosTimeoutMs,chaosForceTimeoutMs\n")
            
            offset = 16
            for i in range(min(pop_size, 16)):
                if offset >= len(data):
                    break
                rule_count = data[offset]
                offset += 1
                
                # 跳过规则 (每个规则 14 bytes)
                offset += rule_count * 14
                
                if offset + 12 > len(data):
                    break
                survival = int.from_bytes(data[offset:offset+4], 'little')
                distance = int.from_bytes(data[offset+4:offset+8], 'little')
                novelty = struct.unpack_from('<f', data, offset+8)[0]
                offset += 12
                
                if offset + 22 > len(data):
                    break
                # 跳过一些字段到 chaosTimeoutMs
                # obstacleThreshold(2) + clearThreshold(2) + encoderDiffThreshold(2) + 
                # encoderDiffMin(2) + wheelSpinThreshold(2) + wheelStopThreshold(2) + 
                # stuckWindowSize(1) + chaosNoiseAmplifier(2) + chaosMinPwm(2) = 15
                offset += 15
                chaos_timeout = int.from_bytes(data[offset:offset+2], 'little')
                offset += 2
                chaos_force_timeout = int.from_bytes(data[offset:offset+2], 'little')
                offset += 2
                
                # 跳过混沌标记
                if version >= 0x0006:
                    offset += 3
                
                f.write(f"{i},{rule_count},{survival},{distance},{novelty:.6f},{chaos_timeout},{chaos_force_timeout}\n")
        
        print(f"  ✅ {bin_file.name} → {csv_path.name} ({pop_size} 个个体)")
        self.stats['success'] += 1
    
    def _convert_novelty_archive(self, bin_file: Path):
        """转换新颖度存档"""
        with open(bin_file, 'rb') as f:
            data = f.read()
        
        if len(data) < 12:
            raise ValueError(f"文件太小: {len(data)} < 12")
        
        magic = int.from_bytes(data[0:4], 'little')
        version = int.from_bytes(data[4:6], 'little')
        count = int.from_bytes(data[6:8], 'little')
        crc32_val = int.from_bytes(data[8:12], 'little')
        
        csv_path = self.output_dir / "novelty_archive.csv"
        with open(csv_path, 'w', encoding='utf-8') as f:
            f.write(f"# Novelty Archive: count={count}, version={version}, crc32=0x{crc32_val:08X}\n")
            f.write("index,leftSensorMean,rightSensorMean,sensorVariance,sensorAsymmetry,avgSpeed,speedVariance,turnBias,totalDistance,forwardRatio,turnRatio,reverseRatio,idleRatio\n")
            
            if count > 0:
                offset = 12 + 12 * 4  # 跳过 maxValues (12 floats)
                for i in range(count):
                    if offset + 48 > len(data):
                        break
                    values = struct.unpack_from('<ffffffffffff', data, offset)
                    offset += 48
                    f.write(f"{i}," + ",".join(f"{v:.6f}" for v in values) + "\n")
        
        print(f"  ✅ {bin_file.name} → {csv_path.name} ({count} 条)")
        self.stats['success'] += 1


# ================================================================
# 任务4: 生成数据摘要报告
# ================================================================

class ReportGenerator:
    def __init__(self, data_dir: str):
        self.data_dir = Path(data_dir)
    
    def generate(self) -> str:
        """生成汇总报告"""
        lines = []
        lines.append("=" * 70)
        lines.append("OEE 数据下载报告")
        lines.append(f"生成时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
        lines.append("=" * 70)
        
        # 统计各目录文件
        for dir_name in ['spiffs_raw', 'ram_data', 'csv_converted']:
            dir_path = self.data_dir / dir_name
            if dir_path.exists():
                files = list(dir_path.rglob('*'))
                file_count = sum(1 for f in files if f.is_file())
                total_size = sum(f.stat().st_size for f in files if f.is_file())
                lines.append(f"\n📁 {dir_name}/")
                lines.append(f"   文件数: {file_count}")
                lines.append(f"   总大小: {total_size/1024:.1f} KB")
        
        # 列出关键文件
        lines.append("\n" + "-" * 70)
        lines.append("📊 关键数据文件:")
        lines.append("-" * 70)
        
        spiffs_dir = self.data_dir / "spiffs_raw"
        if spiffs_dir.exists():
            for subdir in ['frame_logs_bin', 'frame_logs_csv', 'pop_bin', 'chaos_snapshots', 'individuals']:
                sub_path = spiffs_dir / subdir
                if sub_path.exists():
                    count = len(list(sub_path.glob('*')))
                    lines.append(f"  {subdir}/: {count} 个文件")
        
        csv_dir = self.data_dir / "csv_converted"
        if csv_dir.exists():
            for subdir in ['frame_logs', 'chaos_snapshots', 'populations']:
                sub_path = csv_dir / subdir
                if sub_path.exists():
                    count = len(list(sub_path.glob('*.csv')))
                    lines.append(f"  CSV {subdir}/: {count} 个文件")
        
        lines.append("\n" + "=" * 70)
        lines.append("✅ 报告生成完成")
        
        report_path = self.data_dir / "data_report.txt"
        with open(report_path, 'w', encoding='utf-8') as f:
            f.write('\n'.join(lines))
        
        return '\n'.join(lines)


# ================================================================
# 主程序
# ================================================================

def main():
    parser = argparse.ArgumentParser(description='OEE 数据工具 v4.0 - 适配 v9.21 固件')
    parser.add_argument('--ip', type=str, default='192.168.4.1',
                        help='ESP32 IP 地址 (默认: 192.168.4.1)')
    parser.add_argument('--output', type=str, default='./oee_data',
                        help='输出目录 (默认: ./oee_data)')
    parser.add_argument('--task', choices=['all', 'spiffs', 'ram', 'convert', 'report'], 
                        default='all', help='执行任务')
    parser.add_argument('--local-dir', type=str, default=None,
                        help='本地 SPIFFS 数据目录 (用于 --task convert)')
    args = parser.parse_args()
    
    print("\n" + "=" * 70)
    print("OEE 数据下载与转换工具 v4.0")
    print("适配固件: v9.21-ChaosTuned+Storage")
    print("=" * 70)
    
    spiffs_stats = {'success': 0, 'failed': 0}
    ram_stats = {'success': 0, 'failed': 0}
    convert_stats = {'success': 0, 'failed': 0, 'skipped': 0}
    spiffs_dir = None
    
    # 任务1: 下载 SPIFFS
    if args.task in ['all', 'spiffs']:
        downloader = SPIFFSDownloader(args.ip, args.output)
        spiffs_stats = downloader.download_all()
        spiffs_dir = downloader.output_dir
    else:
        spiffs_dir = Path(args.local_dir) if args.local_dir else Path(args.output) / "spiffs_raw"
    
    # 任务2: 下载 RAM 数据
    if args.task in ['all', 'ram']:
        ram_downloader = RAMDataDownloader(args.ip, args.output)
        ram_stats = ram_downloader.download_all()
    
    # 任务3: BIN 转 CSV
    if args.task in ['all', 'convert']:
        converter = BinToCsvConverter(str(spiffs_dir), args.output)
        convert_stats = converter.convert_all()
    
    # 任务4: 生成报告
    if args.task in ['all', 'report']:
        reporter = ReportGenerator(args.output)
        print(reporter.generate())
    
    print("\n" + "=" * 70)
    print("✅ 完成!")
    print(f"📁 输出: {Path(args.output).absolute()}")
    print("=" * 70)


if __name__ == '__main__':
    sys.exit(main())