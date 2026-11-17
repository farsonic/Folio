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
