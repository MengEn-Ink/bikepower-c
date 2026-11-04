#!/usr/bin/env python3
"""校验仓库 Markdown 的本地相对链接与标题锚点，不请求外部网络。"""
import argparse
import html
from pathlib import Path
import re
import unicodedata
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[2]


def without_code(text):
    lines = []
    fence = None
    for line in text.splitlines():
        marker = re.match(r"^\s{0,3}(`{3,}|~{3,})", line)
        if marker:
            char = marker.group(1)[0]
            if fence is None:
                fence = (char, len(marker.group(1)))
            elif fence[0] == char and len(marker.group(1)) >= fence[1]:
                fence = None
            lines.append("")
        else:
            lines.append(line if fence is None else "")
    return "\n".join(lines)


def anchors(text):
    text = without_code(text)
    found = set(re.findall(r'\b(?:id|name)\s*=\s*["\']([^"\']+)["\']', text))
    counts = {}
    lines = text.splitlines()
    for index, line in enumerate(lines):
        match = re.match(r"^\s{0,3}#{1,6}\s+(.+?)(?:\s+#+\s*)?$", line)
        if match:
            title = match.group(1)
        elif index and re.fullmatch(r"\s{0,3}(?:=+|-+)\s*", line) and lines[index - 1].strip():
            title = lines[index - 1].strip()
        else:
            continue
        title = re.sub(r"<[^>]*>", "", title)
        title = re.sub(r"!?\[([^\]]*)\]\([^)]*\)", r"\1", title)
        title = html.unescape(title).lower()
        slug = "".join(ch for ch in title if ch in "_- " or
                       not unicodedata.category(ch).startswith(("P", "S", "C")))
        slug = slug.replace(" ", "-")
        candidate = slug
        suffix = counts.get(slug, 0)
        while candidate in found:
            suffix += 1
            candidate = f"{slug}-{suffix}"
        counts[slug] = suffix
        found.add(candidate)
    return found


def check_document(doc, root):
    text = without_code(doc.read_text(encoding="utf-8"))
    text = re.sub(r"(`+).*?\1", "", text)
    targets = re.findall(r'!?\[[^\]]*\]\(\s*(<[^>]+>|[^\s)]+)(?:\s+["\'][^)]*)?\s*\)', text)
    targets += re.findall(r"^\s{0,3}\[[^\]]+\]:\s*(<[^>]+>|[^\s]+)", text, re.M)
    errors = []
    for target in targets:
        target = html.unescape(target.strip("<>"))
        parsed = urlsplit(target)
        if parsed.scheme or parsed.netloc:
            continue
        path = unquote(parsed.path)
        dest = (root / path.lstrip("/") if path.startswith("/") else doc.parent / path).resolve() if path else doc
        if not dest.exists():
            errors.append(f"{doc.relative_to(root)}: 链接不存在 {target}")
        elif parsed.fragment and dest.suffix.lower() == ".md":
            if unquote(parsed.fragment) not in anchors(dest.read_text(encoding="utf-8")):
                errors.append(f"{doc.relative_to(root)}: 锚点不存在 {target}")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="*", type=Path)
    args = parser.parse_args()
    paths = args.paths or sorted((ROOT / "docs").rglob("*.md")) + [
        ROOT / "README.md", ROOT / "AGENTS.md", ROOT / "PROGRESS.md",
    ]
    errors = [error for path in paths for error in check_document(path.resolve(), ROOT)]
    for error in errors:
        print(f"[markdown-links] FAIL {error}")
    print(f"[markdown-links] {'FAIL' if errors else 'PASS'} {len(paths)} 文档，{len(errors)} 错误")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
