---
name: webapp-testing
description: Test and debug web apps in a real browser with Playwright - start the dev server, drive the UI, assert behaviour, capture screenshots and console errors. Use when verifying frontend changes or reproducing UI bugs.
---
# Web app testing

## Run the app
Use `scripts/with_server.py` to start one or more servers, wait until their ports answer, run a command, and shut everything down:
```sh
python3 scripts/with_server.py --server "npm run dev" --port 5173 -- python3 check.py
python3 scripts/with_server.py --server "cd api && uvicorn main:app" --port 8000 --server "npm run dev" --port 5173 -- npx playwright test
```

## Write the check
- Prefer the project's existing Playwright/Cypress setup. Otherwise write a small script (Python `playwright` or Node `@playwright/test`).
- Wait on conditions (`page.wait_for_selector`, `expect(locator).to_be_visible()`), never fixed sleeps.
- Select elements by role/label/text (`get_by_role("button", name="Save")`) rather than brittle CSS paths.
- Always collect console errors and failed requests; a page that renders with errors is not passing.
- Take screenshots at key states and look at them. Test a narrow (390px) and a wide viewport.

```python
from playwright.sync_api import sync_playwright
with sync_playwright() as p:
    page = p.chromium.launch().new_page()
    errors = []
    page.on("console", lambda m: m.type == "error" and errors.append(m.text))
    page.goto("http://localhost:5173")
    page.get_by_role("button", name="Sign in").click()
    page.wait_for_selector("text=Welcome")
    page.screenshot(path="after-login.png", full_page=True)
    assert not errors, errors
```

## Debug failures
Re-run headed or with tracing (`--trace on`), read the console and network log, inspect the DOM with `page.content()`, and reduce to the smallest failing step before changing code.
