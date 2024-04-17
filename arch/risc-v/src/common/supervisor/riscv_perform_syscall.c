/****************************************************************************
 * arch/risc-v/src/common/supervisor/riscv_perform_syscall.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

#include <nuttx/addrenv.h>

#include "sched/sched.h"
#include "riscv_internal.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void *riscv_perform_syscall(uintptr_t *regs)
{
  struct tcb_s *tcb;
  int cpu;

  /* Set up the interrupt register set needed by swint() */

  up_set_current_regs(regs);

  /* Run the system call handler (swint) */

  riscv_swint(0, regs, NULL);

#ifdef CONFIG_ARCH_ADDRENV
  if (regs != up_current_regs())
    {
      /* Make sure that the address environment for the previously
       * running task is closed down gracefully (data caches dump,
       * MMU flushed) and set up the address environment for the new
       * thread at the head of the ready-to-run list.
       */

      addrenv_switch(NULL);
    }
#endif

  if (regs != up_current_regs())
    {
      /* Restore the cpu lock */

      cpu = this_cpu();
      tcb = current_task(cpu);
      restore_critical_section(tcb, cpu);

      /* If a context switch occurred while processing the interrupt then
       * current_regs may have change value.  If we return any value
       * different from the input regs, then the lower level will know
       * that a context switch occurred during interrupt processing.
       */

      regs = up_current_regs();
    }

  up_set_current_regs(NULL);

  return regs;
}
