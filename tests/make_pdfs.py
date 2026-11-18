# Builds Folio's synthetic test PDFs (reportlab and pypdf) into tests/gen/.
import os, io, math
from reportlab.pdfgen import canvas
from reportlab.lib.colors import CMYKColor, Color, toColor
from reportlab.lib.utils import ImageReader
from PIL import Image, ImageDraw
import pypdf
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'gen')
os.makedirs(OUT, exist_ok=True)

def graphics():
    c = canvas.Canvas(os.path.join(OUT, 'graphics.pdf'), pagesize=(595, 842))
    c.setFont('Helvetica-Bold', 20); c.drawString(40, 800, 'Graphics: joins, caps, dashes, clips')
    y = 740
    for j in range(3):
        c.setLineWidth(14); c.setLineJoin(j); c.setLineCap(j)
        p = c.beginPath(); p.moveTo(60 + j * 170, y - 40); p.lineTo(110 + j * 170, y + 20); p.lineTo(160 + j * 170, y - 40)
        c.drawPath(p, stroke=1, fill=0)
    c.setLineWidth(3)
    for k, d in enumerate([[6, 3], [12, 4, 2, 4], [1, 5]]):
        c.setDash(d, k); c.setStrokeColorRGB(0.2, 0.3, 0.8)
        c.line(60, 640 - k * 20, 540, 640 - k * 20)
    c.setDash([])
    # even-odd star and nonzero star
    for k, eo in enumerate([0, 1]):
        p = c.beginPath()
        cx, cy, r = 150 + k * 250, 480, 80
        for i in range(5):
            a = math.pi / 2 + i * 4 * math.pi / 5
            (p.moveTo if i == 0 else p.lineTo)(cx + r * math.cos(a), cy + r * math.sin(a))
        p.close()
        c.setFillColorRGB(0.9, 0.4, 0.1)
        c.drawPath(p, stroke=1, fill=1, fillMode=eo)
    # clip to a circle, draw stripes
    c.saveState()
    p = c.beginPath(); p.circle(300, 280, 90); c.clipPath(p, stroke=0)
    for i in range(20):
        c.setFillColorRGB(i / 20, 0.5, 1 - i / 20); c.rect(200 + i * 10, 180, 10, 200, stroke=0, fill=1)
    c.restoreState()
    # transparency and CMYK
    c.setFillColor(CMYKColor(0, 1, 1, 0)); c.rect(60, 80, 120, 80, fill=1, stroke=0)
    c.setFillColor(Color(0, 0, 1, alpha=0.5)); c.rect(120, 110, 120, 80, fill=1, stroke=0)
    c.setFillColor(CMYKColor(1, 0, 0, 0.2)); c.circle(420, 130, 50, fill=1, stroke=0)
    c.showPage()
    # shadings
    c.setFont('Helvetica', 18); c.drawString(40, 800, 'Shadings')
    c.saveState(); p = c.beginPath(); p.rect(50, 500, 495, 250); c.clipPath(p, stroke=0)
    c.linearGradient(50, 500, 545, 750, (toColor('red'), toColor('yellow'), toColor('blue')), extend=False)
    c.restoreState()
    c.saveState(); p = c.beginPath(); p.circle(300, 280, 150); c.clipPath(p, stroke=0)
    c.radialGradient(300, 280, 150, (toColor('white'), toColor('darkgreen')), extend=False)
    c.restoreState()
    c.showPage()
    c.save()

def fonts():
    c = canvas.Canvas(os.path.join(OUT, 'fonts.pdf'), pagesize=(595, 842))
    y = 800
    for f in ['Helvetica', 'Helvetica-Bold', 'Helvetica-Oblique', 'Times-Roman', 'Times-Bold', 'Times-Italic', 'Courier', 'Courier-BoldOblique']:
        c.setFont(f, 16); c.drawString(40, y, '%s: The quick brown fox jumps over the lazy dog 0123456789 \xe9\xe8\xfc\xdf' % f); y -= 30
    c.setFont('Symbol', 18); c.drawString(40, y, 'abgdepqlm'); y -= 30
    c.setFont('ZapfDingbats', 18); c.drawString(40, y, 'abcdefg'); y -= 40
    c.setFont('Helvetica', 14)
    c.saveState(); c.translate(300, 300); 
    for a in range(0, 360, 45):
        c.saveState(); c.rotate(a); c.drawString(20, 0, 'rotated %d' % a); c.restoreState()
    c.restoreState()
    t = c.beginText(40, 120); t.setFont('Times-Roman', 14); t.setCharSpace(2); t.textLine('Character spacing 2'); t.setCharSpace(0)
    t.setWordSpace(8); t.textLine('Word spacing is wide here'); t.setWordSpace(0); t.setHorizScale(150); t.textLine('Horizontal scale 150'); t.setHorizScale(100)
    t.setTextRenderMode(1); t.textLine('Outlined text (stroke)'); c.drawText(t)
    c.showPage(); c.save()

def images():
    c = canvas.Canvas(os.path.join(OUT, 'images.pdf'), pagesize=(595, 842))
    im = Image.new('RGBA', (120, 80), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    d.ellipse((10, 10, 110, 70), fill=(255, 0, 0, 200)); d.rectangle((50, 20, 70, 60), fill=(0, 0, 255, 255))
    c.drawImage(ImageReader(im), 40, 600, 240, 160, mask='auto')
    g = Image.linear_gradient('L').resize((64, 64)); c.drawImage(ImageReader(g), 320, 600, 200, 200)
    rgb = Image.new('RGB', (300, 200)); d = ImageDraw.Draw(rgb)
    for i in range(300): d.line((i, 0, i, 200), fill=(i * 255 // 300, 128, 255 - i * 255 // 300))
    b = io.BytesIO(); rgb.save(b, 'JPEG', quality=85); b.seek(0)
    c.drawImage(ImageReader(b), 40, 300, 300, 200)
    bw = Image.new('1', (40, 40)); d = ImageDraw.Draw(bw)
    for i in range(0, 40, 4): d.line((0, i, 40, i), fill=1)
    c.drawImage(ImageReader(bw.convert('L')), 380, 300, 160, 160)
    c.drawInlineImage(g.resize((16, 16)), 380, 100, 100, 100)
    c.showPage(); c.save()

def rotated_and_encrypted():
    w = pypdf.PdfWriter(); r = pypdf.PdfReader(os.path.join(OUT, 'fonts.pdf'))
    p = r.pages[0]; w.add_page(p); w.pages[0].rotate(90)
    w.add_metadata({'/Title': 'Rotated page'})
    with open(os.path.join(OUT, 'rotated.pdf'), 'wb') as f: w.write(f)
    for alg, name in [('RC4-40', 'enc_rc4_40'), ('RC4-128', 'enc_rc4_128'), ('AES-128', 'enc_aes128'), ('AES-256', 'enc_aes256'), ('AES-256-R5', 'enc_aes256r5')]:
        w = pypdf.PdfWriter(); w.append(os.path.join(OUT, 'fonts.pdf'))
        try:
            w.encrypt(user_password='', owner_password='owner', algorithm=alg)
        except Exception as e:
            print('skip', alg, e); continue
        with open(os.path.join(OUT, name + '.pdf'), 'wb') as f: w.write(f)
    w = pypdf.PdfWriter(); w.append(os.path.join(OUT, 'fonts.pdf'))
    w.encrypt(user_password='secret', owner_password='owner', algorithm='AES-128')
    with open(os.path.join(OUT, 'enc_password.pdf'), 'wb') as f: w.write(f)
    # an outline with links, via pypdf
    w = pypdf.PdfWriter(); w.append(os.path.join(OUT, 'graphics.pdf'))
    a = w.add_outline_item('Graphics', 0); w.add_outline_item('Shadings', 1, parent=a)
    with open(os.path.join(OUT, 'outline.pdf'), 'wb') as f: w.write(f)

def forms():
    # an AcroForm: text fields (one of several lines), a check box, radio buttons, a drop-down, a list box,
    # and on page 2 blanks drawn without fields (underscores and boxes) for Detect Form Fields
    c = canvas.Canvas(os.path.join(OUT, 'forms.pdf'), pagesize=(595, 842))
    f = c.acroForm
    c.setFont('Helvetica', 12)
    c.drawString(50, 780, 'Name:'); f.textfield(name='name', x=120, y=770, width=250, height=22, value='', borderColor=toColor('black'), fillColor=toColor('white'))
    c.drawString(50, 740, 'Notes:'); f.textfield(name='notes', x=120, y=680, width=250, height=70, fieldFlags='multiline', value='')
    c.drawString(50, 650, 'Agree:'); f.checkbox(name='agree', x=120, y=645, size=16, checked=False, buttonStyle='check')
    c.drawString(50, 610, 'Colour:')
    for k, v in enumerate(['red', 'green', 'blue']):
        f.radio(name='colour', value=v, x=120 + k * 60, y=605, size=16, selected=False); c.drawString(140 + k * 60, 608, v)
    c.drawString(50, 570, 'Country:'); f.choice(name='country', x=120, y=560, width=150, height=20, options=['France', 'Germany', 'Italy'], value='France')
    c.drawString(50, 530, 'Fruit:'); f.listbox(name='fruit', x=120, y=460, width=150, height=70, options=['Apple', 'Banana', 'Cherry'], value='Apple')
    c.showPage()
    c.setFont('Helvetica', 12)
    c.drawString(50, 780, 'Surname: ____________________')
    c.drawString(50, 750, '[ ] Tick here')
    c.showPage()
    c.save()

graphics(); fonts(); images(); rotated_and_encrypted(); forms()
# object streams + xref stream (pypdf compress)
w = pypdf.PdfWriter(); w.append(os.path.join(OUT, 'graphics.pdf')); w.compress_identical_objects()
for p in w.pages: p.compress_content_streams()
with open(os.path.join(OUT, 'compressed.pdf'), 'wb') as f: w.write(f)
print('ok', sorted(os.listdir(OUT)))
