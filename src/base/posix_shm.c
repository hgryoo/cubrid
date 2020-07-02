#include "posix_shm.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <semaphore.h>

#include "storage_common.h"
#include "memory_alloc.h"
#include "error_code.h"
#include "error_manager.h"
#include "byte_order.h"


#define POSIX_SHM_CHUNK_SIZE (IO_MAX_PAGE_SIZE + MAX_ALIGNMENT)

#define POSIX_SHM_CNT 1
#define POSIX_SHM_TOTAL (POSIX_SHM_CHUNK_SIZE * POSIX_SHM_CNT)

#define POSIX_SHM_CLIENT_CNT (4)
#define POSIX_SHM_CLIENT_TOTAL (POSIX_SHM_CHUNK_SIZE * POSIX_SHM_CLIENT_CNT)

#define POSIX_SEM_PRODUCE_CAS "mutex-produce-cas"
#define POSIX_SEM_CONSUME_CAS "mutex-consume-cas"

#define POSIX_SEM_PRODUCE "mutex-produce"
#define POSIX_SEM_CONSUME "mutex-consume"

int posix_fd = -1;
int posix_idx = 0;
int posix_fd_client = -1;
int idxs = 0;

int ndes = -1;

sem_t *mutex_produce2, *mutex_consume2;
void* buffer_shm;
void* posix_shm_open(char* name, int size)
{
    if (posix_fd == -1)
    {
        if ((mutex_produce2 = sem_open (POSIX_SEM_PRODUCE_CAS, O_CREAT, 0777, 0)) == SEM_FAILED) {
            er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 1, "sem_open()");
            //return ER_FAILED;
        }

        if ((mutex_consume2 = sem_open (POSIX_SEM_CONSUME_CAS, O_CREAT, 0777, 0)) == SEM_FAILED) {
            er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 1, "sem_open()");
            //return ER_FAILED;
        }

        posix_fd = shm_open(name, O_RDWR | O_CREAT, 0777);
        if (posix_fd < 0) {
            er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 1, "shm_open()");
            //return ER_FAILED;
        }

        if (ftruncate(posix_fd, size) == -1) {
            er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 1, "ftruncate()");
            //return ER_FAILED;
        }

    }
         buffer_shm = (void *) mmap (0, size, PROT_READ | PROT_WRITE, MAP_SHARED, posix_fd, 0);

    return buffer_shm;
}

int sem_wait_produce2 () {
    if (sem_wait (mutex_produce2) == -1) {
        er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 1, "mutex_produce2()");
            return ER_FAILED;
    }
}

int sem_post_produce2 () {
    if (sem_post (mutex_produce2) == -1) {
        er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 1, "mutex_produce2()");
            return ER_FAILED;
    }
}

int sem_wait_consume2 () {
    if (sem_wait (mutex_consume2) == -1) {
        er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 1, "mutex_consume2()");
            return ER_FAILED;
    }
}

int sem_post_consume2 () {
    if (sem_post (mutex_consume2) == -1) {
        er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 1, "mutex_consume2()");
            return ER_FAILED;
    }
}

sem_t *mutex_produce, *mutex_consume;
void* data = NULL;
void* posix_shm_open_client(char* name, int size)
{
    if (posix_fd_client == -1)
    {
        if ((mutex_produce = sem_open (POSIX_SEM_PRODUCE, O_CREAT, 0777, 0)) == SEM_FAILED) {
            er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 1, "sem_open()");
            //return ER_FAILED;
        }

        if ((mutex_consume = sem_open (POSIX_SEM_CONSUME, O_CREAT, 0777, 0)) == SEM_FAILED) {
            er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 1, "sem_open()");
            //return ER_FAILED;
        }

        posix_fd_client = shm_open(name, O_RDWR | O_CREAT, 0777);
        if (posix_fd_client < 0) {
            er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 1, "shm_open()");
            //return ER_FAILED;
        }

        if (ftruncate(posix_fd_client, size) == -1) {
            er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 1, "ftruncate()");
            //return ER_FAILED;
        }

        data = (void *) mmap (0, size, PROT_READ | PROT_WRITE, MAP_SHARED, posix_fd_client, 0);
    }
    return data;
}

int posix_shm_close(void* mem, int size)
{
    munmap (mem, size);
    return NO_ERROR;
}

int posix_shm_destroy(char* name)
{
    shm_unlink(name);
    return NO_ERROR;
}

int posix_shm_write (char* buffer, int idx, int size, int fd)
{
    if (sem_wait (mutex_produce2) == -1) {
        er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 1, "mutex_produce()");
            return ER_FAILED;
    }

    char* data = (char *) mmap (0, POSIX_SHM_TOTAL, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    memcpy (data, buffer, size);

    munmap (data, POSIX_SHM_TOTAL);

    if (sem_post (mutex_consume2) == -1) {
        er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 1, "mutex_consume()");
            return ER_FAILED;
    }

    return NO_ERROR;
}

int posix_shm_read (char* buffer, int idx, int size, int fd)
{
    if (sem_wait (mutex_consume2) == -1) {
        er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 1, "mutex_produce()");
            return ER_FAILED;
    }

    char *data = (char *) mmap (0, POSIX_SHM_TOTAL, PROT_READ, MAP_SHARED, fd, 0);

    memcpy (buffer, data, size);

    if (sem_post (mutex_produce2) == -1) {
        er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 1, "mutex_consume()");
            return ER_FAILED;
    }

    return NO_ERROR;
}

int posix_shm_close_client(char* name)
{
    shm_unlink(name);
    return NO_ERROR;
}

int posix_shm_write_client (char* buffer, int idx, int size, int fd)
{
    static const char magic[] = "CUB";
    static const char end_magic[] = "CUC";

    //printf ("\tCAS mutex_produce()\n");
    if (sem_wait (mutex_produce) == -1) {
        er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 1, "mutex_produce()");
            return ER_FAILED;
    }

    char* data = (char *) mmap (0, POSIX_SHM_CLIENT_TOTAL, PROT_READ | PROT_WRITE, MAP_SHARED, posix_fd_client, 0);
    //memset (data, POSIX_SHM_CLIENT_TOTAL, 0);
    
    memcpy (data, end_magic, 4);
    memcpy (data + 4, &size, sizeof(uint32_t));
    memcpy (data + 4 + sizeof (uint32_t), buffer, size);

    //msync(data, 4 + size + sizeof(uint32_t), MS_SYNC);

    munmap (data, POSIX_SHM_CLIENT_TOTAL);

    int value = -1;
    if (sem_post (mutex_consume) == -1) {
        er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 1, "mutex_consume()");
            return ER_FAILED;
    }
    //sem_getvalue(mutex_consume, &value);
    //printf ("\tCAS mutex_consume(): %d\n", value);

    return NO_ERROR;
}

int sem_wait_produce () {
    if (sem_wait (mutex_produce) == -1) {
        er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 1, "mutex_produce()");
            return ER_FAILED;
    }
}

int sem_post_consume () {
    if (sem_post (mutex_consume) == -1) {
        er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 1, "mutex_consume()");
            return ER_FAILED;
    }
}

void* posix_shm_data_client (char* buffer, int size, int fd)
{
    char* data = (char *) mmap (0, POSIX_SHM_CLIENT_TOTAL, PROT_READ | PROT_WRITE, MAP_SHARED, posix_fd_client, 0);
}