#!/usr/bin/env python3
"""Simple RAW HID test client for AgenticKbd UI TLV protocol.

Windows quick start:
    py -m pip install hidapi
    py tools/agentickbd_hid_test.py --list
    py tools/agentickbd_hid_test.py --progress 50 --top-text "Top text" --bottom-text "Bottom text"
"""

from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass
from typing import Iterable, List, Optional

try:
    import hid  # type: ignore
except ImportError:  # pragma: no cover - runtime guidance only
    print("Missing dependency: hid", file=sys.stderr)
    print("Install it with: py -m pip install hidapi", file=sys.stderr)
    raise SystemExit(1)


DEFAULT_VID = 0x1D50
DEFAULT_PID = 0x615E
DEFAULT_USAGE_PAGE = 0xFF60
DEFAULT_USAGE = 0x61
DEFAULT_REPORT_SIZE = 32
MAX_TEXT_LEN = 36

TLV_PROGRESS = 0x01
TLV_BAR_COLOR = 0x02
TLV_PERCENT_COLOR = 0x03
TLV_TOP_TEXT = 0x04
TLV_TOP_COLOR = 0x05
TLV_BOTTOM_TEXT = 0x06
TLV_BOTTOM_COLOR = 0x07


@dataclass
class DeviceInfo:
    path: bytes
    vendor_id: int
    product_id: int
    usage_page: int
    usage: int
    product_string: str
    manufacturer_string: str
    serial_number: str
    interface_number: int


def parse_hex_or_int(value: str) -> int:
    return int(value, 0)


def parse_color(value: str) -> bytes:
    text = value.strip()
    if text.startswith("#"):
        text = text[1:]
    if len(text) != 6:
        raise argparse.ArgumentTypeError(f"invalid color '{value}', expected RRGGBB")
    try:
        return bytes.fromhex(text)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"invalid color '{value}', expected RRGGBB") from exc


def encode_text(value: str) -> bytes:
    encoded = value.encode("utf-8")
    if len(encoded) > MAX_TEXT_LEN:
        encoded = encoded[:MAX_TEXT_LEN]
    return encoded


def make_tlv(tag: int, value: bytes) -> bytes:
    if len(value) > 0xFF:
        raise ValueError(f"TLV value too long for tag 0x{tag:02X}")
    return bytes((tag, len(value))) + value


def chunk_packets(tlvs: Iterable[bytes], report_size: int) -> List[bytes]:
    packets: List[bytes] = []
    current = bytearray()

    for tlv in tlvs:
        if len(tlv) > report_size:
            raise ValueError(
                f"Single TLV too large for report: {len(tlv)} bytes > report size {report_size}"
            )
        if current and len(current) + len(tlv) > report_size:
            packets.append(bytes(current))
            current.clear()
        current.extend(tlv)

    if current:
        packets.append(bytes(current))

    return packets


def enumerate_devices(vid: int, pid: int, usage_page: int, usage: int) -> List[DeviceInfo]:
    matched: List[DeviceInfo] = []
    for item in hid.enumerate(vid, pid):
        item_usage_page = int(item.get("usage_page", 0) or 0)
        item_usage = int(item.get("usage", 0) or 0)
        if item_usage_page != usage_page or item_usage != usage:
            continue
        matched.append(
            DeviceInfo(
                path=item["path"],
                vendor_id=int(item.get("vendor_id", 0) or 0),
                product_id=int(item.get("product_id", 0) or 0),
                usage_page=item_usage_page,
                usage=item_usage,
                product_string=item.get("product_string") or "",
                manufacturer_string=item.get("manufacturer_string") or "",
                serial_number=item.get("serial_number") or "",
                interface_number=int(item.get("interface_number", -1) or -1),
            )
        )
    return matched


def open_device(
    path: Optional[str], vid: int, pid: int, usage_page: int, usage: int
) -> tuple[hid.device, DeviceInfo]:
    dev = hid.device()

    if path:
        encoded_path = path.encode("utf-8") if isinstance(path, str) else path
        dev.open_path(encoded_path)
        info = DeviceInfo(
            path=encoded_path,
            vendor_id=vid,
            product_id=pid,
            usage_page=usage_page,
            usage=usage,
            product_string="",
            manufacturer_string="",
            serial_number="",
            interface_number=-1,
        )
        return dev, info

    matches = enumerate_devices(vid, pid, usage_page, usage)
    if not matches:
        raise RuntimeError(
            "No matching RAW HID device found. Check USB connection, VID/PID, usage page, and usage."
        )

    info = matches[0]
    dev.open_path(info.path)
    return dev, info


def build_tlvs(args: argparse.Namespace) -> List[bytes]:
    tlvs: List[bytes] = []

    if args.progress is not None:
        tlvs.append(make_tlv(TLV_PROGRESS, bytes((args.progress,))))
    if args.bar_color is not None:
        tlvs.append(make_tlv(TLV_BAR_COLOR, args.bar_color))
    if args.percent_color is not None:
        tlvs.append(make_tlv(TLV_PERCENT_COLOR, args.percent_color))
    if args.top_text is not None:
        tlvs.append(make_tlv(TLV_TOP_TEXT, encode_text(args.top_text)))
    if args.top_color is not None:
        tlvs.append(make_tlv(TLV_TOP_COLOR, args.top_color))
    if args.bottom_text is not None:
        tlvs.append(make_tlv(TLV_BOTTOM_TEXT, encode_text(args.bottom_text)))
    if args.bottom_color is not None:
        tlvs.append(make_tlv(TLV_BOTTOM_COLOR, args.bottom_color))

    return tlvs


def write_packets(dev: hid.device, packets: Iterable[bytes], report_size: int) -> None:
    for index, packet in enumerate(packets, start=1):
        report = bytes([0]) + packet.ljust(report_size, b"\x00")
        written = dev.write(report)
        expected = report_size + 1
        if written != expected:
            raise RuntimeError(f"Write failed for packet {index}: wrote {written}, expected {expected}")
        print(f"Sent packet {index}: {packet.hex(' ')}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="AgenticKbd RAW HID TLV test client")
    parser.add_argument("--list", action="store_true", help="list matching RAW HID devices and exit")
    parser.add_argument("--path", help="open a specific HID device path returned by --list")
    parser.add_argument("--vid", type=parse_hex_or_int, default=DEFAULT_VID, help="USB VID, default 0x1D50")
    parser.add_argument("--pid", type=parse_hex_or_int, default=DEFAULT_PID, help="USB PID, default 0x615E")
    parser.add_argument(
        "--usage-page",
        type=parse_hex_or_int,
        default=DEFAULT_USAGE_PAGE,
        help="RAW HID usage page, default 0xFF60",
    )
    parser.add_argument(
        "--usage",
        type=parse_hex_or_int,
        default=DEFAULT_USAGE,
        help="RAW HID usage, default 0x61",
    )
    parser.add_argument(
        "--report-size",
        type=int,
        default=DEFAULT_REPORT_SIZE,
        help="RAW HID report size in bytes, default 32",
    )
    parser.add_argument("--progress", type=int, choices=range(0, 101), help="progress percent 0..100")
    parser.add_argument("--bar-color", type=parse_color, help="progress bar color, e.g. 00C853")
    parser.add_argument("--percent-color", type=parse_color, help="reserved percent text color, e.g. FFFFFF")
    parser.add_argument("--top-text", help=f"top line text, up to {MAX_TEXT_LEN} UTF-8 bytes")
    parser.add_argument("--top-color", type=parse_color, help="top text color, e.g. FFFFFF")
    parser.add_argument("--bottom-text", help=f"bottom line text, up to {MAX_TEXT_LEN} UTF-8 bytes")
    parser.add_argument("--bottom-color", type=parse_color, help="bottom text color, e.g. 7F8FA6")
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    if args.list:
        devices = enumerate_devices(args.vid, args.pid, args.usage_page, args.usage)
        if not devices:
            print("No matching RAW HID devices found.")
            return 1
        for index, info in enumerate(devices, start=1):
            print(f"[{index}] path={info.path.decode(errors='replace')}")
            print(
                "    "
                f"vid=0x{info.vendor_id:04X} pid=0x{info.product_id:04X} "
                f"usage_page=0x{info.usage_page:04X} usage=0x{info.usage:02X} "
                f"interface={info.interface_number}"
            )
            print(
                "    "
                f"manufacturer={info.manufacturer_string!r} "
                f"product={info.product_string!r} serial={info.serial_number!r}"
            )
        return 0

    tlvs = build_tlvs(args)
    if not tlvs:
        parser.error("no TLV fields specified; pass at least one of --progress/--top-text/--bottom-text/etc.")

    packets = chunk_packets(tlvs, args.report_size)
    dev, info = open_device(args.path, args.vid, args.pid, args.usage_page, args.usage)

    try:
        print(
            f"Opened RAW HID device vid=0x{info.vendor_id:04X} pid=0x{info.product_id:04X} "
            f"usage_page=0x{info.usage_page:04X} usage=0x{info.usage:02X}"
        )
        write_packets(dev, packets, args.report_size)
    finally:
        dev.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
