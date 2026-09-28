---
name: accessibility
description: Audit and fix web accessibility against WCAG 2.2 AA - semantics, keyboard, focus, labels, contrast, ARIA, screen readers, motion. Use for a11y reviews and making UI usable for everyone.
---
# Accessibility (WCAG 2.2 AA)

## Audit checklist
1. **Semantics**: headings in order (one `h1`), landmarks (`header`, `nav`, `main`, `footer`), lists as lists, buttons for actions, links for navigation.
2. **Keyboard**: every interactive element reachable with Tab in a logical order, operable with Enter/Space/arrows, no traps; visible focus indicator (≥ 3:1 contrast); skip link to main content.
3. **Names**: every input has a `<label>`; icon-only buttons have `aria-label`; images have meaningful `alt` (or `alt=""` if decorative).
4. **Contrast**: text 4.5:1 (large text 3:1); UI component boundaries and focus rings 3:1; don't convey meaning by colour alone.
5. **Forms**: errors identified in text, linked with `aria-describedby`, announced; required fields marked; autocomplete attributes.
6. **Dynamic content**: `aria-live` for async updates; dialogs trap focus, restore it on close, close with Esc (`<dialog>` or proper ARIA).
7. **Motion and media**: respect `prefers-reduced-motion`; captions for video; no content flashing > 3 times/second.
8. **Zoom and reflow**: usable at 200% zoom and 320px width without horizontal scrolling; target size ≥ 24×24px.

## ARIA
First rule: use native HTML instead. ARIA only fills gaps; wrong ARIA is worse than none. Keep roles, states (`aria-expanded`, `aria-selected`) and focus in sync.

## Verify
Keyboard-only walkthrough; automated scan (axe via Playwright: `@axe-core/playwright`) — it catches ~40%, so also test with a screen reader (VoiceOver/NVDA) on key flows. Report issues with the WCAG criterion, element, impact and fix.
