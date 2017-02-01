/*
 * Copyright (c) 2017 Ilya Zhuravlev
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#pragma once

#include <inttypes.h>
#include <stdlib.h>

typedef struct {
  uint32_t len;
  uint32_t cmd;
  uint32_t unk1;
  uint32_t unk2;
  uint8_t pad1[48];
  uint8_t data[0x1000-64];
} command_t;

typedef union {
  struct {
    uint32_t unk1;
    void *rvk_paddr_list;
  } rvk_init;
  struct {
    void *paddr;
    size_t len;
  } buf_0x80_init;
  struct {
    size_t num_paddrs;
    void *paddr_list;
    void *buf_0x40; // field_30 in sm_handle
    int field_18;
    int ctx_0x4;
    int ctx_0x8;
    int ctx_0xC;
    int pad_unk;
    int field_60;
    int partition_id;
    uint64_t auth_id;
    uint64_t part_0xF00C;
    uint64_t part_0xFFFF;
    uint64_t part_unk;
    uint64_t part_unk2;
  } sm;
  struct {
    size_t num_paddrs;
    void *paddr_list;
    void *buf_0x40;
    void *delayed_cmd;
  } sm2;
  unsigned char raw[0x100];
} shared_buffer_t;

typedef struct {
  volatile int state;
  int field_34;
  void *suspendbuf;
  void *suspendbuf_plist;
  int suspendbuf_plist_cnt;
  uint32_t *some_0x40_buffer;
  uint32_t *delayed_cmd;
  uint64_t auth_id;
  uint64_t part_0xF00C;
  uint64_t part_0xFFFF;

  int num_paddrs;
  uint32_t *paddr_list;

  int field_60;
  int partition_id;

  int field_28;

  volatile int cry123;
} task_t;

enum {
	F00D_REG_BASE = 0xE0000000,
	DELAYED_CMD_SIZE = 0x40,
	SUSPENDBUF_SIZE = 0x20000,
};

void smsched_init(void);
task_t *smsched_create_task(void *sm, size_t sm_size, int partition_id);
int smsched_load_task(task_t *t);
int smsched_suspend_current_task(void);
int smsched_submit_command(command_t *cmd);
