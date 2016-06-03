/*
 *
 * cdev.h
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
#ifndef CDEV_H
#define CDEV_H

#include <stdint.h>

#include "setup.h"

/*
 * Character device buffered asynchronous i/o module.
 *
 * James L Macfarlane 2008.
 */

#ifndef CDEV_NUMOF
    #define CDEV_NUMOF (2)
    #warning "CDEV_NUMOF not defined - defaulting to 2"
#endif

#ifndef CDEV_BUF_SIZE
    #define CDEV_BUF_SIZE (64)
    #warning "CDEV_BUF_SIZE not defined - defaulting to 64"
#endif


/*
 * The code in this module only works if the buf_in / buf_out indices
 * are 8-bit wide. Larger widths will not work because they
 * cannot be read into registers in an atomic manner - an
 * interrupt may occur between reading the high byte and the low
 * byte and ruin our day.
 */
typedef struct cdev_s {
    volatile uint8_t tx_in;
    volatile uint8_t tx_out;
    volatile uint8_t rx_in;
    volatile uint8_t rx_out;
    void (*tx_enable_fn)(struct cdev_s *cdev);
    uint8_t rxbuf[CDEV_BUF_SIZE];
    uint8_t txbuf[CDEV_BUF_SIZE];
    void *device;
} cdev_t;

typedef void (cdev_fn_t)(cdev_t *);

void cdev_init(int fd, void (*tx_enable_fn)(cdev_t *cdev), void *device);
void *cdev_device(int fd);
cdev_t *cdev_p(int fd);
void cdev_put(int fd, char data);
int cdev_write(int fd, const uint8_t *buf, int size);
int cdev_read(int fd, uint8_t *buf, int size);
char cdev_get(int fd);
char cdev_check_read(int fd);
void cdev_rx(cdev_t *cd, uint8_t c);
int16_t cdev_tx(cdev_t *cdev);

#endif /* CDEV_H */
