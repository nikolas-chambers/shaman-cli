---
name: docx
description: Create, read and edit Word documents (.docx) - extract text and tables, generate documents from Markdown, find-and-replace while keeping formatting, add tables, convert to PDF. Use whenever a .docx is involved.
---
# Word documents (.docx)

Helper: `scripts/docx_tool.py` (needs `python-docx`: `python3 -m pip install python-docx`).

| Task | Command |
|---|---|
| Read as Markdown (headings, lists, tables) | `docx_tool.py read in.docx` |
| Create from Markdown | `docx_tool.py create out.docx --from draft.md [--template base.docx]` |
| Find and replace, keeping formatting | `docx_tool.py replace in.docx out.docx "old" "new" [more pairs...]` |
| Append a table from CSV | `docx_tool.py add-table in.docx out.docx data.csv [--style "Light Grid Accent 1"]` |
| Convert to PDF (LibreOffice) | `soffice --headless --convert-to pdf --outdir outdir in.docx` |

## Guidance
- Read the document first and mirror its structure and styles when editing.
- For new documents, draft the content in Markdown (`#` headings, `-` bullets, `1.` numbered lists, `**bold**`, `*italic*`, `|` tables), then `create`. Pass `--template` to inherit an organisation's fonts, margins and heading styles.
- `replace` works run-by-run so formatting survives; if a phrase is split across differently formatted runs it rewrites the paragraph's first run instead and reports it.
- For advanced edits (tracked changes, comments, fields, headers/footers) write a short python-docx script, or edit `word/document.xml` inside the zip and keep the XML valid.
- Check the result with `read` (and a PDF conversion when layout matters). Never overwrite the source file.
