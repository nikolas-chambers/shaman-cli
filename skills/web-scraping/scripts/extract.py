#!/usr/bin/env python3
"""Fetch a page (or read an HTML file) and list its title, meta, headings, links and tables. Standard library only.

  extract.py URL_OR_FILE [--links] [--tables] [--text]
"""
import html.parser
import json
import sys
import urllib.parse
import urllib.request


class Page(html.parser.HTMLParser):
    def __init__(self, base):
        super().__init__(convert_charrefs=True)
        self.base, self.title, self.meta, self.headings, self.links, self.tables, self.text = base, "", {}, [], [], [], []
        self._tag, self._href, self._buf, self._table, self._row, self._cell, self._skip = None, None, [], None, None, None, 0

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if tag in ("script", "style", "noscript"):
            self._skip += 1
        elif tag == "meta" and (a.get("name") or a.get("property")) and a.get("content"):
            self.meta[a.get("name") or a.get("property")] = a["content"]
        elif tag == "a" and a.get("href"):
            self._href, self._buf = urllib.parse.urljoin(self.base, a["href"]), []
        elif tag == "table":
            self._table = []
        elif tag == "tr" and self._table is not None:
            self._row = []
        elif tag in ("td", "th") and self._row is not None:
            self._cell = []
        self._tag = tag

    def handle_endtag(self, tag):
        if tag in ("script", "style", "noscript"):
            self._skip = max(0, self._skip - 1)
        elif tag == "a" and self._href:
            self.links.append((" ".join("".join(self._buf).split()), self._href))
            self._href = None
        elif tag in ("td", "th") and self._cell is not None:
            self._row.append(" ".join("".join(self._cell).split()))
            self._cell = None
        elif tag == "tr" and self._row is not None:
            if self._row:
                self._table.append(self._row)
            self._row = None
        elif tag == "table" and self._table is not None:
            if self._table:
                self.tables.append(self._table)
            self._table = None

    def handle_data(self, data):
        if self._skip:
            return
        if self._tag == "title" and not self.title:
            self.title = data.strip()
        if self._tag in ("h1", "h2", "h3") and data.strip():
            self.headings.append((self._tag, data.strip()))
        if self._href is not None:
            self._buf.append(data)
        if self._cell is not None:
            self._cell.append(data)
        if data.strip():
            self.text.append(data.strip())


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    src, flags = sys.argv[1], set(sys.argv[2:])
    if src.startswith(("http://", "https://")):
        req = urllib.request.Request(src, headers={"User-Agent": "shaman-cli web-scraping skill"})
        with urllib.request.urlopen(req, timeout=30) as r:
            body = r.read().decode(r.headers.get_content_charset() or "utf-8", "replace")
    else:
        body = open(src, encoding="utf-8", errors="replace").read()
    p = Page(src)
    p.feed(body)
    print(f"title: {p.title}")
    for k in ("description", "og:title", "og:description"):
        if k in p.meta:
            print(f"{k}: {p.meta[k]}")
    print("headings:")
    for tag, h in p.headings[:40]:
        print(f"  {tag}: {h}")
    print(f"links: {len(p.links)}  tables: {len(p.tables)}")
    if "--links" in flags:
        for text, href in p.links[:200]:
            print(f"  {text[:60]!r} -> {href}")
    if "--tables" in flags:
        for i, t in enumerate(p.tables):
            print(f"table {i + 1}:")
            print(json.dumps(t[:50], ensure_ascii=False))
    if "--text" in flags:
        print("\n".join(p.text)[:20000])


if __name__ == "__main__":
    main()
