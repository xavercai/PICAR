#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
================================================================
OEE 数据下载与转换工具 v5.8 - 适配 v10.6-WebDiag 固件
仿草履虫应激机制 - 脱困能力进化系统 数据导出工具
================================================================

【v5.8 更新说明 — 种群数据深度校验】
    1. L4 新增: 80% 个体 survivalTime=0 检测 (固件导出 Bug 告警)
    2. L3 增强: 行为比例归一化 (仅对 survivalTime>0 的个体)
    3. L4 新增: sensorVariance 极端值告警
    4. L4 新增: chaosMinPwm 语义提示 (值过小时告警)
    5. 保留 v5.7 全部修复

【v5.7 更新说明 — 修复固件 off-by-one】
    1. 【P0】ChaosSnapshotHeader 动态探测真实 payload 偏移
    2. 【P0】novelty_archive count off-by-one 容错
    3. 放宽 sensor/noise/motor 字段约束至类型全范围
    4. L3 批量失败阈值从 50% 调整为 90%

【v5.6 更新说明 — 契约校验层集成】
    1. L1: Magic Number 强校验 (ABORT)
    2. L2: Struct Size 断言 + count 声明校验 (ABORT)
    3. L3: FieldConstraint 字段级校验 (WARN/ABORT)
    4. L4: SemanticValidator 语义校验 (WARN)
    5. 生成统一 JSON 校验报告 validation_report.json

【用法】
    python downparse.py --ip 192.168.4.1 --output ./oee_data
    python downparse.py --task convert --local-dir ./oee_data/spiffs_raw
    python downparse.py --ip 192.168.4.1 --diag

【依赖】
    pip install requests
================================================================
"""


import os
import sys
import struct
import socket
import time
import argparse
import requests
import json
import re
from pathlib import Path
from datetime import datetime, timezone
from typing import List, Dict, Tuple, Optional, Any

# ===== 契约校验层导入 =====
from contract_validator import (
    ContractViolationError, SemanticWarning,
    validate_magic_number, validate_file_size, validate_struct_layout,
    check_schema, validate_record_fields, validate_timestamp_monotonicity,
    validate_behavior_ratio, SemanticValidator, ValidationReport,
)
from version_table import VERSION_LAYOUT_MAP, SUPPORTED_VERSIONS, get_layout
from constraints import FIELD_CONSTRAINTS, TEST_DURATION_MS


# ================================================================
# 常量定义
# ================================================================

MAGIC_GENE_POP       = 0x47454E45  # 'GENE' 种群快照
MAGIC_FRAME_LOG      = 0x46524D45  # 'FRME' 帧日志
MAGIC_NOVA_ARCHIVE   = 0x41564F4E  # 'NOVA' 新奇度档案 (小端)
MAGIC_ARCH_OLD       = 0x48435241  # 'ARCH' 旧版新奇度档案 (小端)
MAGIC_CHAOS_SNAPSHOT = 0x4348534E  # 'CHSN' 混沌快照
MAGIC_NOVG           = 0x47564F4E  # 'NOVG' nova_gen 增量快照 (小端)

EXPECTED_FW_PREFIX   = "v10."

SIZE_GENE_HEADER           = 16
SIZE_FRAME_LOG_HEADER      = 24
SIZE_FILE_HEADER_OLD       = 24
SIZE_COMPRESSED_FRAME      = 11
SIZE_BEHAVIOR_RULE         = 12
SIZE_BEHAVIOR_DESCRIPTOR        = 64
SIZE_BEHAVIOR_DESCRIPTOR_LEGACY = 48
SIZE_CHAOS_SNAPSHOT_HEADER = 16
SIZE_CHAOS_SNAPSHOT_ENTRY_V1 = 12
SIZE_CHAOS_SNAPSHOT_ENTRY_V2 = 16
SIZE_NOVA_GEN_HEADER       = 12

MAX_RULES = 16
MIN_RULES = 2
DEFAULT_POP_SIZE = 8

COND_TYPE_MAX = 7
COND_OP_MAX   = 2
NEXT_RULE_MAX = 15
SENSOR_ABS_MAX = 32000
TIMESTAMP_MAX_MS = 3600000

STATE_NAMES = ['IDLE', 'WALKING', 'STUCK', 'CHAOS']

COND_NAMES = ['L', 'R', 'BOTH', 'ANY', 'DIST', 'TIME', 'IDLE', 'ALWAYS']
COND_TYPE_FULL_NAMES = {
    0: 'COND_SENSOR_LEFT', 1: 'COND_SENSOR_RIGHT',
    2: 'COND_SENSOR_BOTH', 3: 'COND_SENSOR_ANY',
    4: 'COND_DISTANCE',   5: 'COND_TIME',
    6: 'COND_IDLE',       7: 'COND_ALWAYS',
}
COND_OP_NAMES = {0: 'OP_GREATER', 1: 'OP_LESS', 2: 'OP_EQUAL'}

ENDPOINTS = {
    'list_files': '/list/files',
    'status': '/status',
    'schema': '/schema.json',
    'history': '/download/history',
    'chaos_history': '/download/chaos',
    'population_summary': '/download/population',
    'individual': '/download/individual?gen={gen}&id={id}',
    'pop_bin': '/download/pop?gen={gen}',
    'frame_bin': '/download/frame_bin?gen={gen}&id={id}',
    'chaos_snap_bin': '/download/chaos_snap?gen={gen}&id={id}',
    'novelty_bin': '/download/novelty',
    'nova_gen_bin': '/download/nova_gen?gen={gen}',
}


# ================================================================
# 网络诊断与重试工具函数
# ================================================================
def check_connectivity(ip: str, port: int = 80, timeout: float = 5.0) -> Dict[str, Any]:
    result = {
        'ip': ip, 'port': port,
        'socket_ok': False, 'urllib_ok': False,
        'requests_ok': False, 'dns_ok': False, 'errors': []
    }

    try:
        socket.gethostbyname(ip)
        result['dns_ok'] = True
    except socket.gaierror as e:
        result['errors'].append(f"DNS解析失败: {e}")

    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(timeout)
        start = time.time()
        s.connect((ip, port))
        elapsed = time.time() - start
        s.close()
        result['socket_ok'] = True
        print(f"  [socket] 连接成功 (耗时 {elapsed:.2f}s)")
    except socket.timeout:
        result['errors'].append("Socket连接超时")
    except socket.error as e:
        result['errors'].append(f"Socket连接失败: {e}")
    except Exception as e:
        result['errors'].append(f"Socket异常: {e}")

    try:
        import urllib.request
        req = urllib.request.Request(f"http://{ip}:{port}/")
        resp = urllib.request.urlopen(req, timeout=timeout)
        result['urllib_ok'] = True
        print(f"  [urllib] 请求成功, 状态: {resp.status}")
    except Exception as e:
        result['errors'].append(f"urllib请求失败: {e}")

    try:
        resp = requests.get(f"http://{ip}:{port}/", timeout=timeout)
        result['requests_ok'] = True
        print(f"  [requests] 请求成功, 状态: {resp.status}")
    except Exception as e:
        result['errors'].append(f"requests请求失败: {e}")

    return result


def http_get(url: str, timeout: float = 30.0, max_retries: int = 3) -> Optional[requests.Response]:
    session = requests.Session()
    session.headers.update({'Connection': 'close'})

    last_exception = None
    for attempt in range(max_retries):
        try:
            resp = session.get(url, timeout=timeout)
            session.close()
            return resp
        except requests.exceptions.ConnectionError as e:
            last_exception = e
            wait_time = 2 ** attempt
            print(f"    连接失败 (尝试 {attempt+1}/{max_retries}), {wait_time}s 后重试...")
            time.sleep(wait_time)
        except requests.exceptions.Timeout as e:
            last_exception = e
            wait_time = 2 ** attempt
            print(f"    请求超时 (尝试 {attempt+1}/{max_retries}), {wait_time}s 后重试...")
            time.sleep(wait_time)
        except Exception as e:
            last_exception = e
            wait_time = 2 ** attempt
            print(f"    异常 (尝试 {attempt+1}/{max_retries}): {e}, {wait_time}s 后重试...")
            time.sleep(wait_time)

    session.close()
    raise last_exception


def detect_firmware_version(ip: str, timeout: float = 5.0) -> str:
    try:
        resp = http_get(f"http://{ip}/status", timeout=timeout, max_retries=1)
        if resp.status_code == 200:
            data = resp.json()
            if 'firmwareVersion' in data:
                return str(data['firmwareVersion'])
            return "unknown"
    except Exception:
        pass
    return "unknown"


# ================================================================
# CRC32 校验
# ================================================================
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
# 文件名解析
# ================================================================
def parse_filename(name: str) -> Dict[str, Any]:
    result = {'gen': None, 'id': None, 'type': None}

    match = re.match(r'pop_gen_(\d+)\.bin', name)
    if match:
        result['type'] = 'pop_bin'
        result['gen'] = int(match.group(1))
        return result

    match = re.match(r'frm_(\d+)_i(\d+)\.(?:inc\.)?bin', name)
    if match:
        result['type'] = 'frame_bin'
        result['gen'] = int(match.group(1))
        result['id'] = int(match.group(2))
        return result

    match = re.match(r'chaos_snaps_g(\d+)_i(\d+)\.bin', name)
    if match:
        result['type'] = 'chaos_snap_bin'
        result['gen'] = int(match.group(1))
        result['id'] = int(match.group(2))
        return result

    match = re.match(r'gen_(\d+)_id_(\d+)\.csv', name)
    if match:
        result['type'] = 'individual_csv'
        result['gen'] = int(match.group(1))
        result['id'] = int(match.group(2))
        return result

    match = re.match(r'chaos_g(\d+)_i(\d+)\.csv', name)
    if match:
        result['type'] = 'chaos_record_csv'
        result['gen'] = int(match.group(1))
        result['id'] = int(match.group(2))
        return result

    match = re.match(r'nova_gen_(\d+)\.bin', name)
    if match:
        result['type'] = 'nova_gen_bin'
        result['gen'] = int(match.group(1))
        return result

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
# 结构体格式串
# ================================================================
RULE_STRUCT         = struct.Struct('<BhBhhHBB')       # 12
DESC_STRUCT         = struct.Struct('<16f')            # 64 (v10.8: 12f → 16f)
DESC_STRUCT_LEGACY  = struct.Struct('<12f')            # 48 (v10.7 及以前)
FRAME_STRUCT        = struct.Struct('<IhhbbB')         # 11
GENE_HEADER_STRUCT  = struct.Struct('<IHHII')          # 16
FRAME_HEADER_STRUCT = struct.Struct('<IHHIIIHH')       # 24
CHAOS_HDR_STRUCT    = struct.Struct('<IHHIB3s')        # 16
CHAOS_SNAP_STRUCT_V1 = struct.Struct('<hhbbHI')        # 12
CHAOS_SNAP_STRUCT_V2 = struct.Struct('<hhbbHIhh')      # 16
INDIV_FIXED_STRUCT  = struct.Struct('<IIfhhhhhhBhhhhBBB')  # 36
NOVA_GEN_HDR_STRUCT = struct.Struct('<IHHI')           # 12

assert RULE_STRUCT.size == 12
assert DESC_STRUCT.size == 64
assert DESC_STRUCT_LEGACY.size == 48
assert FRAME_STRUCT.size == 11
assert GENE_HEADER_STRUCT.size == 16
assert FRAME_HEADER_STRUCT.size == 24
assert CHAOS_HDR_STRUCT.size == 16
assert CHAOS_SNAP_STRUCT_V1.size == 12
assert CHAOS_SNAP_STRUCT_V2.size == 16
assert INDIV_FIXED_STRUCT.size == 36
assert NOVA_GEN_HDR_STRUCT.size == 12


# ================================================================
# 历史 CSV 时间戳语义校验
# ================================================================
def validate_history_csv(csv_path: Path) -> Dict[str, Any]:
    result = {
        'path': str(csv_path), 'total_rows': 0, 'valid_rows': 0,
        'invalid_ts_rows': 0, 'ts_min': None, 'ts_max': None,
        'is_v103_semantics': False,
    }

    if not csv_path.exists():
        return result

    with open(csv_path, 'r', encoding='utf-8') as f:
        lines = f.readlines()

    data_lines = [l.strip() for l in lines if l.strip() and not l.startswith('#')]
    if data_lines and data_lines[0].startswith('timestamp'):
        data_lines = data_lines[1:]

    ts_values = []
    for line in data_lines:
        parts = line.split(',')
        if len(parts) < 1:
            continue
        try:
            ts = int(parts[0])
            ts_values.append(ts)
            result['total_rows'] += 1
            if 0 <= ts <= TIMESTAMP_MAX_MS:
                result['valid_rows'] += 1
            else:
                result['invalid_ts_rows'] += 1
        except ValueError:
            continue

    if ts_values:
        result['ts_min'] = min(ts_values)
        result['ts_max'] = max(ts_values)
        result['is_v103_semantics'] = (result['ts_max'] < TIMESTAMP_MAX_MS)

    return result


# ================================================================
# 任务1: 下载 SPIFFS 数据
# ================================================================
class SPIFFSDownloader:
    def __init__(self, ip: str, output_dir: str):
        self.ip = ip
        self.base_url = f"http://{ip}"
        self.output_dir = Path(output_dir) / "spiffs_raw"
        self.output_dir.mkdir(parents=True, exist_ok=True)
        self.stats = {'success': 0, 'failed': 0, 'total_bytes': 0}

        self.subdirs = {
            'pop_bin': self.output_dir / 'pop_bin',
            'frame_bin': self.output_dir / 'frame_bin',
            'chaos_snap_bin': self.output_dir / 'chaos_snap_bin',
            'nova_gen_bin': self.output_dir / 'nova_gen_bin',
            'individual_csv': self.output_dir / 'individual_csv',
            'chaos_record_csv': self.output_dir / 'chaos_record_csv',
        }
        for d in self.subdirs.values():
            d.mkdir(parents=True, exist_ok=True)

    def check_schema(self) -> Optional[Dict]:
        try:
            resp = http_get(f"{self.base_url}{ENDPOINTS['schema']}")
            if resp.status_code == 200:
                schema = resp.json()
                print("\n  📐 固件 schema:")
                for k, v in schema.items():
                    print(f"     {k} = {v}")

                local_schema = {
                    'BehaviorRule': RULE_STRUCT.size,
                    'FrameLogEntry': FRAME_STRUCT.size,
                }
                check_schema(schema, local_schema, "SchemaCheck",
                             file_path=f"{self.base_url}{ENDPOINTS['schema']}")
                print("  ✅ Schema 校验通过")
                return schema
        except ContractViolationError:
            raise
        except Exception as e:
            print(f"  ⚠️ 无法获取 schema.json: {e}")
        return None

    def list_files(self) -> List[str]:
        try:
            resp = http_get(f"{self.base_url}{ENDPOINTS['list_files']}")
            if resp.status_code == 200:
                data = resp.json()
                return data.get('files', [])
        except Exception as e:
            print(f"  ❌ 获取文件列表失败: {e}")
        return []

    def download_file(self, url_path: str, save_name: str, subdir: str = "") -> bool:
        url = f"{self.base_url}{url_path}"
        save_dir = self.output_dir / subdir if subdir else self.output_dir
        save_dir.mkdir(parents=True, exist_ok=True)
        save_path = save_dir / save_name

        try:
            session = requests.Session()
            session.headers.update({'Connection': 'close'})
            resp = session.get(url, timeout=60, stream=True)
            if resp.status_code == 200 and len(resp.content) > 0:
                with open(save_path, 'wb') as f:
                    f.write(resp.content)
                self.stats['success'] += 1
                self.stats['total_bytes'] += len(resp.content)
                print(f"  ✅ {save_name} ({len(resp.content):,} bytes)")
                session.close()
                return True
            else:
                print(f"  ⚠️ {save_name} - HTTP {resp.status_code}")
                self.stats['failed'] += 1
                session.close()
                return False
        except Exception as e:
            print(f"  ❌ {save_name} - {e}")
            self.stats['failed'] += 1
            return False

    def download_all(self) -> Dict:
        print("\n" + "=" * 70)
        print("【任务1】下载 SPIFFS 数据")
        print("=" * 70)

        fw_ver = detect_firmware_version(self.ip)
        if fw_ver != "unknown":
            print(f"\n  固件版本: {fw_ver}")
            if not fw_ver.startswith(EXPECTED_FW_PREFIX):
                print(f"  ⚠️ 建议升级到 {EXPECTED_FW_PREFIX}x")
        else:
            print(f"\n  ⚠️ 无法检测固件版本")

        print("\n[0/6] 校验固件 schema...")
        self.check_schema()

        print("\n[1/6] 获取文件列表...")
        files = self.list_files()
        if not files:
            print("  ❌ 无法获取文件列表")
            return self.stats

        categories = {
            'oe_history': [], 'chaos_history': [], 'population_summary': [],
            'pop_bin': [], 'frame_bin': [], 'individual_csv': [],
            'chaos_record_csv': [], 'chaos_snap_bin': [],
            'novelty_archive': [], 'nova_gen_bin': [], 'experiment_state': [],
            'version_marker': [], 'unknown': []
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
        print(f"  新颖度增量: {len(categories['nova_gen_bin'])}")

        print("\n[2/6] 下载历史与摘要...")
        if categories['oe_history']:
            self.download_file(ENDPOINTS['history'], "oe_history.csv")
        if categories['chaos_history']:
            self.download_file(ENDPOINTS['chaos_history'], "chaos_history.csv")
        if categories['population_summary']:
            self.download_file(ENDPOINTS['population_summary'], "population_summary.csv")

        if categories['novelty_archive']:
            ok = self.download_file(ENDPOINTS['novelty_bin'], "novelty_archive.bin")
            if not ok:
                self.download_file("/novelty_archive.bin", "novelty_archive.bin")

        print("\n[3/6] 下载新颖度增量快照...")
        for name, info in categories['nova_gen_bin']:
            gen = info['gen']
            url = ENDPOINTS['nova_gen_bin'].format(gen=gen)
            self.download_file(url, name, subdir="nova_gen_bin")

        print("\n[4/6] 下载种群BIN...")
        for name, info in categories['pop_bin']:
            gen = info['gen']
            url = ENDPOINTS['pop_bin'].format(gen=gen)
            self.download_file(url, name, subdir="pop_bin")

        print("\n[5/6] 下载帧日志...")
        for name, info in categories['frame_bin']:
            gen = info['gen']
            id_ = info['id']
            url = ENDPOINTS['frame_bin'].format(gen=gen, id=id_)
            self.download_file(url, name, subdir="frame_bin")

        print("\n[6/6] 下载个体CSV、混沌记录、混沌快照...")
        for name, info in categories['individual_csv']:
            gen = info['gen']
            id_ = info['id']
            url = ENDPOINTS['individual'].format(gen=gen, id=id_)
            self.download_file(url, name, subdir="individual_csv")

        for name, info in categories['chaos_record_csv']:
            self.download_file(f"/{name}", name, subdir="chaos_record_csv")

        for name, info in categories['chaos_snap_bin']:
            gen = info['gen']
            id_ = info['id']
            url = ENDPOINTS['chaos_snap_bin'].format(gen=gen, id=id_)
            self.download_file(url, name, subdir="chaos_snap_bin")

        other_files = categories['experiment_state'] + categories['version_marker']
        if other_files:
            print("\n  下载其他文件...")
            for name, info in other_files:
                self.download_file(f"/{name}", name)

        print(f"\n📁 保存到: {self.output_dir}")
        print(f"   成功: {self.stats['success']}, 失败: {self.stats['failed']}, "
              f"总大小: {self.stats['total_bytes']:,} bytes")
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
            (ENDPOINTS['schema'], "schema.json"),
            (ENDPOINTS['history'], "history_snapshot.csv"),
            (ENDPOINTS['chaos_history'], "chaos_snapshot.csv"),
            (ENDPOINTS['population_summary'], "population_snapshot.csv"),
        ]

        for endpoint, name in endpoints:
            try:
                resp = http_get(f"{self.base_url}{endpoint}", timeout=30)
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
# 任务3: BIN 转 CSV (含契约校验)
# ================================================================
class BinToCsvConverter:
    def __init__(self, input_dir: str, output_dir: str):
        self.input_dir = Path(input_dir)
        self.output_dir = Path(output_dir) / "csv_converted"
        self.output_dir.mkdir(parents=True, exist_ok=True)

        self.report = ValidationReport(script_version="v5.8")

        self.subdirs = {
            'frame_bin': self.output_dir / "frame_logs",
            'chaos_snap_bin': self.output_dir / "chaos_snapshots",
            'pop_bin': self.output_dir / "populations",
            'nova_gen_bin': self.output_dir / "nova_gen",
        }
        for d in self.subdirs.values():
            d.mkdir(parents=True, exist_ok=True)

        self.stats = {'success': 0, 'failed': 0, 'skipped': 0,
                      'contract_violations': 0}

    def convert_all(self) -> Dict:
        print("\n" + "=" * 70)
        print("【任务3】BIN 转 CSV (含契约校验)")
        print("=" * 70)

        bin_files = sorted(self.input_dir.rglob("*.bin"))
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
                elif name.startswith('nova_gen_'):
                    self._convert_nova_gen(bin_file)
                else:
                    self.stats['skipped'] += 1
            except ContractViolationError as e:
                print(f"  ❌ [契约违规] {name} - {e}")
                self.report.add_exception(e, str(bin_file))
                self.stats['contract_violations'] += 1
                self.stats['failed'] += 1
            except Exception as e:
                print(f"  ❌ {name} - {e}")
                self.stats['failed'] += 1

        report_path = self.output_dir / "validation_report.json"
        self.report.save(str(report_path))
        print(f"\n  📋 校验报告: {report_path}")
        print(f"     汇总: {self.report.summary()}")

        print(f"\n   成功: {self.stats['success']}, 失败: {self.stats['failed']}, "
              f"跳过: {self.stats['skipped']}, 契约违规: {self.stats['contract_violations']}")
        return self.stats

    # ================================================================
    # 帧日志转换
    # ================================================================
    def _convert_frame_log(self, bin_file: Path):
        with open(bin_file, 'rb') as f:
            data = f.read()

        file_path = str(bin_file)
        ctx = "FrameLog"

        validate_file_size(data, SIZE_FRAME_LOG_HEADER, ctx, file_path)
        validate_magic_number(data, MAGIC_FRAME_LOG, ctx, file_path)

        magic, version, header_size, crc32_val, frame_count, gen, ind, reserved = \
            FRAME_HEADER_STRUCT.unpack_from(data, 0)

        validate_struct_layout("FrameLogHeader", FRAME_HEADER_STRUCT.format,
                               SIZE_FRAME_LOG_HEADER, ctx, file_path)
        validate_struct_layout("CompressedFrameEntry", FRAME_STRUCT.format,
                               SIZE_COMPRESSED_FRAME, ctx, file_path)

        frames_data = data[header_size:]
        actual_count = len(frames_data) // SIZE_COMPRESSED_FRAME

        if frame_count != actual_count and actual_count > 0:
            raise ContractViolationError(
                f"[{ctx}] 帧数声明错误: 期望 {frame_count}, 实际 {actual_count}.",
                check_id="L2-COUNT-FRAMELOG", layer="L2",
                expected=str(frame_count), actual=str(actual_count),
                file_path=file_path, field_name="frame_count",
            )

        if frame_count > 0 and crc32_val != 0:
            calc_crc = crc32_calculate(frames_data[:frame_count * SIZE_COMPRESSED_FRAME])
            if calc_crc != crc32_val:
                self.report.add({
                    'check_id': 'L1-CRC-FRAMELOG', 'layer': 'L1',
                    'file_path': file_path, 'field_name': 'crc32',
                    'expected': f"0x{crc32_val:08X}", 'actual': f"0x{calc_crc:08X}",
                    'status': 'WARN',
                    'message': f'CRC32 不匹配 (固件算法可能不同)',
                    'timestamp': datetime.now(timezone.utc).isoformat(),
                })

        csv_path = self.subdirs['frame_bin'] / f"{bin_file.stem}.csv"
        overflow_count = 0
        with open(csv_path, 'w', encoding='utf-8') as f:
            f.write("timestamp_ms,sensorLeft,sensorRight,motorLeftPWM,motorRightPWM,"
                    "directionL,directionR,chaosActive,isChaosFrame,state,stateName\n")
            pwmL, pwmR = 0, 0
            for i in range(frame_count):
                off = i * SIZE_COMPRESSED_FRAME
                if off + SIZE_COMPRESSED_FRAME > len(frames_data):
                    break
                ts, sL, sR, mL, mR, flags = FRAME_STRUCT.unpack_from(frames_data, off)

                directionL = flags & 0x01
                directionR = (flags >> 1) & 0x01
                chaosActive = (flags >> 2) & 0x01
                isChaosFrame = (flags >> 3) & 0x01
                state = (flags >> 4) & 0x07
                stateName = STATE_NAMES[state] if state < 4 else f"UNKNOWN({state})"

                if abs(sL) > SENSOR_ABS_MAX or abs(sR) > SENSOR_ABS_MAX:
                    overflow_count += 1

                if i == 0:
                    pwmL = mL & 0xFF
                    pwmR = mR & 0xFF
                else:
                    pwmL = max(0, min(255, pwmL + mL))
                    pwmR = max(0, min(255, pwmR + mR))

                f.write(f"{ts},{sL},{sR},{pwmL},{pwmR},"
                        f"{directionL},{directionR},"
                        f"{chaosActive},{isChaosFrame},"
                        f"{state},{stateName}\n")

        if overflow_count > 0:
            print(f"  ⚠️ {bin_file.name}: 共 {overflow_count} 帧传感器值疑似溢出")

        self.report.add_pass("L1-MAGIC-FRAMELOG", "L1", file_path, "magic",
                             f"0x{MAGIC_FRAME_LOG:08X}", f"0x{magic:08X}",
                             "Magic Number 匹配")
        print(f"  ✅ {bin_file.name} → {csv_path.name} ({frame_count} 帧)")
        self.stats['success'] += 1

    # ================================================================
    # 混沌快照转换 (动态探测 header 偏移)
    # ================================================================
    def _convert_chaos_snapshot(self, bin_file: Path):
        with open(bin_file, 'rb') as f:
            data = f.read()

        file_path = str(bin_file)
        ctx = "ChaosSnapshot"

        validate_file_size(data, SIZE_CHAOS_SNAPSHOT_HEADER, ctx, file_path)
        validate_magic_number(data, MAGIC_CHAOS_SNAPSHOT, ctx, file_path)

        magic, version, hsize, crc32_val, count, reserved = \
            CHAOS_HDR_STRUCT.unpack_from(data, 0)

        validate_struct_layout("ChaosSnapshotHeader", CHAOS_HDR_STRUCT.format,
                               SIZE_CHAOS_SNAPSHOT_HEADER, ctx, file_path)

        is_v2 = (version >= 0x0002)
        if is_v2:
            fmt = CHAOS_SNAP_STRUCT_V2
            entry_size = SIZE_CHAOS_SNAPSHOT_ENTRY_V2
        else:
            fmt = CHAOS_SNAP_STRUCT_V1
            entry_size = SIZE_CHAOS_SNAPSHOT_ENTRY_V1

        validate_struct_layout(
            f"ChaosSnapshotEntry_v{'2' if is_v2 else '1'}",
            fmt.format, entry_size, ctx, file_path
        )

        # ✅ 动态探测真实 payload 起始偏移
        payload_start = hsize
        detected_delta = 0

        for delta in range(0, 8):
            candidate = hsize + delta
            if candidate >= len(data):
                break
            payload_len = len(data) - candidate
            if payload_len % entry_size == 0:
                actual_count = payload_len // entry_size
                if actual_count == count:
                    payload_start = candidate
                    detected_delta = delta
                    break

        if detected_delta > 0:
            print(f"  ℹ️ {bin_file.name}: header 声明 {hsize} 字节, "
                  f"实际偏移 {payload_start} (修正 +{detected_delta})")
            self.report.add({
                'check_id': 'L2-HEADER-OFFSET-CHAOS', 'layer': 'L2',
                'file_path': file_path, 'field_name': 'header_size',
                'expected': str(hsize), 'actual': str(payload_start),
                'status': 'WARN',
                'message': (f'固件 header 声明 {hsize} 字节, 实际 payload '
                            f'从 {payload_start} 开始 (off-by-one +{detected_delta})'),
                'timestamp': datetime.now(timezone.utc).isoformat(),
            })
        else:
            print(f"  ⚠️ {bin_file.name}: payload 偏移符合声明 {hsize}")

        payload = data[payload_start:]

        actual_count = len(payload) // entry_size
        if count != actual_count and actual_count > 0:
            raise ContractViolationError(
                f"[{ctx}] count 声明错误: 期望 {count}, 实际 {actual_count}.",
                check_id="L2-COUNT-CHAOS", layer="L2",
                expected=str(count), actual=str(actual_count),
                file_path=file_path, field_name="count",
            )

        if crc32_val != 0 and len(payload) > 0:
            calc = crc32_calculate(payload)
            if calc != crc32_val:
                self.report.add({
                    'check_id': 'L1-CRC-CHAOS', 'layer': 'L1',
                    'file_path': file_path, 'field_name': 'crc32',
                    'expected': f"0x{crc32_val:08X}", 'actual': f"0x{calc:08X}",
                    'status': 'WARN',
                    'message': f'CRC32 不匹配 (固件算法可能不同)',
                    'timestamp': datetime.now(timezone.utc).isoformat(),
                })

        csv_path = self.subdirs['chaos_snap_bin'] / f"{bin_file.stem}.csv"
        records = []
        with open(csv_path, 'w', encoding='utf-8') as f:
            f.write("index,sensorLeft,sensorRight,motorLeftPWM,motorRightPWM,"
                    "durationMs,timestamp_ms,rawNoiseL,rawNoiseR\n")
            written = 0
            for i in range(count):
                off = i * entry_size
                if off + entry_size > len(payload):
                    break
                if is_v2:
                    sL, sR, mL, mR, dur, ts, rawL, rawR = fmt.unpack_from(payload, off)
                    rec = {'index': i, 'sensorLeft': sL, 'sensorRight': sR,
                           'motorLeftPWM': mL, 'motorRightPWM': mR,
                           'durationMs': dur, 'timestamp_ms': ts,
                           'rawNoiseL': rawL, 'rawNoiseR': rawR}
                    f.write(f"{i},{sL},{sR},{mL},{mR},{dur},{ts},{rawL},{rawR}\n")
                else:
                    sL, sR, mL, mR, dur, ts = fmt.unpack_from(payload, off)
                    rec = {'index': i, 'sensorLeft': sL, 'sensorRight': sR,
                           'motorLeftPWM': mL, 'motorRightPWM': mR,
                           'durationMs': dur, 'timestamp_ms': ts,
                           'rawNoiseL': 0, 'rawNoiseR': 0}
                    f.write(f"{i},{sL},{sR},{mL},{mR},{dur},{ts},0,0\n")
                records.append(rec)
                written += 1

        field_fail_count = 0
        for rec in records:
            results = validate_record_fields(rec, FIELD_CONSTRAINTS, ctx, file_path)
            for r in results:
                if r['status'] == 'FAIL':
                    field_fail_count += 1
                self.report.add(r)

        if records and field_fail_count / len(records) > 0.9:
            raise ContractViolationError(
                f"[{ctx}] L3 字段校验批量失败: {field_fail_count}/{len(records)} "
                f"条记录越界.",
                check_id="L3-BATCH-CHAOS", layer="L3",
                expected="< 90% 记录越界",
                actual=f"{field_fail_count}/{len(records)}",
                file_path=file_path, field_name="batch",
            )

        timestamps = [r['timestamp_ms'] for r in records]
        status, msg = validate_timestamp_monotonicity(timestamps, ctx)
        if status == 'FAIL':
            self.report.add({
                'check_id': 'L3-TS-MONO-CHAOS', 'layer': 'L3',
                'file_path': file_path, 'field_name': 'timestamp_ms',
                'expected': '单调递增', 'actual': '存在回退',
                'status': 'WARN', 'message': msg,
                'timestamp': datetime.now(timezone.utc).isoformat(),
            })

        self.report.add_pass("L1-MAGIC-CHAOS", "L1", file_path, "magic",
                             f"0x{MAGIC_CHAOS_SNAPSHOT:08X}", f"0x{magic:08X}",
                             "Magic Number 匹配")
        print(f"  ✅ {bin_file.name} → {csv_path.name} "
              f"({written} 条, v{version}, payload@{payload_start})")
        self.stats['success'] += 1

    # ================================================================
    # 种群转换 (v5.8 增强 L3/L4)
    # ================================================================
    def _convert_population(self, bin_file: Path):
        with open(bin_file, 'rb') as f:
            data = f.read()

        file_path = str(bin_file)
        ctx = "Population"

        validate_file_size(data, SIZE_GENE_HEADER, ctx, file_path)
        validate_magic_number(data, MAGIC_GENE_POP, ctx, file_path)

        magic, version, pop_size, gen, exp_id = GENE_HEADER_STRUCT.unpack_from(data, 0)

        validate_struct_layout("GeneBinaryHeader", GENE_HEADER_STRUCT.format,
                               SIZE_GENE_HEADER, ctx, file_path)
        validate_struct_layout("BehaviorRule", RULE_STRUCT.format,
                               SIZE_BEHAVIOR_RULE, ctx, file_path)
        validate_struct_layout("IndividualFixed", INDIV_FIXED_STRUCT.format,
                               INDIV_FIXED_STRUCT.size, ctx, file_path)

        print(f"  📊 {bin_file.name}: gen={gen}, popSize={pop_size}, "
              f"version=0x{version:04X}, expId={exp_id}")

        csv_path = self.subdirs['pop_bin'] / f"{bin_file.stem}.csv"
        rules_csv_path = self.subdirs['pop_bin'] / f"{bin_file.stem}_rules.csv"

        offset = SIZE_GENE_HEADER
        individuals = []
        survival_times = []
        illegal_cond_type = 0
        illegal_cond_op = 0
        illegal_next_rule = 0

        for i in range(pop_size):
            if offset + 1 > len(data):
                print(f"  ⚠️ 个体 {i}: 数据不足")
                break

            rule_count = data[offset]; offset += 1
            if rule_count > MAX_RULES:
                rule_count = MAX_RULES

            rules = []
            for j in range(rule_count):
                if offset + SIZE_BEHAVIOR_RULE > len(data):
                    break
                r = RULE_STRUCT.unpack_from(data, offset)
                cond_type = r[0]
                cond_value = r[1]
                cond_op   = r[2]
                motor_l   = r[3]
                motor_r   = r[4]
                duration_ms = r[5]
                next_rule = r[6]
                _padding  = r[7]

                if cond_type > COND_TYPE_MAX:
                    illegal_cond_type += 1
                if cond_op > COND_OP_MAX:
                    illegal_cond_op += 1
                if next_rule > NEXT_RULE_MAX:
                    illegal_next_rule += 1

                rules.append({
                    'rule_index': j,
                    'cond_type': cond_type, 'cond_value': cond_value, 'cond_op': cond_op,
                    'motor_l': motor_l, 'motor_r': motor_r, 'duration_ms': duration_ms,
                    'next_rule': next_rule, 'padding': _padding,
                })
                offset += SIZE_BEHAVIOR_RULE

            if offset + INDIV_FIXED_STRUCT.size > len(data):
                print(f"  ⚠️ 个体 {i}: 固定部分数据不足")
                break
            (survival, dist, novelty,
             obs_th, clr_th, enc_th, enc_min, spin_th, stop_th,
             stuck_win, noise_amp, min_pwm, chaos_to, chaos_force_to,
             has_chaos, chaos_cnt, chaos_start) = \
                INDIV_FIXED_STRUCT.unpack_from(data, offset)
            offset += INDIV_FIXED_STRUCT.size

            behavior = None
            if version >= 0x000A:
                # v10.8: 16f/64B
                if offset + SIZE_BEHAVIOR_DESCRIPTOR > len(data):
                    print(f"  ⚠️ 个体 {i}: behavior 数据不足")
                    break
                behavior = DESC_STRUCT.unpack_from(data, offset)
                offset += SIZE_BEHAVIOR_DESCRIPTOR
            elif version >= 0x0007:
                # v10.7 及以前: 12f/48B
                if offset + SIZE_BEHAVIOR_DESCRIPTOR_LEGACY > len(data):
                    print(f"  ⚠️ 个体 {i}: behavior 数据不足")
                    break
                legacy = DESC_STRUCT_LEGACY.unpack_from(data, offset)
                behavior = legacy + (0.0, 0.0, 0.0, 0.0)
                offset += SIZE_BEHAVIOR_DESCRIPTOR_LEGACY

            individuals.append({
                'index': i, 'rule_count': rule_count, 'rules': rules,
                'survival': survival, 'distance': dist, 'novelty': novelty,
                'obs_th': obs_th, 'clr_th': clr_th,
                'enc_th': enc_th, 'enc_min': enc_min,
                'spin_th': spin_th, 'stop_th': stop_th,
                'stuck_win': stuck_win,
                'noise_amp': noise_amp, 'min_pwm': min_pwm,
                'chaos_to': chaos_to, 'chaos_force_to': chaos_force_to,
                'has_chaos': has_chaos,
                'chaos_cnt': chaos_cnt, 'chaos_start': chaos_start,
                'behavior': behavior,
            })

        # ============================================================
        # L3 + L4: 逐个体校验
        # ============================================================
        for ind in individuals:
            # L3: 字段级校验
            results = validate_record_fields({
                'survivalTime': ind['survival'],
                'distanceTicks': ind['distance'],
                'chaosTimeoutMs': ind['chaos_to'],
                'chaosMinPwm': ind['min_pwm'],
            }, FIELD_CONSTRAINTS, ctx, file_path)
            for r in results:
                if r['status'] == 'FAIL':
                    self.report.add(r)

            survival_times.append(ind['survival'])

            # ✅ [v5.8 L3 增强] 行为比例归一化 (仅对有真实数据的个体)
            if ind['behavior'] and ind['survival'] > 0:
                b = ind['behavior']
                fwd, turn, rev, idle = b[8], b[9], b[10], b[11]
                status, msg = validate_behavior_ratio(fwd, turn, rev, idle, ctx,
                                                      tolerance=0.01)
                if status == 'FAIL':
                    self.report.add({
                        'check_id': 'L3-RATIO-NORM', 'layer': 'L3',
                        'file_path': file_path,
                        'field_name': f'individual[{ind["index"]}].behavior_ratios',
                        'expected': 'sum=1.0 ± 0.01',
                        'actual': f'sum={fwd+turn+rev+idle:.4f}',
                        'status': 'FAIL', 'message': msg,
                        'timestamp': datetime.now(timezone.utc).isoformat(),
                    })

                # L3: 行为分项范围校验
                behavior_fields = {
                    'forwardRatio': fwd, 'turnRatio': turn,
                    'reverseRatio': rev, 'idleRatio': idle,
                }
                br_results = validate_record_fields(
                    behavior_fields, FIELD_CONSTRAINTS, ctx,
                    f"{file_path}#ind{ind['index']}"
                )
                for r in br_results:
                    self.report.add(r)

                # L4: 物理合理性 (仅对有数据的个体)
                physics_warns = SemanticValidator.check_physics_consistency({
                    'avgSpeed': b[4],
                    'totalDistance': b[7],
                    'survivalTime': ind['survival'],
                    'distanceTicks': ind['distance'],
                }, ctx, file_path)
                for w in physics_warns:
                    self.report.add_warning(w)

                # ✅ [v5.8 L4 新增] sensorVariance 极端值告警
                sensor_var = b[2]
                if sensor_var > 1e6:
                    self.report.add_warning(SemanticWarning(
                        check_id="L4-POP-SENSOR-VAR-HIGH",
                        message=(f"个体[{ind['index']}] sensorVariance="
                                 f"{sensor_var:.0f} > 1e6, 疑似单位或计算异常"),
                        field_name="sensorVariance",
                        expected="< 1e6",
                        actual=f"{sensor_var:.0f}",
                        file_path=file_path,
                    ))

        # ============================================================
        # ✅ [v5.8 L4 新增] 80% 个体 survivalTime=0 检测
        # ============================================================
        if individuals:
            zero_survival_count = sum(1 for ind in individuals if ind['survival'] == 0)
            zero_ratio = zero_survival_count / len(individuals)
            if zero_ratio > 0.5:
                self.report.add_warning(SemanticWarning(
                    check_id="L4-POP-MOST-ZERO-SURVIVAL",
                    message=(f"种群 {zero_survival_count}/{len(individuals)} "
                             f"({zero_ratio:.1%}) 个体 survivalTime=0, "
                             f"疑似固件导出循环逻辑错误 (P0)"),
                    field_name="survivalTime",
                    expected="survivalTime=0 比例 < 50%",
                    actual=f"{zero_ratio:.1%}",
                    file_path=file_path,
                ))
                print(f"  ⚠️ {bin_file.name}: {zero_survival_count}/{len(individuals)} "
                      f"({zero_ratio:.1%}) 个体 survivalTime=0 (疑似固件 Bug)")

        # L4: 跨文件一致性
        cross_warns = SemanticValidator.check_cross_file_consistency(
            survival_times, ctx, file_path
        )
        for w in cross_warns:
            self.report.add_warning(w)

        # ============================================================
        # 写 CSV
        # ============================================================
        with open(csv_path, 'w', encoding='utf-8') as f:
            f.write(f"# Population: version=0x{version:04X}, popSize={pop_size}, "
                    f"gen={gen}, expId={exp_id}\n")
            f.write("individual,ruleCount,survivalTime_ms,distanceTicks,noveltyScore,"
                    "obstacleThreshold,clearThreshold,encoderDiffThreshold,encoderDiffMin,"
                    "wheelSpinThreshold,wheelStopThreshold,stuckWindowSize,"
                    "chaosNoiseAmplifier,chaosMinPwm,chaosTimeoutMs,chaosForceTimeoutMs,"
                    "hasChaosRules,chaosRuleCount,chaosRulesStartIndex,"
                    "leftSensorMean,rightSensorMean,sensorVariance,sensorAsymmetry,"
                    "avgSpeed,speedVariance,turnBias,totalDistance,"
                    "forwardRatio,turnRatio,reverseRatio,idleRatio,"
                    "chaosFrameRatio,chaosSpeedDeltaL,chaosSpeedDeltaR,chaosPwmVariance\n")

            for ind in individuals:
                if ind['behavior']:
                    if len(ind['behavior']) == 16:
                        b = ind['behavior']
                    else:
                        b = tuple(ind['behavior']) + (0.0,) * (16 - len(ind['behavior']))
                else:
                    b = (0.0,) * 16
                f.write(
                    f"{ind['index']},{ind['rule_count']},{ind['survival']},{ind['distance']},"
                    f"{ind['novelty']:.6f},"
                    f"{ind['obs_th']},{ind['clr_th']},{ind['enc_th']},{ind['enc_min']},"
                    f"{ind['spin_th']},{ind['stop_th']},{ind['stuck_win']},"
                    f"{ind['noise_amp']},{ind['min_pwm']},{ind['chaos_to']},{ind['chaos_force_to']},"
                    f"{ind['has_chaos']},{ind['chaos_cnt']},{ind['chaos_start']},"
                    + ",".join(f"{v:.6f}" for v in b) + "\n"
                )

        with open(rules_csv_path, 'w', encoding='utf-8') as f:
            f.write("individual,ruleIndex,condType,condTypeName,condValue,condOp,condOpName,"
                    "motorL,motorR,durationMs,nextRule,isChaosRule\n")
            for ind in individuals:
                for r in ind['rules']:
                    ct = r['cond_type']
                    co = r['cond_op']
                    ct_name = COND_TYPE_FULL_NAMES.get(ct, f'UNKNOWN({ct})')
                    co_name = COND_OP_NAMES.get(co, f'UNKNOWN({co})')
                    is_chaos = 0
                    if ind['has_chaos'] and ind['chaos_cnt'] > 0:
                        if ind['chaos_start'] <= r['rule_index'] < ind['chaos_start'] + ind['chaos_cnt']:
                            is_chaos = 1
                    f.write(
                        f"{ind['index']},{r['rule_index']},{ct},{ct_name},{r['cond_value']},"
                        f"{co},{co_name},{r['motor_l']},{r['motor_r']},"
                        f"{r['duration_ms']},{r['next_rule']},{is_chaos}\n"
                    )

        if illegal_cond_type > 0 or illegal_cond_op > 0 or illegal_next_rule > 0:
            print(f"  ⚠️ {bin_file.name}: 非法值 "
                  f"condType×{illegal_cond_type}, condOp×{illegal_cond_op}, "
                  f"nextRule×{illegal_next_rule}")

        print(f"  ✅ {bin_file.name} → {csv_path.name} + {rules_csv_path.name} "
              f"({len(individuals)} 个体)")
        self.stats['success'] += 1

    # ================================================================
    # 新颖度存档转换
    # ================================================================
    def _convert_novelty_archive(self, bin_file: Path):
        with open(bin_file, 'rb') as f:
            data = f.read()

        file_path = str(bin_file)
        ctx = "NoveltyArchive"

        validate_file_size(data, 12, ctx, file_path)

        magic = int.from_bytes(data[0:4], 'little')
        version = int.from_bytes(data[4:6], 'little')
        count = int.from_bytes(data[6:8], 'little')
        crc32_val = int.from_bytes(data[8:12], 'little')

        if magic not in (MAGIC_NOVA_ARCHIVE, MAGIC_ARCH_OLD):
            raise ContractViolationError(
                f"[{ctx}] Magic Number 违规: 实际 0x{magic:08X}",
                check_id="L1-MAGIC-NOVELTY", layer="L1",
                expected=f"0x{MAGIC_ARCH_OLD:08X}/0x{MAGIC_NOVA_ARCHIVE:08X}",
                actual=f"0x{magic:08X}", file_path=file_path, field_name="magic",
            )

        is_nova = (magic == MAGIC_NOVA_ARCHIVE)
        print(f"  📦 {bin_file.name}: magic=0x{magic:08X} "
              f"({'NOVA' if is_nova else 'ARCH'}), version={version}, "
              f"count={count}")

        payload = data[12:]

        # v10.8: 自动探测 16f(64B) / 12f(48B)
        count_16 = len(payload) // SIZE_BEHAVIOR_DESCRIPTOR
        count_12 = len(payload) // SIZE_BEHAVIOR_DESCRIPTOR_LEGACY

        if count_16 == count:
            desc_fmt = DESC_STRUCT
            desc_size = SIZE_BEHAVIOR_DESCRIPTOR
            actual_count = count_16
            print(f"  📦 {bin_file.name}: 检测为 v10.8 新格式 (16f/64B)")
        elif count_12 == count:
            desc_fmt = DESC_STRUCT_LEGACY
            desc_size = SIZE_BEHAVIOR_DESCRIPTOR_LEGACY
            actual_count = count_12
            print(f"  📦 {bin_file.name}: 检测为 v10.7 旧格式 (12f/48B)")
        elif len(payload) % SIZE_BEHAVIOR_DESCRIPTOR == 0 and count_16 > 0:
            desc_fmt = DESC_STRUCT
            desc_size = SIZE_BEHAVIOR_DESCRIPTOR
            actual_count = count_16
            print(f"  📦 {bin_file.name}: 检测为 v10.8 新格式 (16f/64B)")
        elif len(payload) % SIZE_BEHAVIOR_DESCRIPTOR_LEGACY == 0 and count_12 > 0:
            desc_fmt = DESC_STRUCT_LEGACY
            desc_size = SIZE_BEHAVIOR_DESCRIPTOR_LEGACY
            actual_count = count_12
            print(f"  📦 {bin_file.name}: 检测为 v10.7 旧格式 (12f/48B)")
        else:
            raise ContractViolationError(
                f"[{ctx}] payload 长度 {len(payload)} 无法被 64 或 48 整除",
                check_id="L2-FORMAT-AMBIGUOUS", layer="L2",
                expected="64 或 48 的整数倍",
                actual=str(len(payload)),
                file_path=file_path, field_name="payload_length",
            )

        if count != actual_count:
            print(f"  ⚠️ {bin_file.name}: header 声明 count={count}, "
                  f"实际 payload 有 {actual_count} 条 (差 {actual_count - count})")
            self.report.add({
                'check_id': 'L2-COUNT-NOVELTY-MISMATCH', 'layer': 'L2',
                'file_path': file_path, 'field_name': 'count',
                'expected': str(count), 'actual': str(actual_count),
                'status': 'WARN',
                'message': (f'count 声明 {count}, 实际 {actual_count} '
                            f'(固件 count 计数 off-by-one)'),
                'timestamp': datetime.now(timezone.utc).isoformat(),
            })
            count = actual_count

        csv_path = self.output_dir / "novelty_archive.csv"
        with open(csv_path, 'w', encoding='utf-8') as f:
            f.write(f"# Novelty Archive: count={count}, version={version}, "
                    f"magic=0x{magic:08X}\n")
            if desc_size == SIZE_BEHAVIOR_DESCRIPTOR:
                f.write("index,leftSensorMean,rightSensorMean,sensorVariance,sensorAsymmetry,"
                        "avgSpeed,speedVariance,turnBias,totalDistance,"
                        "forwardRatio,turnRatio,reverseRatio,idleRatio,"
                        "chaosFrameRatio,chaosSpeedDeltaL,chaosSpeedDeltaR,chaosPwmVariance\n")
            else:
                f.write("index,leftSensorMean,rightSensorMean,sensorVariance,sensorAsymmetry,"
                        "avgSpeed,speedVariance,turnBias,totalDistance,"
                        "forwardRatio,turnRatio,reverseRatio,idleRatio\n")

            written = 0
            for i in range(count):
                off = i * desc_size
                if off + desc_size > len(payload):
                    break
                values = desc_fmt.unpack_from(payload, off)
                f.write(f"{i}," + ",".join(f"{v:.6f}" for v in values) + "\n")
                written += 1

        self.report.add_pass("L1-MAGIC-NOVELTY", "L1", file_path, "magic",
                             f"0x{MAGIC_ARCH_OLD:08X}", f"0x{magic:08X}",
                             "Magic Number 匹配")
        print(f"  ✅ {bin_file.name} → {csv_path.name} ({written} 条)")
        self.stats['success'] += 1

    # ================================================================
    # nova_gen 增量快照转换
    # ================================================================
    def _convert_nova_gen(self, bin_file: Path):
        with open(bin_file, 'rb') as f:
            data = f.read()

        file_path = str(bin_file)
        ctx = "NovaGen"

        validate_file_size(data, SIZE_NOVA_GEN_HEADER, ctx, file_path)
        validate_magic_number(data, MAGIC_NOVG, ctx, file_path)

        magic, version, reserved, crc32_val = NOVA_GEN_HDR_STRUCT.unpack_from(data, 0)

        validate_struct_layout("NovaGenHeader", NOVA_GEN_HDR_STRUCT.format,
                               SIZE_NOVA_GEN_HEADER, ctx, file_path)

        cur_size = int.from_bytes(data[12:16], 'little')
        prev_size = int.from_bytes(data[16:20], 'little')
        new_count = int.from_bytes(data[20:22], 'little')

        print(f"  📦 {bin_file.name}: magic=0x{magic:08X} 'NOVG', "
              f"version={version}, curSize={cur_size}, prevSize={prev_size}, "
              f"newCount={new_count}")

        entries_start = 22

        # v10.8: 自动探测 16f(64B) / 12f(48B)
        entries_payload_len = len(data) - entries_start
        if entries_payload_len % SIZE_BEHAVIOR_DESCRIPTOR == 0 and \
           entries_payload_len // SIZE_BEHAVIOR_DESCRIPTOR == new_count:
            desc_fmt = DESC_STRUCT
            desc_size = SIZE_BEHAVIOR_DESCRIPTOR
            print(f"  📦 {bin_file.name}: 检测为 v10.8 新格式 (16f/64B)")
        elif entries_payload_len % SIZE_BEHAVIOR_DESCRIPTOR_LEGACY == 0 and \
             entries_payload_len // SIZE_BEHAVIOR_DESCRIPTOR_LEGACY == new_count:
            desc_fmt = DESC_STRUCT_LEGACY
            desc_size = SIZE_BEHAVIOR_DESCRIPTOR_LEGACY
            print(f"  📦 {bin_file.name}: 检测为 v10.7 旧格式 (12f/48B)")
        else:
            desc_fmt = DESC_STRUCT
            desc_size = SIZE_BEHAVIOR_DESCRIPTOR
            print(f"  ⚠️ {bin_file.name}: 格式不明确，按 v10.8 新格式解析")

        csv_path = self.subdirs['nova_gen_bin'] / f"{bin_file.stem}.csv"
        with open(csv_path, 'w', encoding='utf-8') as f:
            f.write(f"# NovaGen Snapshot: version={version}, curSize={cur_size}, "
                    f"prevSize={prev_size}, newCount={new_count}, magic=0x{magic:08X}\n")
            if desc_size == SIZE_BEHAVIOR_DESCRIPTOR:
                f.write("index,leftSensorMean,rightSensorMean,sensorVariance,sensorAsymmetry,"
                        "avgSpeed,speedVariance,turnBias,totalDistance,"
                        "forwardRatio,turnRatio,reverseRatio,idleRatio,"
                        "chaosFrameRatio,chaosSpeedDeltaL,chaosSpeedDeltaR,chaosPwmVariance\n")
            else:
                f.write("index,leftSensorMean,rightSensorMean,sensorVariance,sensorAsymmetry,"
                        "avgSpeed,speedVariance,turnBias,totalDistance,"
                        "forwardRatio,turnRatio,reverseRatio,idleRatio\n")

            written = 0
            for i in range(new_count):
                off = entries_start + i * desc_size
                if off + desc_size > len(data):
                    break
                values = desc_fmt.unpack_from(data, off)
                f.write(f"{i}," + ",".join(f"{v:.6f}" for v in values) + "\n")
                written += 1

        print(f"  ✅ {bin_file.name} → {csv_path.name} ({written} 条)")
        self.stats['success'] += 1


# ================================================================
# 任务4: 生成汇总报告
# ================================================================
class ReportGenerator:
    def __init__(self, output_dir: str):
        self.output_dir = Path(output_dir)
        self.report_path = self.output_dir / "data_summary.txt"

    def generate(self, spiffs_stats: Dict, ram_stats: Dict, convert_stats: Dict) -> None:
        lines = []
        lines.append("=" * 70)
        lines.append("OEE 数据汇总报告")
        lines.append(f"生成时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
        lines.append(f"脚本版本: v5.8 (含契约校验层 + 种群深度校验)")
        lines.append("=" * 70)

        lines.append("\n【SPIFFS 下载】")
        lines.append(f"  成功: {spiffs_stats.get('success', 0)}")
        lines.append(f"  失败: {spiffs_stats.get('failed', 0)}")
        lines.append(f"  总大小: {spiffs_stats.get('total_bytes', 0):,} bytes")

        lines.append("\n【RAM 下载】")
        lines.append(f"  成功: {ram_stats.get('success', 0)}")
        lines.append(f"  失败: {ram_stats.get('failed', 0)}")

        lines.append("\n【BIN 转换】")
        lines.append(f"  成功: {convert_stats.get('success', 0)}")
        lines.append(f"  失败: {convert_stats.get('failed', 0)}")
        lines.append(f"  跳过: {convert_stats.get('skipped', 0)}")
        lines.append(f"  契约违规: {convert_stats.get('contract_violations', 0)}")
        
 
        history_csv = self.output_dir / "spiffs_raw" / "oe_history.csv"
        if history_csv.exists():
            lines.append("\n【历史 CSV 时间戳校验】")
            hist = validate_history_csv(history_csv)
            lines.append(f"  总行数: {hist['total_rows']}")
            lines.append(f"  合法行: {hist['valid_rows']}")
            lines.append(f"  非法时间戳: {hist['invalid_ts_rows']}")
            if hist['ts_min'] is not None:
                lines.append(f"  时间戳范围: {hist['ts_min']} ~ {hist['ts_max']} ms")

        lines.append("\n【文件清单】")
        spiffs_dir = self.output_dir / "spiffs_raw"
        if spiffs_dir.exists():
            for root, dirs, files in os.walk(spiffs_dir):
                rel_path = Path(root).relative_to(spiffs_dir)
                for f in sorted(files):
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
        description='OEE 数据工具 v5.8 - 含契约校验层 (适配 v10.6-WebDiag 固件)',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  python downparse.py --ip 192.168.4.1 --output ./oee_data
  python downparse.py --task convert --local-dir ./oee_data/spiffs_raw
  python downparse.py --task spiffs --ip 192.168.4.1
  python downparse.py --ip 192.168.4.1 --diag
        """
    )
    parser.add_argument('--ip', type=str, default='192.168.4.1', help='ESP32 IP地址')
    parser.add_argument('--output', type=str, default='./oee_data', help='输出目录')
    parser.add_argument('--task', choices=['all', 'spiffs', 'ram', 'convert', 'report'],
                        default='all', help='执行的任务')
    parser.add_argument('--local-dir', type=str, default=None,
                        help='本地SPIFFS目录 (用于convert/report)')
    parser.add_argument('--diag', action='store_true',
                        help='运行网络诊断模式，不执行下载')
    parser.add_argument('--retries', type=int, default=3,
                        help='HTTP请求最大重试次数 (默认3)')
    args = parser.parse_args()

    if args.diag:
        print("\n" + "=" * 70)
        print("🔍 OEE 网络诊断工具")
        print("=" * 70)
        print(f"\n目标: http://{args.ip}:80")
        diag = check_connectivity(args.ip)
        print(f"\n{'=' * 70}")
        print("诊断结果:")
        print(f"  DNS解析:     {'✅ 通过' if diag['dns_ok'] else '❌ 失败'}")
        print(f"  Socket连接:  {'✅ 通过' if diag['socket_ok'] else '❌ 失败'}")
        print(f"  urllib:      {'✅ 通过' if diag['urllib_ok'] else '❌ 失败'}")
        print(f"  requests:    {'✅ 通过' if diag['requests_ok'] else '❌ 失败'}")

        if diag['errors']:
            print(f"\n错误详情:")
            for err in diag['errors']:
                print(f"  ⚠️ {err}")

        if not diag['requests_ok'] and not diag['urllib_ok'] and not diag['socket_ok']:
            print(f"\n{'=' * 70}")
            print("⚠️ 所有连接方式均失败！可能的原因和解决方案:")
            print("  1. Windows防火墙阻止了Python")
            print("  2. 杀毒软件拦截")
            print("  3. 代理设置冲突")
            print("  4. Python未以管理员身份运行")
            print("  5. 网络适配器问题 (确认连接到了ESP32热点)")
            print("  6. ESP32 WebServer未启动")
        elif diag['requests_ok']:
            print(f"\n✅ 网络连接正常，脚本应该可以正常工作！")
        print("=" * 70)
        sys.exit(0)

    print("\n" + "=" * 70)
    print("OEE 数据下载与转换工具 v5.8 (含契约校验层 + 种群深度校验)")
    print("=" * 70)

    print("\n🔍 快速网络检测...")
    try:
        import urllib.request
        urllib.request.urlopen(f"http://{args.ip}:80/", timeout=5)
        print("  ✅ 网络连通正常")
    except Exception as e:
        print(f"  ⚠️ 网络检测异常: {e}")
        print("  💡 运行 python downparse.py --diag 查看详细诊断")

    spiffs_dir = None
    spiffs_stats = {'success': 0, 'failed': 0, 'total_bytes': 0}
    ram_stats = {'success': 0, 'failed': 0}
    convert_stats = {'success': 0, 'failed': 0, 'skipped': 0,
                     'contract_violations': 0}

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
            print(f"\n⚠️ SPIFFS 目录不存在，跳过转换: {spiffs_dir}")

    if args.task in ['all', 'report']:
        reporter = ReportGenerator(args.output)
        reporter.generate(spiffs_stats, ram_stats, convert_stats)

    print("\n" + "=" * 70)
    print("✅ 完成!")
    print(f"📁 输出: {Path(args.output).absolute()}")
    print("=" * 70)


if __name__ == '__main__':
    sys.exit(main())