#
# Top level Makefile
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

all: host

host:
	$(MAKE) -C host_src


install: host
	sudo $(MAKE) -C host_src install
	$(MAKE) -C node-red install

clean:
	$(MAKE) -C host_src clean
	$(MAKE) -C firmware_src clean
