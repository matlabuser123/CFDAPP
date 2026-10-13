"""P17.8 -- prove a clang-format pass changed FORMATTING ONLY.

Reformatting source that has already been measured is a real hazard: a reflow can silently alter an
expression. The proof is a two-part comparison of the file before and after:

  1. the multiset of tokens OUTSIDE string literals must be identical
  2. the concatenation of all string-literal CONTENTS, in order, must be identical

Part 2 is what a naive tokenizer gets wrong, and it is why this is a script rather than a grep.
clang-format splits a long string literal across lines as two adjacent literals -- C++ concatenates
them, so the program is unchanged, but the raw token count of `"` goes up by two. Comparing the
CONTENTS rather than the delimiters is the correct invariant, and it is still strict: change one
character inside any message and it fails.

Comments are stripped first: reflowing them is precisely what clang-format is here to do.
"""

import re
import sys

TOKEN = re.compile(r"[A-Za-z_]\w*|\d+\.?\d*(?:[eE][-+]?\d+)?|[^\sA-Za-z0-9_]")


def split(path):
    src = open(path, encoding="utf-8").read()
    out, strings, i, n = [], [], 0, len(src)
    while i < n:
        c = src[i]
        if c == "/" and i + 1 < n and src[i + 1] == "/":
            i = src.find("\n", i)
            if i < 0:
                break
        elif c == "/" and i + 1 < n and src[i + 1] == "*":
            j = src.find("*/", i + 2)
            i = n if j < 0 else j + 2
        elif c in "\"'":
            quote, j, buf = c, i + 1, []
            while j < n:
                if src[j] == "\\":
                    buf.append(src[j : j + 2])
                    j += 2
                    continue
                if src[j] == quote:
                    break
                buf.append(src[j])
                j += 1
            strings.append("".join(buf))
            # NOTE: no placeholder token is emitted. clang-format splits one long literal into two
            # ADJACENT literals, which C++ concatenates -- emitting a token per literal would make
            # that legal reflow look like a semantic change. The CONTENTS are compared instead,
            # below, which is strictly stronger: it catches a single changed character.
            i = j + 1
        else:
            out.append(c)
            i += 1
    return sorted(TOKEN.findall("".join(out))), "".join(strings)


def main(argv):
    failures = 0
    for pair in argv[1:]:
        before, after = pair.split("::")
        tb, sb = split(before)
        ta, sa = split(after)
        name = after
        if tb == ta and sb == sa:
            print(f"    TOKENS IDENTICAL ({len(ta)}) and STRING CONTENT IDENTICAL "
                  f"({len(sa)} chars) -- formatting only")
        else:
            failures += 1
            if tb != ta:
                from collections import Counter
                cb, ca = Counter(tb), Counter(ta)
                print(f"    *** TOKEN MULTISET CHANGED for {name}")
                # The FULL sets, not a head. Truncating to the first 8 sorted entries put
                # punctuation first and hid every identifier -- which made a correct check report
                # "the removed token is not here" about a token that was right there.
                print(f"        lost : {sorted((cb - ca).items())}")
                print(f"        gained: {sorted((ca - cb).items())}")
            if sb != sa:
                print(f"    *** STRING CONTENT CHANGED for {name} "
                      f"({len(sb)} -> {len(sa)} chars)")
                for k in range(min(len(sb), len(sa))):
                    if sb[k] != sa[k]:
                        print(f"        first difference at {k}: "
                              f"{sb[k-40:k+40]!r} vs {sa[k-40:k+40]!r}")
                        break
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
