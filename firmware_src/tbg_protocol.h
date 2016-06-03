/*
 *
 * tbg_protocol.h
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
#ifndef TBG_PROTOCOL_H
#define TBG_PROTOCOL_H

#include <stdint.h>

#define TBG_PROTOCOL_VER_TYPE   (1)
#define TBG_PROTOCOL_VER_MAJOR  (1)
#define TBG_PROTOCOL_VER_MINOR  (0)

#define TBG_MSG_TYPE_RESP       (0)
#define TBG_MSG_TYPE_REQ        (1)
#define TBG_MSG_TYPE_ERR_RESP   (2)
#define TBG_MSG_TYPE_IND        (3)

#define TBG_MSG_CONT            (1)

#define TBG_ADDR_BROADCAST      (0)
#define TBG_ADDR_UNASSIGNED     (63)

typedef union {
    struct {
        uint32_t src        : 12;
        uint32_t dst        : 12;
        uint32_t state      : 1;
        uint32_t continued  : 1;
        uint32_t reserved   : 1;
        uint32_t type       : 2;
    };
  uint32_t raw;
} tbg_id_t;

typedef union {
    struct {
        uint32_t id         : 29;
        uint32_t eid        : 1;
        uint32_t rtr        : 1;
        uint32_t reserved   : 1;
    };
    uint32_t raw;
} tbg_can_id_t;

typedef struct __attribute__ ((__packed__)) tbg_msg_s {
    uint32_t id;
    uint8_t len;
    union {
        uint8_t data[8];
        uint16_t data16[4];
        uint32_t data32[2];
    };
} tbg_msg_t;

#define TBG_MSG_SIZE         (sizeof(tbg_msg_t))
#define TBG_HEX_MSG_SIZE     (TBG_MSG_SIZE*2 + 1)

typedef union {
    struct {
        uint16_t port : 6;
        uint16_t node : 6;
    };
    uint16_t raw;
} tbg_addr_t;

#define TBG_GET_ID_BITS(x, shift, mask)        (((x) >> (shift)) & (uint32_t)(mask))
#define TBG_SET_ID_BITS(x, shift, mask, value) ((x) = (((x) & ~((uint32_t)(mask) << (shift))) | (((uint32_t)(value) & (uint32_t)(mask)) << (shift))))

#define TBG_MSG_GET_SRC_PORT(msg)       (TBG_GET_ID_BITS((msg)->id,  0, 0x3f))
#define TBG_MSG_GET_SRC_ADDR(msg)       (TBG_GET_ID_BITS((msg)->id,  6, 0x3f))
#define TBG_MSG_GET_DST_PORT(msg)       (TBG_GET_ID_BITS((msg)->id, 12, 0x3f))
#define TBG_MSG_GET_DST_ADDR(msg)       (TBG_GET_ID_BITS((msg)->id, 18, 0x3f))
#define TBG_MSG_GET_STATE(msg)          (TBG_GET_ID_BITS((msg)->id, 24, 0x01))
#define TBG_MSG_GET_CONT(msg)           (TBG_GET_ID_BITS((msg)->id, 25, 0x01))
#define TBG_MSG_GET_RESVD(msg)          (TBG_GET_ID_BITS((msg)->id, 26, 0x01))
#define TBG_MSG_GET_TYPE(msg)           (TBG_GET_ID_BITS((msg)->id, 27, 0x03))
#define TBG_MSG_GET_EID(msg)            (TBG_GET_ID_BITS((msg)->id, 29, 0x01))
#define TBG_MSG_GET_RTR(msg)            (TBG_GET_ID_BITS((msg)->id, 30, 0x01))

#define TBG_MSG_SET_SRC_PORT(msg, x)    (TBG_SET_ID_BITS((msg)->id,  0, 0x3f, x))
#define TBG_MSG_SET_SRC_ADDR(msg, x)    (TBG_SET_ID_BITS((msg)->id,  6, 0x3f, x))
#define TBG_MSG_SET_DST_PORT(msg, x)    (TBG_SET_ID_BITS((msg)->id, 12, 0x3f, x))
#define TBG_MSG_SET_DST_ADDR(msg, x)    (TBG_SET_ID_BITS((msg)->id, 18, 0x3f, x))
#define TBG_MSG_SET_STATE(msg, x)       (TBG_SET_ID_BITS((msg)->id, 24, 0x01, x))
#define TBG_MSG_SET_CONT(msg, x)        (TBG_SET_ID_BITS((msg)->id, 25, 0x01, x))
#define TBG_MSG_SET_RESVD(msg, x)       (TBG_SET_ID_BITS((msg)->id, 26, 0x01, x))
#define TBG_MSG_SET_TYPE(msg, x)        (TBG_SET_ID_BITS((msg)->id, 27, 0x03, x))
#define TBG_MSG_SET_EID(msg, x)         (TBG_SET_ID_BITS((msg)->id, 29, 0x01, x))
#define TBG_MSG_SET_RTR(msg, x)         (TBG_SET_ID_BITS((msg)->id, 30, 0x01, x))

#define TBG_MSG_IS_ERR_RESP(msg)        (TBG_MSG_GET_TYPE(msg) == TBG_MSG_TYPE_ERR_RESP)

#define TBG_MSG_ID_BIT_EID              (1 << 29)
#define TBG_MSG_ID_BIT_RTR              (1 << 30)
#define TBG_MSG_ID_BITS_ID              (0x1fffffff)

#define TBG_MSG_IS_BROADCAST(msg)       (TBG_MSG_GET_DST_ADDR(msg) == TBG_ADDR_BROADCAST)

// Error Codes
#define TBG_ERR_NONE                    (0)
#define TBG_ERR_UNIMPLEMENTED           (1)
#define TBG_ERR_NO_PORT                 (2)
#define TBG_ERR_NO_CONF                 (3)
#define TBG_ERR_RDONLY                  (4)
#define TBG_ERR_WRONLY                  (5)
#define TBG_ERR_LENGTH                  (6)
#define TBG_ERR_RANGE                   (7)
#define TBG_ERR_VALUE                   (8)
#define TBG_ERR_FAULT                   (9)

#define TBG_ERROR_STRINGS {\
    "Success",\
    "Unimplemented",\
    "Port not available",\
    "Config not available",\
    "Read only",\
    "Write only",\
    "Incorrect message length",\
    "Out of range",\
    "Incorrect Value",\
    "Hardware Fault",\
}


// Ports common to all nodes
#define TBG_PORT_TSTRIGGER              (0)
#define TBG_PORT_ADISC                  (1)
#define TBG_PORT_CONFIG                 (2)
#define TBG_PORT_FAULTS                 (3)

// Base port number for device-specific ports
#define TBG_DEVICE_PORT_BASE            (8)

#define TBG_PORTS_MAX                   (64)

// Ports classes
enum {
    TBG_PORT_CLASS_NOT_PRESENT = 0,
    TBG_PORT_CLASS_COMMON,

    TBG_PORT_CLASS_ESTOP,
    TBG_PORT_CLASS_ALARMS,
    TBG_PORT_CLASS_RESERVED,
    TBG_PORT_CLASS_DIGITAL_IN,
    TBG_PORT_CLASS_DIGITAL_OUT,
    TBG_PORT_CLASS_ANALOGUE_IN,
    TBG_PORT_CLASS_ANALOGUE_OUT,
    TBG_PORT_CLASS_ANALOGUE_OUT_CH,
    TBG_PORT_CLASS_COUNTER_TIMER,
    TBG_PORT_CLASS_UART,
    TBG_PORT_CLASS_SPI,
    TBG_PORT_CLASS_I2C,
    TBG_PORT_CLASS_BUF8_READ,
    TBG_PORT_CLASS_BUF8_WRITE,
    TBG_PORT_CLASS_BUF18_READ,
    TBG_PORT_CLASS_BUF16_WRITE,
    TBG_PORT_CLASS_BUF_EXEC,
    TBG_PORT_CLASS_STEPPER,
    TBG_PORT_CLASS_MOTION_BUF,
    TBG_PORT_CLASS_IN32,
    TBG_PORT_CLASS_OUT32,
};

// Address Discovery Message Data, Request
#define TBG_ADISC_REQ_DATA_CMD          (0)
#define TBG_ADISC_REQ_DATA_ADDR         (1)
#define TBG_ADISC_REQ_DATA_ID0          (2)
#define TBG_ADISC_REQ_DATA_ID1          (3)
#define TBG_ADISC_REQ_DATA_ID2          (4)
#define TBG_ADISC_REQ_DATA_ID3          (5)
#define TBG_ADISC_REQ_DATA_ID4          (6)
#define TBG_ADISC_REQ_DATA_ID5          (7)

#define TBG_ADISC_REQ_LEN_MIN           (2)
#define TBG_ADISC_REQ_LEN_ID            (8)

// Address Discovery Message Data, Response
#define TBG_ADISC_RESP_DATA_ID0         (0)
#define TBG_ADISC_RESP_DATA_ID1         (1)
#define TBG_ADISC_RESP_DATA_ID2         (2)
#define TBG_ADISC_RESP_DATA_ID3         (3)
#define TBG_ADISC_RESP_DATA_ID4         (4)
#define TBG_ADISC_RESP_DATA_ID5         (5)
#define TBG_ADISC_RESP_DATA_SOFT_ADDR   (6)
#define TBG_ADISC_RESP_DATA_HARD_ADDR   (7)

#define TBG_NODE_ID_HALF_WORD_SIZE      (6)

// Address Discovery Command Bits
#define TBG_ADISC_BIT_RETURN_ID         (0x01)
#define TBG_ADISC_BIT_RETURN_ID_MSW     (0x02)
#define TBG_ADISC_BIT_MATCH_ID          (0x04)
#define TBG_ADISC_BIT_MATCH_ID_MSW      (0x08)
#define TBG_ADISC_BIT_ASSIG_ADDR        (0x10)
#define TBG_ADISC_BIT_SET_SHORTLIST     (0x20)
#define TBG_ADISC_BIT_CLR_SHORTLIST     (0x40)
#define TBG_ADISC_BIT_MATCH_SHORTLIST   (0x80)

// Global Configuration Commands
#define TBG_CONF_GLOBAL_NOP                 (0)
#define TBG_CONF_GLOBAL_PING                (1)
#define TBG_CONF_GLOBAL_PROTOCOL            (2)
#define TBG_CONF_GLOBAL_RESET               (3)
#define TBG_CONF_GLOBAL_ID_LSW              (4)
#define TBG_CONF_GLOBAL_ID_MSW              (5)
#define TBG_CONF_GLOBAL_HARD_ADDR           (6)
#define TBG_CONF_GLOBAL_BLINK_LEDS          (7)
#define TBG_CONF_GLOBAL_PRODUCT_ID_STR      (8)
#define TBG_CONF_GLOBAL_FIRMWARE_VER_STR    (9)
#define TBG_CONF_GLOBAL_USER_ID_STRING      (10)
#define TBG_CONF_GLOBAL_SAVE_USER_ID_STRING (11)

#define TBG_CONF_GLOBAL_NUMOF               (12)

#define TBG_CONF_REQ_LEN_MIN                (1)
#define TBG_PORTCONF_REQ_LEN_MIN            (2)

#define TBG_CONF_REQ_DATA_CMD               (0)
#define TBG_CONF_REQ_DATA_PORT              (1)

#define TBG_CONF_BITS_CMD                   (0x3f)
#define TBG_CONF_BIT_PORT                   (0x40)
#define TBG_CONF_BIT_WRITE                  (0x80)

// Fault flags
#define TBG_FAULT_OVERVOLT                  (0x0001)
#define TBG_FAULT_UNDERVOLT                 (0x0002)
#define TBG_FAULT_OVERCURRENT               (0x0004)
#define TBG_FAULT_OVERTEMP                  (0x0008)


// Port Config commands common to all ports
#define TBG_PORTCONF_CMD_COM_GET_CLASS      (0)
#define TBG_PORTCONF_CMD_COM_GET_DESCR      (1)
#define TBG_PORTCONF_CMD_COM_GET_CONF_DESCR (2)
#define TBG_PORTCONF_CMD_COM_EN_TSTRIGGER   (3)
#define TBG_PORTCONF_CMD_COM_RESERVED0      (4)
#define TBG_PORTCONF_CMD_COM_RESERVED1      (5)
#define TBG_PORTCONF_CMD_COM_RESERVED2      (6)
#define TBG_PORTCONF_CMD_COM_RESERVED3      (7)

#define TBG_PORTCONF_CMD_COM_NUMOF          (4)

#define TBG_DEVICE_PORTCONF_CMD_BASE        (8)

// Port Config commands for digital input class
#define TBG_PORTCONF_CMD_DIN_INPUT_PRESENT_MSK  (8)
#define TBG_PORTCONF_CMD_DIN_EV_RISING_EN_MSK   (9)
#define TBG_PORTCONF_CMD_DIN_EV_FALLING_EN_MSK  (10)
#define TBG_PORTCONF_CMD_DIN_EV_DEBOUNCE_EN_MSK (11)
#define TBG_PORTCONF_CMD_DIN_EV_DEBOUNCE_TIME   (12)

#endif // TBG_PROTOCOL_H
