/*
 * server_boot_spike.cpp - can a SERVER_MODE engine be booted in-process,
 *                         without the network layer?
 *
 * The storage fuzz target (roadmap N66, cubrid-testkit §6a-E9) works on
 * concurrent interleavings, so it needs the engine SERVER_MODE builds -- the
 * one with page-buffer, log and vacuum daemons -- rather than the SA library
 * the earlier reset spike used.  The first question is whether that engine can
 * come up inside a fuzz process at all, since a fuzz harness has no listener
 * and wants none.
 *
 * The sequence below is net_server_start () (src/communication/network_sr.c)
 * with the network half removed.  boot_restart_server () already runs *before*
 * css_init () there, so the ordering is upstream's, not invented here.  Two
 * calls are deliberately dropped: net_server_init (), which is static and only
 * fills the request-dispatch table an in-process harness never uses, and
 * css_init (), which opens the socket.
 *
 * Not a fuzz target, not in the build.  Build it against an instrumented or
 * ordinary build tree:
 *
 *   g++ -O2 -o server_boot_spike server_boot_spike.cpp \
 *       $(grep -o '\-I[^ ]*' <build>/cubrid/CMakeFiles/cubrid.dir/flags.make | sort -u) \
 *       -DSERVER_MODE -L<build>/cubrid -lcubrid -latomic
 *   ./server_boot_spike <dbname>
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "config.h"
#include "boot_sr.h"
#include "critical_section.h"
#include "error_manager.h"
#include "memory_monitor_sr.hpp"
#include "message_catalog.h"
#include "system_parameter.h"
#include "thread_manager.hpp"
#include "tz_support.h"

int
main (int argc, char **argv)
{
  if (argc < 2)
    {
      fprintf (stderr, "usage: %s <dbname>\n", argv[0]);
      return 2;
    }
  const char *db_name = argv[1];

  THREAD_ENTRY *thread_p = NULL;

#define STEP(expr, what)                                                      \
  do {                                                                        \
    int rc_ = (expr);                                                         \
    printf ("%-46s rc=%d\n", (what), rc_);                                    \
    if (rc_ != NO_ERROR)                                                      \
      {                                                                       \
        fprintf (stderr, "  stopped at %s: %s\n", (what), er_msg () ? er_msg () : "(no message)"); \
        return 1;                                                             \
      }                                                                       \
  } while (0)

  STEP (er_init (NULL, ER_NEVER_EXIT), "er_init (bootstrap)");

  cubthread::initialize (thread_p);
  printf ("%-46s thread_p=%p\n", "cubthread::initialize", (void *) thread_p);
  if (thread_p == NULL)
    {
      fprintf (stderr, "  thread manager gave no entry\n");
      return 1;
    }

  STEP (msgcat_init (), "msgcat_init");
  STEP (tz_load (), "tz_load");
  STEP (sysprm_load_and_init (db_name, NULL, SYSPRM_LOAD_ALL), "sysprm_load_and_init");
  sysprm_set_er_log_file (db_name);
  STEP (sync_initialize_sync_stats (), "sync_initialize_sync_stats");
  STEP (csect_initialize_static_critical_sections (), "csect_initialize_static_critical_sections");
  STEP (er_init (NULL, prm_get_integer_value (PRM_ID_ER_EXIT_ASK)), "er_init (with parameters)");
#if !defined(WINDOWS)
  STEP (mmon_initialize (db_name), "mmon_initialize");
#endif

  /* net_server_init () and css_init () are the two the harness does without --
   * see the file comment. */

  CHECK_ARGS check_coll_and_timezone = { true, true };
  STEP (boot_restart_server (thread_p, true, db_name, false, &check_coll_and_timezone, NULL, false),
	"boot_restart_server");

  printf ("\nBOOTED -- SERVER_MODE engine is up in-process with no listener.\n");

  /* The point of SERVER_MODE is the daemons.  A boot that returns NO_ERROR but
   * leaves one thread would be the SA shape under a different name, so count. */
  {
    FILE *f = fopen ("/proc/self/status", "r");
    char line[256];
    if (f != NULL)
      {
	while (fgets (line, sizeof (line), f) != NULL)
	  {
	    if (strncmp (line, "Threads:", 8) == 0)
	      {
		printf ("%-46s %s", "live threads while booted", line + 9);
		break;
	      }
	  }
	fclose (f);
      }
  }

  bool ok = xboot_shutdown_server (thread_p, ER_ALL_FINAL);
  printf ("%-46s ok=%d\n", "xboot_shutdown_server", (int) ok);

  cubthread::finalize ();
  printf ("%-46s done\n", "cubthread::finalize");
  return ok ? 0 : 1;
}
