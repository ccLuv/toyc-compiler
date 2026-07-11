from pathlib import Path
import csv
import zipfile
from xml.sax.saxutils import escape


ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "bug-collection"
CSV = OUT_DIR / "commits-log.csv"
XLSX = ROOT / "commits-log.xlsx"

ROWS = [
    ["Issue", "PR", "阶段", "分支", "测试文件", "修复文件", "关键 Commit", "状态"],
    [
        "#1",
        "待补",
        "前期",
        "fix/issue-001-short-circuit-side-effect",
        "tests/short_circuit.tc; tests/short_circuit.expected",
        "src/main.cpp",
        "568865a 或实际 PR 合入 commit",
        "待创建 Issue/PR",
    ],
    [
        "#2",
        "待补",
        "中期",
        "fix/issue-002-nested-loop-labels",
        "tests/nested_loops.tc; tests/nested_loops.expected",
        "src/main.cpp",
        "248743d 或实际 PR 合入 commit",
        "待创建 Issue/PR",
    ],
    [
        "#3",
        "待补",
        "前期",
        "fix/issue-003-stack-arguments",
        "tests/many_args.tc; tests/many_args.expected",
        "src/main.cpp",
        "568865a 或实际 PR 合入 commit",
        "待创建 Issue/PR",
    ],
    [
        "#4",
        "待补",
        "中期",
        "fix/issue-004-tail-recursion-result",
        "tests/tail_recursion.tc; tests/tail_recursion.expected",
        "src/main.cpp",
        "634b1d7 或实际 PR 合入 commit",
        "待创建 Issue/PR",
    ],
    [
        "#5",
        "待补",
        "后期",
        "fix/issue-005-readonly-global-propagation",
        "tests/readonly_global.tc; tests/readonly_global.expected",
        "src/main.cpp",
        "634b1d7 或实际 PR 合入 commit",
        "待创建 Issue/PR",
    ],
]


def write_csv():
    with CSV.open("w", newline="", encoding="utf-8-sig") as f:
        writer = csv.writer(f)
        writer.writerows(ROWS)


def cell_ref(col, row):
    letters = ""
    col += 1
    while col:
        col, rem = divmod(col - 1, 26)
        letters = chr(65 + rem) + letters
    return f"{letters}{row}"


def sheet_xml():
    rows = []
    for r_idx, row in enumerate(ROWS, start=1):
        cells = []
        for c_idx, value in enumerate(row):
            ref = cell_ref(c_idx, r_idx)
            text = escape(str(value))
            cells.append(
                f'<c r="{ref}" t="inlineStr"><is><t>{text}</t></is></c>'
            )
        rows.append(f'<row r="{r_idx}">{"".join(cells)}</row>')
    return (
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        '<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" '
        'xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">'
        '<cols>'
        '<col min="1" max="1" width="10" customWidth="1"/>'
        '<col min="2" max="2" width="10" customWidth="1"/>'
        '<col min="3" max="3" width="10" customWidth="1"/>'
        '<col min="4" max="4" width="38" customWidth="1"/>'
        '<col min="5" max="5" width="46" customWidth="1"/>'
        '<col min="6" max="6" width="18" customWidth="1"/>'
        '<col min="7" max="7" width="34" customWidth="1"/>'
        '<col min="8" max="8" width="20" customWidth="1"/>'
        '</cols>'
        f'<sheetData>{"".join(rows)}</sheetData>'
        '</worksheet>'
    )


def write_xlsx():
    content_types = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
<Default Extension="xml" ContentType="application/xml"/>
<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>
<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>
</Types>"""
    root_rels = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>
</Relationships>"""
    workbook = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">
<sheets><sheet name="commits-log" sheetId="1" r:id="rId1"/></sheets>
</workbook>"""
    workbook_rels = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>
</Relationships>"""
    with zipfile.ZipFile(XLSX, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", content_types)
        z.writestr("_rels/.rels", root_rels)
        z.writestr("xl/workbook.xml", workbook)
        z.writestr("xl/_rels/workbook.xml.rels", workbook_rels)
        z.writestr("xl/worksheets/sheet1.xml", sheet_xml())


if __name__ == "__main__":
    OUT_DIR.mkdir(exist_ok=True)
    write_csv()
    write_xlsx()
    print(str(XLSX))
    print(str(CSV))
