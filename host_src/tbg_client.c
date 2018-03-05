/*
 *
 * tbg_client.c
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

#define _BSD_SOURCE
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <inttypes.h>
#include <sys/time.h>

#include <glib.h>

#include "debug.h"

#include "tbg_api.h"
#include "tbg_util.h"

#define MAX_LINE_LENGTH (80)


// For debug.h
char *progname;
int debug_level = 0;

int get_int(char *s, int *x)
{
    char *end;
    int tmp = strtol(s, &end, 0);
    if (end > s) {
        *x = tmp;
        return 1;
    } else {
        ERROR("Failed to parse \"%s\" as integer.", s);
        return 0;
    }
}


#define RESP_BUF_SIZE   (256)

// Get a line from stdin and parse up to nargs values as integers.
int get_input(tbg_socket_t *tsock, int nargs, ...)
{
    char line[MAX_LINE_LENGTH];
    va_list ap;
    unsigned char recv_buf[1024];
    buf_t buf;

    buf_init_from_static(&buf, recv_buf, 1024);

    /*
     * Discard incomming Touchbridge messages while waiting for input in stdin.
     */
    while (1) {
        struct pollfd items[] = {
            { .fd = tsock->sock,  .events = POLLIN, .revents = 0 },
            { .fd = STDIN_FILENO, .events = POLLIN, .revents = 0 },
        };
        int ret = poll(items, 2, -1);
        SYSERROR_IF(ret < 0, "poll");
        if (items[0].revents & POLLIN) {
            // Got a tbg message
            int ret = buf_recv(tsock->sock, &buf, 0);
            if (ret < 0) {
                SYSERROR("%s: read from %d", __FUNCTION__, tsock->sock);
            }
            while (netbuf_decode(tsock->nb, &buf) > 0);

        }
        if (items[1].revents & POLLIN) {
            // Got something on stdin.
            PRINTD(3, "got data on stdin\n");
            break;
        }
    }


    char * ret = fgets(line, MAX_LINE_LENGTH, stdin);
    if (ret == NULL)
        return 0;

    va_start(ap, nargs);
    char *start = line, *end;
    int  x;
    x = strtol(start, &end, 0);
    int i = 0;
    for (i = 0; (end != start) && i < nargs; i++) {
        *va_arg(ap, int *) = x;
        start = end;
        while (isblank(*start) || *start == ',') start++;
        if (*start == 0) break;
        x = strtol(start, &end, 0);
    }
    va_end(ap);
    return i;
}

void tbgc_req_init(tbg_msg_t *msg, uint8_t addr, uint8_t port, uint8_t len)
{
    msg->id = 0;
    TBG_MSG_SET_DST_PORT(msg, port);
    TBG_MSG_SET_DST_ADDR(msg, addr);
    TBG_MSG_SET_TYPE(msg, TBG_MSG_TYPE_REQ);
    msg->len = len;
}

void tbgc_msg_set32(tbg_msg_t *msg, uint8_t offset, uint32_t data)
{
    uint32_t *dptr = (void *)(msg->data + offset);
    dptr[0] = data;
}

uint32_t tbgc_msg_get32(tbg_msg_t *msg, uint8_t offset)
{
    uint32_t *dptr = (void *)(msg->data + offset);
    return dptr[0];
}

void tbgc_data_set32(uint8_t *data, uint32_t value, uint8_t offset)
{
    data[offset+0] = (value >>  0) & 0xff;
    data[offset+1] = (value >>  8) & 0xff;
    data[offset+2] = (value >> 16) & 0xff;
    data[offset+3] = (value >> 24) & 0xff;
}

int dout_cmd(int argc, char **argv, tbg_socket_t *tsock)
{
    int ret;
    uint8_t mask, value;
    int node = atoi(argv[1]);
    tbg_port_t *port = tbg_port_open(tsock, node, 8);
    int pin = atoi(argv[2]);
    if (pin < 1 || pin > 8) 
        ERROR("pin number \"%s\" out of bounds", argv[2]);
    pin--;
    mask = 1 << pin;
    if (argc > 3) {
        // Get value from cmd line, send it & exit
        value = atoi(argv[3]);
        value = (value) ? 0xff : 0;
        tbg_dout(port, value, mask);
    } else {
        // Get values from stdin & send them until EOF.
        ret = get_input(tsock, 1, &value);
        while (!feof(stdin)) {
            if (ret > 0) {
                // Value is boolean.
                value = (value) ? 0xff : 0;
                tbg_dout(port, value, mask);
            } else {
                fprintf(stderr, "Expected numeric value on stdin (ctrl-d to exit.)\n");
            }
            ret = get_input(tsock, 1, &value);
        }
    }
    tbg_port_close(port);
    return 0;
}

void dout_usage(char *name)
{
    printf("usage: %s %s node pin_number [value]\n", progname, name);
}

int dout_cmd2(int argc, char **argv, tbg_socket_t *tsock)
{
    int ret;
    int mask = 0;
    int value;
    int node = atoi(argv[1]);
    int portnum = 8;
    tbg_port_t *port = tbg_port_open(tsock, node, portnum);
    get_int(argv[2], &mask);
    if (argc > 3) {
        // Get value from cmd line, send it & exit
        get_int(argv[3], &value);
        PRINTD(2, "node %d, port %d, value 0x%02X, mask 0x%02X\n", port->addr, port->port, value, mask);
        tbg_dout(port, value, mask);
    } else {
        // Get values from stdin & send them until EOF.
        ret = get_input(tsock, 1, &value);
        while (!feof(stdin)) {
            if (ret > 0) {
                // Value is boolean.
                PRINTD(2, "node %d, port %d, value 0x%02X, mask 0x%02X\n", port->addr, port->port, value, mask);
                tbg_dout(port, value, mask);
            } else {
                fprintf(stderr, "Expected numeric value on stdin (ctrl-d to exit.)\n");
            }
            ret = get_input(tsock, 1, &value);
        }
    }
    tbg_port_close(port);
    return 0;
}

void dout_cmd2_usage(char *name)
{
    printf("usage: %s %s node mask [value]\n", progname, name);
}

int din_cmd(int argc, char **argv, tbg_socket_t *tsock)
{
    tbg_msg_t msg;

    int addr = atoi(argv[1]);
    int portnum = 8;
    tbg_port_t *port = tbg_port_open(tsock, addr, portnum);
    int pin = atoi(argv[2]);
    if (pin < 1 || pin > 8) 
        ERROR("pin number \"%s\" out of bounds", argv[2]);
    pin--;
    uint32_t mask = 1 << pin;
    // Ensure rising & falling event masks are set
    // This also serves a second purpose which is to 
    // tell the server we're here.
    // TODO: think of a better way, so we can just set the
    // mask bits we want but without read-modify-write races when dealing
    // with multiple clients. For now, we set the mask for all bits.
    uint32_t conf_data = 0xff;
    tbg_port_conf_write(port, TBG_PORTCONF_CMD_DIN_EV_RISING_EN_MSK,  (uint8_t *)&conf_data, sizeof(conf_data));
    tbg_port_conf_write(port, TBG_PORTCONF_CMD_DIN_EV_FALLING_EN_MSK, (uint8_t *)&conf_data, sizeof(conf_data));
    uint8_t debounce_time = 25;
    tbg_port_conf_write(port, TBG_PORTCONF_CMD_DIN_EV_DEBOUNCE_TIME, (uint8_t *)&debounce_time, sizeof(debounce_time));

    while (1) {
        tbg_port_wait_msg(port, TBG_MSG_TYPE_IND, TBG_TIMEOUT_FOREVER, &msg);
        uint32_t events = msg.data32[0];
        uint32_t state = msg.data32[1];
        if (events & mask) {
            //printf("mask = 0x%08X, events = 0x%08X, state = 0x%08X\n", mask, events, state);
            printf("%c\n", (state & mask) ? '1' : '0');
            fflush(stdout);
        }
    }
    tbg_port_close(port);
    return 0;
}

void din_usage(char *name)
{
    printf("usage: %s %s node pin\n", progname, name);
}

int aout_cmd(int argc, char **argv, tbg_socket_t *tsock)
{
    int ret;
    int value;
    int node = atoi(argv[1]);
    int portnum = 11;
    tbg_port_t *port = tbg_port_open(tsock, node, portnum);
    int pin = atoi(argv[2]);
    if (pin < 1 || pin > 8) 
        ERROR("pin number \"%s\" out of bounds", argv[2]);
    pin--;
    if (argc > 3) {
        // Get value from cmd line, send it & exit
        value = atoi(argv[3]);
        tbg_aout(port, pin, value);
    } else {
        // Get values from stdin & send them until EOF.
        ret = get_input(tsock, 1, &value);
        while (!feof(stdin)) {
            if (ret > 0) {
                tbg_aout(port, pin, value);
            } else {
                fprintf(stderr, "Expected numeric value on stdin (ctrl-d to exit.)\n");
            }
            ret = get_input(tsock, 1, &value);
        }
    }
    tbg_port_close(port);
    return 0;
}

void aout_usage(char *name)
{
    printf("usage: %s node pin [value]\n", name);
}

int ain_cmd(int argc, char **argv, tbg_socket_t *tsock)
{
    return 0;
}

void ain_usage(char *name)
{
    printf("Not implemented yet\n");
    printf("usage: %s node pin\n", name);
}

int tbg_cmd(int argc, char **argv, tbg_socket_t *tsock)
{
    tbg_msg_t resp;
    uint8_t msgdata[8];

    int argptr = 1;
    uint8_t dst_addr = strtol(argv[argptr++], NULL, 0);
    uint8_t dst_port = strtol(argv[argptr++], NULL, 0);

    int j =0;
    for (int i = argptr; i < argc; i++) {
        char *s = argv[i];
        int len = strlen(s);
        unsigned long long data = strtoll(s, NULL, 0);
        void *p = &data;
        int width = 1;
        if (s[0] == '0' && s[1] == 'x') {
            width = (len-1)/2;
        }
        if ((j + width) >= 8) {
            width = 8-j;
            if (width <= 0) {
                break;
            }
        }
        memcpy(msgdata+j, p, width);
        j += width;
    }
    int ret = tbg_request(tsock, dst_addr, dst_port, msgdata, j, &resp);
    if (!ret) {
        printf("Timeout\n");
    } else {
        tbg_msg_dump(&resp);
    }
    return 0;
}

void tbg_usage(char *name)
{
    printf("usage: %s %s dst_addr dst_port [data]\n", progname, name);
}


int adisc2_cmd(int argc, char ** argv, tbg_socket_t *tsock)
{
    GArray *nodes = tbg_adisc_and_assign(tsock);
    if (nodes == NULL || nodes->len == 0) {
        WARNING("no nodes found\n");
        return 1;
    }
    tbg_print_nodes_json(nodes);
    return 0;
}


void adisc_usage(char *name)
{
    printf("usage: %s %s\n", progname, name);
}

int getstr_cmd(int argc, char **argv, tbg_socket_t *tsock)
{
    char s[256];
    s[0] = '\0';
    uint8_t conf_port = 0;
    int argptr = 1;
    uint8_t addr = strtol(argv[argptr++], NULL, 0);
    uint8_t conf_cmd = strtol(argv[argptr++], NULL, 0) & TBG_CONF_BITS_CMD;
    if (argc > 3) {
        conf_port = strtol(argv[argptr++], NULL, 0);
    }
    if (conf_cmd == TBG_PORTCONF_CMD_COM_GET_CONF_DESCR) {
        if (argc <= 4) {
            printf("usage: %s getstr addr 2 port conf_num\n", progname);
            return 1;
        }
        uint8_t conf_num = strtol(argv[argptr++], NULL, 0);
        tbg_get_conf_descr(tsock, addr, conf_num, conf_port, s);
    } else {
        if (argc > 3) {
            tbg_get_conf_string(tsock, addr, conf_cmd | TBG_CONF_BIT_PORT, conf_port, s);
        } else {
            tbg_get_conf_string(tsock, addr, conf_cmd, 0, s);
        }
    }

    if (s[0]) {
        printf("%s\n", s);
    }
    return 0;
}

void getstr_usage(char *name)
{
    printf("usage: %s %s addr conf [port]\n", progname, name);
    printf("usage: %s %s addr 2 port conf_num\n", progname, name);
}

int info_cmd(int argc, char **argv, tbg_socket_t *tsock)
{
    uint8_t addr = strtol(argv[1], NULL, 0);

    tbg_node_info_t *node = tbg_node_info(tsock, addr);

    if (!node) { printf("Timeout\n"); return 1; }

    printf("{ \"addr\":%d, \"id\":\"0x%012" PRIX64 "%012" PRIX64 "\", \"product\":%s }\n", node->addr, node->id_msw, node->id_lsw, node->product_id);

    tbg_node_info_free(node);
    return 0;
}

void info_usage(char *name)
{
    printf("usage: %s %s addr\n", progname, name);
}


typedef int (inv_fn_t)(int argc, char **argv, tbg_socket_t *tsock);
typedef void (usage_fn_t)(char *name);

typedef struct command_s {
    const char *name;
    inv_fn_t *fn;
    int min_args;
    usage_fn_t *usage;
} command_t;

const command_t commands[] = {
    {  "dout", dout_cmd, 2, dout_usage },
    {  "dout2", dout_cmd2, 2, dout_cmd2_usage },
    {  "din", din_cmd, 2, din_usage },
    {  "aout", aout_cmd, 2, aout_usage },
    {  "ain", ain_cmd, 2, ain_usage },
    {  "tbg", tbg_cmd, 2, tbg_usage },
    {  "adisc", adisc2_cmd, 0, adisc_usage },
    {  "adisc2", adisc2_cmd, 0, adisc_usage },
    {  "getstr", getstr_cmd, 2, getstr_usage },
    {  "info", info_cmd, 1, info_usage },
};

#define N_COMMANDS (sizeof(commands)/sizeof(command_t))

char *server_addr = "tcp://127.0.0.1:5555";
int iflag = 0;

static GOptionEntry cmd_line_options[] = {
    { "server",      's', 0, G_OPTION_ARG_STRING, &server_addr, "Set server address to A (e.g. tcp://localhost:5555)", "A" },
    { "debug-level", 'd', 0, G_OPTION_ARG_INT,    &debug_level, "Set debug level to d", "d" },
    { "interactive", 'i', 0, G_OPTION_ARG_NONE,   &iflag, "Interactive mode - reads commands from stdin", NULL },
    { NULL }
};

/*
 * Pull an argv out of argv and shift it down, preserving argv[0].
 */
char *shift_args(int *argc, char **argv)
{
    if (*argc > 1) {
        char *ret = argv[1];
        int i;
        for (i = 1; i < (*argc - 1); i++) {
            argv[i] = argv[i+1];
        }
        *argc = i;
        return ret;
    } else {
        return NULL;
    }
}


int run_command(int argc, char **argv, tbg_socket_t *tsock)
{
    int ret = 1;

    char *cmd = shift_args(&argc, argv);
    if (!cmd) {
        ERROR("usage: %s command [args]\n", argv[0]);
    }

    int i;
    for (i = 0; i < N_COMMANDS; i++) {
        const command_t *inv = commands + i;
        if (strcmp(cmd, inv->name) == 0) {
            if (argc <= inv->min_args) {
                inv->usage(cmd);
            } else {
                ret = inv->fn(argc, argv, tsock);
            }
            break;
        }
    }
    if (i == N_COMMANDS) {
        ERROR("Incorrect command \"%s\".", cmd);
    }
    return ret;
}

/*
 * Split-out whitespace-separated tokens from a string.
 * Unlike g_strplit, etc, we treat multiple contiguous
 * whitespace characters as a single separator.
 */
GPtrArray *strsplit(char *str)
{
    GPtrArray *args = g_ptr_array_new();
    char *stringp = str;
    char *arg;
    arg = strsep(&stringp, " \t"); 
    while (arg) {
        if (*arg != '\0') {
            g_ptr_array_add(args, arg);
        }
        arg = strsep(&stringp, " \t"); 
    }
    // Add sentinel value
    g_ptr_array_add(args, NULL);
    return args;
}

/*
 * Read lines from stdin and interpret them in the same way as
 * the commnd line parameters for the same function. This
 * allows 'interactive' operation.
 */
int interactive(tbg_socket_t *tsock)
{
    char *linebuf = NULL;
    size_t linebufsize = 0;
    int ret;

    ret = getline(&linebuf, &linebufsize, stdin);
    while (ret >= 0) {
        // Add a substitute for the normal argv[0] program name
        char *cmdline = g_strconcat("<stdin> ", linebuf, NULL);
        //char **line_argv = g_strsplit_set(cmdline, " \t", -1);
        GPtrArray *args = strsplit(cmdline);
        run_command(args->len, (char **)args->pdata, tsock);
        g_ptr_array_free(args, FALSE);
        g_free(cmdline);
        // Get next line
        ret = getline(&linebuf, &linebufsize, stdin);
    }
    return 0;
}

int main (int argc, char **argv)
{
    int ret;
    progname = argv[0]; // This is used by ael-debug.h

    tbg_socket_t *tsock;

    GError *error = NULL;
    GOptionContext *context = g_option_context_new("- Touchbridge Command Line Client");
    g_option_context_add_main_entries (context, cmd_line_options, NULL);
    if (!g_option_context_parse (context, &argc, &argv, &error)) {
        ERROR("option parsing failed: %s\n", error->message);
    }

    tbg_init();
    tsock = tbg_open(server_addr);
    SYSERROR_IF(tsock == NULL, "tbg_socket: %s", server_addr);

    if (iflag) {
        ret = interactive(tsock);
    } else {
        ret = run_command(argc, argv, tsock);
    }

    usleep(10000);
    tbg_close(tsock);
    return ret;
}
