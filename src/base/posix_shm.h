/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
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
 * posix_shm.h
 */

#ifndef _POSIX_SHM_H_
#define _POSIX_SHM_H_

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#include "storage_common.h"
#include "memory_alloc.h"

#include "error_code.h"

#define POSIX_SHM_CHUNK_SIZE (IO_MAX_PAGE_SIZE + MAX_ALIGNMENT)
#define POSIX_SHM_CNT 100
#define POSIX_SHM_TOTAL (POSIX_SHM_CHUNK_SIZE * POSIX_SHM_CNT)

extern int posix_fd;
extern int posix_idx;
extern int posix_fd_client;

int posix_shm_open(char* name);

void* posix_shm_open_client(char* name, int size);

int posix_shm_close(void* mem, int size);

int posix_shm_write (char* buffer, int idx, int size, int fd);

int posix_shm_write_client (char* buffer, int idx, int size, int fd);

int posix_shm_read (char* buffer, int idx, int size, int fd);

int sem_wait_produce ();

int sem_post_consume ();

#endif /* _POSIX_SHM_H_ */
