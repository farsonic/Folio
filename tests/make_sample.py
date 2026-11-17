# make_sample.py - writes Welcome.pdf, Folio's example document: a short guide in a few pages
# with bookmarks, a link, text in several fonts, graphics and a table (reportlab)
import sys
from reportlab.lib.pagesizes import A4
from reportlab.lib import colors
from reportlab.pdfgen import canvas
out = sys.argv[1] if len(sys.argv) > 1 else 'Welcome.pdf'
c = canvas.Canvas(out, pagesize=A4)
W, H = A4
c.setTitle('Welcome to Folio'); c.setAuthor('Folio'); c.setSubject('An example document'); c.setKeywords('Folio, Amiga, PDF')
def heading(text, y, key):
    c.bookmarkPage(key); c.addOutlineEntry(text, key, level=0)
    c.setFont('Helvetica-Bold', 22); c.setFillColor(colors.HexColor('#2a5db0')); c.drawString(60, y, text); c.setFillColor(colors.black)
def para(lines, y, font='Times-Roman', size=12, lead=16):
    c.setFont(font, size)
    for l in lines: c.drawString(60, y, l); y -= lead
    return y
# page 1
heading('Welcome to Folio', H - 90, 'p1')
y = para(['Folio reads, comments on and organizes PDF documents on the Amiga.', '',
          'Try these with this document:',
          '  - Drag across this line with Highlight to highlight it.',
          '  - Click with Sticky Note to leave a comment, or drag a box with Add Text Box.',
          '  - Find a word with the magnifier, then copy text with Select Text.',
          '  - Open the thumbnails or bookmarks on the left to move about.',
          '  - Choose Organize Pages to turn, move, delete or insert pages.'], H - 130)
c.setFont('Helvetica', 12); c.drawString(60, y - 10, 'Go to the page of shapes and colours')
c.linkAbsolute('', 'p2', (58, y - 14, 300, y + 2)); c.setStrokeColor(colors.HexColor('#2a5db0')); c.line(60, y - 12, 268, y - 12)
y -= 50
for f in ('Helvetica', 'Helvetica-Bold', 'Times-Roman', 'Times-Italic', 'Courier'):
    c.setFont(f, 14); c.drawString(60, y, f + ': the quick brown fox jumps over the lazy dog'); y -= 22
# page 2
c.showPage()
heading('Shapes and colours', H - 90, 'p2')
for i, col in enumerate(('#e04040', '#f0a020', '#40a050', '#2a5db0', '#8040c0')):
    c.setFillColor(colors.HexColor(col)); c.circle(100 + i * 95, H - 190, 38, fill=1, stroke=0)
c.setLineWidth(6); c.setStrokeColor(colors.black)
for i, (cap, join) in enumerate(((0, 0), (1, 1), (2, 2))):
    c.setLineCap(cap); c.setLineJoin(join)
    p = c.beginPath(); p.moveTo(80 + i * 170, H - 330); p.lineTo(140 + i * 170, H - 270); p.lineTo(200 + i * 170, H - 330); c.drawPath(p)
c.setLineWidth(2); c.setDash(8, 4); c.line(60, H - 370, W - 60, H - 370); c.setDash()
for i in range(40):
    c.setFillColor(colors.Color(i / 40, 0.3, 1 - i / 40)); c.rect(60 + i * 12, H - 470, 12, 70, fill=1, stroke=0)
c.setFillColor(colors.black); c.setFont('Helvetica', 11); c.drawString(60, H - 490, 'A row of colours, round and dashed lines, and joins.')
# page 3
c.showPage()
heading('A table', H - 90, 'p3')
rows = [('Format', 'Read', 'Write'), ('PDF 1.0 - 2.0', 'yes', 'PDF 1.7'), ('Encrypted (RC4, AES)', 'yes', 'unprotected'),
        ('Comments', 'yes', 'yes'), ('Bookmarks and links', 'yes', 'kept'), ('Page text', 'find, copy', 'export')]
y = H - 140
for r, row in enumerate(rows):
    c.setFillColor(colors.HexColor('#dfe8f6') if r == 0 else colors.white); c.rect(60, y - 6, 420, 22, fill=1, stroke=1)
    c.setFillColor(colors.black); c.setFont('Helvetica-Bold' if r == 0 else 'Helvetica', 11)
    for k, cell in enumerate(row): c.drawString(68 + k * 160, y, cell)
    y -= 22
c.save()
