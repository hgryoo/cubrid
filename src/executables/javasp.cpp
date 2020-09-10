/*
 * Copyright (C) 2008 Search Solution Corporation
 * Copyright (C) 2016 CUBRID Corporation
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */


/*
 * javasp.cpp - utility java stored procedure server main routine
 *
 */

#ident "$Id$"

#include "config.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#if !defined(WINDOWS)
#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#else /* not WINDOWS */
#include <winsock2.h>
#include <windows.h>
#endif /* not WINDOWS */

#include "environment_variable.h"
#include "system_parameter.h"
#include "error_code.h"
#include "message_catalog.h"
#include "utility.h"
#include "databases_file.h"
#include "object_representation.h"

#include "jsp_common.h"
#include "jsp_file.h"
#include "jsp_sr.h"

#define JAVASP_PING_DATA  "JAVASP_PING"
#define JAVASP_PING_SIZE  sizeof(JAVASP_PING_DATA)

static int javasp_start_server (const char *db_name, char *path, const int server_port);
static int javasp_stop_server (const SOCKET socket);
static int javasp_check_server (const char *db_name);
static int javasp_status_server (const SOCKET socket, char **check_db_name, int *check_db_name_len,
				 int *check_server_port);
static int javasp_ping_server (const SOCKET socket, char *buf);

static bool javasp_is_running (const int server_port);

/*
 * main() - javasp main function
 */

int
main (int argc, char *argv[])
{
  int status = NO_ERROR;
  char *command;
  char *db_name = NULL;
  SOCKET socket = INVALID_SOCKET;

  char javasp_info_file[PATH_MAX];
  JAVASP_SERVER_INFO jsp_info;
  {
#if !defined(WINDOWS)
    if (os_set_signal_handler (SIGPIPE, SIG_IGN) == SIG_ERR)
      {
	status = ER_GENERIC_ERROR;
	goto exit;
      }
#endif /* ! WINDOWS */

    /* initialize message catalog for argument parsing and usage() */
    if (utility_initialize () != NO_ERROR)
      {
	status = ER_GENERIC_ERROR;
	goto exit;
      }

    ER_SAFE_INIT (NULL, ER_NEVER_EXIT);

    /* check argument number */
    if (argc == 3)
      {
	command = argv[1];
	db_name = argv[2];
      }
    else
      {
	// error
	status = ER_GENERIC_ERROR;
	goto exit;
      }

    /* check database exists */
    DB_INFO *db = cfg_find_db (db_name);
    if (db == NULL)
      {
	fprintf (stderr, "database '%s' does not exist.\n", db_name);
	status = ER_GENERIC_ERROR;
	goto exit;
      }

    // load system parameter
    status = sysprm_load_and_init (db_name, NULL, SYSPRM_IGNORE_INTL_PARAMS);
    if (status != NO_ERROR)
      {
	status = ER_GENERIC_ERROR;
	goto exit;
      }

    // check java stored procedure is not enabled
    if (prm_get_bool_value (PRM_ID_JAVA_STORED_PROCEDURE) == false)
      {
	fprintf (stderr, "%s parameter is not enabled\n", prm_get_name (PRM_ID_JAVA_STORED_PROCEDURE));
	status = ER_GENERIC_ERROR;
	goto exit;
      }

    // try to create info dir and get absolute path for info file; $CUBRID/var/javasp_<db_name>.info
    if (javasp_get_info_dir ())
      {
	if (javasp_get_info_file (javasp_info_file, sizeof (javasp_info_file), db_name) == true)
	  {
	    jsp_info = javasp_read_info (javasp_info_file);
	  }
	else
	  {
	    fprintf (stderr, "Error while opening file: %s\n", javasp_info_file);
	    status = ER_GENERIC_ERROR;
	    goto exit;
	  }
      }
    else
      {
	char javasp_dir[PATH_MAX] = {0};
	envvar_vardir_file (javasp_dir, sizeof (javasp_dir), "javasp");
	fprintf (stderr, "Error while creating or opening folder: %s\n", javasp_dir);
	status = ER_GENERIC_ERROR;
	goto exit;
      }

    /* javasp command main routine */
    if (strcasecmp (command, "start") == 0)
      {
	if (javasp_is_running (jsp_info.port))
	  {
	    status = ER_GENERIC_ERROR;
	    goto exit;
	  }

	// start javasp server routine
	int start_port = prm_get_integer_value (PRM_ID_JAVA_STORED_PROCEDURE_PORT);
	status = javasp_start_server (db_name, db->pathname, start_port);
	if (status == NO_ERROR)
	  {
	    assert ((start_port != 0) ? (jsp_server_port () == start_port) : true);

	    jsp_info.pid = getpid();
	    jsp_info.port = jsp_server_port (); /* set for randomly assigned port number */
	    javasp_write_info (javasp_info_file, jsp_info);

	    while (true)
	      {
		SLEEP_MILISEC (0, 100);
	      }
	  }
      }
    else if (strcasecmp (command, "stop") == 0)
      {
	socket = jsp_connect_server (jsp_info.port);
	if (socket != INVALID_SOCKET)
	  {
	    status = javasp_stop_server (socket);
	    if (status != NO_ERROR)
	      {
		status = ER_GENERIC_ERROR;
		goto exit;
	      }

	    jsp_disconnect_server (socket);
	    socket = INVALID_SOCKET;

	    socket = jsp_connect_server (jsp_info.port);
	    if (socket != INVALID_SOCKET)
	      {
		// error: server is running already
		printf ("javasp_stop_server failed, still running");

		kill (jsp_info.pid, SIGINT);

		status = EXIT_FAILURE;
		goto exit;
	      }

	    jsp_info.pid = -1;
	    jsp_info.port = -1;
	    javasp_get_info_file (javasp_info_file, sizeof (javasp_info_file), db_name);
	    javasp_write_info (javasp_info_file, jsp_info);
	  }
	else
	  {
	    // error: server is not running
	    printf ("server is not running");
	    status = EXIT_FAILURE;
	    goto exit;
	  }
      }
    else if (strcasecmp (command, "status") == 0)
      {
	char *check_db_name = NULL;
	int check_db_name_len = 0;
	int check_port = -1;

	socket = jsp_connect_server (jsp_info.port);
	if (socket != INVALID_SOCKET)
	  {
	    status = javasp_status_server (socket, &check_db_name, &check_db_name_len, &check_port);
	    if (status != NO_ERROR)
	      {
		// error: server is running already
		printf ("javasp_status_server failed");
		status = EXIT_FAILURE;
		goto exit;
	      }

	    /*
	        if (port != check_port || strncasecmp (db_name, check_db_name, check_db_name_len) != 0)
	          {
	    	// weird situation
	    	printf ("db name differnet %s, %s", db_name, check_db_name);
	    	status = EXIT_FAILURE;
	    	goto exit;
	          }
	    */

	    fprintf (stdout, "Java Stored Procedure Server (%s, pid %d, port %d)", db_name, jsp_info.pid, check_port);
	  }
	else
	  {
	    // error: server is not running
	    printf ("server is not running");
	    status = EXIT_FAILURE;
	    goto exit;
	  }
      }
    else if (strcasecmp (command, "ping") == 0)
      {
	char buffer[12] = {0};
	socket = jsp_connect_server (jsp_info.port);
	if (socket != INVALID_SOCKET)
	  {
	    if (javasp_ping_server (socket, buffer) == NO_ERROR)
	      {
		fprintf (stdout, buffer);
	      }
	  }
      }
    else
      {
	assert (false);
	status = ER_FAILED;
	goto exit;
      }
  }
exit:
  if (socket != INVALID_SOCKET)
    {
      jsp_disconnect_server (socket);
    }
  return status;
}

static int
javasp_start_server (const char *db_name, char *path, const int server_port)
{
  int status = NO_ERROR;
#if !defined(WINDOWS)
  /* create a new session */
  setsid ();
#endif

  return jsp_start_server (db_name, path, server_port);
}

static int
javasp_stop_server (const SOCKET socket)
{
  int status = NO_ERROR;
  char *buffer = NULL;
  int req_size = (int) sizeof (int);
  int nbytes;

  int stop_code = 0xFF;
  nbytes = jsp_writen (socket, (void *) &stop_code, (int) sizeof (int));
  if (nbytes != (int) sizeof (int))
    {
      status = ER_SP_NETWORK_ERROR;
    }

  return status;
}

static int
javasp_status_server (const SOCKET socket, char **check_db_name, int *check_db_name_len, int *check_server_port)
{
  int status = NO_ERROR;
  char *buffer = NULL, *ptr = NULL;
  OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_request;
  char *request = OR_ALIGNED_BUF_START (a_request);
  assert (check_db_name != NULL && check_server_port != NULL);
  {
    if (socket == INVALID_SOCKET)
      {
	// server is not running
	status = ER_SP_CANNOT_CONNECT_JVM;
	goto exit;
      }

    ptr = or_pack_int (request, SP_CODE_UTIL_STATUS);
    ptr = or_pack_int (ptr, SP_CODE_UTIL_TERMINATE_THREAD);

    int nbytes = jsp_writen (socket, request, (int) sizeof (int));
    if (nbytes != (int) sizeof (int))
      {
	// stopping failed
	status = ER_SP_NOT_RUNNING_JVM;
	goto exit;
      }

    int res_size = 0;
    nbytes = jsp_readn (socket, (char *) &res_size, (int) sizeof (int));
    if (nbytes != (int) sizeof (int))
      {
	status = ER_SP_NETWORK_ERROR;
	goto exit;
      }
    res_size = ntohl (res_size);

    buffer = (char *) malloc (res_size);
    if (buffer == NULL)
      {
	status = ER_OUT_OF_VIRTUAL_MEMORY;
	goto exit;
      }

    nbytes = jsp_readn (socket, buffer, res_size);
    if (nbytes != res_size)
      {
	status = ER_SP_NETWORK_ERROR;
	goto exit;
      }

    ptr = or_unpack_int (buffer, check_server_port);
    ptr = or_unpack_string_alloc (ptr, check_db_name);
  }

exit:
  if (buffer)
    {
      free_and_init (buffer);
    }
  return status;
}

static int
javasp_ping_server (const SOCKET socket, char *buf)
{
  int status = NO_ERROR;
  OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_request;
  char *request = OR_ALIGNED_BUF_START (a_request);
  char *ptr = NULL;
  {
    if (socket == INVALID_SOCKET)
      {
	// server is not running
	status = ER_SP_CANNOT_CONNECT_JVM;
	goto exit;
      }

    ptr = or_pack_int (request, SP_CODE_UTIL_PING);
    ptr = or_pack_int (ptr, SP_CODE_UTIL_TERMINATE_THREAD);

    int nbytes = jsp_writen (socket, request, (int) sizeof (int));
    if (nbytes != (int) sizeof (int))
      {
	// stopping failed
	status = ER_SP_NOT_RUNNING_JVM;
	goto exit;
      }

    int res_size = 0;
    nbytes = jsp_readn (socket, (char *) &res_size, (int) sizeof (int));
    if (nbytes != (int) sizeof (int))
      {
	status = ER_SP_NETWORK_ERROR;
	goto exit;
      }
    res_size = ntohl (res_size);

    nbytes = jsp_readn (socket, buf, res_size);
    if (nbytes != res_size)
      {
	status = ER_SP_NETWORK_ERROR;
	goto exit;
      }
  }

exit:
  return status;
}

static bool javasp_is_running (const int server_port)
{
  // check server running
  bool result = false;
  char buffer[JAVASP_PING_SIZE] = {0};
  SOCKET socket = jsp_connect_server (server_port);
  if (socket != INVALID_SOCKET)
    {
      if (javasp_ping_server (socket, buffer) == NO_ERROR)
	{
	  if (strncmp (buffer, JAVASP_PING_DATA, JAVASP_PING_SIZE) == 0)
	    {
	      result = true;
	    }
	}
      jsp_disconnect_server (server_port);
    }
  return result;
}