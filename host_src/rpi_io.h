/*
 *
 * rpi_io.h
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
#ifndef RPI_IO_H
#define RPI_IO_H

#include <stdint.h>
 
#define BCM_GPFSEL0             (0x000)
#define BCM_GPFSEL1             (0x004)
#define BCM_GPFSEL2             (0x008)
#define BCM_GPFSEL3             (0x00C)
#define BCM_GPFSEL4             (0x010)
#define BCM_GPFSEL5             (0x014)

#define BCM_GPSET0              (0x01C)
#define BCM_GPSET1              (0x020)

#define BCM_GPCLR0              (0x028)
#define BCM_GPCLR1              (0x02C)

#define BCM_GPLEV0              (0x034)
#define BCM_GPLEV1              (0x038)

#define BCM_GPPUD               (0x094)
#define BCM_GPPUDCLK0           (0x098)
#define BCM_GPPUDCLK1           (0x09C)
 
#define RPI_IO_MODE_IN          (0b000)
#define RPI_IO_MODE_OUT         (0b001)
#define RPI_IO_MODE_AF0         (0b100)
#define RPI_IO_MODE_AF1         (0b101)
#define RPI_IO_MODE_AF2         (0b110)
#define RPI_IO_MODE_AF3         (0b111)
#define RPI_IO_MODE_AF4         (0b010)
#define RPI_IO_MODE_AF5         (0b011)

#define RPI_IO_REG(base)                *((volatile uint32_t *)(base))
#define RPI_IO_REG_INDEX(base, index)   *((volatile uint32_t *)(base)+(index))

#define RPI_IO_SET(io, data)            (RPI_IO_REG((io->gpio) + BCM_GPSET0) = (data))
#define RPI_IO_CLR(io, data)            (RPI_IO_REG((io->gpio) + BCM_GPCLR0) = (data))

#define RPI_IO_SET_PIN(io, pin)         RPI_IO_SET(io, 1 << (pin))
#define RPI_IO_CLR_PIN(io, pin)         RPI_IO_CLR(io, 1 << (pin))

#define RPI_IO_READ(io)                 (RPI_IO_REG((io->gpio) + BCM_GPLEV0))

#define RPI_IO_WRITE(io, data, mask)  do { \
    RPI_IO_CLR(io, (mask)); \
    RPI_IO_SET(io, (data) & (mask)); \
} while (0)

#define RPI_IO_EDGE_FALLING    (0)
#define RPI_IO_EDGE_RISING     (1)

typedef struct rpi_io_s{
    void *gpio;
} rpi_io_t;

rpi_io_t *rpi_io_open(void);
void rpi_io_close(rpi_io_t *io);
void rpi_io_set_pin_mode(rpi_io_t *io, int pin, int mode);
void rpi_io_set_pins_mode(rpi_io_t *io, uint32_t mask, int mode);
void rpi_io_busywait_ns(long delay_ns);
int rpi_io_interrupt_open(int pin, int edge);
void rpi_io_interrupt_clear(int fd);
int rpi_io_interrupt_flush(int fd);

 
#endif // RPI_IO_H
