#include <cpu/cpu.h>
#include <cpu/exec.h>
#include <cpu/difftest.h>
#include <cpu/decode.h>
#include <memory/host-tlb.h>
#include <isa-all-instr.h>
#include <locale.h>
#include <setjmp.h>

/* The assembly code of instructions executed is only output to the screen
 * when the number of instructions executed is less than this value.
 * This is useful when you use the `si' command.
 * You can modify this value as you want.
 */
#define MAX_INSTR_TO_PRINT 10
#define BATCH_SIZE 65536

CPU_state cpu = {};
uint64_t g_nr_guest_instr = 0;
static uint64_t g_timer = 0; // unit: us
static bool g_print_step = false;
const rtlreg_t rzero = 0;
rtlreg_t tmp_reg[4];

#ifdef CONFIG_DEBUG
static inline void debug_hook(vaddr_t pc, const char *asmbuf) {
  g_nr_guest_instr ++;

  log_write("%s\n", asmbuf);
  if (g_print_step) { puts(asmbuf); }

  void scan_watchpoint(vaddr_t pc);
  scan_watchpoint(pc);
}
#endif

static jmp_buf jbuf_exec = {};
static uint64_t n_remain_total;
static int n_remain;
static Decode *prev_s;

void save_globals(Decode *s) {
  IFDEF(CONFIG_PERF_OPT, prev_s = s);
}

static void update_instr_cnt() {
#ifdef CONFIG_ENABLE_INSTR_CNT
  int n_batch = n_remain_total >= BATCH_SIZE ? BATCH_SIZE : n_remain_total;
  uint32_t n_executed = n_batch - n_remain;
  n_remain_total -= n_executed;
  IFNDEF(CONFIG_DEBUG, g_nr_guest_instr += n_executed);
#endif
}

void monitor_statistic() {
  update_instr_cnt();
  setlocale(LC_NUMERIC, "");
  Log("host time spent = %'ld us", g_timer);
#ifdef CONFIG_ENABLE_INSTR_CNT
  Log("total guest instructions = %'ld", g_nr_guest_instr);
  if (g_timer > 0) Log("simulation frequency = %'ld instr/s", g_nr_guest_instr * 1000000 / g_timer);
  else Log("Finish running in less than 1 us and can not calculate the simulation frequency");
#else
  Log("CONFIG_ENABLE_INSTR_CNT is not defined");
#endif
}

static word_t g_ex_cause = 0;
static int g_mmu_state_flag = 0;

void set_mmu_state_flag(int flag) {
  g_mmu_state_flag |= flag;
}

void mmu_tlb_flush(vaddr_t vaddr) {
  hosttlb_flush(vaddr);
}

void longjmp_exec(int cause) {
  longjmp(jbuf_exec, cause);
}

void longjmp_exception(int ex_cause) {
  g_ex_cause = ex_cause;
  longjmp_exec(NEMU_EXEC_EXCEPTION);
}

int fetch_decode(Decode *s, vaddr_t pc) {
  s->pc = pc;
  s->snpc = pc;
  IFDEF(CONFIG_DEBUG, log_bytebuf[0] = '\0');
  int idx = isa_fetch_decode(s);
  IFDEF(CONFIG_DEBUG, snprintf(s->logbuf, sizeof(s->logbuf), FMT_WORD ":   %s%*.s%s",
        s->pc, log_bytebuf, 50 - (12 + 3 * (int)(s->snpc - s->pc)), "", log_asmbuf));
  return idx;
}

#ifdef CONFIG_PERF_OPT
#define FILL_EXEC_TABLE(name) [concat(EXEC_ID_, name)] = &&name,

#define rtl_j(s, target) do { \
  IFDEF(CONFIG_ENABLE_INSTR_CNT, n -= s->idx_in_bb); \
  s = s->tnext; \
  goto end_of_bb; \
} while (0)
#define rtl_jr(s, target) do { \
  IFDEF(CONFIG_ENABLE_INSTR_CNT, n -= s->idx_in_bb); \
  s = jr_fetch(s, *(target)); \
  goto end_of_bb; \
} while (0)
#define rtl_jrelop(s, relop, src1, src2, target) do { \
  IFDEF(CONFIG_ENABLE_INSTR_CNT, n -= s->idx_in_bb); \
  if (interpret_relop(relop, *src1, *src2)) s = s->tnext; \
  else s = s->ntnext; \
  goto end_of_bb; \
} while (0)

#define rtl_priv_next(s) goto check_priv
#define rtl_priv_jr(s, target) do { \
  IFDEF(CONFIG_ENABLE_INSTR_CNT, n -= s->idx_in_bb); \
  s = jr_fetch(s, *(target)); \
  goto end_of_priv; \
} while (0)

Decode* tcache_jr_fetch(Decode *s, vaddr_t jpc);
Decode* tcache_decode(Decode *s, const void **exec_table);
void tcache_handle_exception(vaddr_t jpc);
Decode* tcache_handle_flush(vaddr_t snpc);

static inline Decode* jr_fetch(Decode *s, vaddr_t target) {
  if (likely(s->tnext->pc == target)) return s->tnext;
  if (likely(s->ntnext->pc == target)) return s->ntnext;
  return tcache_jr_fetch(s, target);
}

#define debug_difftest(this, next) do { \
  IFDEF(CONFIG_DEBUG, debug_hook(this->pc, this->logbuf)); \
  IFDEF(CONFIG_DIFFTEST, save_globals(next)); \
  IFDEF(CONFIG_DIFFTEST, cpu.pc = next->pc); \
  IFDEF(CONFIG_DIFFTEST, difftest_step(this->pc, next->pc)); \
} while (0)

static int execute(int n) {
  static const void* exec_table[TOTAL_INSTR + 1] = {
    MAP(INSTR_LIST, FILL_EXEC_TABLE)
  };
  static int init_flag = 0;
  Decode *s = prev_s;

  if (likely(init_flag == 0)) {
    exec_table[TOTAL_INSTR] = &&nemu_decode;
    extern Decode* tcache_init(const void **speical_exec_table, vaddr_t reset_vector);
    s = tcache_init(exec_table + TOTAL_INSTR, cpu.pc);
    hosttlb_init();
    init_flag = 1;
  }

  while (true) {
    IFDEF(CONFIG_DEBUG, Decode *this_s = s);
    IFNDEF(CONFIG_DEBUG, IFDEF(CONFIG_DIFFTEST, Decode *this_s = s));
    __attribute__((unused)) rtlreg_t ls0, ls1, ls2;

    goto *(s->EHelper);

#undef s0
#undef s1
#undef s2
#define s0 &ls0
#define s1 &ls1
#define s2 &ls2

#include "isa-exec.h"

def_EHelper(check_priv) {
  if (g_mmu_state_flag) {
    int mmu_state_flag = g_mmu_state_flag;
    g_mmu_state_flag = 0;

    if (mmu_state_flag & MMU_STATE_FLUSH_TCACHE) {
      s = tcache_handle_flush(s->snpc);
    } else {
      s ++;
    }
    debug_difftest(this_s, s);
    break;
  }
}

def_EHelper(nemu_decode) {
  s = tcache_decode(s, exec_table);
  continue;
}

end_of_priv:
    IFDEF(CONFIG_ENABLE_INSTR_CNT, n_remain = n);
    debug_difftest(this_s, s);
    break;

end_of_bb:
#ifdef CONFIG_ENABLE_INSTR_CNT
    n_remain = n;
    if (unlikely(n <= 0)) {
      debug_difftest(this_s, s);
      break;
    }
#endif

    def_finish();
    debug_difftest(this_s, s);
  }
  prev_s = s;
  return n;
}
#else
typedef void (*EHelper_t)(Decode *s);
#define FILL_EXEC_TABLE(name) [concat(EXEC_ID_, name)] = concat(exec_, name),

#define rtl_priv_next(s)
#define rtl_priv_jr(s, target) rtl_jr(s, target)

#include "isa-exec.h"

static int execute(int n) {
  static const EHelper_t exec_table[TOTAL_INSTR] = {
    MAP(INSTR_LIST, FILL_EXEC_TABLE)
  };
  static Decode s;
  prev_s = &s;
  for (;n > 0; n --) {
    int idx = fetch_decode(&s, cpu.pc);
    cpu.pc = s.snpc;
    n_remain = n;
    exec_table[idx](&s);
    IFDEF(CONFIG_DEBUG, debug_hook(s.pc, s.logbuf));
    IFDEF(CONFIG_DIFFTEST, difftest_step(s.pc, cpu.pc));
  }
  return n;
}
#endif

static void update_global() {
  update_instr_cnt();
  IFDEF(CONFIG_PERF_OPT, cpu.pc = prev_s->pc);
}

/* Simulate how the CPU works. */
void cpu_exec(uint64_t n) {
  g_print_step = (n < MAX_INSTR_TO_PRINT);
  switch (nemu_state.state) {
    case NEMU_END: case NEMU_ABORT:
      printf("Program execution has ended. To restart the program, exit NEMU and run again.\n");
      return;
    default: nemu_state.state = NEMU_RUNNING;
  }

  uint64_t timer_start = get_time();

  n_remain_total = n; // deal with setjmp()
  int cause;
  if ((cause = setjmp(jbuf_exec))) {
    update_global();
  }

  while (nemu_state.state == NEMU_RUNNING &&
      MUXDEF(CONFIG_ENABLE_INSTR_CNT, n_remain_total > 0, true)) {
#ifdef CONFIG_DEVICE
    extern void device_update();
    device_update();
#endif

    if (cause != NEMU_EXEC_EXCEPTION) {
#if 0 //!_SHARE
      word_t intr = isa_query_intr();
      if (intr != INTR_EMPTY) {
        g_ex_cause = intr;
        cause = NEMU_EXEC_EXCEPTION;
        IFDEF(CONFIG_PERF_OPT, difftest_skip_dut(1, 2));
        IFDEF(CONFIG_DIFFTEST, ref_difftest_raise_intr(intr));
      }
#endif
    }
    if (cause == NEMU_EXEC_EXCEPTION) {
      cause = 0;
      cpu.pc = raise_intr(g_ex_cause, prev_s->pc);
      IFDEF(CONFIG_DIFFTEST, difftest_step(prev_s->pc, cpu.pc));
      IFDEF(CONFIG_PERF_OPT, tcache_handle_exception(cpu.pc));
    }

    int n_batch = n >= BATCH_SIZE ? BATCH_SIZE : n;
    n_remain = execute(n_batch);
    update_global();
  }

  uint64_t timer_end = get_time();
  g_timer += timer_end - timer_start;

  switch (nemu_state.state) {
    case NEMU_RUNNING: nemu_state.state = NEMU_STOP; break;

    case NEMU_END: case NEMU_ABORT:
      Log("nemu: %s\33[0m at pc = " FMT_WORD,
          (nemu_state.state == NEMU_ABORT ? "\33[1;31mABORT" :
           (nemu_state.halt_ret == 0 ? "\33[1;32mHIT GOOD TRAP" : "\33[1;31mHIT BAD TRAP")),
          nemu_state.halt_pc);
      // fall through
    case NEMU_QUIT:
      monitor_statistic();
  }
}
