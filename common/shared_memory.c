/*
 *   This file is part of DroneBridge: https://github.com/seeul8er/DroneBridge
 *
 *   Copyright 2017 Wolfgang Christl
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 *
 */

#include <sys/mman.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <zconf.h>
#include <errno.h>
#include "db_protocol.h"
#include "shared_memory.h"
#include "db_common.h"

db_gnd_status_t *db_gnd_status_memory_open(void) {
    int fd;
    for(;;) {
        fd = shm_open("/db_gnd_status_t", O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
        if(fd > 0) {
            break;
        }
        perror("db_gnd_status_t");
        usleep((__useconds_t) 1e5);
    }

    if (ftruncate(fd, sizeof(db_gnd_status_t)) == -1) {
        perror("db_gnd_status_t: ftruncate");
        exit(1);
    }

    void *retval = mmap(NULL, sizeof(db_gnd_status_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (retval == MAP_FAILED) {
        perror("db_gnd_status_t: mmap");
        exit(1);
    }
    return (db_gnd_status_t*)retval;
}

db_rc_status_t *db_rc_status_memory_open(void) {
    int fd;
    for(;;) {
        fd = shm_open("/db_rc_status_t", O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
        if(fd > 0) {
            break;
        }
        perror("db_rc_status_memory_open");
        usleep((__useconds_t) 1e5);
    }

    if (ftruncate(fd, sizeof(db_rc_status_t)) == -1) {
        perror("db_rc_status_memory_open: ftruncate");
        exit(1);
    }

    void *retval = mmap(NULL, sizeof(db_rc_status_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (retval == MAP_FAILED) {
        perror("db_rc_status_memory_open: mmap");
        exit(1);
    }
    return (db_rc_status_t*)retval;
}

db_uav_status_t *db_uav_status_memory_open(void) {
    int fd;
    do {
        fd = shm_open("/db_uav_status_t", O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);
        if(fd < 0) {
            LOG_SYS_STD(LOG_ERR, "Could not open db_uav_status_memory_open: %s - Will try again ...\n", strerror(errno));
            usleep(100000);
        }
    } while(fd < 0);

    if (ftruncate(fd, sizeof(db_uav_status_t)) == -1) {
        perror("db_uav_status_t: ftruncate");
        exit(1);
    }

    void *retval = mmap(NULL, sizeof(db_uav_status_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (retval == MAP_FAILED) { perror("mmap"); exit(1); }
    return (db_uav_status_t*)retval;
}

void db_rc_values_memory_init(db_rc_values_t *rc_values) {
    for(int i = 0; i < NUM_CHANNELS; i++) {
        rc_values->ch[i] = 1000;
    }
}

db_rc_values_t *db_rc_values_memory_open(void) {
    int fd;
    for(;;) {
        fd = shm_open("/db_rc_values_t", O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
        if(fd > 0) {
            break;
        }
        perror("db_rc_values_memory_open: Waiting for init ... %s");
        usleep((__useconds_t) 1e5);
    }

    if (ftruncate(fd, sizeof(db_rc_values_t)) == -1) {
        perror("db_rc_values_memory_open: ftruncate");
        exit(1);
    }

    void *retval = mmap(NULL, sizeof(db_rc_values_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (retval == MAP_FAILED) {
        perror("db_rc_values_memory_open: mmap");
        exit(1);
    }
    db_rc_values_t *tretval = (db_rc_values_t*)retval;
    db_rc_values_memory_init(tretval);
    return (db_rc_values_t*)retval;
}

void db_rc_overwrite_values_memory_init(db_rc_overwrite_values_t *rc_values) {
    for(int i = 0; i < NUM_CHANNELS; i++) {
        rc_values->ch[i] = 0;
    }
}

db_rc_overwrite_values_t *db_rc_overwrite_values_memory_open(void) {
    int fd;
    for(;;) {
        fd = shm_open("/db_rc_overwrite", O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
        if(fd > 0) {
            break;
        }
        perror("db_rc_overwrite_values_memory_open: Waiting for init ... %s");
        usleep((__useconds_t) 1e5);
    }

    if (ftruncate(fd, sizeof(db_rc_overwrite_values_t)) == -1) {
        perror("db_rc_overwrite_values_memory_open: ftruncate");
        exit(1);
    }

    void *retval = mmap(NULL, sizeof(db_rc_overwrite_values_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (retval == MAP_FAILED) {
        perror("db_rc_overwrite_values_memory_open: mmap");
        exit(1);
    }
    db_rc_overwrite_values_t *tretval = (db_rc_overwrite_values_t*)retval;
    db_rc_overwrite_values_memory_init(tretval);
    return tretval;
}