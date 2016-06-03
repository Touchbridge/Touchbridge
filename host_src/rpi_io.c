/*
 *
 * rpi_io.c
 *
 * This file is part of Touchbridge
 *
 * Copyright 2015 James L Macfarlane
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#define _POSIX_C_SOURCE 200112L
 
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <poll.h> 

#include "debug.h"
#include "rpi_int.h"
#include "rpi_io.h"
 
// For Pi 1
//#define BCM2708_PERI_BASE        0x20000000
// For Pi 2
#define BCM2708_PERI_BASE        0x3F000000
#define GPIO_BASE                (BCM2708_PERI_BASE + 0x200000) /* GPIO controller */
#define BLOCK_SIZE (4*1024)
 
void rpi_io_set_pin_mode(rpi_io_t *io, int pin, int mode)
{
    int reg, bit;

    // BCM chip has IO pins number uo to 53 but we only deal with
    // the first 32 in this library.
    if (pin < 0 || pin > 31) {
        return;
    }
    reg = pin / 10;
    bit = (pin % 10) * 3;

    RPI_IO_REG_INDEX(io->gpio + BCM_GPFSEL0, reg) &= ~(7 << bit);
    RPI_IO_REG_INDEX(io->gpio + BCM_GPFSEL0, reg) |= (mode & 7) << bit;
}

// Set a bunch of pins to the same mode.
void rpi_io_set_pins_mode(rpi_io_t *io, uint32_t mask, int mode)
{
    for (int i = 0; i < 8 * sizeof(uint32_t); i++) {
        if ((mask >> i) & 1) rpi_io_set_pin_mode(io, i, mode);
    }
}

//
// Set up a memory regions to access GPIO
//
rpi_io_t *rpi_io_open(void)
{
    rpi_io_t *io = malloc(sizeof(rpi_io_t));
    int  fd;

    /* open /dev/mem */
    if ((fd = open("/dev/mem", O_RDWR|O_SYNC) ) < 0) {
        printf("open: /dev/mem");
        perror("");
        exit(-1);
    }

    /* mmap gpio */
    io->gpio = mmap(
        NULL,                   // any adddress in our space will do
        BLOCK_SIZE,             // map length
        PROT_READ|PROT_WRITE,   // enable reading & writting to mapped memory
        MAP_SHARED,             // shared with other processes
        fd,                     // file to map
        GPIO_BASE               // offset to gpio peripheral
    );


    if (io->gpio == MAP_FAILED) {
        perror("mmap");
        exit(-1);
    }

    close(fd);                  // No need to keep mem_fd open after mmap
 
    return io; 
}

void rpi_io_close(rpi_io_t *io)
{
    munmap(io->gpio, BLOCK_SIZE);
    free(io);
}

void rpi_io_busywait_ns(long delay_ns)
{
    struct timespec now;
    struct timespec end;

    clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    end.tv_sec = now.tv_sec;
    end.tv_nsec = now.tv_nsec + delay_ns;
    if (end.tv_nsec >= 1000000000) {
        end.tv_sec++;
        end.tv_nsec -= 1000000000;
    }
    
    do {
        clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    } while (now.tv_sec < end.tv_sec && now.tv_nsec < end.tv_nsec);
}

int rpi_io_interrupt_open(int pin, int edge)
{
	gpio_export(pin);
	gpio_set_dir(pin, 0);

    if (edge == RPI_IO_EDGE_RISING) {
        gpio_set_edge(pin, "rising");
    } else {
        gpio_set_edge(pin, "falling");
    }

    int fd = gpio_fd_open(pin);
    SYSERROR_IF(fd < 0, "open");
	return fd;
}

#define INT_CLEAR_BUF_SIZE      (16)

void rpi_io_interrupt_clear(int fd)
{
    unsigned char buf[INT_CLEAR_BUF_SIZE];
    int ret;
    ret = read(fd, buf, INT_CLEAR_BUF_SIZE);
    SYSERROR_IF(ret < 0, "read");
    ret = lseek(fd, 0, SEEK_SET); // This is bizzare but important. It resets the int.
    SYSERROR_IF(ret < 0, "read");
    // Not sure yet if both the read and the seek are required. Seek
    // definitely is. Need to go read kernel driver source.
}

#define INT_FLUSH_TIMEOUT (20) // milliseconds

int rpi_io_interrupt_flush(int fd)
{
	struct pollfd fdset[1];
    memset((void*)fdset, 0, sizeof(fdset));
    fdset[0].fd = fd;
    fdset[0].events = POLLPRI;

    int ret = poll(fdset, 1, INT_FLUSH_TIMEOUT);      
    SYSERROR_IF(ret < 0, "poll");

    if (fdset[0].revents & POLLPRI) {
        rpi_io_interrupt_clear(fdset[0].fd);
        return 1;
    } else {
        return 0;
    }
}

