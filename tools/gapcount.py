#!/usr/bin/env python3
# recount docs/pdfcraft-gap.md: the marks under each "### Group (n: ...)" heading, printed; with --write
# the headings and the summary tables' rows are brought up to date (the summary's sentences by hand)
import re, sys
p = sys.argv[1]
s = open(p, encoding='utf-8').read()
lines = s.split('\n')
sec = None; counts = {}; order = []
for ln in lines:
    m = re.match(r'^### (.+?) \((\d+): ', ln)
    if m: sec = m.group(1); counts[sec] = [0, 0, 0, 0]; order.append(sec); continue
    if ln.startswith('## '): sec = None
    if sec and ln.startswith('- ') and len(ln) > 2:
        k = '✅🟨⬜➖'.find(ln[2])
        if k >= 0: counts[sec][k] += 1
for n in order: print(n, counts[n])
if len(sys.argv) > 2 and sys.argv[2] == '--write':
    out = []; T = [0, 0, 0, 0]; total_done = False
    for ln in lines:
        m = re.match(r'^### (.+?) \((\d+): ✅ \d+ · 🟨 \d+ · ⬜ \d+ · ➖ \d+\)$', ln)
        if m and m.group(1) in counts:
            c = counts[m.group(1)]; ln = '### %s (%d: ✅ %d · 🟨 %d · ⬜ %d · ➖ %d)' % (m.group(1), sum(c), *c)
        m = re.match(r'^\| (.+?) \| (\d+) \| (\d+) \| (\d+) \| (\d+) \| (\d+) \|$', ln)
        if m and m.group(1) in counts:
            c = counts[m.group(1)]; ln = '| %s | %d | %d | %d | %d | %d |' % (m.group(1), sum(c), *c)
            if not total_done: T = [T[i] + c[i] for i in range(4)]
        if ln.startswith('| **All** |') and not total_done:
            ln = '| **All** | **%d** | **%d** | **%d** | **%d** | **%d** |' % (sum(T), *T); total_done = True
        out.append(ln)
    open(p, 'w', encoding='utf-8').write('\n'.join(out))
    print('written', T)
