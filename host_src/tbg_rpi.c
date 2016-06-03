/*
 *
 * tbg_rpi.c
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

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "rpi_io.h"
#include "tbg_rpi.h"

static inline void tbgrpi_bus_out(tbgrpi_t *tpi)
{
    rpi_io_set_pins_mode(tpi->gpio, TBGRPI_PINS_DBUS, RPI_IO_MODE_OUT);
}

static inline void tbgrpi_bus_in(tbgrpi_t *tpi)
{
    rpi_io_set_pins_mode(tpi->gpio, TBGRPI_PINS_DBUS, RPI_IO_MODE_IN);
}

static inline void tbgrpi_bus_write(tbgrpi_t *tpi, uint8_t data)
{
    // Set bus direction for writing
    tbgrpi_bus_out(tpi);
    // Set WRSEL for writing
    RPI_IO_SET_PIN(tpi->gpio, TBGRPI_PIN_WRSEL);
    // Put data on the bus
    RPI_IO_WRITE(tpi->gpio, (uint32_t)data << TBGRPI_PIN_D0, TBGRPI_PINS_DBUS);
    // Assert enable line
    RPI_IO_CLR_PIN(tpi->gpio, TBGRPI_PIN_EN);
    // Wait for ACK to go low (assert)
    while (RPI_IO_READ(tpi->gpio) & (1 << TBGRPI_PIN_ACK));
    // De-assert enable line
    RPI_IO_SET_PIN(tpi->gpio, TBGRPI_PIN_EN);
    // Wait for ACK to go high
    while (!(RPI_IO_READ(tpi->gpio) & (1 << TBGRPI_PIN_ACK)));
}

static inline uint8_t tbgrpi_bus_read(tbgrpi_t *tpi)
{
    uint8_t data;

    // Set bus direction for reading
    tbgrpi_bus_in(tpi);
    // Set WRSEL for writing
    RPI_IO_CLR_PIN(tpi->gpio, TBGRPI_PIN_WRSEL);
    // Assert enable line
    RPI_IO_CLR_PIN(tpi->gpio, TBGRPI_PIN_EN);
    // Wait for ACK to go low
    while (RPI_IO_READ(tpi->gpio) & (1 << TBGRPI_PIN_ACK));
    // Read the data
    data = (RPI_IO_READ(tpi->gpio) & TBGRPI_PINS_DBUS) >> TBGRPI_PIN_D0;
    // De-assert enable line
    RPI_IO_SET_PIN(tpi->gpio, TBGRPI_PIN_EN);
    // Wait for ACK to go high
    while (!(RPI_IO_READ(tpi->gpio) & (1 << TBGRPI_PIN_ACK)));

    return data;
}

void tbgrpi_init_io(tbgrpi_t *tpi)
{
    // Outputs
    rpi_io_set_pin_mode(tpi->gpio, TBGRPI_PIN_ADSEL, RPI_IO_MODE_OUT);
    rpi_io_set_pin_mode(tpi->gpio, TBGRPI_PIN_WRSEL, RPI_IO_MODE_OUT);
    rpi_io_set_pin_mode(tpi->gpio, TBGRPI_PIN_EN, RPI_IO_MODE_OUT);

    // Inputs
    rpi_io_set_pin_mode(tpi->gpio, TBGRPI_PIN_ACK, RPI_IO_MODE_IN);
    rpi_io_set_pin_mode(tpi->gpio, TBGRPI_PIN_INT, RPI_IO_MODE_IN);

    // Data bus (bi-dir but we make it an input for now.)
    tbgrpi_bus_in(tpi);

    // Default pin states
    RPI_IO_SET_PIN(tpi->gpio, TBGRPI_PIN_EN);   // De-assert enable line
    RPI_IO_CLR_PIN(tpi->gpio, TBGRPI_PIN_WRSEL);
    RPI_IO_CLR_PIN(tpi->gpio, TBGRPI_PIN_ADSEL);
}

tbgrpi_t *tbgrpi_open(void)
{
    tbgrpi_t *tpi = malloc(sizeof(tbgrpi_t));
    tpi->gpio = rpi_io_open();
    return tpi;
}

void tbgrpi_close(tbgrpi_t *tpi)
{
    rpi_io_close(tpi->gpio);
    free(tpi);
}

void tbgrpi_write_config(tbgrpi_t *tpi, uint8_t data)
{
    RPI_IO_SET_PIN(tpi->gpio, TBGRPI_PIN_ADSEL);
    tbgrpi_bus_write(tpi, data);
}

void tbgrpi_write_data(tbgrpi_t *tpi, uint8_t *data, int size)
{
    RPI_IO_CLR_PIN(tpi->gpio, TBGRPI_PIN_ADSEL);
    for (int i = 0; i < size; i++) {
        tbgrpi_bus_write(tpi, data[i]);
    }
}

uint8_t tbgrpi_read_status(tbgrpi_t *tpi)
{
    RPI_IO_SET_PIN(tpi->gpio, TBGRPI_PIN_ADSEL);
    return tbgrpi_bus_read(tpi);
}

void tbgrpi_read_data(tbgrpi_t *tpi, uint8_t *data, int size)
{
    RPI_IO_CLR_PIN(tpi->gpio, TBGRPI_PIN_ADSEL);
    for (int i = 0; i < size; i++) {
        data[i] = tbgrpi_bus_read(tpi);
    }
}

void tbgrpi_send_msg(tbgrpi_t *tpi, tbg_msg_t *msg)
{
    uint8_t *data = (void *)msg;
    RPI_IO_CLR_PIN(tpi->gpio, TBGRPI_PIN_ADSEL);
    for (int i = 0; i < TBG_MSG_SIZE; i++) {
        tbgrpi_bus_write(tpi, data[i]);
    }
}

void tbgrpi_recv_msg(tbgrpi_t *tpi, tbg_msg_t *msg)
{
    uint8_t *data = (void *)msg;
    RPI_IO_CLR_PIN(tpi->gpio, TBGRPI_PIN_ADSEL);
    for (int i = 0; i < TBG_MSG_SIZE; i++) {
        data[i] = tbgrpi_bus_read(tpi);
    }
}

