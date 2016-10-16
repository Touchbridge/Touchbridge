#
# Makefile
#
# This file is part of Touchbridge
#
# Copyright 2015 James L Macfarlane
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# 

INSTDIR=/usr/local/bin/
INSTFILES=tbg_client tbg_server
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    #CPPFLAGS += -D LINUX
    TARGETS=tbg_server tbg_client
    LRT=-lrt
endif
ifeq ($(UNAME_S),Darwin)
    #CPPFLAGS += -D OSX
    CPPFLAGS += -I/usr/local/include
    LDFLAGS += -L/usr/local/lib
    TARGETS=tbg_client netbuf_srv netbuf_cli
endif

CPPFLAGS_GTK = `pkg-config --cflags glib-2.0`
CPPFLAGS += $(CPPFLAGS_GTK)
LOADLIBES_GTK = `pkg-config --libs glib-2.0`
LOADLIBES += $(LOADLIBES_GTK)
LOADLIBES += -lzmq -lczmq $(LRT)
#CFLAGS = -Wall -g -std=c99 -O
CFLAGS = -Wall -g -std=c99

# Generate dependency information
CFLAGS += -MMD -MP -MF .dep/$(@F).d

all: $(TARGETS)

tbg_server: tbg_server.o rpi_io.o rpi_int.o tbg_rpi.o tbg_util.o clients.o server_socket.o

tbg_client: tbg_client.o tbg_util.o tbg_api.o

netbuf_srv: netbuf_srv.o netbuf.o server_socket.o netcon.o

netbuf_cli: netbuf_cli.o netbuf.o netcon.o


clean:
	rm -rf .dep/* $(TARGETS) *.o

install:
	cp $(INSTFILES) $(INSTDIR)


# Include the dependency files, should be the last of the makefile
#
-include $(shell mkdir .dep 2>/dev/null) $(wildcard .dep/*)
