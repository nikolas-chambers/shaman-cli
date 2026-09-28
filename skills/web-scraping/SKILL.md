---
name: web-scraping
description: Extract data from websites responsibly - find APIs first, parse HTML robustly, handle pagination and JavaScript-rendered pages, respect robots.txt and rate limits. Use when asked to collect or monitor data from web pages.
---
# Web scraping

## Before scraping
- Check for an official API, RSS feed, sitemap or data export first. Check the site's terms and `robots.txt`; don't scrape where it's disallowed or behind logins without permission.
- Open the page's network requests (or read its JavaScript) to find the JSON endpoints the page itself uses; those are cleaner than HTML.

## Fetch
- Quick look: the `webfetch` tool, or `scripts/extract.py URL` to list links, headings, tables and meta data.
- Identify yourself with a User-Agent, rate-limit (≥ 1 s between requests), cache responses, back off on 429/5xx, and stop on repeated errors.
- JavaScript-rendered pages: use Playwright (see webapp-testing) and wait for the content selector.

## Parse
- Use a real HTML parser (BeautifulSoup/lxml, or Python's html.parser); select by stable attributes (ids, `data-*`, semantic tags), not deep positional paths.
- Normalise whitespace, units, dates and currencies; keep the source URL and fetch time with each record.
- Handle pagination explicitly (next links or page params) with a hard page limit.

## Output
Write CSV/JSON with a stable schema, validate row counts against the page, and report what was skipped and why.
