#!/usr/bin/env python3
"""FORMAT-001 proof, refined: for each reformatted file, the C++ code with comments removed and
all whitespace removed must be identical before and after -- except that #include directives are
compared as a SET (clang-format's IncludeBlocks/SortIncludes may reorder them), with the rest of
the code compared in order. Comment TEXT is additionally compared word by word (reflow keeps the
words and their order; only the '//' markers move)."""
import os
import re
import sys


def split_code_comments(src):
    code, comments, i, n = [], [], 0, len(src)
    while i < n:
        c = src[i]
        if src.startswith("//", i):
            j = src.find("\n", i)
            j = n if j < 0 else j
            comments.append(src[i + 2:j])
            code.append("\n")
            i = j
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            comments.append(src[i + 2:j - 2])
            code.append(" ")
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and src[j] != c:
                j += 2 if src[j] == "\\" else 1
            code.append(src[i:j + 1])
            i = j + 1
        else:
            code.append(c)
            i += 1
    return "".join(code), comments


def analyse(text):
    code, comments = split_code_comments(text)
    includes = sorted(re.sub(r"\s", "", l) for l in code.splitlines() if l.strip().startswith("#include"))
    body = re.sub(r"\s", "", "\n".join(l for l in code.splitlines() if not l.strip().startswith("#include")))
    words = " ".join(" ".join(comments).split())
    return includes, body, words


def main():
    before_root, after_root = sys.argv[1], sys.argv[2]
    bad = 0
    for rel in sys.argv[3:]:
        b = analyse(open(os.path.join(before_root, rel), encoding="utf-8").read())
        a = analyse(open(os.path.join(after_root, rel), encoding="utf-8").read())
        verdict = []
        verdict.append("code SAME" if b[1] == a[1] else "code DIFFERENT")
        verdict.append("includes SAME-SET" if b[0] == a[0] else "includes DIFFERENT")
        verdict.append("comment words SAME" if b[2] == a[2] else "comment words DIFFERENT")
        if b != a:
            bad += 1
        print(f"{rel}: {', '.join(verdict)}")
    print(f"files with any difference: {bad}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
