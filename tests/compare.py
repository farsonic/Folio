# compare.py: every page (up to MAXP) of the test PDFs drawn by Folio and by poppler; mean difference per page
import subprocess, sys, os, glob
import numpy as np
from PIL import Image
MAXP = 4
R = sys.argv[1] if len(sys.argv) > 1 else '/tmp/pdfrender'
files = sorted(glob.glob('tests/pdfs/*.pdf') + glob.glob('tests/gen/*.pdf'))
os.makedirs('/tmp/cmp', exist_ok=True)
worst = []
for f in files:
    name = os.path.basename(f)[:-4]
    try: n = int(subprocess.run(['pdfinfo', f], capture_output=True, text=True, env=dict(os.environ)).stdout.split('Pages:')[1].split()[0])
    except Exception: n = 1
    for p in range(min(n, MAXP)):
        env = dict(os.environ, PW='secret')
        r = subprocess.run([R, f, str(p), '72', '/tmp/cmp/o.ppm'], capture_output=True, text=True, env=env, timeout=120)
        if r.returncode: print(name, p, 'FAIL', r.stderr.strip()[-300:]); continue
        subprocess.run(['pdftoppm', '-r', '72', '-f', str(p + 1), '-l', str(p + 1), '-singlefile', '-png', '-upw', 'secret', f, '/tmp/cmp/ref'], capture_output=True)
        a = Image.open('/tmp/cmp/o.ppm').convert('RGB'); b = Image.open('/tmp/cmp/ref.png').convert('RGB')
        if a.size != b.size: print(name, p, 'size', a.size, b.size); b = b.resize(a.size)
        d = np.abs(np.asarray(a, dtype=int) - np.asarray(b, dtype=int)).mean()
        flag = '  <<<' if d > 3 else ''
        print('%-22s p%d diff %5.2f%s' % (name, p, d, flag))
        if r.stderr and 'ERROR' in r.stderr: print(r.stderr[-300:])
        c = Image.new('RGB', (a.width * 2 + 10, a.height), (255, 0, 0)); c.paste(a, (0, 0)); c.paste(b, (a.width + 10, 0))
        c.save('/tmp/cmp/%s_%d.png' % (name, p))
