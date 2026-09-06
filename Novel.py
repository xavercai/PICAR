#!/usr/bin/env python3
"""
OEE Novelty Archive Decoder
下载并解码 novelty_archive.bin 文件为 CSV/JSON 格式
支持 ESP8266/ESP32 的 SPIFFS 存储格式

对应固件: v9.22-ObserveOnly (TieredStorage)
"""

import requests
import struct
import csv
import json
import os
import sys
import argparse
import hashlib
from datetime import datetime
from typing import Optional, List, Dict, Tuple, Any

# ============================================================
# 常量定义 (与固件 NoveltyArchive 类保持一致)
# ============================================================
MAGIC_ARCHIVE = 0x41524348  # "ARCH" - 正确魔数
VERSION = 1

# 特征维度 (BehaviorDescriptor 12维)
FEATURE_DIM = 12

# 特征名称 (与固件 BehaviorDescriptor 字段对应)
FEATURE_NAMES = [
    'left_sensor_mean',
    'right_sensor_mean',
    'sensor_variance',
    'sensor_asymmetry',
    'avg_speed',
    'speed_variance',
    'turn_bias',
    'total_distance',
    'forward_ratio',
    'turn_ratio',
    'reverse_ratio',
    'idle_ratio'
]

# ============================================================
# 数据结构定义 (与固件严格对齐)
# ============================================================
class NoveltyArchiveHeader:
    """新颖度存档文件头 (12 bytes)"""
    def __init__(self):
        self.magic: int = 0
        self.version: int = 0
        self.entry_count: int = 0
        self.crc32: int = 0

    @classmethod
    def from_bytes(cls, data: bytes) -> 'NoveltyArchiveHeader':
        """从字节数据解析文件头"""
        header = cls()
        header.magic = struct.unpack('<I', data[0:4])[0]
        header.version = struct.unpack('<H', data[4:6])[0]
        header.entry_count = struct.unpack('<H', data[6:8])[0]
        header.crc32 = struct.unpack('<I', data[8:12])[0]
        return header

    def is_valid(self) -> bool:
        """验证文件头是否有效"""
        return self.magic == MAGIC_ARCHIVE


class NoveltyEntry:
    """单个新颖度条目 (48 bytes = 12个float)"""
    def __init__(self):
        self.features: List[float] = []

    @classmethod
    def from_bytes(cls, data: bytes, offset: int) -> 'NoveltyEntry':
        """从字节数据解析条目"""
        entry = cls()
        entry.features = list(struct.unpack_from('<ffffffffffff', data, offset))
        return entry
    
    def to_dict(self) -> Dict[str, float]:
        """转换为字典"""
        return {FEATURE_NAMES[i]: self.features[i] for i in range(len(self.features))}


# ============================================================
# 主解码器类
# ============================================================
class NoveltyArchiveDecoder:
    """新颖度存档解码器"""
    
    def __init__(self, url: str = "http://192.168.4.1/download/novelty", timeout: int = 30):
        """
        初始化解码器
        
        Args:
            url: 下载URL (正确端点: /download/novelty)
            timeout: 下载超时时间(秒)
        """
        self.url = url
        self.timeout = timeout
        self.raw_data: Optional[bytes] = None
        self.header: Optional[NoveltyArchiveHeader] = None
        self.max_values: List[float] = []
        self.entries: List[NoveltyEntry] = []
        self.crc_ok: bool = False
        self.is_valid_format: bool = False
    
    def download(self) -> bool:
        """
        下载 novelty_archive.bin 文件
        """
        try:
            print(f"📥 正在下载: {self.url}")
            response = requests.get(self.url, timeout=self.timeout)
            
            if response.status_code != 200:
                print(f"❌ 下载失败: HTTP {response.status_code}")
                return False
            
            content_type = response.headers.get('Content-Type', '')
            if 'text/plain' in content_type and 'File not found' in response.text:
                print(f"❌ 文件不存在: {response.text.strip()}")
                return False
            
            self.raw_data = response.content
            print(f"✅ 下载完成: {len(self.raw_data)} 字节 ({len(self.raw_data)/1024:.2f} KB)")
            return True
            
        except requests.exceptions.ConnectionError:
            print(f"❌ 连接失败: 无法访问 {self.url}")
            print("   请检查: 设备IP是否正确，设备是否在AP模式")
            return False
        except requests.exceptions.Timeout:
            print(f"❌ 下载超时 (>{self.timeout}秒)")
            return False
        except Exception as e:
            print(f"❌ 下载异常: {e}")
            return False
    
    def parse(self) -> bool:
        """
        解析下载的数据
        """
        if not self.raw_data or len(self.raw_data) < 12:
            print("❌ 数据为空或太小")
            return False
        
        # 先尝试作为标准格式解析
        if self._parse_standard_format():
            self.is_valid_format = True
            return True
        
        # 标准格式失败，尝试作为原始数据解析
        print("\n⚠️ 标准格式解析失败，尝试作为原始数据解析...")
        return self._parse_as_raw()
    
    def _parse_standard_format(self) -> bool:
        """解析标准格式 (ARCH 魔数)"""
        try:
            # 解析文件头
            self.header = NoveltyArchiveHeader.from_bytes(self.raw_data)
            
            if not self.header.is_valid():
                print(f"⚠️  无效的文件头 Magic: 0x{self.header.magic:08X}")
                print(f"   期望: 0x{MAGIC_ARCHIVE:08X} ('ARCH')")
                return False
            
            print(f"📋 文件信息:")
            print(f"  Magic: 0x{self.header.magic:08X} ('ARCH') ✅")
            print(f"  Version: {self.header.version}")
            print(f"  Entry Count: {self.header.entry_count}")
            print(f"  CRC32: 0x{self.header.crc32:08X}")
            
            # 验证 CRC32
            self.crc_ok = self._verify_crc()
            print(f"  CRC32 Verify: {'✅ 通过' if self.crc_ok else '❌ 失败'}")
            
            # 解析 MaxValues (48 bytes)
            offset = 12
            self.max_values = list(struct.unpack_from('<ffffffffffff', self.raw_data, offset))
            offset += 48
            
            print(f"  MaxValues: {[f'{v:.4f}' for v in self.max_values[:4]]}...")
            
            # 解析条目
            self.entries = self._parse_entries(offset)
            print(f"  Parsed Entries: {len(self.entries)}")
            
            return True
            
        except struct.error as e:
            print(f"❌ 解析错误 (struct): {e}")
            return False
        except Exception as e:
            print(f"❌ 解析错误: {e}")
            return False
    
    def _parse_as_raw(self) -> bool:
        """
        作为原始数据解析 (回退模式)
        尝试从二进制数据中提取浮点数序列
        """
        data = self.raw_data
        result = {
            'valid': False,
            'file_size': len(data),
            'entries': []
        }
        
        print(f"📄 原始数据解析:")
        print(f"   大小: {len(data)} 字节")
        print(f"   前64字节: {data[:64].hex()}")
        
        # 尝试查找可能的浮点数序列
        floats_found = []
        for i in range(0, len(data) - 4, 4):
            try:
                val = struct.unpack('<f', data[i:i+4])[0]
                # 合理的浮点数范围 (排除NaN和无穷)
                if -10000 < val < 10000 and val != 0 and not (val != val):  # 不是NaN
                    floats_found.append((i, val))
            except:
                pass
        
        if floats_found:
            print(f"   发现 {len(floats_found)} 个可能的浮点数")
            
            # 按12个一组组织成条目
            entries_data = []
            current_entry = []
            for pos, val in floats_found[:200]:  # 最多处理200个浮点数
                current_entry.append(val)
                if len(current_entry) == FEATURE_DIM:
                    entries_data.append(current_entry)
                    current_entry = []
            
            # 创建 NoveltyEntry 对象
            for entry_data in entries_data:
                entry = NoveltyEntry()
                entry.features = entry_data[:FEATURE_DIM]
                self.entries.append(entry)
            
            # 设置默认的 header
            self.header = NoveltyArchiveHeader()
            self.header.magic = 0  # 无效魔数
            self.header.version = 0
            self.header.entry_count = len(self.entries)
            self.header.crc32 = 0
            
            print(f"   提取了 {len(self.entries)} 个条目")
            self.is_valid_format = False
            
            if self.entries:
                return True
        
        print("❌ 无法从原始数据中提取有效信息")
        return False
    
    def _verify_crc(self) -> bool:
        """验证CRC32校验和"""
        if not self.raw_data or len(self.raw_data) < 12:
            return False
        
        # CRC32 计算从第12字节开始 (跳过文件头)
        payload = self.raw_data[12:]
        calculated = hashlib.crc32(payload) & 0xFFFFFFFF
        return calculated == self.header.crc32
    
    def _parse_entries(self, offset: int) -> List[NoveltyEntry]:
        """解析所有条目"""
        entries = []
        entry_size = FEATURE_DIM * 4  # 48 bytes
        
        for i in range(self.header.entry_count):
            if offset + entry_size > len(self.raw_data):
                print(f"⚠️ 数据截断: 预期 {self.header.entry_count} 条, 实际解析 {len(entries)} 条")
                break
            
            entry = NoveltyEntry.from_bytes(self.raw_data, offset)
            entries.append(entry)
            offset += entry_size
        
        return entries
    
    def get_raw_hex_dump(self, length: int = 200) -> str:
        """获取原始数据的十六进制转储"""
        if not self.raw_data:
            return ""
        if len(self.raw_data) <= length:
            return self.raw_data.hex()
        return self.raw_data[:length].hex() + "..."
    
    def to_csv(self, output_path: Optional[str] = None) -> str:
        """
        转换为CSV格式
        """
        if not self.entries:
            print("❌ 没有条目可导出")
            return ""
        
        if output_path is None:
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            output_path = f"novelty_archive_{timestamp}.csv"
        
        try:
            with open(output_path, 'w', newline='', encoding='utf-8') as f:
                # 写注释头
                f.write(f"# Magic: 0x{self.header.magic:08X} ({'ARCH' if self.header.magic == MAGIC_ARCHIVE else 'UNKNOWN'})\n")
                f.write(f"# Version: {self.header.version}\n")
                f.write(f"# Entry Count: {self.header.entry_count}\n")
                f.write(f"# CRC32: 0x{self.header.crc32:08X}\n")
                f.write(f"# CRC OK: {self.crc_ok}\n")
                f.write(f"# Valid Format: {self.is_valid_format}\n")
                if self.max_values:
                    f.write(f"# MaxValues: {','.join([f'{v:.6f}' for v in self.max_values])}\n")
                f.write(f"# Generated: {datetime.now().isoformat()}\n")
                
                # 表头
                header = ['entry_id'] + FEATURE_NAMES
                writer = csv.writer(f)
                writer.writerow(header)
                
                for idx, entry in enumerate(self.entries):
                    row = [idx] + [f"{v:.6f}" for v in entry.features]
                    writer.writerow(row)
            
            print(f"✅ CSV已保存: {output_path} ({len(self.entries)} 行)")
            return output_path
            
        except Exception as e:
            print(f"❌ 保存CSV失败: {e}")
            return ""
    
    def to_json(self, output_path: Optional[str] = None, pretty: bool = True) -> str:
        """
        导出为JSON格式
        
        Args:
            output_path: 输出文件路径
            pretty: 是否格式化输出
            
        Returns:
            实际输出文件路径
        """
        if not self.entries:
            print("❌ 没有条目可导出")
            return ""
        
        if output_path is None:
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            output_path = f"novelty_archive_{timestamp}.json"
        
        # 构建导出数据
        export_data = {
            'metadata': {
                'magic': f"0x{self.header.magic:08X}",
                'magic_string': 'ARCH' if self.header.magic == MAGIC_ARCHIVE else 'UNKNOWN',
                'version': self.header.version,
                'entry_count': self.header.entry_count,
                'crc32': f"0x{self.header.crc32:08X}",
                'crc_ok': self.crc_ok,
                'valid_format': self.is_valid_format,
                'file_size': len(self.raw_data) if self.raw_data else 0,
                'generated': datetime.now().isoformat()
            },
            'max_values': self.max_values if self.max_values else [],
            'entries': []
        }
        
        # 添加特征名称到metadata
        export_data['metadata']['feature_names'] = FEATURE_NAMES
        
        # 添加条目数据
        for idx, entry in enumerate(self.entries):
            entry_dict = {
                'entry_id': idx,
                'features': entry.features,
                'feature_names': FEATURE_NAMES,
                'feature_dict': entry.to_dict()
            }
            export_data['entries'].append(entry_dict)
        
        # 添加原始数据信息
        if self.raw_data:
            export_data['raw_data'] = {
                'size': len(self.raw_data),
                'hex_preview': self.get_raw_hex_dump(100)
            }
        
        try:
            with open(output_path, 'w', encoding='utf-8') as f:
                if pretty:
                    json.dump(export_data, f, indent=2, ensure_ascii=False, default=str)
                else:
                    json.dump(export_data, f, ensure_ascii=False, default=str)
            
            print(f"✅ JSON已保存: {output_path} ({len(self.entries)} 条)")
            return output_path
            
        except Exception as e:
            print(f"❌ 保存JSON失败: {e}")
            return ""
    
    def get_summary(self) -> Dict:
        """获取数据摘要"""
        if not self.entries:
            return {}
        
        # 计算各维度的统计信息
        stats = {}
        for i, name in enumerate(FEATURE_NAMES):
            values = [e.features[i] for e in self.entries if len(e.features) > i]
            if values:
                stats[name] = {
                    'mean': sum(values) / len(values),
                    'max': max(values),
                    'min': min(values),
                    'std': (sum((v - sum(values)/len(values))**2 for v in values) / len(values)) ** 0.5
                }
        
        return {
            'total_entries': len(self.entries),
            'crc_ok': self.crc_ok,
            'valid_format': self.is_valid_format,
            'feature_stats': stats
        }
    
    def print_summary(self) -> None:
        """打印数据摘要"""
        summary = self.get_summary()
        if not summary:
            print("⚠️ 无数据摘要")
            return
        
        print("\n" + "="*60)
        print("📊 数据摘要")
        print("="*60)
        print(f"  总条目数: {summary['total_entries']}")
        print(f"  有效格式: {'✅ 是 (ARCH)' if summary['valid_format'] else '⚠️ 原始数据模式'}")
        print(f"  CRC校验: {'✅ 通过' if summary['crc_ok'] else '❌ 失败'}")
        
        if self.header and self.header.magic:
            print(f"  魔数: 0x{self.header.magic:08X} ({'ARCH' if self.header.magic == MAGIC_ARCHIVE else 'UNKNOWN'})")
        if self.header:
            print(f"  版本: {self.header.version}")
        
        print("\n  各维度统计:")
        for name, stats in summary['feature_stats'].items():
            print(f"    {name}: mean={stats['mean']:.4f}, max={stats['max']:.4f}, min={stats['min']:.4f}, std={stats['std']:.4f}")
        print("="*60)


# ============================================================
# 命令行接口
# ============================================================
def main():
    parser = argparse.ArgumentParser(
        description='OEE Novelty Archive 下载与解码工具 (v9.22-TieredStorage)',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  %(prog)s                              # 使用默认地址下载
  %(prog)s -u http://192.168.4.1/download/novelty
  %(prog)s -o novelty_data.csv          # 指定输出文件 (CSV)
  %(prog)s --json novelty_data.json     # 导出为JSON
  %(prog)s --no-download -f archive.bin # 解析本地文件
  %(prog)s --raw-parse                  # 强制使用原始数据解析模式

端点: /download/novelty
魔数: 0x41524348 ('ARCH')
        """
    )
    
    parser.add_argument(
        '-u', '--url',
        default='http://192.168.4.1/download/novelty',
        help='下载URL (默认: http://192.168.4.1/download/novelty)'
    )
    parser.add_argument(
        '-o', '--output',
        help='输出CSV文件路径 (默认自动生成)'
    )
    parser.add_argument(
        '--json',
        dest='json_output',
        help='导出为JSON文件'
    )
    parser.add_argument(
        '-f', '--file',
        help='直接从本地文件解析 (跳过下载)'
    )
    parser.add_argument(
        '--no-download',
        action='store_true',
        help='不下载, 配合 -f 使用'
    )
    parser.add_argument(
        '--raw-parse',
        action='store_true',
        help='强制使用原始数据解析模式 (跳过标准格式验证)'
    )
    parser.add_argument(
        '--timeout',
        type=int,
        default=30,
        help='下载超时时间(秒) (默认: 30)'
    )
    parser.add_argument(
        '--summary-only',
        action='store_true',
        help='仅显示摘要, 不保存CSV'
    )
    parser.add_argument(
        '--verbose',
        action='store_true',
        help='显示详细信息'
    )
    parser.add_argument(
        '--pretty',
        action='store_true',
        default=True,
        help='JSON格式化输出 (默认: True)'
    )
    
    args = parser.parse_args()
    
    # 创建解码器
    decoder = NoveltyArchiveDecoder(args.url, args.timeout)
    
    # 加载数据
    if args.file and os.path.exists(args.file):
        print(f"📂 从本地文件加载: {args.file}")
        with open(args.file, 'rb') as f:
            decoder.raw_data = f.read()
        print(f"✅ 读取成功: {len(decoder.raw_data)} 字节")
        success = True
    elif args.no_download:
        print("❌ 未指定本地文件 (使用 -f) 且禁止下载")
        sys.exit(1)
    else:
        success = decoder.download()
    
    if not success:
        sys.exit(1)
    
    # 解析数据
    if args.raw_parse:
        print("\n🔍 强制使用原始数据解析模式...")
        success = decoder._parse_as_raw()
    else:
        success = decoder.parse()
    
    if not success:
        print("❌ 解析失败")
        sys.exit(1)
    
    # 显示摘要
    decoder.print_summary()
    
    # 显示详细信息
    if args.verbose:
        print("\n🔍 详细信息:")
        if decoder.is_valid_format:
            print(f"  MaxValues: {[f'{v:.4f}' for v in decoder.max_values]}")
        print(f"  原始数据大小: {len(decoder.raw_data) if decoder.raw_data else 0} 字节")
        print(f"  有效条目: {len(decoder.entries)}")
        if decoder.entries:
            print(f"  第一条目特征: {[f'{v:.4f}' for v in decoder.entries[0].features]}")
    
    # 导出CSV
    if not args.summary_only:
        csv_path = decoder.to_csv(args.output)
    
    # 导出JSON
    if args.json_output:
        decoder.to_json(args.json_output, pretty=args.pretty)
    
    print("\n✅ 完成")


# ============================================================
# 作为模块使用的接口
# ============================================================
def download_and_decode(
    url: str = "http://192.168.4.1/download/novelty",
    output_csv: Optional[str] = None,
    output_json: Optional[str] = None,
    timeout: int = 30,
    raw_parse: bool = False
) -> Tuple[bool, Optional[str], Optional[str], List[Dict]]:
    """
    下载并解码新颖度存档
    
    Args:
        url: 下载地址
        output_csv: 输出CSV路径 (可选)
        output_json: 输出JSON路径 (可选)
        timeout: 超时时间
        raw_parse: 是否强制使用原始解析模式
        
    Returns:
        Tuple[bool, Optional[str], Optional[str], List[Dict]]: 
        (成功标志, CSV路径, JSON路径, 数据列表)
    """
    decoder = NoveltyArchiveDecoder(url, timeout)
    
    if not decoder.download():
        return False, None, None, []
    
    if raw_parse:
        success = decoder._parse_as_raw()
    else:
        success = decoder.parse()
    
    if not success:
        return False, None, None, []
    
    csv_path = None
    json_path = None
    
    if output_csv is not None:
        csv_path = decoder.to_csv(output_csv)
    elif output_json is None and not output_csv:
        # 如果都没有指定，默认生成CSV
        csv_path = decoder.to_csv()
    
    if output_json is not None:
        json_path = decoder.to_json(output_json)
    
    # 转换为字典列表
    data = []
    for entry in decoder.entries:
        row = {'entry_id': len(data)}
        row.update(entry.to_dict())
        data.append(row)
    
    return True, csv_path, json_path, data


if __name__ == '__main__':
    main()