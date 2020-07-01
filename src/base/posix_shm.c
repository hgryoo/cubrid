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

#define POSIX_SHM_CNT 10
#define POSIX_SHM_TOTAL (POSIX_SHM_CHUNK_SIZE * POSIX_SHM_CNT)

#define POSIX_SHM_CLIENT_CNT (7)
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
int posix_shm_open(char* name)
{
    if (posix_fd == -1)
    {
        if ((mutex_produce2 = sem_open (POSIX_SEM_PRODUCE_CAS, O_CREAT, 0777, 1)) == SEM_FAILED) {
            er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 1, "sem_open()");
            return ER_FAILED;
        }

        if ((mutex_consume2 = sem_open (POSIX_SEM_CONSUME_CAS, O_CREAT, 0777, 0)) == SEM_FAILED) {
            er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 1, "sem_open()");
            return ER_FAILED;
        }

        posix_fd = shm_open(name, O_CREAT | O_RDWR, 0666);
        if (posix_fd < 0) {
            er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 1, "shm_open()");
            return ER_FAILED;
        }

        ftruncate(posix_fd, POSIX_SHM_TOTAL);
    }
}

sem_t *mutex_produce, *mutex_consume;
int posix_shm_open_client(char* name)
{
    static char *data = NULL;
    
    if (posix_fd_client == -1)
    {
        if ((mutex_produce = sem_open (POSIX_SEM_PRODUCE, O_CREAT, 0777, 1)) == SEM_FAILED) {
            er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 1, "sem_open()");
            return ER_FAILED;
        }

        if ((mutex_consume = sem_open (POSIX_SEM_CONSUME, O_CREAT, 0777, 0)) == SEM_FAILED) {
            er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 1, "sem_open()");
            return ER_FAILED;
        }

        posix_fd_client = shm_open(name, O_RDWR | O_CREAT, 0777);
        if (posix_fd_client < 0) {
            er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 1, "shm_open()");
            return ER_FAILED;
        }

        if (ftruncate(posix_fd_client, POSIX_SHM_CLIENT_TOTAL) == -1) {
            er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 1, "ftruncate()");
            return ER_FAILED;
        }
    }
}

int posix_shm_close(char* name)
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

    int len = size;
    int offset = 0;
    idxs = 0;

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
