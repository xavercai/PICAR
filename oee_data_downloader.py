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

import os
import sys
import struct
import argparse
import requests
import json
from pathlib import Path
from datetime import datetime
from typing import List, Dict

# ================================================================
# 常量
# ================================================================

MAGIC_FILE_HEADER = 0x47454E45
MAGIC_CHAOS_SNAPSHOT = 0x4348534E
MAGIC_POPULATION = 0x47454E45
MAGIC_NOVELTY_ARCHIVE = 0x41524348

SIZE_FILE_HEADER = 24
SIZE_COMPRESSED_FRAME = 12
SIZE_BEHAVIOR_RULE = 14
SIZE_CHAOS_SNAPSHOT_HEADER = 16
SIZE_CHAOS_SNAPSHOT_ENTRY = 12

STATE_NAMES = ['IDLE', 'WALKING', 'STUCK', 'CHAOS']


def crc32_calculate(data: bytes) -> int:
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
# 任务1: 下载 SPIFFS 数据
# ================================================================

class SPIFFSDownloader:
    def __init__(self, ip: str, output_dir: str):
        self.base_url = f"http://{ip}"
        self.output_dir = Path(output_dir) / "spiffs_raw"
        self.output_dir.mkdir(parents=True, exist_ok=True)
        self.stats = {'success': 0, 'failed': 0, 'total_bytes': 0}
    
    def list_files(self) -> List[str]:
        try:
            resp = requests.get(f"{self.base_url}/list/files", timeout=10)
            if resp.status_code == 200:
                return resp.json().get('files', [])
        except Exception as e:
            print(f"  ❌ 获取文件列表失败: {e}")
        return []
    
    def download_file(self, url_path: str, save_name: str, subdir: str = "") -> bool:
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
        except Exception as e:
            print(f"  ❌ {save_name} - {e}")
            self.stats['failed'] += 1
            return False
    
    def download_all(self) -> Dict:
        print("\n" + "=" * 60)
        print("【任务1】下载 SPIFFS 数据")
        print("=" * 60)
        
        print("\n[1/4] 获取文件列表...")
        files = self.list_files()
        if not files:
            print("  ❌ 无法获取文件列表")
            return self.stats
        
        categories = {
            'history': [], 'chaos': [], 'population_summary': [],
            'pop_bin': [], 'frame_logs': [], 'individuals': [],
            'chaos_snapshots': [], 'other': []
        }
        
        for f in files:
            name = f.lstrip('/')
            if name == 'oe_history.csv' or name == 'history.csv':
                categories['history'].append(name)
            elif name.startswith('chaos_g') and name.endswith('.csv'):
                categories['chaos'].append(name)
            elif name == 'chaos_history.csv':
                categories['chaos'].append(name)
            elif name == 'population_summary.csv' or name.startswith('pop_summary'):
                categories['population_summary'].append(name)
            elif name.startswith('pop_gen_') and name.endswith('.bin'):
                categories['pop_bin'].append(name)
            elif name.startswith('frm_') and name.endswith('.bin') and not name.endswith('.inc.bin'):
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
        
        print("\n[2/4] 下载历史与摘要...")
        if categories['history']:
            self.download_file("/download/history", "oe_history.csv")
        if categories['chaos']:
            self.download_file("/download/chaos", "chaos_history.csv")
        if categories['population_summary']:
            self.download_file("/download/population", "population_summary.csv")
        
        print("\n[3/4] 下载种群快照...")
        for f in categories['pop_bin']:
            self.download_file(f"/{f}", f, subdir="pop_bin")
        
        print("\n[4/4] 下载帧日志与个体数据...")
        for f in categories['frame_logs']:
            parts = f.replace('.bin', '').split('_')
            if len(parts) >= 3:
                gen = parts[1]
                ind = parts[2].lstrip('i')
                self.download_file(f"/download/frame?gen={gen}&id={ind}", f, subdir="frame_logs")
        
        for f in categories['individuals']:
            parts = f.replace('.csv', '').split('_')
            if len(parts) >= 4:
                gen = parts[1]
                ind = parts[3]
                self.download_file(f"/download/individual?gen={gen}&id={ind}", f, subdir="individuals")
        
        for f in categories['chaos_snapshots']:
            self.download_file(f"/{f}", f, subdir="chaos_snapshots")
        
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
        
        for endpoint, name in [
            ("/status", "status.json"),
            ("/list/files", "file_list.json"),
            ("/download/history", "history_snapshot.csv"),
            ("/download/chaos", "chaos_snapshot.csv"),
            ("/download/population", "population_snapshot.csv"),
        ]:
            try:
                resp = requests.get(f"{self.base_url}{endpoint}", timeout=30)
                if resp.status_code == 200 and len(resp.content) > 0:
                    with open(self.output_dir / name, 'wb') as f:
                        f.write(resp.content)
                    print(f"  ✅ {name}")
                    self.stats['success'] += 1
            except Exception as e:
                print(f"  ❌ {name} - {e}")
                self.stats['failed'] += 1
        
        return self.stats


# ================================================================
# 任务3: BIN 转 CSV（核心修复版）
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
    # ★★★ 核心修复：帧日志转换 - 使用 int.from_bytes 完全手动解析 ★★★
    # ================================================================
    def _convert_frame_log(self, bin_file: Path):
        """转换帧日志 - 100% 可靠的手动解析"""
        with open(bin_file, 'rb') as f:
            data = f.read()
        
        if len(data) < 24:
            raise ValueError(f"文件太小: {len(data)} < 24")
        
        # === 手动解析 FileHeader (24 bytes) ===
        # 每个字段精确控制字节数，避免 struct.unpack 格式错误
        magic = int.from_bytes(data[0:4], 'little')
        version = int.from_bytes(data[4:6], 'little')
        header_size = int.from_bytes(data[6:8], 'little')
        crc32_val = int.from_bytes(data[8:12], 'little')
        frame_count = int.from_bytes(data[12:16], 'little')
        gen = int.from_bytes(data[16:20], 'little')
        ind = int.from_bytes(data[20:22], 'little')      # ★ 2字节 (uint16_t)
        reserved = int.from_bytes(data[22:24], 'little') # ★ 2字节 (uint16_t)
        
        if magic != 0x47454E45:
            print(f"  ⚠️ {bin_file.name} - 魔数不匹配: 0x{magic:08X}")
        
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
    
    # ================================================================
    # 混沌快照转换
    # ================================================================
    def _convert_chaos_snapshot(self, bin_file: Path):
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
            print(f"  ⚠️ {bin_file.name} - 魔数不匹配: 0x{magic:08X}")
        
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
    
    # ================================================================
    # 种群转换
    # ================================================================
    def _convert_population(self, bin_file: Path):
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
            f.write(f"# Population: version=0x{version:04X}, popSize={pop_size}, gen={gen}\n")
            f.write("individual,ruleCount,survivalTime_ms,distanceTicks,noveltyScore,chaosTimeoutMs\n")
            
            offset = 16
            for i in range(min(pop_size, 16)):
                if offset >= len(data):
                    break
                rule_count = data[offset]
                offset += 1
                
                # 跳过规则
                offset += rule_count * 14
                
                if offset + 12 > len(data):
                    break
                survival = int.from_bytes(data[offset:offset+4], 'little')
                distance = int.from_bytes(data[offset+4:offset+8], 'little')
                novelty = struct.unpack_from('<f', data, offset+8)[0]
                offset += 12
                
                if offset + 22 > len(data):
                    break
                chaos_timeout = int.from_bytes(data[offset+18:offset+20], 'little')
                offset += 22
                
                # 跳过混沌标记
                if version >= 0x0006:
                    offset += 3
                
                f.write(f"{i},{rule_count},{survival},{distance},{novelty:.6f},{chaos_timeout}\n")
        
        print(f"  ✅ {bin_file.name} → {csv_path.name} ({pop_size} 个个体)")
        self.stats['success'] += 1
    
    # ================================================================
    # 新颖度存档转换
    # ================================================================
    def _convert_novelty_archive(self, bin_file: Path):
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
            f.write(f"# Novelty Archive: count={count}, version={version}\n")
            f.write("index,leftSensorMean,rightSensorMean,sensorVariance,sensorAsymmetry,avgSpeed,speedVariance,turnBias,totalDistance,forwardRatio,turnRatio,reverseRatio,idleRatio\n")
            
            if count > 0:
                offset = 12 + 12 * 4
                for i in range(count):
                    if offset + 48 > len(data):
                        break
                    values = struct.unpack_from('<ffffffffffff', data, offset)
                    offset += 48
                    f.write(f"{i}," + ",".join(f"{v:.6f}" for v in values) + "\n")
        
        print(f"  ✅ {bin_file.name} → {csv_path.name} ({count} 条)")
        self.stats['success'] += 1


# ================================================================
# 主程序
# ================================================================

def main():
    parser = argparse.ArgumentParser(description='OEE 数据工具 v3.2')
    parser.add_argument('--ip', type=str, default='192.168.4.1')
    parser.add_argument('--output', type=str, default='./oee_data')
    parser.add_argument('--task', choices=['all', 'spiffs', 'ram', 'convert'], default='all')
    parser.add_argument('--local-dir', type=str, default=None)
    args = parser.parse_args()
    
    print("\n" + "=" * 70)
    print("OEE 数据下载与转换工具 v3.2")
    print("=" * 70)
    
    spiffs_stats = {'success': 0, 'failed': 0}
    ram_stats = {'success': 0, 'failed': 0}
    convert_stats = {'success': 0, 'failed': 0, 'skipped': 0}
    spiffs_dir = None
    
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
        converter = BinToCsvConverter(str(spiffs_dir), args.output)
        convert_stats = converter.convert_all()
    
    print("\n" + "=" * 70)
    print("✅ 完成!")
    print(f"📁 输出: {Path(args.output).absolute()}")
    print("=" * 70)


if __name__ == '__main__':
    sys.exit(main())