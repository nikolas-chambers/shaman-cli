---
name: frontend-ui
description: Build or polish user interfaces that look deliberate and work for everyone - layout, typography, colour, states, responsiveness, accessibility. Use for UI features, components and visual fixes.
---
# Frontend UI

## Start from the existing system
Reuse the project's components, tokens (colours, spacing, radii, fonts) and patterns. New values need a reason.

## Layout and type
- A clear hierarchy: one primary action per view; headings that scan; generous, consistent spacing on a scale (4/8px).
- Readable text: 15-18px body, 1.4-1.6 line height, ~60-80 characters per line, sufficient contrast (WCAG AA: 4.5:1 body text).
- Align to a grid; avoid near-misses in alignment and size.

## Every state
Design and implement loading, empty, error, success, disabled, long content, and very small/large screens. Skeletons over spinners for content; errors that say what to do next.

## Responsive
Mobile first; no horizontal scroll; touch targets ≥ 44px; test at 360px, tablet and wide desktop.

## Accessibility
Semantic HTML first; labels for every input; visible focus styles; full keyboard operation; `alt` text; announce async changes (`aria-live`); respect `prefers-reduced-motion` and `prefers-color-scheme`.

## Performance
Avoid layout shift (reserve media space), lazy-load below the fold, keep bundles lean, debounce expensive handlers.

## Verify
Run it and look: check it in light and dark mode, keyboard-only, narrow and wide viewports. Screenshot before/after when changing visuals.
