/*
 *
 * cdev.c
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


#include "cdev.h"

static cdev_t cdevs[CDEV_NUMOF];
 
void cdev_init(int fd, void (*tx_enable_fn)(cdev_t *cdev), void *device)
{
    /* initialise ring buffer */
    cdevs[fd].tx_in = 0;
    cdevs[fd].tx_out = 0;
    cdevs[fd].rx_in = 0;
    cdevs[fd].rx_out = 0;
    cdevs[fd].tx_enable_fn = tx_enable_fn;
    cdevs[fd].device = device;
}

void *cdev_device(int fd)
{
    return cdevs[fd].device;
}

cdev_t *cdev_p(int fd)
{
    return cdevs+fd;
}

void cdev_put(int fd, char c)
{
    uint8_t tmp1, tmp2;

    tmp1 = cdevs[fd].tx_in;
    tmp2 = tmp1 + 1;
    tmp2 %= CDEV_BUF_SIZE;
    while (cdevs[fd].tx_out == tmp2);
    cdevs[fd].txbuf[tmp1] = c;
    cdevs[fd].tx_in = tmp2;
    cdevs[fd].tx_enable_fn(cdevs+fd);
}

int cdev_write(int fd, const uint8_t *buf, int size)
{
    int i;

    for (i = 0; i < size; i++) {
        cdev_put(fd, buf[i]);
    }
    return i;
}

int cdev_read(int fd, uint8_t *buf, int size)
{
    int i;

    for (i = 0; i < size; i++) {
        buf[i] = cdev_get(fd);
    }
    return i;
}

/*
 * Checks if data available for reading.
 * Returns non-zero if data available, zero if not.
 */
char cdev_check_read(int fd)
{
    return (cdevs[fd].rx_in != cdevs[fd].rx_out);
}

char cdev_get(int fd)
{
    char c;
    uint8_t tmp;

    tmp = cdevs[fd].rx_out;
    /* wait for data to arrive */
    while (cdevs[fd].rx_in == tmp);

    c = cdevs[fd].rxbuf[tmp++];

    cdevs[fd].rx_out = tmp % CDEV_BUF_SIZE;

    return c;
}

void cdev_rx(cdev_t *cd, uint8_t c)
{
    uint8_t tmp;

    tmp = cd->rx_in;
    cd->rxbuf[tmp++] = c;
    tmp %= CDEV_BUF_SIZE;

    /* if buffer is full, overwrite old data first */
    if (cd->rx_out == tmp) {
        cd->rx_out = (tmp + 1) % CDEV_BUF_SIZE;
    }
    cd->rx_in = tmp;
}

int16_t cdev_tx(cdev_t *cd)
{
    uint8_t c;

    if (cd->tx_in != cd->tx_out) {
        c = cd->txbuf[cd->tx_out++];
        cd->tx_out %= CDEV_BUF_SIZE;
        return c;
    } else {
        return -1;
    }
}

