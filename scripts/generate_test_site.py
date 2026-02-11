#!/usr/bin/env python3
"""Generates a synthetic markdown site for load-testing the SSG.

Usage: python3 generate_test_site.py [num_pages] [content_dir]

Each page gets a realistic mix of headers, bold/italic, inline code,
links, lists, blockquotes, and (every 20th page) a fenced code block.
Pages are spread across nested folders (content/blog/vol{N}/postX.md)
to also exercise recursive directory traversal at depth.
"""
import os
import random
import sys

def make_page(i: int) -> str:
    words = ["forge", "cache", "hash", "render", "graph", "node", "build",
             "static", "markdown", "template", "content", "pipeline"]
    para = " ".join(random.choice(words) for _ in range(12))
    lines = [f"# Post {i}", ""]
    lines.append(f"This is **paragraph one** of post {i}, with *italic text* and {para}.")
    lines.append("")
    lines.append(f"Here's some `inline code` and a [link](https://example.com/{i}).")
    lines.append("")
    lines.append("- First point")
    lines.append("- Second point with **emphasis**")
    lines.append("- Third point")
    lines.append("")
    lines.append("> A blockquote for good measure.")
    lines.append("")
    if i % 20 == 0:
        lines.append("```")
        lines.append(f"function post{i}() {{ return {i}; }}")
        lines.append("```")
        lines.append("")
    lines.append(f"Closing paragraph for post {i}, wrapping things up.")
    return "\n".join(lines) + "\n"

def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 10000
    content_dir = sys.argv[2] if len(sys.argv) > 2 else "content"
    per_folder = 200

    os.makedirs(content_dir, exist_ok=True)
    for i in range(1, n + 1):
        vol = (i - 1) // per_folder
        folder = os.path.join(content_dir, "blog", f"vol{vol}")
        os.makedirs(folder, exist_ok=True)
        path = os.path.join(folder, f"post{i}.md")
        with open(path, "w") as f:
            f.write(make_page(i))

    print(f"Generated {n} pages under {content_dir}/blog/ ({(n + per_folder - 1) // per_folder} folders)")

if __name__ == "__main__":
    main()
