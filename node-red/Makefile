#
# node-red Makefile
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

INST_DIR = /usr/local/bin

NR_ROOT = ~/.node-red
NR_DIR = $(NR_ROOT)/nodes/core/hardware

NR_FILES = 35-touchbridge.html 35-touchbridge.js icons

install:
	mkdir -p $(NR_DIR)
	cp -r $(NR_FILES) $(NR_DIR)
	cd $(NR_DIR) && ln -sf $(INST_DIR)/tbg_client .

