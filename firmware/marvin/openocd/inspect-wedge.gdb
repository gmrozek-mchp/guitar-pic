# Halt-and-inspect a wedged marvin over JTAG, without resetting it.
#
#   openocd -f sam9x75-chybrid.cfg                  # separate terminal, no reset
#   arm-none-eabi-gdb out/marvin/default.elf \
#       -ex 'set architecture arm' -ex 'target remote :3333' \
#       -x openocd/inspect-wedge.gdb
#
# ARCHIVE THE ELF THAT WAS RUNNING. A rebuild can move .bss (and shift parts of
# .text by a few bytes) while most symbols still resolve, so offset arithmetic
# looks correct right up until it silently isn't. Keep the .elf beside the log.
#
# Do NOT `reset`, `reset halt`, or `reset init` — those destroy the evidence.
# Plain `init` only does a TAP reset, which leaves the running core alone.
#
# Trust struct field reads over stack word scans. `info symbol` on stale words
# inside printf's large frames will happily invent a plausible call chain — that
# cost a whole round on 2026-08-07 (see firmware/marvin/docs/journal.md).

set pagination off
set print pretty on

define marvin_wedge_overview
  printf "\n== scheduler ==\n"
  output xTickCount
  printf "  <- tick count; re-run later, still advancing => scheduler alive\n"
  printf "current task: "
  output (char *) pxCurrentTCB->pcTaskName
  printf "\n"
  printf "(in prvIdleTask => nothing runnable: every task is blocked, not spinning)\n"
  printf "\n== AIC (non-zero ISR => CPU pinned in an ISR) ==\n"
  printf "AIC_ISR  "
  monitor mdw 0xfffff118 1
  printf "AIC_IPR0 "
  monitor mdw 0xfffff120 1
end

document marvin_wedge_overview
Scheduler liveness, the running task, and whether an interrupt storm is pinning
the CPU. Start here: it separates "one task blocked" from "whole system dead".
end

# The log mutex is the highest-value single check: it is a global lock every task
# takes, so a wedged holder blocks all logging and the board goes quiet and idle.
define marvin_log_lock
  printf "\n== log mutex ==\n"
  output *(Queue_t *) 'log.c'::s_mutex
  printf "\nholder: "
  output (char *) ((TCB_t *) ((Queue_t *) 'log.c'::s_mutex)->u.xSemaphore.xMutexHolder)->pcTaskName
  printf "\nwaiters: "
  output ((Queue_t *) 'log.c'::s_mutex)->xTasksWaitingToReceive.uxNumberOfItems
  printf "\n"
  printf "If the holder also appears in xTasksWaitingToReceive (its TCB+24, the\n"
  printf "xEventListItem), it is waiting on a mutex it already owns.\n"
  printf "nested / lock-timeout counters: "
  output log_nested_count()
  printf " / "
  output log_lock_timeout_count()
  printf "\n"
end

document marvin_log_lock
Dump the log mutex and name its holder. Needs Queue_t/TCB_t in scope — run
`frame 1` from a FreeRTOS frame first if the types don't resolve (try xQUEUE).
end

# Pass a TCB address: marvin allocates every task statically, so each has a
# symbol (e.g. &'video.c'::s_task_tcb, &'ui_manager.c'::s_boot_tcb).
define marvin_task
  set $t = (TCB_t *) $arg0
  printf "\n== task "
  output (char *) $t->pcTaskName
  printf " ==\nprio "
  output $t->uxPriority
  printf "\nstack: base "
  output/x $t->pxStack
  printf "  top "
  output/x $t->pxTopOfStack
  printf "\n  top < base => OVERFLOW. Check the 0xa5a5a5a5 fill at the base too:\n"
  x/8wx $t->pxStack
  printf "state list (== &xSuspendedTaskList => blocked with NO timeout): "
  output/x $t->xStateListItem.pvContainer
  printf "\n  &xSuspendedTaskList        "
  output/x &xSuspendedTaskList
  printf "\n  pxDelayedTaskList         "
  output/x pxDelayedTaskList
  printf "\nevent list (the object it waits on): "
  output/x $t->xEventListItem.pvContainer
  printf "\n  log mutex wait list       "
  output/x &((Queue_t *) 'log.c'::s_mutex)->xTasksWaitingToReceive
  printf "\n"
end

document marvin_task
marvin_task <tcb-addr> — priority, stack bounds (overflow check), and what the
task is blocked on. Compare the event list against candidate lock addresses.
end

# ARM926 portSAVE_CONTEXT (portASM.S) pushes, ascending from pxTopOfStack:
#   [0] ulCriticalNesting  [1] SPSR  [2..16] R0-R14  [17] return address (PC)
define marvin_task_regs
  set $t = (TCB_t *) $arg0
  set $s = (unsigned long *) $t->pxTopOfStack
  printf "\n== saved context ==\ncritical nesting "
  output $s[0]
  printf "  SPSR "
  output/x $s[1]
  printf "\nR0-R14:\n"
  output/x $s[2]@15
  printf "\nSP  "
  output/x $s[15]
  printf "\nLR  "
  output/x $s[16]
  printf "  "
  info symbol $s[16]
  printf "PC  "
  output/x $s[17]
  printf "  "
  info symbol $s[17]
  printf "A queue/semaphore handle in R4-R7 is usually the object being waited on.\n"
end

document marvin_task_regs
marvin_task_regs <tcb-addr> — decode a blocked task's saved ARM926 context.
Look for a known lock handle among R4-R7 to identify what it is waiting for.
end

# Retargets the CPU registers, so the board cannot be resumed afterwards. The
# unwind is often still poor (the saved SP is the user-mode SP at the SWI, not
# the frame base gdb expects) — prefer the struct reads above.
define marvin_task_bt
  set $t = (TCB_t *) $arg0
  set $s = (unsigned long *) $t->pxTopOfStack
  set $sp = $s[15]
  set $lr = $s[16]
  set $pc = $s[17]
  bt
  printf "\nRegisters are now clobbered: do NOT continue. Reload with load-ram.sh.\n"
end

document marvin_task_bt
marvin_task_bt <tcb-addr> — attempt a real backtrace of a blocked task.
DESTRUCTIVE: overwrites CPU registers; the board must be reloaded afterwards.
end

printf "\nmarvin wedge helpers loaded:\n"
printf "  marvin_wedge_overview\n"
printf "  marvin_log_lock\n"
printf "  marvin_task <tcb>        e.g. marvin_task &'video.c'::s_task_tcb\n"
printf "  marvin_task_regs <tcb>\n"
printf "  marvin_task_bt <tcb>     DESTRUCTIVE\n\n"
