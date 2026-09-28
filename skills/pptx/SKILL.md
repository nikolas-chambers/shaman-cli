---
name: pptx
description: Create, read and edit PowerPoint decks (.pptx) - extract slide text and notes, build decks from an outline, update existing slides, render previews. Use whenever a .pptx or a slide deck file is involved.
---
# PowerPoint (.pptx)

Helper: `scripts/pptx_tool.py` (needs `python-pptx`: `python3 -m pip install python-pptx`).

| Task | Command |
|---|---|
| Slide text, tables and speaker notes | `pptx_tool.py read deck.pptx` |
| Build a deck from a Markdown outline | `pptx_tool.py create out.pptx --from outline.md [--template brand.pptx]` |
| Find and replace across all slides | `pptx_tool.py replace in.pptx out.pptx "old" "new" [...]` |
| Render to PDF / PNG previews | `soffice --headless --convert-to pdf deck.pptx` then view pages |

## Outline format for `create`
```markdown
# Deck title
Subtitle line

## Slide title
- Bullet
  - Sub-bullet (two-space indent)
> Speaker notes for this slide

## Another slide
![](chart.png)
```
Each `##` starts a slide; the first `#` becomes the title slide. `>` lines become speaker notes; an image line adds a picture.

## Guidance
- One idea per slide, 3-6 bullets, short phrases. Put detail in speaker notes.
- Use `--template` with the organisation's deck so layouts, fonts and colours match; the tool uses its Title and "Title and Content" layouts.
- When editing an existing deck, read it first, keep its layouts, and change only the requested slides.
- Render to PDF and look at the result before delivering; check for overflowing text.
