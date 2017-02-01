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
#include <app.h>
#include <stdio.h>
#include <trace.h>
#include <string.h>

#include "sm.h"

static void f00d_init(const struct app_descriptor *app) {
  TRACEF("f00d_init\n");

  smsched_init();
}

command_t *test_cmd = (void*)0x50000000; // we have to use NS memory for command buffer

static void f00d_entry(const struct app_descriptor *app, void *args) {
  TRACEF("f00d_entry\n");

  task_t *t = smsched_create_task(sm, sm_size, 2);
  smsched_load_task(t);

  // wait for task to load
  while (t->state != 2) {}

  // submit cmd 1
  memset(test_cmd, 0, sizeof(*test_cmd));
  test_cmd->len = sizeof(*test_cmd);
  test_cmd->cmd = 1;
  smsched_submit_command(test_cmd);

  // wait for cry2arm123 to trigger which indicates result
  while (!t->cry123) {}; t->cry123 = 0;

  // print output
  arch_invalidate_cache_range((addr_t)test_cmd, sizeof(*test_cmd));
  hexdump(test_cmd, 0x100);

  // suspend our task
  smsched_suspend_current_task();

  // wait for suspend to finish
  while (t->state != 1) {}

  // print suspend buffer
  arch_invalidate_cache_range((addr_t)t->suspendbuf, SUSPENDBUF_SIZE);
  hexdump(t->suspendbuf, 0x100);

  // resume the task
  smsched_load_task(t); // this function either starts or resumes the task

  // wait for resume
  while (t->state != 2) {}

  // submit cmd 2
  memset(test_cmd, 0, sizeof(*test_cmd));
  test_cmd->len = sizeof(*test_cmd);
  test_cmd->cmd = 2;
  smsched_submit_command(test_cmd);

  // wait for cry2arm123 to trigger which indicates result
  while (!t->cry123) {}; t->cry123 = 0;

  // print output
  arch_invalidate_cache_range((addr_t)test_cmd, sizeof(*test_cmd));
  hexdump(test_cmd, 0x100);

  TRACEF("all done\n");
}

APP_START(f00d)
    .init = f00d_init,
    .entry = f00d_entry,
APP_END
