#!/usr/bin/env python3
"""Regenerate the ymsoffice demo assets (report.docx / budget.xlsx /
pitch.pptx).

Hand-authored minimal OOXML — no Office producer involved — sized as a
showcase for the ymsoffice renderer: headings, styled runs, lists, a table,
an image placeholder (docx); a multi-column budget grid with formulas
(xlsx); title/content/picture slides with brand-palette fills (pptx).
Run from this directory:

    ./make-assets.py
"""

import zipfile

RELS_XMLNS = "http://schemas.openxmlformats.org/package/2006/relationships"
DOC_REL_TYPE = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"


def content_types(overrides: dict[str, str]) -> str:
    lines = [
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>',
        '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">',
        '  <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>',
        '  <Default Extension="xml" ContentType="application/xml"/>',
    ]
    for part, content_type in overrides.items():
        lines.append(f'  <Override PartName="{part}" ContentType="{content_type}"/>')
    lines.append("</Types>")
    return "\n".join(lines) + "\n"


def root_rels(target: str) -> str:
    return f"""<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="{RELS_XMLNS}">
  <Relationship Id="rId1" Type="{DOC_REL_TYPE}/officeDocument" Target="{target}"/>
</Relationships>
"""


# ---------------------------------------------------------------- report.docx

DOCUMENT_XML = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"
            xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"
            xmlns:wp="http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing">
  <w:body>
    <w:p>
      <w:pPr><w:pStyle w:val="Heading1"/></w:pPr>
      <w:r><w:t>Yetty Quarterly Report</w:t></w:r>
    </w:p>
    <w:p>
      <w:r><w:t xml:space="preserve">This document exercises the </w:t></w:r>
      <w:r><w:rPr><w:b/></w:rPr><w:t>ymsoffice</w:t></w:r>
      <w:r><w:t xml:space="preserve"> renderer: </w:t></w:r>
      <w:r><w:rPr><w:i/></w:rPr><w:t>italic</w:t></w:r>
      <w:r><w:t xml:space="preserve">, </w:t></w:r>
      <w:r><w:rPr><w:u w:val="single"/></w:rPr><w:t>underline</w:t></w:r>
      <w:r><w:t xml:space="preserve">, </w:t></w:r>
      <w:r><w:rPr><w:strike/></w:rPr><w:t>strikethrough</w:t></w:r>
      <w:r><w:t xml:space="preserve">, </w:t></w:r>
      <w:r><w:rPr><w:color w:val="74C5A5"/></w:rPr><w:t>colored text</w:t></w:r>
      <w:r><w:t xml:space="preserve"> and </w:t></w:r>
      <w:r><w:rPr><w:highlight w:val="yellow"/></w:rPr><w:t>highlighted text</w:t></w:r>
      <w:r><w:t>.</w:t></w:r>
    </w:p>
    <w:p>
      <w:pPr><w:pStyle w:val="Heading2"/></w:pPr>
      <w:r><w:t>Highlights</w:t></w:r>
    </w:p>
    <w:p>
      <w:pPr><w:numPr><w:ilvl w:val="0"/><w:numId w:val="1"/></w:numPr></w:pPr>
      <w:r><w:t>Terminal-native document viewing, no external converter</w:t></w:r>
    </w:p>
    <w:p>
      <w:pPr><w:numPr><w:ilvl w:val="0"/><w:numId w:val="1"/></w:numPr></w:pPr>
      <w:r><w:t>ZIP + XML parsed in-process</w:t></w:r>
    </w:p>
    <w:p>
      <w:pPr><w:numPr><w:ilvl w:val="1"/><w:numId w:val="1"/></w:numPr></w:pPr>
      <w:r><w:t>nested bullet: yxml SAX walker</w:t></w:r>
    </w:p>
    <w:p>
      <w:pPr><w:numPr><w:ilvl w:val="1"/><w:numId w:val="1"/></w:numPr></w:pPr>
      <w:r><w:t>nested bullet: zlib inflate</w:t></w:r>
    </w:p>
    <w:p>
      <w:pPr><w:pStyle w:val="Heading2"/></w:pPr>
      <w:r><w:t>Next steps</w:t></w:r>
    </w:p>
    <w:p>
      <w:pPr><w:numPr><w:ilvl w:val="0"/><w:numId w:val="2"/></w:numPr></w:pPr>
      <w:r><w:t>Embedded image decode</w:t></w:r>
    </w:p>
    <w:p>
      <w:pPr><w:numPr><w:ilvl w:val="0"/><w:numId w:val="2"/></w:numPr></w:pPr>
      <w:r><w:t>Convert into the editable yrich model</w:t></w:r>
    </w:p>
    <w:p>
      <w:pPr><w:numPr><w:ilvl w:val="0"/><w:numId w:val="2"/></w:numPr></w:pPr>
      <w:r><w:t>Theme colors for decks</w:t></w:r>
    </w:p>
    <w:p>
      <w:pPr><w:pStyle w:val="Heading2"/></w:pPr>
      <w:r><w:t>Revenue by region</w:t></w:r>
    </w:p>
    <w:tbl>
      <w:tr>
        <w:tc><w:p><w:r><w:rPr><w:b/></w:rPr><w:t>Region</w:t></w:r></w:p></w:tc>
        <w:tc><w:p><w:r><w:rPr><w:b/></w:rPr><w:t>Q1</w:t></w:r></w:p></w:tc>
        <w:tc><w:p><w:r><w:rPr><w:b/></w:rPr><w:t>Q2</w:t></w:r></w:p></w:tc>
        <w:tc><w:p><w:r><w:rPr><w:b/></w:rPr><w:t>Trend</w:t></w:r></w:p></w:tc>
      </w:tr>
      <w:tr>
        <w:tc><w:p><w:r><w:t>North</w:t></w:r></w:p></w:tc>
        <w:tc><w:p><w:r><w:t>1200</w:t></w:r></w:p></w:tc>
        <w:tc><w:p><w:r><w:t>1350</w:t></w:r></w:p></w:tc>
        <w:tc><w:p><w:r><w:rPr><w:color w:val="6BA892"/></w:rPr><w:t>up</w:t></w:r></w:p></w:tc>
      </w:tr>
      <w:tr>
        <w:tc><w:p><w:r><w:t>South</w:t></w:r></w:p></w:tc>
        <w:tc><w:p><w:r><w:t>980</w:t></w:r></w:p></w:tc>
        <w:tc><w:p><w:r><w:t>870</w:t></w:r></w:p></w:tc>
        <w:tc><w:p><w:r><w:rPr><w:color w:val="C55A5A"/></w:rPr><w:t>down</w:t></w:r></w:p></w:tc>
      </w:tr>
      <w:tr>
        <w:tc>
          <w:tcPr><w:gridSpan w:val="4"/></w:tcPr>
          <w:p><w:r><w:rPr><w:i/></w:rPr><w:t>Figures unaudited — spanning footer cell</w:t></w:r></w:p>
        </w:tc>
      </w:tr>
    </w:tbl>
    <w:p>
      <w:r>
        <w:drawing>
          <wp:inline>
            <wp:extent cx="3810000" cy="1905000"/>
            <wp:docPr id="1" name="architecture-diagram.png"/>
          </wp:inline>
        </w:drawing>
      </w:r>
    </w:p>
    <w:p>
      <w:pPr><w:jc w:val="center"/></w:pPr>
      <w:r><w:rPr><w:sz w:val="40"/><w:b/></w:rPr><w:t>Centered 20pt closing line</w:t></w:r>
    </w:p>
    <w:p>
      <w:pPr><w:jc w:val="right"/></w:pPr>
      <w:hyperlink r:id="rId9">
        <w:r><w:t>github.com/zokrezyl/yetty</w:t></w:r>
      </w:hyperlink>
    </w:p>
  </w:body>
</w:document>
"""

STYLES_XML = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:styles xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
  <w:style w:type="paragraph" w:styleId="Heading1"><w:name w:val="heading 1"/></w:style>
  <w:style w:type="paragraph" w:styleId="Heading2"><w:name w:val="heading 2"/></w:style>
</w:styles>
"""

NUMBERING_XML = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:numbering xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
  <w:abstractNum w:abstractNumId="0">
    <w:lvl w:ilvl="0"><w:numFmt w:val="bullet"/></w:lvl>
    <w:lvl w:ilvl="1"><w:numFmt w:val="bullet"/></w:lvl>
  </w:abstractNum>
  <w:abstractNum w:abstractNumId="1">
    <w:lvl w:ilvl="0"><w:numFmt w:val="decimal"/></w:lvl>
  </w:abstractNum>
  <w:num w:numId="1"><w:abstractNumId w:val="0"/></w:num>
  <w:num w:numId="2"><w:abstractNumId w:val="1"/></w:num>
</w:numbering>
"""


def make_docx() -> None:
    overrides = {
        "/word/document.xml":
            "application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml",
    }
    with zipfile.ZipFile("report.docx", "w", zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("[Content_Types].xml", content_types(overrides))
        archive.writestr("_rels/.rels", root_rels("word/document.xml"))
        archive.writestr("word/document.xml", DOCUMENT_XML)
        archive.writestr("word/styles.xml", STYLES_XML)
        archive.writestr("word/numbering.xml", NUMBERING_XML)


# ---------------------------------------------------------------- budget.xlsx

WORKBOOK_XML = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"
          xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">
  <sheets>
    <sheet name="Budget" sheetId="1" r:id="rId1"/>
    <sheet name="Notes" sheetId="2" r:id="rId2"/>
  </sheets>
</workbook>
"""

WORKBOOK_RELS = f"""<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="{RELS_XMLNS}">
  <Relationship Id="rId1" Type="{DOC_REL_TYPE}/worksheet" Target="worksheets/sheet1.xml"/>
  <Relationship Id="rId2" Type="{DOC_REL_TYPE}/worksheet" Target="worksheets/sheet2.xml"/>
  <Relationship Id="rId3" Type="{DOC_REL_TYPE}/sharedStrings" Target="sharedStrings.xml"/>
</Relationships>
"""

BUDGET_ROWS = [
    ("Item", "Q1", "Q2", "Q3", "Q4", "Total"),
    ("Hosting", 1200, 1350, 1280, 1400, None),
    ("Licenses", 400, 400, 450, 450, None),
    ("Hardware", 0, 2200, 0, 350, None),
    ("Travel", 180, 820, 150, 600, None),
    ("Total", None, None, None, None, None),
]


def budget_sheet_xml() -> tuple[str, list[str]]:
    """Build sheet1 XML; returns (xml, shared_strings)."""
    strings: list[str] = []

    def string_index(text: str) -> int:
        if text not in strings:
            strings.append(text)
        return strings.index(text)

    lines = [
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>',
        '<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">',
        "  <sheetData>",
    ]
    # Header row (shared strings).
    lines.append('    <row r="1">')
    for col, text in enumerate(BUDGET_ROWS[0]):
        ref = f"{chr(ord('A') + col)}1"
        lines.append(f'      <c r="{ref}" t="s"><v>{string_index(text)}</v></c>')
    lines.append("    </row>")

    # Line items with a per-row SUM formula (cached values included since
    # the renderer does not evaluate).
    for row_index, row in enumerate(BUDGET_ROWS[1:-1], start=2):
        name = row[0]
        quarters = row[1:5]
        total = sum(quarters)
        lines.append(f'    <row r="{row_index}">')
        lines.append(
            f'      <c r="A{row_index}" t="s"><v>{string_index(name)}</v></c>')
        for col, value in enumerate(quarters, start=1):
            ref = f"{chr(ord('A') + col)}{row_index}"
            lines.append(f'      <c r="{ref}"><v>{value}</v></c>')
        lines.append(
            f'      <c r="F{row_index}"><f>SUM(B{row_index}:E{row_index})</f>'
            f"<v>{total}</v></c>")
        lines.append("    </row>")

    # Totals row: one formula per quarter column.
    last = len(BUDGET_ROWS)
    lines.append(f'    <row r="{last}">')
    lines.append(f'      <c r="A{last}" t="s"><v>{string_index("Total")}</v></c>')
    for col in range(1, 6):
        letter = chr(ord("A") + col)
        column_total = sum(
            (row[col] if col < 5 else sum(row[1:5])) for row in BUDGET_ROWS[1:-1])
        lines.append(
            f'      <c r="{letter}{last}"><f>SUM({letter}2:{letter}{last - 1})</f>'
            f"<v>{column_total}</v></c>")
    lines.append("    </row>")
    lines.append("  </sheetData>")
    lines.append("</worksheet>")
    return "\n".join(lines) + "\n", strings


SHEET2_XML = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">
  <sheetData>
    <row r="1"><c r="A1" t="inlineStr"><is><t>Assumptions</t></is></c></row>
    <row r="2"><c r="A2" t="inlineStr"><is><t>Hosting renews in Q4</t></is></c></row>
    <row r="3"><c r="A3" t="inlineStr"><is><t>Hardware refresh in Q2</t></is></c></row>
    <row r="4"><c r="A4" t="b"><v>1</v></c><c r="B4" t="inlineStr"><is><t>reviewed</t></is></c></row>
  </sheetData>
</worksheet>
"""


def make_xlsx() -> None:
    sheet1_xml, strings = budget_sheet_xml()
    shared = [
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>',
        f'<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" '
        f'count="{len(strings)}" uniqueCount="{len(strings)}">',
    ]
    for text in strings:
        shared.append(f"  <si><t>{text}</t></si>")
    shared.append("</sst>")

    overrides = {
        "/xl/workbook.xml":
            "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml",
        "/xl/worksheets/sheet1.xml":
            "application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml",
        "/xl/worksheets/sheet2.xml":
            "application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml",
        "/xl/sharedStrings.xml":
            "application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml",
    }
    with zipfile.ZipFile("budget.xlsx", "w", zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("[Content_Types].xml", content_types(overrides))
        archive.writestr("_rels/.rels", root_rels("xl/workbook.xml"))
        archive.writestr("xl/workbook.xml", WORKBOOK_XML)
        archive.writestr("xl/_rels/workbook.xml.rels", WORKBOOK_RELS)
        archive.writestr("xl/sharedStrings.xml", "\n".join(shared) + "\n")
        archive.writestr("xl/worksheets/sheet1.xml", sheet1_xml)
        archive.writestr("xl/worksheets/sheet2.xml", SHEET2_XML)


# ----------------------------------------------------------------- pitch.pptx

PRESENTATION_XML = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<p:presentation xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main"
                xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">
  <p:sldIdLst>
    <p:sldId id="256" r:id="rId1"/>
    <p:sldId id="257" r:id="rId2"/>
    <p:sldId id="258" r:id="rId3"/>
  </p:sldIdLst>
  <p:sldSz cx="12192000" cy="6858000"/>
</p:presentation>
"""

PRESENTATION_RELS = f"""<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="{RELS_XMLNS}">
  <Relationship Id="rId1" Type="{DOC_REL_TYPE}/slide" Target="slides/slide1.xml"/>
  <Relationship Id="rId2" Type="{DOC_REL_TYPE}/slide" Target="slides/slide2.xml"/>
  <Relationship Id="rId3" Type="{DOC_REL_TYPE}/slide" Target="slides/slide3.xml"/>
</Relationships>
"""

SLIDE_HEADER = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<p:sld xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main"
       xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main">
  <p:cSld><p:spTree>
"""

SLIDE_FOOTER = """  </p:spTree></p:cSld>
</p:sld>
"""


def shape(name: str, shape_id: int, x: int, y: int, cx: int, cy: int, *,
          preset: str = "rect", fill: str | None = None,
          paragraphs: list[str] | None = None) -> str:
    parts = [
        "    <p:sp>",
        f'      <p:nvSpPr><p:cNvPr id="{shape_id}" name="{name}"/></p:nvSpPr>',
        "      <p:spPr>",
        f'        <a:xfrm><a:off x="{x}" y="{y}"/><a:ext cx="{cx}" cy="{cy}"/></a:xfrm>',
        f'        <a:prstGeom prst="{preset}"/>',
    ]
    if fill:
        parts.append(f'        <a:solidFill><a:srgbClr val="{fill}"/></a:solidFill>')
    parts.append("      </p:spPr>")
    if paragraphs:
        parts.append("      <p:txBody>")
        parts.extend(f"        {paragraph}" for paragraph in paragraphs)
        parts.append("      </p:txBody>")
    parts.append("    </p:sp>")
    return "\n".join(parts) + "\n"


def slide1() -> str:
    title = shape(
        "Title", 2, 914400, 1600200, 10363200, 1700000,
        paragraphs=[
            '<a:p><a:pPr algn="ctr"/>'
            '<a:r><a:rPr sz="5400" b="1"/><a:t>yetty &#183; ymsoffice</a:t></a:r></a:p>',
        ])
    subtitle = shape(
        "Subtitle", 3, 914400, 3600000, 10363200, 900000,
        paragraphs=[
            '<a:p><a:pPr algn="ctr"/>'
            '<a:r><a:rPr sz="2400"><a:solidFill><a:srgbClr val="6BA892"/></a:solidFill></a:rPr>'
            "<a:t>docx / xlsx / pptx in the terminal</a:t></a:r></a:p>",
        ])
    return SLIDE_HEADER + title + subtitle + SLIDE_FOOTER


def slide2() -> str:
    title = shape(
        "Title", 2, 914400, 400000, 10363200, 900000,
        paragraphs=[
            '<a:p><a:r><a:rPr sz="3200" b="1"/><a:t>Pipeline</a:t></a:r></a:p>',
        ])
    box_zip = shape(
        "Stage zip", 3, 914400, 2200000, 2800000, 1400000, fill="364A47",
        paragraphs=[
            '<a:p><a:pPr algn="ctr"/><a:r><a:rPr sz="1800"/><a:t>OPC / ZIP</a:t></a:r></a:p>',
        ])
    box_xml = shape(
        "Stage xml", 4, 4700000, 2200000, 2800000, 1400000, fill="5A8979",
        paragraphs=[
            '<a:p><a:pPr algn="ctr"/><a:r><a:rPr sz="1800"/><a:t>yxml SAX</a:t></a:r></a:p>',
        ])
    box_draw = shape(
        "Stage draw", 5, 8480000, 2200000, 2800000, 1400000, fill="6BA892",
        paragraphs=[
            '<a:p><a:pPr algn="ctr"/><a:r><a:rPr sz="1800" b="1"/><a:t>ydraw</a:t></a:r></a:p>',
        ])
    connector = shape("Flow", 6, 914400, 4200000, 10363200, 0, preset="line")
    oval = shape(
        "Badge", 7, 5300000, 4600000, 1600000, 1600000, preset="ellipse",
        fill="74C5A5")
    return SLIDE_HEADER + title + box_zip + box_xml + box_draw + connector + oval + SLIDE_FOOTER


SLIDE3_PIC = """    <p:pic>
      <p:nvPicPr><p:cNvPr id="8" name="screenshot.png"/></p:nvPicPr>
      <p:spPr>
        <a:xfrm><a:off x="2286000" y="1143000"/><a:ext cx="7620000" cy="3810000"/></a:xfrm>
      </p:spPr>
    </p:pic>
"""


def slide3() -> str:
    caption = shape(
        "Caption", 9, 2286000, 5200000, 7620000, 800000,
        paragraphs=[
            '<a:p><a:pPr algn="ctr"/>'
            '<a:r><a:rPr sz="1600" i="1"/><a:t>Embedded media renders as a named placeholder (for now)</a:t></a:r></a:p>',
        ])
    return SLIDE_HEADER + SLIDE3_PIC + caption + SLIDE_FOOTER


def make_pptx() -> None:
    overrides = {
        "/ppt/presentation.xml":
            "application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml",
        "/ppt/slides/slide1.xml":
            "application/vnd.openxmlformats-officedocument.presentationml.slide+xml",
        "/ppt/slides/slide2.xml":
            "application/vnd.openxmlformats-officedocument.presentationml.slide+xml",
        "/ppt/slides/slide3.xml":
            "application/vnd.openxmlformats-officedocument.presentationml.slide+xml",
    }
    with zipfile.ZipFile("pitch.pptx", "w", zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("[Content_Types].xml", content_types(overrides))
        archive.writestr("_rels/.rels", root_rels("ppt/presentation.xml"))
        archive.writestr("ppt/presentation.xml", PRESENTATION_XML)
        archive.writestr("ppt/_rels/presentation.xml.rels", PRESENTATION_RELS)
        archive.writestr("ppt/slides/slide1.xml", slide1())
        archive.writestr("ppt/slides/slide2.xml", slide2())
        archive.writestr("ppt/slides/slide3.xml", slide3())


if __name__ == "__main__":
    make_docx()
    make_xlsx()
    make_pptx()
    print("wrote report.docx budget.xlsx pitch.pptx")
