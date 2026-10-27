"""Remove @addMethod / @addField members from redscript sources, by class and member name.

    py -3.13 -B tools/patchday/reds_strip.py <file.reds> <Class>.<member> [...]

A patch can make a framework's script additions native in the game (2.5 shipped much of Codeware's), and then the
compiler rejects them as duplicates (DUP_FN_ANN). Each named member is removed with its annotation, its body (a
{ ... } block or a one-line native declaration) and the blank lines after it; a file left without code is deleted.
"""
import os
import re
import sys


def strip(text, cls, member):
    ann = re.compile(r'^@add(?:Method|Field)\(\s*' + re.escape(cls) + r'\s*\)\s*\n', re.M)
    out, pos, removed = [], 0, 0
    for m in ann.finditer(text):
        if m.start() < pos:
            continue
        decl_start = m.end()
        line_end = text.find('\n', decl_start)
        line_end = len(text) if line_end < 0 else line_end
        decl = text[decl_start:line_end]
        if not re.search(r'\b(?:func|let)\s+' + re.escape(member) + r'\b', decl):
            continue
        end = line_end
        if '{' in decl or (re.search(r'\bfunc\b', decl) and not decl.rstrip().endswith(';')
                           and not re.search(r'\bnative\b', decl)):
            # a body: walk braces from the first '{' (it may open on the next line)
            brace = text.index('{', decl_start)
            depth, i = 0, brace
            while True:
                c = text[i]
                if c == '{':
                    depth += 1
                elif c == '}':
                    depth -= 1
                    if depth == 0:
                        break
                i += 1
            end = text.find('\n', i)
            end = len(text) if end < 0 else end
        while end < len(text) and text[end] == '\n':
            end += 1
        out.append(text[pos:m.start()])
        pos = end
        removed += 1
    out.append(text[pos:])
    return ''.join(out), removed


def main():
    path, members = sys.argv[1], sys.argv[2:]
    text = open(path, encoding='utf-8').read()
    for spec in members:
        cls, member = spec.rsplit('.', 1)
        text, n = strip(text, cls, member)
        print(f'{os.path.basename(path)}: {spec} removed {n}')
        if n == 0:
            sys.exit(f'{spec} not found in {path}')
    code = re.sub(r'//.*', '', text).strip()
    if not code:
        os.remove(path)
        print(f'{path}: no code left, deleted')
    else:
        open(path, 'w', encoding='utf-8').write(text)


if __name__ == '__main__':
    main()
