/*
 * record_fuzzer.cpp - libFuzzer target for the record value decode path.
 *
 * Target: or_get_value (), the function every on-disk record and every network
 * message goes through to turn packed bytes back into a DB_VALUE.  It reads a
 * packed domain out of the buffer and then reads the value that domain
 * describes, so a malformed buffer drives both the domain decoder and the
 * per-type readers.  Nothing here boots a server: the domain system is the
 * only global state involved, and it is initialised once.
 *
 * Why or_get_value () and not or_unpack_value ():  or_unpack_value () calls
 * or_init (buf, data, 0), and or_init () turns a length of 0 into
 * OR_INFINITE_POINTER, so the read is unbounded *by contract* and every
 * malformed input would report a buffer overrun that says nothing about the
 * decoder.  Bounding the OR_BUF with the real input length is what the disk
 * and network callers do, and it is the only shape in which an overrun is a
 * defect rather than misuse.
 */

#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include "config.h"
#include "dbtype.h"
#include "language_support.h"
#include "object_domain.h"
#include "object_primitive.h"
#include "object_representation.h"

extern "C" int LLVMFuzzerInitialize (int *argc, char ***argv);
extern "C" int LLVMFuzzerTestOneInput (const uint8_t * data, size_t size);

int
LLVMFuzzerInitialize (int *argc, char ***argv)
{
  (void) argc;
  (void) argv;

  /* Charset and collation tables first: the string domains read them while
   * decoding, and tp_init () builds domains that refer to them. */
  if (lang_init () != NO_ERROR)
    {
      abort ();
    }
  if (tp_init () != NO_ERROR)
    {
      abort ();
    }
  return 0;
}

int
LLVMFuzzerTestOneInput (const uint8_t * data, size_t size)
{
  if (size == 0 || size > (size_t) DB_INT32_MAX)
    {
      return 0;
    }

  OR_BUF buf;
  DB_VALUE value;

  /* Bounded, unlike or_unpack_value () -- see the file comment. */
  or_init (&buf, const_cast < char *>(reinterpret_cast < const char *>(data)), (int) size);
  db_make_null (&value);

  /* domain = NULL makes or_get_value () read the packed domain from the
   * buffer; expected = -1 means "length is not known in advance"; copy = true
   * exercises the allocating paths, which is where the interesting bugs are.
   */
  (void) or_get_value (&buf, &value, NULL, -1, true);

  /* copy = true above may have allocated.  Without this every input leaks and
   * LeakSanitizer drowns the real findings. */
  pr_clear_value (&value);

  return 0;
}
