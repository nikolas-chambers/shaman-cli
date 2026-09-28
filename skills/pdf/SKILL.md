---
name: pdf
description: Read, extract text and tables from, create, merge, split, rotate, stamp and fill forms in PDF files. Use whenever a .pdf is an input or the requested output.
---
# PDF

Helper: `scripts/pdf_tool.py` (Python 3; needs `pypdf`, plus `pdfplumber` for text/tables and `reportlab` for creating PDFs).
If a module is missing, install it: `python3 -m pip install pypdf pdfplumber reportlab` (use a venv if the system Python is managed).

| Task | Command |
|---|---|
| Page count, metadata, encryption | `pdf_tool.py info in.pdf` |
| Text, optional page range (`--layout` keeps columns) | `pdf_tool.py text in.pdf [--pages 1-3] [--layout]` |
| Tables to CSV (one file per table) | `pdf_tool.py tables in.pdf outdir/` |
| Merge | `pdf_tool.py merge out.pdf a.pdf b.pdf ...` |
| Split into single pages / extract a range | `pdf_tool.py split in.pdf outdir/` · `pdf_tool.py extract in.pdf out.pdf --pages 2-5` |
| Rotate pages | `pdf_tool.py rotate in.pdf out.pdf --degrees 90 [--pages 1,3]` |
| Stamp text on every page (watermark, "DRAFT") | `pdf_tool.py stamp in.pdf out.pdf --text DRAFT` |
| List form fields | `pdf_tool.py fields form.pdf` |
| Fill a form | `pdf_tool.py fill form.pdf values.json out.pdf` (JSON: `{"field name": "value", "checkbox": true}`) |
| Create from Markdown-ish text | `pdf_tool.py create out.pdf --from notes.md [--title "Report"]` |

## Workflow
1. Start with `info`, then `text`. For scanned PDFs (text comes back empty), say so; OCR needs `ocrmypdf` or `tesseract` if installed.
2. For forms, always run `fields` first and use the exact field names it prints. Checkboxes take `true`/`false`.
3. When creating documents, write the content to a `.md` file first (headings with `#`, bullets with `-`, blank lines between paragraphs), then `create`. For complex layouts write a reportlab script instead.
4. After producing a PDF, verify it: `info` for the page count and `text` on the first page.
5. Never overwrite the input; write to a new file and tell the user its path.
