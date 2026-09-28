#!/usr/bin/env python3
"""PDF helper for shaman's pdf skill. Run with --help for commands."""
import argparse
import csv
import json
import os
import sys


def need(module, pip_name=None):
    try:
        return __import__(module)
    except ImportError:
        sys.exit(f"missing Python module '{module}': python3 -m pip install {pip_name or module}")


def page_list(spec, count):
    if not spec:
        return list(range(count))
    pages = []
    for part in spec.split(","):
        if "-" in part:
            a, b = part.split("-")
            pages.extend(range(int(a) - 1, min(int(b), count)))
        else:
            pages.append(int(part) - 1)
    return [p for p in pages if 0 <= p < count]


def cmd_info(a):
    pypdf = need("pypdf")
    r = pypdf.PdfReader(a.input)
    meta = {k.lstrip("/"): str(v) for k, v in (r.metadata or {}).items()}
    print(json.dumps({"pages": len(r.pages), "encrypted": r.is_encrypted, "metadata": meta,
                      "form_fields": len(r.get_fields() or {})}, indent=2))


def cmd_text(a):
    plumber = need("pdfplumber")
    with plumber.open(a.input) as pdf:
        for i in page_list(a.pages, len(pdf.pages)):
            text = (pdf.pages[i].extract_text(layout=a.layout) or "").replace("(cid:127)", "•")  # standard-font bullet
            print(f"--- page {i + 1} ---\n{text.rstrip()}")


def cmd_tables(a):
    plumber = need("pdfplumber")
    os.makedirs(a.outdir, exist_ok=True)
    n = 0
    with plumber.open(a.input) as pdf:
        for i, page in enumerate(pdf.pages):
            for j, table in enumerate(page.extract_tables()):
                path = os.path.join(a.outdir, f"page{i + 1}_table{j + 1}.csv")
                with open(path, "w", newline="") as f:
                    csv.writer(f).writerows(table)
                n += 1
                print(path)
    if n == 0:
        print("no tables found")


def cmd_merge(a):
    pypdf = need("pypdf")
    w = pypdf.PdfWriter()
    for path in a.inputs:
        w.append(path)
    with open(a.output, "wb") as f:
        w.write(f)
    print(f"wrote {a.output}")


def cmd_split(a):
    pypdf = need("pypdf")
    r = pypdf.PdfReader(a.input)
    os.makedirs(a.outdir, exist_ok=True)
    base = os.path.splitext(os.path.basename(a.input))[0]
    for i, page in enumerate(r.pages):
        w = pypdf.PdfWriter()
        w.add_page(page)
        path = os.path.join(a.outdir, f"{base}_p{i + 1}.pdf")
        with open(path, "wb") as f:
            w.write(f)
        print(path)


def cmd_extract(a):
    pypdf = need("pypdf")
    r = pypdf.PdfReader(a.input)
    w = pypdf.PdfWriter()
    for i in page_list(a.pages, len(r.pages)):
        w.add_page(r.pages[i])
    with open(a.output, "wb") as f:
        w.write(f)
    print(f"wrote {a.output} ({len(w.pages)} pages)")


def cmd_rotate(a):
    pypdf = need("pypdf")
    r = pypdf.PdfReader(a.input)
    w = pypdf.PdfWriter()
    targets = set(page_list(a.pages, len(r.pages)))
    for i, page in enumerate(r.pages):
        if i in targets:
            page.rotate(a.degrees)
        w.add_page(page)
    with open(a.output, "wb") as f:
        w.write(f)
    print(f"wrote {a.output}")


def cmd_stamp(a):
    pypdf = need("pypdf")
    need("reportlab")
    from io import BytesIO
    from reportlab.pdfgen import canvas
    r = pypdf.PdfReader(a.input)
    w = pypdf.PdfWriter()
    for page in r.pages:
        width, height = float(page.mediabox.width), float(page.mediabox.height)
        buf = BytesIO()
        c = canvas.Canvas(buf, pagesize=(width, height))
        c.setFont("Helvetica-Bold", min(width, height) / 8)
        c.setFillGray(0.5, 0.25)
        c.translate(width / 2, height / 2)
        c.rotate(45)
        c.drawCentredString(0, 0, a.text)
        c.save()
        buf.seek(0)
        page.merge_page(pypdf.PdfReader(buf).pages[0])
        w.add_page(page)
    with open(a.output, "wb") as f:
        w.write(f)
    print(f"wrote {a.output}")


def cmd_fields(a):
    pypdf = need("pypdf")
    fields = pypdf.PdfReader(a.input).get_fields() or {}
    out = {}
    for name, f in fields.items():
        kind = {"/Tx": "text", "/Btn": "checkbox/radio", "/Ch": "choice", "/Sig": "signature"}.get(f.get("/FT"), str(f.get("/FT")))
        entry = {"type": kind, "value": str(f.get("/V", ""))}
        if f.get("/_States_"):
            entry["options"] = [str(s) for s in f["/_States_"]]
        out[name] = entry
    print(json.dumps(out, indent=2) if out else "no form fields")


def cmd_fill(a):
    pypdf = need("pypdf")
    values = json.load(open(a.values))
    r = pypdf.PdfReader(a.input)
    w = pypdf.PdfWriter()
    w.append(r)
    fields = r.get_fields() or {}
    unknown = [k for k in values if k not in fields]
    if unknown:
        sys.exit(f"unknown field(s): {', '.join(unknown)} (run: pdf_tool.py fields {a.input})")
    converted = {}
    for k, v in values.items():
        if isinstance(v, bool):
            states = [s for s in fields[k].get("/_States_", []) if s != "/Off"]
            converted[k] = (states[0] if states else "/Yes") if v else "/Off"
        else:
            converted[k] = str(v)
    for page in w.pages:
        w.update_page_form_field_values(page, converted, auto_regenerate=False)
    w.set_need_appearances_writer(True)
    with open(a.output, "wb") as f:
        w.write(f)
    print(f"filled {len(converted)} field(s) -> {a.output}")


def cmd_create(a):
    need("reportlab")
    from reportlab.lib.pagesizes import A4, letter
    from reportlab.lib.styles import getSampleStyleSheet
    from reportlab.lib.units import cm
    from reportlab.platypus import Paragraph, SimpleDocTemplate, Spacer
    from xml.sax.saxutils import escape
    import re

    styles = getSampleStyleSheet()
    from reportlab.lib.styles import ParagraphStyle
    bullet_style = ParagraphStyle("Bullet", parent=styles["BodyText"], leftIndent=14, bulletIndent=4)
    text = open(a.source, encoding="utf-8").read() if a.source else sys.stdin.read()

    def inline(s):
        s = escape(s)
        s = re.sub(r"\*\*(.+?)\*\*", r"<b>\1</b>", s)
        s = re.sub(r"(?<!\*)\*(?!\s)(.+?)\*", r"<i>\1</i>", s)
        return re.sub(r"`(.+?)`", r'<font face="Courier">\1</font>', s)

    story = []
    if a.title:
        story += [Paragraph(escape(a.title), styles["Title"]), Spacer(1, 0.4 * cm)]
    para, bullets = [], []

    def flush():
        nonlocal para, bullets
        if para:
            story.append(Paragraph(inline(" ".join(para)), styles["BodyText"]))
            para = []
        if bullets:
            for b in bullets:  # bulletText keeps the glyph extractable as a real "•"
                story.append(Paragraph(inline(b), bullet_style, bulletText="•"))
            bullets = []

    for line in text.splitlines():
        s = line.strip()
        if not s:
            flush()
        elif s.startswith("#"):
            flush()
            level = min(len(s) - len(s.lstrip("#")), 3)
            story.append(Paragraph(inline(s.lstrip("#").strip()), styles[f"Heading{level}"]))
        elif s.startswith(("- ", "* ")):
            if para:
                flush()
            bullets.append(s[2:])
        else:
            if bullets:
                flush()
            para.append(s)
    flush()
    size = letter if a.letter else A4
    SimpleDocTemplate(a.output, pagesize=size, leftMargin=2 * cm, rightMargin=2 * cm, topMargin=2 * cm,
                      bottomMargin=2 * cm, title=a.title or "").build(story)
    print(f"wrote {a.output}")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("info"); s.add_argument("input"); s.set_defaults(fn=cmd_info)
    s = sub.add_parser("text"); s.add_argument("input"); s.add_argument("--pages")
    s.add_argument("--layout", action="store_true", help="keep column positions"); s.set_defaults(fn=cmd_text)
    s = sub.add_parser("tables"); s.add_argument("input"); s.add_argument("outdir"); s.set_defaults(fn=cmd_tables)
    s = sub.add_parser("merge"); s.add_argument("output"); s.add_argument("inputs", nargs="+"); s.set_defaults(fn=cmd_merge)
    s = sub.add_parser("split"); s.add_argument("input"); s.add_argument("outdir"); s.set_defaults(fn=cmd_split)
    s = sub.add_parser("extract"); s.add_argument("input"); s.add_argument("output"); s.add_argument("--pages", required=True); s.set_defaults(fn=cmd_extract)
    s = sub.add_parser("rotate"); s.add_argument("input"); s.add_argument("output"); s.add_argument("--degrees", type=int, default=90)
    s.add_argument("--pages"); s.set_defaults(fn=cmd_rotate)
    s = sub.add_parser("stamp"); s.add_argument("input"); s.add_argument("output"); s.add_argument("--text", required=True); s.set_defaults(fn=cmd_stamp)
    s = sub.add_parser("fields"); s.add_argument("input"); s.set_defaults(fn=cmd_fields)
    s = sub.add_parser("fill"); s.add_argument("input"); s.add_argument("values"); s.add_argument("output"); s.set_defaults(fn=cmd_fill)
    s = sub.add_parser("create"); s.add_argument("output"); s.add_argument("--from", dest="source"); s.add_argument("--title")
    s.add_argument("--letter", action="store_true", help="US Letter instead of A4"); s.set_defaults(fn=cmd_create)
    a = p.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
