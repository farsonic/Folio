/* clip.c - the clipboard: plain text as an IFF FTXT form with one CHRS chunk (Latin-1) */
#include <stdlib.h>
#include <exec/memory.h>
#include <devices/clipboard.h>
#include <proto/exec.h>
#include "folio.h"

/* one clipboard.device command (CMD_READ, CMD_WRITE, CMD_UPDATE), done synchronously with DoIO:
   1 if it went well. The device moves io_Offset on by itself, so successive reads or writes walk
   through the clip */
static int clip_io(struct IOClipReq *io, UWORD cmd, APTR data, ULONG len)
{
  io->io_Command = cmd; io->io_Data = (STRPTR)data; io->io_Length = len;
  DoIO((struct IORequest *)io);
  return io->io_Error == 0;
}
/* s (len bytes of Latin-1) onto clipboard unit 0 as an FTXT form, for any program to paste: 1 if
   written. The 20-byte header is FORM, its length, FTXT, CHRS and the text's length; a chunk of odd
   length gets a pad byte, as IFF wants. The device takes the form written in order from offset 0,
   then CMD_UPDATE to say the clip is complete (sent whatever happened, so the clipboard isn't
   left waiting for the rest) */
int clip_write(const char *s, long len)
{
  struct MsgPort *mp;
  struct IOClipReq *io;
  int ok = 0;
  if (!(mp = CreateMsgPort())) return 0;
  if ((io = (struct IOClipReq *)CreateIORequest(mp, sizeof(struct IOClipReq)))) {
    if (!OpenDevice((STRPTR)"clipboard.device", 0, (struct IORequest *)io, 0)) {
      ULONG hdr[5];
      UBYTE pad = 0;
      hdr[0] = 0x464F524DUL; hdr[1] = 4 + 8 + len + (len & 1); hdr[2] = 0x46545854UL; hdr[3] = 0x43485253UL; hdr[4] = len;
      io->io_Offset = 0; io->io_Error = 0; io->io_ClipID = 0;
      ok = clip_io(io, CMD_WRITE, hdr, 20) && clip_io(io, CMD_WRITE, (APTR)s, len);
      if (ok && (len & 1)) clip_io(io, CMD_WRITE, &pad, 1);
      clip_io(io, CMD_UPDATE, 0, 0);
      CloseDevice((struct IORequest *)io);
    }
    DeleteIORequest((struct IORequest *)io);
  }
  DeleteMsgPort(mp);
  return ok;
}
/* (after 0.1.2) the text on clipboard unit 0, if it holds an FTXT form: its CHRS chunks joined,
   malloc'd and 0-terminated (Latin-1); NULL if there is none. The device is read to the end of
   the clip (as it wants), whatever was found */
char *clip_read(void)
{
  struct MsgPort *mp;
  struct IOClipReq *io;
  char *res = NULL;
  if (!(mp = CreateMsgPort())) return NULL;
  if ((io = (struct IOClipReq *)CreateIORequest(mp, sizeof(struct IOClipReq)))) {
    if (!OpenDevice((STRPTR)"clipboard.device", 0, (struct IORequest *)io, 0)) {
      ULONG hdr[3], ch[2];
      long total, pos = 4, got = 0;
      io->io_Offset = 0; io->io_Error = 0; io->io_ClipID = 0;
      if (clip_io(io, CMD_READ, hdr, 12) && io->io_Actual == 12 && hdr[0] == 0x464F524DUL && hdr[2] == 0x46545854UL && hdr[1] < 0x1000000UL) {
        total = (long)hdr[1];
        res = (char *)malloc((size_t)total + 1);
        while (res && pos + 8 <= total && clip_io(io, CMD_READ, ch, 8) && io->io_Actual == 8) {
          long n = (long)ch[1];
          pos += 8;
          if (n < 0 || pos + n > total) break;
          if (ch[0] == 0x43485253UL) { if (!clip_io(io, CMD_READ, res + got, (ULONG)n)) break; got += (long)io->io_Actual; }
          else { char skip[256]; long k = n; while (k > 0) { long s = k > 256 ? 256 : k; if (!clip_io(io, CMD_READ, skip, (ULONG)s)) break; k -= s; } }
          pos += n;
          if (n & 1) { char p; clip_io(io, CMD_READ, &p, 1); pos++; }
        }
        if (res) res[got] = 0;
      }
      /* read on to the end, so the device knows this reader is done */
      { char rest[256]; do { if (!clip_io(io, CMD_READ, rest, sizeof rest)) break; } while (io->io_Actual == sizeof rest); }
      CloseDevice((struct IORequest *)io);
    }
    DeleteIORequest((struct IORequest *)io);
  }
  DeleteMsgPort(mp);
  return res;
}
