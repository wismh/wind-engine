#!/usr/bin/env python3
"""Render docs/audit/wind-architecture-audit-uk.md to a printable PDF."""

from __future__ import annotations

import re
import sys
from pathlib import Path

from reportlab.lib import colors
from reportlab.lib.enums import TA_JUSTIFY, TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (
    ListFlowable,
    ListItem,
    PageBreak,
    Paragraph,
    Preformatted,
    SimpleDocTemplate,
    Spacer,
    Table,
    TableStyle,
)

ROOT = Path(__file__).resolve().parent
MD = ROOT / "wind-architecture-audit-uk.md"
PDF = ROOT / "wind-architecture-audit-uk.pdf"

FONT_DIR = Path("/usr/share/fonts/truetype/dejavu")
pdfmetrics.registerFont(TTFont("DejaVu", str(FONT_DIR / "DejaVuSans.ttf")))
pdfmetrics.registerFont(TTFont("DejaVuBold", str(FONT_DIR / "DejaVuSans-Bold.ttf")))
pdfmetrics.registerFont(TTFont("DejaVuMono", str(FONT_DIR / "DejaVuSansMono.ttf")))


def md_inline(text: str) -> str:
    text = text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    text = re.sub(r"\*\*([^*]+)\*\*", r"<b>\1</b>", text)
    def code_span(match: re.Match[str]) -> str:
        inner = match.group(1).replace("*", "&#42;")
        return f'<font name="DejaVuMono" size="8">{inner}</font>'

    text = re.sub(r"`([^`]+)`", code_span, text)
    return text


def parse_table(lines: list[str], i: int) -> tuple[list[list[str]], int]:
    rows: list[list[str]] = []
    while i < len(lines) and lines[i].startswith("|"):
        raw = lines[i].strip()
        cells = [c.strip() for c in raw.strip("|").split("|")]
        if not all(re.fullmatch(r":?-{3,}:?", c.replace(" ", "")) for c in cells):
            rows.append(cells)
        i += 1
    return rows, i


def build_styles() -> dict[str, ParagraphStyle]:
    base = getSampleStyleSheet()
    styles = {
        "h1": ParagraphStyle(
            "H1Uk",
            parent=base["Heading1"],
            fontName="DejaVuBold",
            fontSize=16,
            leading=20,
            spaceAfter=10,
            textColor=colors.HexColor("#1a1a1a"),
        ),
        "h2": ParagraphStyle(
            "H2Uk",
            parent=base["Heading2"],
            fontName="DejaVuBold",
            fontSize=12.5,
            leading=16,
            spaceBefore=12,
            spaceAfter=6,
            textColor=colors.HexColor("#1a1a1a"),
        ),
        "h3": ParagraphStyle(
            "H3Uk",
            parent=base["Heading3"],
            fontName="DejaVuBold",
            fontSize=10.5,
            leading=14,
            spaceBefore=9,
            spaceAfter=4,
            textColor=colors.HexColor("#222222"),
        ),
        "body": ParagraphStyle(
            "BodyUk",
            parent=base["BodyText"],
            fontName="DejaVu",
            fontSize=9,
            leading=12.5,
            alignment=TA_JUSTIFY,
            spaceAfter=6,
        ),
        "cell": ParagraphStyle(
            "CellUk",
            parent=base["BodyText"],
            fontName="DejaVu",
            fontSize=7.5,
            leading=10,
            alignment=TA_LEFT,
        ),
        "cellh": ParagraphStyle(
            "CellHUk",
            parent=base["BodyText"],
            fontName="DejaVuBold",
            fontSize=7.5,
            leading=10,
            alignment=TA_LEFT,
        ),
        "code": ParagraphStyle(
            "CodeUk",
            parent=base["Code"],
            fontName="DejaVuMono",
            fontSize=7.5,
            leading=10,
            leftIndent=6,
            spaceBefore=4,
            spaceAfter=8,
            backColor=colors.HexColor("#f4f4f4"),
        ),
        "bullet": ParagraphStyle(
            "BulletUk",
            parent=base["BodyText"],
            fontName="DejaVu",
            fontSize=9,
            leading=12.5,
            leftIndent=12,
            spaceAfter=2,
        ),
        "footer": ParagraphStyle(
            "FooterUk",
            parent=base["Normal"],
            fontName="DejaVu",
            fontSize=8,
            textColor=colors.HexColor("#666666"),
        ),
    }
    return styles


def table_flow(rows: list[list[str]], styles: dict[str, ParagraphStyle], col_count: int) -> Table:
    header, *body = rows
    data = [[Paragraph(md_inline(c), styles["cellh"]) for c in header]]
    for row in body:
        padded = row + [""] * (col_count - len(row))
        data.append([Paragraph(md_inline(c), styles["cell"]) for c in padded[:col_count]])
    widths = [150 / col_count * mm] * col_count
    if col_count == 2:
        widths = [40 * mm, 130 * mm]
    elif col_count == 5:
        widths = [38 * mm, 18 * mm, 22 * mm, 18 * mm, 74 * mm]
    elif col_count >= 4:
        widths = [70 * mm] + [100 / (col_count - 1) * mm] * (col_count - 1)
    t = Table(data, colWidths=widths, repeatRows=1)
    t.setStyle(
        TableStyle(
            [
                ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#ececec")),
                ("GRID", (0, 0), (-1, -1), 0.3, colors.HexColor("#bdbdbd")),
                ("VALIGN", (0, 0), (-1, -1), "TOP"),
                ("LEFTPADDING", (0, 0), (-1, -1), 4),
                ("RIGHTPADDING", (0, 0), (-1, -1), 4),
                ("TOPPADDING", (0, 0), (-1, -1), 3),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 3),
                (
                    "ROWBACKGROUNDS",
                    (0, 1),
                    (-1, -1),
                    [colors.white, colors.HexColor("#fafafa")],
                ),
            ]
        )
    )
    return t


def story_from_md(text: str, styles: dict[str, ParagraphStyle]) -> list:
    lines = text.splitlines()
    story: list = []
    i = 0
    in_code = False
    code_buf: list[str] = []
    bullets: list[str] = []

    def flush_bullets() -> None:
        nonlocal bullets
        if not bullets:
            return
        items = [
            ListItem(Paragraph(md_inline(b), styles["bullet"]), leftIndent=8, bulletColor=colors.HexColor("#444"))
            for b in bullets
        ]
        story.append(ListFlowable(items, bulletType="bullet", start="•", leftIndent=14, bulletFontName="DejaVu"))
        story.append(Spacer(1, 4))
        bullets = []

    while i < len(lines):
        line = lines[i]
        if line.startswith("```"):
            flush_bullets()
            if in_code:
                story.append(Preformatted("\n".join(code_buf) if code_buf else " ", styles["code"]))
                code_buf = []
                in_code = False
            else:
                in_code = True
            i += 1
            continue
        if in_code:
            code_buf.append(line)
            i += 1
            continue
        if line.startswith("|") and i + 1 < len(lines) and lines[i + 1].startswith("|"):
            flush_bullets()
            rows, i = parse_table(lines, i)
            if rows:
                story.append(table_flow(rows, styles, max(len(r) for r in rows)))
                story.append(Spacer(1, 8))
            continue
        if not line.strip():
            flush_bullets()
            i += 1
            continue
        if line.startswith("# "):
            flush_bullets()
            story.append(Paragraph(md_inline(line[2:]), styles["h1"]))
        elif line.startswith("## "):
            flush_bullets()
            story.append(Paragraph(md_inline(line[3:]), styles["h2"]))
        elif line.startswith("### "):
            flush_bullets()
            story.append(Paragraph(md_inline(line[4:]), styles["h3"]))
        elif line.startswith("---"):
            flush_bullets()
            story.append(Spacer(1, 6))
        elif line.startswith("- "):
            bullets.append(line[2:])
        else:
            flush_bullets()
            story.append(Paragraph(md_inline(line), styles["body"]))
        i += 1
    flush_bullets()
    return story


def add_page(_canvas, doc) -> None:
    _canvas.saveState()
    _canvas.setFont("DejaVu", 8)
    _canvas.setFillColor(colors.HexColor("#666666"))
    _canvas.drawString(18 * mm, 12 * mm, "Wind — архітектурний аудит  ·  main @ 14b1945")
    _canvas.drawRightString(A4[0] - 18 * mm, 12 * mm, str(doc.page))
    _canvas.restoreState()


def main() -> int:
    md = MD.read_text(encoding="utf-8")
    styles = build_styles()
    doc = SimpleDocTemplate(
        str(PDF),
        pagesize=A4,
        leftMargin=16 * mm,
        rightMargin=16 * mm,
        topMargin=16 * mm,
        bottomMargin=18 * mm,
        title="Архітектурний аудит Wind",
        author="Cursor architecture audit",
    )
    doc.build(story_from_md(md, styles), onFirstPage=add_page, onLaterPages=add_page)
    print(f"wrote {PDF} ({PDF.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
