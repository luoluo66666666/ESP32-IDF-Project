#!/usr/bin/env python3
"""Generate ESP-wash command code Excel (.xlsx) using stdlib only."""

import zipfile
from pathlib import Path
from xml.sax.saxutils import escape

NS_MAIN = "http://schemas.openxmlformats.org/spreadsheetml/2006/main"
NS_REL = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
NS_PKG = "http://schemas.openxmlformats.org/package/2006/relationships"
NS_CT = "http://schemas.openxmlformats.org/package/2006/content-types"

HEADERS = ("分类", "命令码", "参数/格式", "成功应答", "错误应答", "适用通道", "备注")

# (category, command, params, ok_resp, err_resp, channels, note)
ROWS_OVERVIEW = [
    ("协议", "—", "一行一条，建议 \\n 结尾", "明文应答，常见 \\r\\n", "—", "BLE / AP TCP / 云端 TCP", "无 CMD|/ACK| 包装"),
    ("协议", "未知命令", "—", "—", "CMD:ERR", "三通道", "ctrl_protocol 未识别时"),
]

ROWS_CFG = [
    ("CFG", "CFG:GET", "无", "CFG:GET,SSID=...,WIFI_PASSWORD=...,SERVER_IP=...,SERVER_PORT=...,OTA_URL=...", "—", "三通道", "含 WiFi 密码明文"),
    ("CFG", "CFG:WIFI_SSID=名称", "SSID 字符串", "CFG:OK,WIFI_SSID", "CFG:ERR,WIFI_SSID", "三通道", "店铺路由器名称"),
    ("CFG", "CFG:WIFI_PASSWORD=密码", "密码字符串", "CFG:OK,WIFI_PASSWORD", "CFG:ERR,WIFI_PASSWORD", "三通道", ""),
    ("CFG", "CFG:SERVER_IP=x.x.x.x", "IPv4", "CFG:OK,SERVER_IP", "CFG:ERR,SERVER_IP", "三通道", "云端 TCP 服务器"),
    ("CFG", "CFG:SERVER_PORT=9000", "1-65535", "CFG:OK,SERVER_PORT", "CFG:ERR,SERVER_PORT", "三通道", ""),
    ("CFG", "CFG:OTA_URL=http://...", "须 http:// 或 https://", "CFG:OK,OTA_URL", "CFG:ERR,OTA_URL", "三通道", "OTA 固件 HTTP 地址"),
    ("CFG", "CFG:SAVE", "无", "CFG:OK,SAVED", "CFG:ERR,SAVE", "三通道", "仅写 NVS，不切网"),
    ("CFG", "CFG:APPLY", "无", "CFG:OK,APPLY", "CFG:ERR,APPLY", "三通道", "等同 SYS:MODE=STA"),
    ("CFG", "CFG:CANCEL", "无", "CFG:OK,CANCEL", "—", "三通道", "退回 AP 配网"),
    ("CFG", "其它 CFG:*", "—", "—", "CFG:ERR,UNKNOWN", "三通道", ""),
]

ROWS_SYS = [
    ("SYS", "SYS:STATUS", "无", "SYS:STATUS,MODE=AP|STA,STA=...,TCP=...,TRIAL=...,PROVISIONED=YES|NO", "—", "三通道", ""),
    ("SYS", "SYS:MODE=STA", "无", "SYS:OK,MODE=STA", "SYS:ERR,MODE", "三通道", "STA 获 IP 后写 wifi_prov"),
    ("SYS", "SYS:MODE=AP", "无", "SYS:OK,MODE=AP", "SYS:ERR,MODE", "三通道", "清除已配网标志"),
    ("SYS", "其它 SYS:*", "—", "—", "SYS:ERR,UNKNOWN", "三通道", ""),
]

ROWS_CMD = [
    ("CMD", "CMD:SN_GET", "无", "CMD:SN,OK,SN=SN_...", "—", "三通道", "查询芯片设备码"),
    ("CMD", "CMD:SN", "无", "同上", "—", "三通道", "同 CMD:SN_GET"),
    ("CMD", "CMD:OTA", "无（用 NVS OTA_URL）", "CMD:OTA,OK,URL=...", "CMD:OTA,ERR,NO_WIFI / NO_TASK / TRIGGER", "三通道", "需 STA 已连 WiFi"),
    ("CMD", "CMD:OTA=http://...", "临时 URL，不写 NVS", "CMD:OTA,OK,URL=...", "CMD:OTA,ERR,BAD_URL 等", "三通道", ""),
    ("CMD", "ota", "无", "同 CMD:OTA", "同上", "三通道", "大小写不敏感别名"),
    ("CMD", "CMD:MODE0", "无", "CMD:MODE0,OK", "CMD:MODE0,ERROR", "三通道", "洗涤模式 0"),
    ("CMD", "CMD:MODE1", "无", "CMD:MODE1,OK", "CMD:MODE1,ERROR", "三通道", "模式 1"),
    ("CMD", "CMD:MODE2", "无", "CMD:MODE2,OK", "CMD:MODE2,ERROR", "三通道", "模式 2"),
    ("CMD", "CMD:MODE3", "无", "CMD:MODE3,OK", "CMD:MODE3,ERROR", "三通道", "模式 3"),
    ("CMD", "CMD:MODE4", "无", "CMD:MODE4,OK", "CMD:MODE4,ERROR", "三通道", "模式 4"),
    ("CMD", "CMD:MODE5_UPPER", "无", "CMD:MODE5_UPPER,OK", "CMD:MODE5_UPPER,ERROR", "三通道", "模式 5 上"),
    ("CMD", "CMD:MODE5_LOWER", "无", "CMD:MODE5_LOWER,OK", "CMD:MODE5_LOWER,ERROR", "三通道", "模式 5 下"),
    ("CMD", "CMD:MODE_GET", "无", "CMD:MODE_GET,OK", "—", "三通道", "查询当前模式状态"),
    ("CMD", "CMD:STOP", "无", "CMD:STOP,OK", "—", "三通道", "停模式+关 DO+停电机"),
    ("CMD", "stop", "无", "CMD:STOP,OK", "—", "三通道", "CMD:STOP 别名"),
]

ROWS_PERIPH = [
    ("DO", "all on", "无", "ALL,ON", "—", "三通道", ""),
    ("DO", "all off", "无", "ALL,OFF", "—", "三通道", ""),
    ("DO", "doN on", "N=0..n-1", "doN,ON", "—", "三通道", "数字输出"),
    ("DO", "doN off", "N=0..n-1", "doN,OFF", "—", "三通道", ""),
    ("DO", "doN toggle", "N=0..n-1", "doN,TOGGLED", "—", "三通道", ""),
    ("水阀", "water ...", "任意", "WATER,ERR,UNSUPPORTED", "—", "三通道", "当前未实现"),
    ("按摩", "massage stop", "无", "MASSAGE,STOP,OK", "MASSAGE,STOP,ERR", "三通道", "Modbus"),
    ("按摩", "massage test", "无", "MASSAGE,TEST,OK", "—", "三通道", "后台测试任务"),
    ("按摩", "massage mode 1|2|3", "模式号", "MASSAGE,MODE,n,OK", "MASSAGE,MODE,ERR", "三通道", ""),
    ("按摩", "massage strength 1|2|3", "强度", "MASSAGE,STRENGTH,n,OK", "MASSAGE,STRENGTH,ERR", "三通道", ""),
    ("按摩", "格式错误", "—", "—", "MASSAGE,ERR,BAD_FORMAT", "三通道", ""),
    ("变频", "inverter start", "无", "INVERTER,START,OK", "INVERTER,START,ERR", "三通道", ""),
    ("变频", "inverter stop", "无", "INVERTER,STOP,OK", "INVERTER,STOP,ERR", "三通道", ""),
    ("变频", "inverter set <float>", "压力值", "INVERTER,SET,x.x,OK", "INVERTER,SET,ERR", "三通道", ""),
    ("变频", "inverter get", "无", "INVERTER,GET,<raw>", "INVERTER,GET,ERR", "三通道", ""),
    ("变频", "inverter test", "无", "INVERTER,TEST,OK", "—", "三通道", ""),
    ("变频", "格式错误", "—", "—", "INVERTER,ERR,BAD_FORMAT", "三通道", ""),
    ("温控", "temp set <n>", "目标温度", "TEMP,SET,<寄存器值>", "TEMP,SET,ERR", "三通道", ""),
    ("温控", "temp get", "无", "TEMP,GET,6个寄存器", "TEMP,GET,ERR", "三通道", ""),
    ("温控", "temp water", "无", "TEMP,WATER,<值>", "TEMP,WATER,ERR", "三通道", ""),
    ("温控", "temp flow", "无", "TEMP,FLOW,<值>", "TEMP,FLOW,ERR", "三通道", ""),
    ("温控", "temp all", "无", "TEMP,ALL,3个值", "TEMP,ALL,ERR", "三通道", ""),
    ("温控", "temp test", "无", "TEMP,TEST,OK", "—", "三通道", ""),
    ("温控", "格式错误", "—", "—", "TEMP,ERR,BAD_FORMAT", "三通道", ""),
]

ROWS_PUSH_IGNORE = [
    ("设备上报", "REG|SN_xxx|版本", "TCP 连上/重连自动发", "—", "—", "云端 TCP", "非用户命令"),
    ("设备上报", "CMD:SN,OK,SN=...", "连上自动发", "—", "—", "AP TCP / BLE", ""),
    ("云端忽略", "REG,OK", "桥接应答", "不应答", "—", "云端 TCP", "should_ignore_server_line"),
    ("云端忽略", "PING / PONG", "心跳", "不应答", "—", "云端 TCP", ""),
    ("桥接忽略", "ERR|*", "桥接控制", "空应答(ctrl)", "—", "三通道", "ctrl 静默忽略"),
    ("桥接忽略", "EVENT|*", "桥接事件", "空应答(ctrl)", "—", "三通道", ""),
]

ROWS_DEFAULTS = [
    ("默认", "AP 热点名", "ESP-WASH", "", "", "", "固定"),
    ("默认", "AP 密码", "12345678", "", "", "", ""),
    ("默认", "本地 TCP", "192.168.4.1:9000", "", "", "", "配网模式"),
    ("默认", "默认路由器 SSID", "ZMJD（可 CFG 覆盖）", "", "", "", ""),
    ("默认", "默认云端", "192.168.1.125:9000", "", "", "", "NVS 可覆盖"),
    ("默认", "设备码格式", "SN_+MAC 12位十六进制", "", "", "", "如 SN_98A316ECF194"),
]

SHEETS = [
    ("命令总表", ROWS_OVERVIEW + ROWS_CFG + ROWS_SYS + ROWS_CMD + ROWS_PERIPH + ROWS_PUSH_IGNORE),
    ("CFG", ROWS_CFG),
    ("SYS", ROWS_SYS),
    ("CMD", ROWS_CMD),
    ("外设", ROWS_PERIPH),
    ("上报与忽略", ROWS_PUSH_IGNORE),
    ("默认参数", ROWS_DEFAULTS),
]


def col_name(idx: int) -> str:
    """1-based column index to Excel column letter."""
    name = ""
    while idx > 0:
        idx, rem = divmod(idx - 1, 26)
        name = chr(65 + rem) + name
    return name


def worksheet_xml(rows: list[tuple]) -> str:
    all_rows = [HEADERS] + [r for r in rows]
    lines = [
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>',
        f'<worksheet xmlns="{NS_MAIN}">',
        "<sheetData>",
    ]
    for r_idx, row in enumerate(all_rows, start=1):
        lines.append(f'<row r="{r_idx}">')
        for c_idx, cell in enumerate(row, start=1):
            ref = f"{col_name(c_idx)}{r_idx}"
            text = escape(str(cell)) if cell is not None else ""
            lines.append(
                f'<c r="{ref}" t="inlineStr"><is><t>{text}</t></is></c>'
            )
        lines.append("</row>")
    lines.append("</sheetData></worksheet>")
    return "\n".join(lines)


def build_xlsx(path: Path) -> None:
    sheet_paths = []
    workbook_sheets = []
    for i, (title, rows) in enumerate(SHEETS, start=1):
        rel = f"worksheets/sheet{i}.xml"
        sheet_paths.append((rel, worksheet_xml(rows)))
        workbook_sheets.append(
            f'<sheet name="{escape(title)}" sheetId="{i}" r:id="rId{i}"/>'
        )

    workbook_xml = (
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        f'<workbook xmlns="{NS_MAIN}" xmlns:r="{NS_REL}">'
        "<sheets>"
        + "".join(workbook_sheets)
        + "</sheets></workbook>"
    )

    workbook_rels = ['<?xml version="1.0" encoding="UTF-8" standalone="yes"?>']
    workbook_rels.append(
        f'<Relationships xmlns="{NS_PKG}">'
    )
    for i in range(1, len(SHEETS) + 1):
        workbook_rels.append(
            f'<Relationship Id="rId{i}" '
            f'Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" '
            f'Target="worksheets/sheet{i}.xml"/>'
        )
    workbook_rels.append("</Relationships>")
    workbook_rels_xml = "".join(workbook_rels)

    content_types = [
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>',
        f'<Types xmlns="{NS_CT}">',
        '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>',
        '<Default Extension="xml" ContentType="application/xml"/>',
        '<Override PartName="/xl/workbook.xml" '
        'ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>',
    ]
    for i in range(1, len(SHEETS) + 1):
        content_types.append(
            f'<Override PartName="/xl/worksheets/sheet{i}.xml" '
            'ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>'
        )
    content_types.append("</Types>")
    content_types_xml = "".join(content_types)

    root_rels = (
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        f'<Relationships xmlns="{NS_PKG}">'
        '<Relationship Id="rId1" '
        'Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" '
        'Target="xl/workbook.xml"/>'
        "</Relationships>"
    )

    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        zf.writestr("[Content_Types].xml", content_types_xml)
        zf.writestr("_rels/.rels", root_rels)
        zf.writestr("xl/workbook.xml", workbook_xml)
        zf.writestr("xl/_rels/workbook.xml.rels", workbook_rels_xml)
        for rel, xml in sheet_paths:
            zf.writestr(f"xl/{rel}", xml)


if __name__ == "__main__":
    base = Path(__file__).resolve().parent
    for name in ("ESP-wash-命令码.xlsx", "ESP-wash-command-codes.xlsx"):
        out = base / name
        build_xlsx(out)
        print(f"Wrote {out}")
