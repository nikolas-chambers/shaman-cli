#!/usr/bin/env python3
"""Spreadsheet helper for shaman's xlsx skill. Run with --help for commands."""
import argparse
import csv
import os
import shutil
import subprocess
import sys
import tempfile

try:
    import openpyxl
    from openpyxl.styles import Alignment, Font, PatternFill
    from openpyxl.utils import get_column_letter
except ImportError:
    sys.exit("missing Python module 'openpyxl': python3 -m pip install openpyxl")

ERRORS = ("#REF!", "#DIV/0!", "#NAME?", "#VALUE!", "#N/A", "#NUM!", "#NULL!")


def cmd_inspect(a):
    wb = openpyxl.load_workbook(a.input)
    for ws in wb.worksheets:
        formulas = sum(1 for row in ws.iter_rows() for c in row if isinstance(c.value, str) and c.value.startswith("="))
        header = [c.value for c in next(ws.iter_rows(max_row=1), [])]
        print(f"{ws.title}: {ws.max_row} rows x {ws.max_column} cols, {formulas} formulas")
        print(f"  header: {header}")
        if ws.freeze_panes:
            print(f"  frozen at {ws.freeze_panes}")
    if wb.defined_names:
        print("named ranges:", ", ".join(wb.defined_names.keys()))


def cmd_read(a):
    wb = openpyxl.load_workbook(a.input, data_only=not a.formulas)
    ws = wb[a.sheet] if a.sheet else wb.active
    rows = [["" if v is None else str(v) for v in r] for r in ws.iter_rows(values_only=True, max_row=a.rows)]
    while rows and not any(rows[-1]):  # formatted-but-empty trailing rows
        rows.pop()
    rows = [r[: max((i + 1 for r2 in rows for i, v in enumerate(r2) if v), default=0)] for r in rows]
    if a.csv:
        csv.writer(sys.stdout).writerows(rows)
        return
    if not rows:
        return print("(empty)")
    width = max(len(r) for r in rows)
    rows = [r + [""] * (width - len(r)) for r in rows]
    print("| " + " | ".join(rows[0]) + " |")
    print("|" + "---|" * width)
    for r in rows[1:]:
        print("| " + " | ".join(c.replace("|", "\\|") for c in r) + " |")
    if ws.max_row > a.rows and len(rows) >= a.rows:
        print(f"\n({ws.max_row - a.rows} more rows; use --rows)")


def convert(value):
    if value.startswith("="):
        return value
    for kind in (int, float):
        try:
            return kind(value)
        except ValueError:
            pass
    return value


def cmd_create(a):
    wb = openpyxl.Workbook()
    wb.remove(wb.active)
    for path in a.csvs:
        ws = wb.create_sheet(os.path.splitext(os.path.basename(path))[0][:31])
        with open(path, newline="", encoding="utf-8-sig") as f:
            for r, row in enumerate(csv.reader(f)):
                ws.append([cell if r == 0 else convert(cell) for cell in row])
        for cell in ws[1]:
            cell.font = Font(bold=True, color="FFFFFF")
            cell.fill = PatternFill("solid", fgColor="4F46E5")
            cell.alignment = Alignment(vertical="center")
        ws.freeze_panes = "A2"
        for col in ws.columns:
            width = max(len(str(c.value or "")) for c in col)
            ws.column_dimensions[get_column_letter(col[0].column)].width = min(max(10, width + 2), 60)
    wb.save(a.output)
    print(f"wrote {a.output} ({len(a.csvs)} sheet(s))")


def cmd_recalc(a):
    soffice = shutil.which("soffice") or shutil.which("libreoffice")
    if not soffice:
        sys.exit("LibreOffice (soffice) is needed to recalculate formulas")
    out = tempfile.mkdtemp()
    profile = "file://" + os.path.join(tempfile.gettempdir(), "shaman-libreoffice")  # isolated profile: works alongside a running office
    subprocess.run([soffice, f"-env:UserInstallation={profile}", "--headless", "--calc", "--convert-to", "xlsx", "--outdir", out,
                    os.path.abspath(a.input)],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=120)
    recalculated = os.path.join(out, os.path.basename(a.input))
    if not os.path.exists(recalculated):
        sys.exit("LibreOffice could not open the file (is the Calc component installed? e.g. apt install libreoffice-calc)")
    values = openpyxl.load_workbook(recalculated, data_only=True)
    problems = []
    for ws in values.worksheets:
        for row in ws.iter_rows():
            for c in row:
                if isinstance(c.value, str) and c.value in ERRORS:
                    problems.append(f"{ws.title}!{c.coordinate}: {c.value}")
    # Keep the original formulas and styles; LibreOffice has stored computed values alongside them.
    shutil.copy(recalculated, a.output or a.input)
    print(f"recalculated -> {a.output or a.input}")
    if problems:
        print(f"{len(problems)} formula error(s):")
        print("\n".join("  " + p for p in problems[:50]))
        sys.exit(1)
    print("no formula errors")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("inspect"); s.add_argument("input"); s.set_defaults(fn=cmd_inspect)
    s = sub.add_parser("read"); s.add_argument("input"); s.add_argument("--sheet"); s.add_argument("--csv", action="store_true")
    s.add_argument("--formulas", action="store_true"); s.add_argument("--rows", type=int, default=50); s.set_defaults(fn=cmd_read)
    s = sub.add_parser("create"); s.add_argument("output"); s.add_argument("csvs", nargs="+"); s.set_defaults(fn=cmd_create)
    s = sub.add_parser("recalc"); s.add_argument("input"); s.add_argument("--output"); s.set_defaults(fn=cmd_recalc)
    a = p.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
