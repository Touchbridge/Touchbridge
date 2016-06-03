/*
 *
 * tbg_util.h
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
#ifndef TBG_UTIL_H
#define TBG_UTIL_H

#include "tbg_protocol.h"
 
void tbg_msg_dump(tbg_msg_t *msg);
int tbg_msg_from_hex(tbg_msg_t *msg, char *hex);
int tbg_msg_to_hex(tbg_msg_t *msg, char *hex);

#endif // TBG_UTIL_H
