#!/usr/bin/env python3
"""Word (.docx) helper for shaman's docx skill. Run with --help for commands."""
import argparse
import csv
import re
import sys

try:
    import docx
    from docx.shared import Pt
except ImportError:
    sys.exit("missing Python module 'docx': python3 -m pip install python-docx")


def iter_blocks(document):
    """Paragraphs and tables in document order."""
    from docx.table import Table
    from docx.text.paragraph import Paragraph
    for child in document.element.body.iterchildren():
        tag = child.tag.split("}")[-1]
        if tag == "p":
            yield Paragraph(child, document)
        elif tag == "tbl":
            yield Table(child, document)


def cmd_read(a):
    d = docx.Document(a.input)
    for block in iter_blocks(d):
        if block.__class__.__name__ == "Table":
            rows = [[c.text.strip().replace("\n", " ") for c in r.cells] for r in block.rows]
            if not rows:
                continue
            print("| " + " | ".join(rows[0]) + " |")
            print("|" + "---|" * len(rows[0]))
            for r in rows[1:]:
                print("| " + " | ".join(r) + " |")
            print()
            continue
        text = block.text.strip()
        if not text:
            continue
        style = (block.style.name or "").lower()
        if style.startswith("heading"):
            level = int(re.sub(r"\D", "", style) or 1)
            print("#" * level + " " + text + "\n")
        elif style == "title":
            print("# " + text + "\n")
        elif "list" in style:
            print(("1. " if "number" in style else "- ") + text)
        else:
            print(text + "\n")


def add_inline(paragraph, text):
    for part in re.split(r"(\*\*.+?\*\*|\*[^*\s][^*]*\*|`[^`]+`)", text):
        if not part:
            continue
        if part.startswith("**") and part.endswith("**"):
            paragraph.add_run(part[2:-2]).bold = True
        elif part.startswith("`") and part.endswith("`"):
            run = paragraph.add_run(part[1:-1])
            run.font.name = "Consolas"
            run.font.size = Pt(10)
        elif part.startswith("*") and part.endswith("*"):
            paragraph.add_run(part[1:-1]).italic = True
        else:
            paragraph.add_run(part)


def cmd_create(a):
    d = docx.Document(a.template) if a.template else docx.Document()
    lines = open(a.source, encoding="utf-8").read().splitlines() if a.source else sys.stdin.read().splitlines()
    i = 0
    while i < len(lines):
        s = lines[i].rstrip()
        if not s.strip():
            i += 1
            continue
        if s.lstrip().startswith("|"):  # table
            rows = []
            while i < len(lines) and lines[i].strip().startswith("|"):
                cells = [c.strip() for c in lines[i].strip().strip("|").split("|")]
                if not all(re.fullmatch(r":?-{2,}:?", c) for c in cells):
                    rows.append(cells)
                i += 1
            t = d.add_table(rows=len(rows), cols=max(len(r) for r in rows))
            t.style = "Table Grid"
            for r, row in enumerate(rows):
                for c, cell in enumerate(row):
                    p = t.cell(r, c).paragraphs[0]
                    add_inline(p, cell)
                    if r == 0:
                        for run in p.runs:
                            run.bold = True
            d.add_paragraph()
            continue
        m = re.match(r"^(#{1,6})\s+(.*)", s)
        if m:
            d.add_heading(m.group(2), level=min(len(m.group(1)), 4) if not (len(m.group(1)) == 1 and i == 0) else 0)
        elif re.match(r"^\s*[-*+]\s+", s):
            add_inline(d.add_paragraph(style="List Bullet"), re.sub(r"^\s*[-*+]\s+", "", s))
        elif re.match(r"^\s*\d+[.)]\s+", s):
            add_inline(d.add_paragraph(style="List Number"), re.sub(r"^\s*\d+[.)]\s+", "", s))
        else:
            para = [s]
            while i + 1 < len(lines) and lines[i + 1].strip() and not re.match(r"^(#|\s*[-*+]\s|\s*\d+[.)]\s|\s*\|)", lines[i + 1]):
                i += 1
                para.append(lines[i].strip())
            add_inline(d.add_paragraph(), " ".join(para))
        i += 1
    d.save(a.output)
    print(f"wrote {a.output}")


def replace_in_paragraph(p, old, new):
    if old not in p.text:
        return 0
    n = 0
    for run in p.runs:  # common case: the phrase sits inside one run
        if old in run.text:
            n += run.text.count(old)
            run.text = run.text.replace(old, new)
    if n == 0 and p.runs:  # phrase spans runs: collapse into the first run
        text = p.text.replace(old, new)
        for run in p.runs[1:]:
            run.text = ""
        p.runs[0].text = text
        n = 1
        print(f"note: '{old}' spanned formatting runs in: {text[:60]!r}", file=sys.stderr)
    return n


def all_paragraphs(d):
    yield from d.paragraphs
    for t in d.tables:
        for row in t.rows:
            for cell in row.cells:
                yield from cell.paragraphs
    for s in d.sections:
        for part in (s.header, s.footer):
            yield from part.paragraphs


def cmd_replace(a):
    if len(a.pairs) % 2:
        sys.exit("give old/new pairs")
    d = docx.Document(a.input)
    for old, new in zip(a.pairs[::2], a.pairs[1::2]):
        n = sum(replace_in_paragraph(p, old, new) for p in all_paragraphs(d))
        print(f"{old!r} -> {new!r}: {n} replacement(s)")
    d.save(a.output)


def cmd_add_table(a):
    d = docx.Document(a.input)
    rows = list(csv.reader(open(a.csv, newline="", encoding="utf-8")))
    t = d.add_table(rows=len(rows), cols=max(len(r) for r in rows))
    try:
        t.style = a.style
    except (KeyError, ValueError):
        t.style = "Table Grid"
    for r, row in enumerate(rows):
        for c, value in enumerate(row):
            t.cell(r, c).text = value
    d.save(a.output)
    print(f"added {len(rows)}x{len(rows[0]) if rows else 0} table -> {a.output}")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("read"); s.add_argument("input"); s.set_defaults(fn=cmd_read)
    s = sub.add_parser("create"); s.add_argument("output"); s.add_argument("--from", dest="source"); s.add_argument("--template")
    s.set_defaults(fn=cmd_create)
    s = sub.add_parser("replace"); s.add_argument("input"); s.add_argument("output"); s.add_argument("pairs", nargs="+")
    s.set_defaults(fn=cmd_replace)
    s = sub.add_parser("add-table"); s.add_argument("input"); s.add_argument("output"); s.add_argument("csv")
    s.add_argument("--style", default="Light Grid Accent 1"); s.set_defaults(fn=cmd_add_table)
    a = p.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
