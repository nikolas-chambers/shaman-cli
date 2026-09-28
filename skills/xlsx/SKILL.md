---
name: xlsx
description: Read, create and edit spreadsheets (.xlsx, .xlsm, .csv) - inspect sheets, export to CSV, build formatted workbooks with live formulas, recalculate and check for formula errors. Use whenever a spreadsheet is an input or output.
---
# Spreadsheets (.xlsx / .csv)

Helper: `scripts/xlsx_tool.py` (needs `openpyxl`; `pandas` optional for analysis).

| Task | Command |
|---|---|
| Overview: sheets, sizes, headers, formula counts | `xlsx_tool.py inspect book.xlsx` |
| A sheet as Markdown or CSV (values or formulas) | `xlsx_tool.py read book.xlsx [--sheet Name] [--csv] [--formulas] [--rows 50]` |
| Build a formatted workbook from CSV files | `xlsx_tool.py create out.xlsx data.csv [more.csv ...]` |
| Recalculate with LibreOffice Calc and report errors | `xlsx_tool.py recalc book.xlsx [--output out.xlsx]` |

## Rules for good spreadsheets
- **Keep formulas live.** Write `=SUM(B2:B13)`, not a number you computed in Python, so the sheet stays correct when inputs change. Put assumptions in labelled input cells and reference them.
- After writing formulas, run `recalc`: openpyxl stores formulas without values, and `recalc` both computes them and lists any `#REF!`, `#DIV/0!`, `#NAME?`, `#VALUE!` or `#N/A`. Fix every error before delivering.
- Preserve existing workbooks: open with `openpyxl.load_workbook(path)` (not `data_only=True`, which drops formulas) and change only what was asked; keep styles, widths, named ranges and other sheets.
- Formatting: bold, frozen header row; sensible column widths; number formats for currency (`#,##0.00`), percentages (`0.0%`) and dates; no merged cells in data tables.
- For analysis, load with pandas (`pd.read_excel(path, sheet_name=None)`), but write results back with formulas when the user will keep working in the file.
- CSV: detect delimiter and encoding; keep leading zeros (read as text).
