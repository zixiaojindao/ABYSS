#include "localio.h"
#include <io.h>
#include <windows.h>
#include <errno.h>

int fsync (int fd)
{
  HANDLE h = (HANDLE) _get_osfhandle (fd);
  DWORD err;

  if (h == INVALID_HANDLE_VALUE)
    {
      errno = EBADF;
      return -1;
    }

  if (!FlushFileBuffers (h))
    {
      /* Translate some Windows errors into rough approximations of Unix
       * errors.  MSDN is useless as usual - in this case it doesn't
       * document the full range of errors.
       */
      err = GetLastError ();
      switch (err)
       {
         /* eg. Trying to fsync a tty. */
       case ERROR_INVALID_HANDLE:
         errno = EINVAL;
         break;

       default:
         errno = EIO;
       }
      return -1;
    }

  return 0;
}
