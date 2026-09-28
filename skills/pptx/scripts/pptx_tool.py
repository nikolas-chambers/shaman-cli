#!/usr/bin/env python3
"""PowerPoint helper for shaman's pptx skill. Run with --help for commands."""
import argparse
import os
import re
import sys

try:
    from pptx import Presentation
    from pptx.util import Inches
except ImportError:
    sys.exit("missing Python module 'pptx': python3 -m pip install python-pptx")


def cmd_read(a):
    prs = Presentation(a.input)
    for n, slide in enumerate(prs.slides, 1):
        print(f"--- slide {n} ({slide.slide_layout.name}) ---")
        for shape in slide.shapes:
            if shape.has_text_frame:
                for para in shape.text_frame.paragraphs:
                    text = "".join(r.text for r in para.runs).strip()
                    if text:
                        print("  " * para.level + ("- " if para.level or shape != slide.shapes.title else "") + text)
            if getattr(shape, "has_table", False) and shape.has_table:
                for row in shape.table.rows:
                    print("| " + " | ".join(c.text for c in row.cells) + " |")
            if shape.shape_type == 13:
                print("[picture]")
        if slide.has_notes_slide and slide.notes_slide.notes_text_frame.text.strip():
            print("notes: " + slide.notes_slide.notes_text_frame.text.strip())


def layout(prs, *names, fallback):
    for l in prs.slide_layouts:
        if l.name in names:
            return l
    return prs.slide_layouts[fallback]


def cmd_create(a):
    prs = Presentation(a.template) if a.template else Presentation()
    if a.template:  # start empty but keep the template's masters and layouts
        for sld_id in list(prs.slides._sldIdLst):
            prs.part.drop_rel(sld_id.rId)
            prs.slides._sldIdLst.remove(sld_id)
    text = open(a.source, encoding="utf-8").read() if a.source else sys.stdin.read()
    base = os.path.dirname(os.path.abspath(a.source)) if a.source else os.getcwd()
    slide = body = None
    notes = []

    def finish():
        if slide is not None and notes:
            slide.notes_slide.notes_text_frame.text = "\n".join(notes)
        notes.clear()

    for line in text.splitlines():
        if not line.strip():
            continue
        if line.startswith("# ") and slide is None:
            slide = prs.slides.add_slide(layout(prs, "Title Slide", fallback=0))
            slide.shapes.title.text = line[2:].strip()
            body = slide.placeholders[1] if len(slide.placeholders) > 1 else None
            continue
        if line.startswith("## ") or line.startswith("# "):
            finish()
            slide = prs.slides.add_slide(layout(prs, "Title and Content", fallback=1))
            slide.shapes.title.text = line.lstrip("#").strip()
            body = slide.placeholders[1] if len(slide.placeholders) > 1 else None
            if body is not None:
                body.text_frame.text = ""
            continue
        if slide is None:
            continue
        if line.lstrip().startswith(">"):
            notes.append(line.lstrip()[1:].strip())
            continue
        img = re.match(r"\s*!\[[^\]]*\]\(([^)]+)\)", line)
        if img:
            path = os.path.join(base, img.group(1))
            slide.shapes.add_picture(path, Inches(1), Inches(1.8), height=Inches(4.5))
            continue
        if body is None:
            continue
        m = re.match(r"^(\s*)[-*+]\s+(.*)", line)
        level, content = (len(m.group(1)) // 2, m.group(2)) if m else (0, line.strip())
        tf = body.text_frame
        para = tf.paragraphs[0] if not tf.paragraphs[0].text else tf.add_paragraph()
        para.text = content.replace("**", "")
        para.level = min(level, 4)
    finish()
    prs.save(a.output)
    print(f"wrote {a.output} ({len(prs.slides)} slides)")


def cmd_replace(a):
    if len(a.pairs) % 2:
        sys.exit("give old/new pairs")
    prs = Presentation(a.input)
    pairs = list(zip(a.pairs[::2], a.pairs[1::2]))
    counts = {old: 0 for old, _ in pairs}
    for slide in prs.slides:
        frames = [s.text_frame for s in slide.shapes if s.has_text_frame]
        if slide.has_notes_slide:
            frames.append(slide.notes_slide.notes_text_frame)
        for tf in frames:
            for para in tf.paragraphs:
                for run in para.runs:
                    for old, new in pairs:
                        if old in run.text:
                            counts[old] += run.text.count(old)
                            run.text = run.text.replace(old, new)
    prs.save(a.output)
    for old, new in pairs:
        print(f"{old!r} -> {new!r}: {counts[old]} replacement(s)")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("read"); s.add_argument("input"); s.set_defaults(fn=cmd_read)
    s = sub.add_parser("create"); s.add_argument("output"); s.add_argument("--from", dest="source"); s.add_argument("--template")
    s.set_defaults(fn=cmd_create)
    s = sub.add_parser("replace"); s.add_argument("input"); s.add_argument("output"); s.add_argument("pairs", nargs="+")
    s.set_defaults(fn=cmd_replace)
    a = p.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
