/*
 *
 * uart.h
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

#ifndef UART_H
#define UART_H

#include <stdint.h>

#define UART_NUM1   (0)
#define UART_NUM2   (1)
#define UART_NUM3   (2)
#define UART_NUM4   (3)
#define UART_NUM5   (4)
#define UART_NUMOF  (5)

void uart_init(int fd, int uart_num, uint16_t rate);

#endif /* UART_H */
