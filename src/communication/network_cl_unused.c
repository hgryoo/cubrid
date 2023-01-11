/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

/*
 * network_cl_unused.c - unused client side support functions.
 */

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * net_client_shutdown_server -
 *
 * return:
 *
 * Note: Sends the server shutdown request to the server.
 *    This is not used and I'm not sure if it even works.
 *    Need to be careful that we don't expect a reply here.
 */
void
net_client_shutdown_server (void)
{
  css_send_request_to_server (net_Server_host, NET_SERVER_SHUTDOWN, NULL, 0);
}
#endif /* ENABLE_UNUSED_FUNCTION */


#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * net_client_request_send_large_data -
 *
 * return: error status
 *
 *   request(in): server request id
 *   argbuf(in): argument buffer (small)
 *   argsize(in): byte size of argbuf
 *   replybuf(in): reply argument buffer (small)
 *   replysize(in): size of reply argument buffer
 *   databuf(in): data buffer to send (large)
 *   datasize(in): size of data buffer
 *   replydata(in): receive data buffer (large)
 *   replydatasize(in): size of expected reply data
 *
 * Note: This is one of two functions that is called to perform a server
 *    request.  All network interface routines will call either this
 *    function or net_client_request2.
 */
int
net_client_request_send_large_data (int request, char *argbuf, int argsize, char *replybuf, int replysize,
				    char *databuf, INT64 datasize, char *replydata, int replydatasize)
{
  unsigned int rc;
  int size;
  int error;
  char *reply = NULL;

  error = 0;

  if (net_Server_name[0] == '\0')
    {
      /* need to have a more appropriate "unexpected disconnect" message */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_CRASHED, 0);
      error = -1;
      return error;
    }

#if defined(HISTO)
  if (net_Histo_setup)
    {
      histo_add_request (request, argsize + datasize);
    }
#endif /* HISTO */

  rc = css_send_req_to_server_with_large_data (net_Server_host, request, argbuf, argsize, databuf, datasize, replybuf,
					       replysize);

  if (rc == 0)
    {
      error = css_Errno;
      return set_server_error (error);
    }

  if (rc)
    {
      if (replydata != NULL)
	{
	  css_queue_receive_data_buffer (rc, replydata, replydatasize);
	}
      error = css_receive_data_from_server (rc, &reply, &size);
      if (error != NO_ERROR)
	{
	  COMPARE_AND_FREE_BUFFER (replybuf, reply);
	  return set_server_error (error);
	}
      else
	{
	  error = COMPARE_SIZE_AND_BUFFER (&replysize, size, &replybuf, reply);
	}

      if (replydata != NULL)
	{
	  error = css_receive_data_from_server (rc, &reply, &size);
	  if (error != NO_ERROR)
	    {
	      COMPARE_AND_FREE_BUFFER (replydata, reply);
	      return set_server_error (error);
	    }
	  else
	    {
	      error = COMPARE_SIZE_AND_BUFFER (&replydatasize, size, &replydata, reply);
	    }
	}
    }
#if defined(HISTO)
  if (net_Histo_setup)
    {
      histo_finish_request (request, replysize + replydatasize);
    }
#endif /* HISTO */
  return error;
}

/*
 * net_client_request_recv_large_data -
 *
 * return: error status
 *
 *   request(in): server request id
 *   argbuf(in): argument buffer (small)
 *   argsize(in):  byte size of argbuf
 *   replybuf(in): reply argument buffer (small)
 *   replysize(in): size of reply argument buffer
 *   databuf(in): data buffer to send (large)
 *   datasize(in): size of data buffer
 *   replydata(in): receive data buffer (large)
 *   replydatasize_ptr(in): size of expected reply data
 *
 * Note:
 */
int
net_client_request_recv_large_data (int request, char *argbuf, int argsize, char *replybuf, int replysize,
				    char *databuf, int datasize, char *replydata, INT64 * replydatasize_ptr)
{
  unsigned int rc;
  int size;
  int error;
  INT64 reply_datasize;
  int num_data;
  char *reply = NULL, *ptr, *packed_desc;
  int i, packed_desc_size;

  error = 0;
  *replydatasize_ptr = 0;

  if (net_Server_name[0] == '\0')
    {
      /* need to have a more appropriate "unexpected disconnect" message */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_CRASHED, 0);
      error = -1;
    }
  else
    {
#if defined(HISTO)
      if (net_Histo_setup)
	{
	  histo_add_request (request, argsize + datasize);
	}
#endif /* HISTO */
      rc = css_send_req_to_server (net_Server_host, request, argbuf, argsize, databuf, datasize, replybuf, replysize);
      if (rc == 0)
	{
	  return set_server_error (css_Errno);
	}

      error = css_receive_data_from_server (rc, &reply, &size);

      if (error != NO_ERROR || reply == NULL)
	{
	  COMPARE_AND_FREE_BUFFER (replybuf, reply);
	  return set_server_error (error);
	}
      else
	{
	  error = COMPARE_SIZE_AND_BUFFER (&replysize, size, &replybuf, reply);
	}

      /* here we assume that the first integer in the reply is the length of the following data block */
      ptr = or_unpack_int64 (reply, &reply_datasize);
      num_data = (int) (reply_datasize / INT_MAX + 1);

      if (reply_datasize)
	{
	  for (i = 0; i < num_data; i++)
	    {
	      packed_desc_size = MIN ((int) reply_datasize, INT_MAX);

	      packed_desc = (char *) malloc (packed_desc_size);
	      if (packed_desc == NULL)
		{
		  return set_server_error (CANT_ALLOC_BUFFER);
		}
	      css_queue_receive_data_buffer (rc, packed_desc, packed_desc_size);
	      error = css_receive_data_from_server (rc, &reply, &size);
	      if (error != NO_ERROR || reply == NULL)
		{
		  COMPARE_AND_FREE_BUFFER (packed_desc, reply);
		  free_and_init (packed_desc);
		  return set_server_error (error);
		}
	      else
		{
		  memcpy (replydata, reply, size);
		  COMPARE_AND_FREE_BUFFER (packed_desc, reply);
		  free_and_init (packed_desc);
		}
	      *replydatasize_ptr += size;
	      reply_datasize -= size;
	      replydata += size;
	    }
	}

#if defined(HISTO)
      if (net_Histo_setup)
	{
	  histo_finish_request (request, replysize + *replydatasize_ptr);
	}
#endif /* HISTO */
    }
  return error;
}
#endif /* ENABLE_UNUSED_FUNCTION */
