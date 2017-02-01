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

#include "smsched.h"

#include <arch/ops.h>
#include <platform/interrupts.h>
#include <kernel/thread.h>
#include <kernel/spinlock.h>
#include <string.h>
#include <malloc.h>
#include <stdio.h>
#include <debug.h>
#include <platform/vita.h>
#include <kernel/semaphore.h>
#include <inttypes.h>
#include <trace.h>

#include "rvk.h"

static shared_buffer_t shared_buffer;
static uint8_t f00d_0x80_block[0x80];
static task_t *g_current_task;

static volatile unsigned int *regs = (void *)F00D_REG_BASE;

static void set_kernel_enp(void) {
  unsigned int x;

  void *enp_buf = (void *)(SCRATCH_BASE + 0x850000);
  TRACEF("wait 1...\n");
  while (!((x = regs[0]) & 0x100));
  TRACEF("wait 1 done: 0x%08X.\n", x);
  x &= 0x600;

  TRACEF("check 1...\n");
  if (x == 0x400) {
    TRACEF("check 1 failed.\n");
    return; // SCE_SBL_ERROR_SERV_ETIMEDOUT 0x800f0427
  }

  TRACEF("check 2...\n");
  if (x == 0x200) {
    TRACEF("check 2 special case\n");
    x = regs[0];
    regs[0] = x;
  } else if (x == 0x600) {
    regs[0x10000/4] = 1;
    regs[0x10000/4] = 0;

    TRACEF("wait 2...\n");
    while (regs[0x10000/4]);
    TRACEF("wait 2 done.\n");

    TRACEF("wait 3...\n");
    while ((regs[0x10004/4] & 0x80000000) == 0);
    TRACEF("wait 3 done.\n");

    TRACEF("set addr.\n");
    regs[0x10/4] = ((uint32_t)enp_buf & ~3) | 1;
    TRACEF("set addr done.\n");

    TRACEF("wait 4...\n");
    while (1) {
      x = regs[0];
      if (x == 2) {
        TRACEF("wait 4 failed\n"); // SCE_SBL_ERROR_SERV_ETIMEDOUT 0x800f0427
        return;
      } else if (x == 1) {
        regs[0] = 1;
        TRACEF("wait 4 done\n");
        break;
      }
    }
  } else {
    TRACEF("check 2 special case2 x = 0x%x\n", x);
  }

  TRACEF("wait 5\n");
  while ((uint16_t)regs[0] != 257);
  regs[0] = 257;
  TRACEF("wait 5 done\n");

  TRACEF("init shared_buffer\n");
  regs[0x10/4] = (uint32_t)&shared_buffer;
  regs[0x10/4] = 1;
  TRACEF("wait 6\n");
  while ((uint16_t)regs[0] != 258);
  regs[0] = 258;
  TRACEF("wait 6 done\n");
}

static void set_rvk(void *rvk_buf, size_t rvk_size) {
  TRACEF("f00d_set_rvk\n");

  uint32_t paddr_list[2] = { (uint32_t)rvk_buf, rvk_size };
  shared_buffer.rvk_init.unk1 = 1;
  shared_buffer.rvk_init.rvk_paddr_list = paddr_list;
  arch_clean_cache_range((addr_t)&shared_buffer, sizeof(shared_buffer));
  arch_clean_cache_range((addr_t)paddr_list, sizeof(paddr_list));
  arch_clean_cache_range((addr_t)rvk_buf, rvk_size);

  TRACEF("wait 1\n");
  regs[0x10/4] = 0x80A01;
  while (regs[0x10/4]);
  TRACEF("wait 1 done\n");

  TRACEF("wait 2\n");
  uint32_t ret = 0;
  do {
    ret = regs[0];
  } while(!(uint16_t)ret);
  regs[0] = ret;
  TRACEF("wait 2 done ret=0x%x\n", ret);
  if (ret & 0x8000)
    TRACEF("wait 2 error: 0x%x\n", 0x800F0300 | (ret & 0xFF));
}

static void set_0x80_block(void) {
  TRACEF("f00d_set_0x80_block\n");

  shared_buffer.buf_0x80_init.paddr = f00d_0x80_block;
  shared_buffer.buf_0x80_init.len = sizeof(f00d_0x80_block);
  arch_clean_cache_range((addr_t)f00d_0x80_block, sizeof(f00d_0x80_block));
  arch_clean_cache_range((addr_t)&shared_buffer, sizeof(shared_buffer));

  TRACEF("wait 1\n");
  regs[0x10/4] = 0x80901;
  while (regs[0x10/4]);
  TRACEF("wait 1 done\n");

  TRACEF("wait 2\n");
  uint32_t ret = 0;
  do {
    ret = regs[0];
  } while(!(uint16_t)ret);
  regs[0] = ret;
  TRACEF("wait 2 done ret=0x%x\n", ret);
  if (ret & 0x8000)
    TRACEF("wait 2 error: 0x%x\n", 0x800F0300 | (ret & 0xFF));
}

static enum handler_return cry2arm0(void *arg) {
  bool resched = false;

  TRACEF("cry2arm0\n");

  uint32_t ret = regs[0];
  TRACEF("ret = 0x%x\n", ret);
  if (ret & 0x40000) {
    regs[0] = 0x40000;
    return resched ? INT_RESCHEDULE : INT_NO_RESCHEDULE;
  } else if (ret & 0x20000) {
    regs[0] = 0x20000;
    TRACEF("(vita would kernel panic)\n");
    return resched ? INT_RESCHEDULE : INT_NO_RESCHEDULE;
  }

  if (ret & 0x10000) {
    ret = 0x10000;
  } else {
    ret = ret & 0xFFFF;
    if (!ret)
      TRACEF("(vita would kernel panic)\n");
  }
  regs[0] = ret;

  task_t *t = g_current_task;
  if (!t) {
    TRACEF("panic: current_task is null\n");
    return resched ? INT_RESCHEDULE : INT_NO_RESCHEDULE;
  }

  TRACEF("old state: %d => ", t->state);

  if (t->some_0x40_buffer)
    arch_invalidate_cache_range((addr_t)t->some_0x40_buffer, 0x40);

  switch (t->state) {
  case 12:
    if (ret == 0x10000) {
      t->state = 10;
      t->field_28 = t->some_0x40_buffer[0];
    } else if (ret == 0x108) {
      t->state = 9;
    } else {
      t->state = 3;
      t->field_28 = ret;
    }
    break;
  case 11:
    if (ret == 0x10000) {
      t->state = 10;
      t->field_28 = t->some_0x40_buffer[0];
    } else if (ret == 0x107) {
      t->state = 7;
    } else {
      t->state = 3;
      t->field_28 = ret;
    }
    break;
  case 10:
    if (ret == 0x103) {
      t->state = 3;
    } else {
      TRACEF("panic: unknown transition from state=11 with ret=0x%x\n", ret);
      return resched ? INT_RESCHEDULE : INT_NO_RESCHEDULE;
    }
    break;
  case 9:
    if (ret == 0x104) {
      t->state = 4;
    } else {
      t->state = 3;
      t->field_28 = ret;
    }
    break;
  case 2:
    if (ret == 0x10000) {
      t->state = 3;
    } else if (ret == 0x104) {
      t->state = 4;
    }
    break;
  case 1:
  case 3:
  case 4:
  case 5:
    g_current_task = NULL;
    break;
  case 7:
    if (ret == 0x104) {
      t->state = 1;
    } else {
      t->state = 3;
      t->field_28 = ret;
    }
    break;
  case 6:
    if (t->field_34 == 3) {
      if (t->suspendbuf) {
        free(t->suspendbuf);
        t->suspendbuf = NULL;
      }
      if (t->suspendbuf_plist) {
        free(t->suspendbuf_plist);
        t->suspendbuf_plist = NULL;
      }
      t->field_34 = 1;
    }
    if (ret == 0x10000) {
      t->state = 10;
      // some other BS
    } else if (ret == 0x103) {
      t->state = 2;
    } else {
      t->state = 3;
      t->field_28 = ret;
    }
    break;
  case 8:
    if (ret == 0x10000) {
      t->state = 10;
      t->field_28 = t->some_0x40_buffer[0];
    } else if (ret == 0x104) {
      t->state = 5;
    } else {
      t->state = 3;
      t->field_28 = ret;
    }
    break;
  }

  TRACEF("new state: %d\n", t->state);

  return resched ? INT_RESCHEDULE : INT_NO_RESCHEDULE;
}

static enum handler_return cry2arm123(void *argv) {
  uint32_t arg = (uint32_t)argv;
  bool resched = false;

  TRACEF("cry2arm123 %d\n", arg);

  // ack result
  uint32_t ret = regs[0x4/4];
  regs[0x4/4] = ret;

  // 1 = good
  // 3 = 0x800F0002 = SCE_SBL_ERROR_COMMON_ENOENT (invalid cmd id)
  // 5 = 0x800F0001 = SCE_SBL_ERROR_COMMON_EPERM (trying to pass secure memory as command buffer)
  // 9 = 0x800F0005 = SCE_SBL_ERROR_COMMON_EIO = ???

  TRACEF("ret = 0x%x\n", ret);

  if (g_current_task)
    g_current_task->cry123 = 1;

  return resched ? INT_RESCHEDULE : INT_NO_RESCHEDULE;
}

static void init_int(void) {
  register_int_handler(200, cry2arm0, NULL);
  unmask_interrupt(200);
  for (int i = 201; i < 204; ++i) {
    register_int_handler(i, cry2arm123, (void*)i);
    unmask_interrupt(i);
  }
}

void smsched_init(void) {
  set_kernel_enp();

  // this is optional in vita firmware
  // (but nothing will work if it's not set)
  set_rvk(g_rvk_buf, g_rvk_size);

  set_0x80_block();

  init_int();
}

task_t *smsched_create_task(void *sm, size_t sm_size, int partition_id) {
  task_t *t = calloc(1, sizeof(*t));

  t->num_paddrs = 1;
  t->paddr_list = malloc(8);
  t->paddr_list[0] = (uint32_t)sm;
  t->paddr_list[1] = sm_size;
  arch_clean_cache_range((addr_t)sm, sm_size);

  t->state = 1;
  t->some_0x40_buffer = calloc(1, 0x40);
  t->field_60 = 2;
  t->partition_id = partition_id;
  t->auth_id = 0x2808000000000001ULL;
  t->part_0xF00C = 0xF000C000000080ULL;
  t->part_0xFFFF = 0xFFFFFFFF00000000ULL;

  t->field_34 = 1;

  t->delayed_cmd = calloc(1, DELAYED_CMD_SIZE);
  arch_clean_cache_range((addr_t)t->delayed_cmd, DELAYED_CMD_SIZE);

  return t;
}

int smsched_suspend_current_task(void) {
  int ret = 0;

  if (!g_current_task) {
    TRACEF("g_current_task is NULL\n");
    return -1;
  }
  if (g_current_task->state != 2) {
    TRACEF("g_current_task->state is not RUNNING\n");
    return -1;
  }

  task_t *t = g_current_task;
  TRACEF("2 => 11\n");
  t->state = 11;

  uint8_t *suspendbuf = calloc(1, SUSPENDBUF_SIZE);
  uint32_t *plist = calloc(1, 0x80);
  plist[0] = (uint32_t)suspendbuf;
  plist[1] = SUSPENDBUF_SIZE;

  t->field_34 = 3; // suspended
  t->suspendbuf = suspendbuf;
  t->suspendbuf_plist = plist;
  t->suspendbuf_plist_cnt = 1;

  memset(&shared_buffer, 0, sizeof(shared_buffer));
  shared_buffer.sm2.num_paddrs = t->suspendbuf_plist_cnt;
  shared_buffer.sm2.paddr_list = t->suspendbuf_plist;
  shared_buffer.sm2.buf_0x40 = t->some_0x40_buffer;
  shared_buffer.sm2.delayed_cmd = t->delayed_cmd;

  arch_clean_cache_range((addr_t)&shared_buffer, sizeof(shared_buffer));
  arch_clean_cache_range((addr_t)t->suspendbuf, SUSPENDBUF_SIZE);
  arch_clean_cache_range((addr_t)t->some_0x40_buffer, 0x40);
  arch_clean_cache_range((addr_t)t->suspendbuf_plist, 0x80);
  arch_clean_cache_range((addr_t)t->delayed_cmd, DELAYED_CMD_SIZE);

  TRACEF("wait\n");
  regs[0x10/4] = 0x100401;
  do {
    ret = regs[0x10/4];
    if (ret < 0)
      TRACEF("ret: 0x%x\n", ret);
  } while (ret);
  TRACEF("wait done\n");

  return 0;
}

int smsched_load_task(task_t *t) {
  int ret = 0;

  if (g_current_task) {
    TRACEF("cannot load: a task is already running\n");
    return -1;
  }

  TRACEF("reschedule: %d => 6\n", t->state);
  t->state = 6;

  // on vita this is done after the following submit, which actually is racy
  g_current_task = t;

  if (t->field_34 == 2 || t->field_34 == 3) {
    TRACEF("resuming previously suspended task\n");

    regs[0x54/4] = -1;
    regs[0x58/4] = -1;
    regs[0x5C/4] = -1;
    regs[0x14/4] = t->delayed_cmd[0];
    regs[0x18/4] = t->delayed_cmd[1];
    regs[0x1C/4] = t->delayed_cmd[2];

    // submit_f00d_2
    memset(&shared_buffer, 0, sizeof(shared_buffer));
    shared_buffer.sm2.num_paddrs = 1;
    shared_buffer.sm2.paddr_list = t->suspendbuf_plist;
    shared_buffer.sm2.buf_0x40 = t->some_0x40_buffer;
    shared_buffer.sm2.delayed_cmd = t->delayed_cmd;

    arch_clean_cache_range((addr_t)&shared_buffer, sizeof(shared_buffer));
    arch_clean_cache_range((addr_t)t->suspendbuf, SUSPENDBUF_SIZE);
    arch_clean_cache_range((addr_t)t->some_0x40_buffer, 0x40);
    arch_clean_cache_range((addr_t)t->suspendbuf_plist, 0x80);
    arch_clean_cache_range((addr_t)t->delayed_cmd, 0x40);

    TRACEF("wait (using suspendbuf)\n");
    regs[0x10/4] = 0x100301;
    do {
      ret = regs[0x10/4];
      if (ret < 0) {
        TRACEF("ret: 0x%x\n", ret);
        return ret;
      }
    } while (ret);
    TRACEF("wait done\n");
  } else if (t->field_34 == 1 || t->field_34 == 4) {
    TRACEF("starting task\n");

    regs[0x54/4] = -1;
    regs[0x58/4] = -1;
    regs[0x5C/4] = -1;
    regs[0x14/4] = t->delayed_cmd[0];
    regs[0x18/4] = t->delayed_cmd[1];
    regs[0x1C/4] = t->delayed_cmd[2];
    regs[0x04/4] = t->delayed_cmd[3];
    regs[0x08/4] = t->delayed_cmd[4];
    regs[0x0C/4] = t->delayed_cmd[5];

    memset(t->delayed_cmd, 0, 6 * 4);
    arch_clean_cache_range((addr_t)t->delayed_cmd, 0x40);    

    // submit_f00d
    memset(&shared_buffer, 0, sizeof(shared_buffer));

    shared_buffer.sm.num_paddrs = t->num_paddrs;
    shared_buffer.sm.paddr_list = t->paddr_list;
    shared_buffer.sm.buf_0x40 = t->some_0x40_buffer; // change = error SCE_SBL_ERROR_DRV_EINVAL 0x800f0316
    shared_buffer.sm.field_60 = t->field_60; // change = error SCE_SBL_ERROR_DRV_ESYSEXVER 0x800f0338
    shared_buffer.sm.partition_id = t->partition_id; // change = error SCE_SBL_ERROR_DRV_ENOTINITIALIZED 0x800f0332
    shared_buffer.sm.auth_id = t->auth_id; // change = no effect
    shared_buffer.sm.part_0xF00C = t->part_0xF00C; // change = no effect
    shared_buffer.sm.part_0xFFFF = t->part_0xFFFF; // change = no effect

    arch_clean_cache_range((addr_t)&shared_buffer, sizeof(shared_buffer));
    arch_clean_cache_range((addr_t)t->paddr_list, 8 * t->num_paddrs);
    arch_clean_cache_range((addr_t)t->some_0x40_buffer, 0x40);

    TRACEF("wait 1\n");
    regs[0x10/4] = 0x500201;
    do {
      ret = regs[0x10/4];
      if (ret < 0) {
        TRACEF("ret: 0x%x\n", ret);
        return ret;
      }
    } while (ret);

    TRACEF("wait 1 done\n");
  } else {
    g_current_task = NULL;
    TRACEF("panic: t->field_34 == %d\n", t->field_34);
    return -1;
  }

  return 0;
}

int smsched_submit_command(command_t *cmd) {
  if (!g_current_task) {
    TRACEF("g_current_task is NULL\n");
    return -1;
  }

  task_t *t = g_current_task;
  if (t->state != 2) {
    TRACEF("state != RUNNING\n");
    return -1;
  }

  arch_clean_cache_range((addr_t)cmd, sizeof(*cmd));
  regs[0x14/4] = (uint32_t)cmd | 1;
}
