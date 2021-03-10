#include <cpu/decode.h>
#include <rtl/rtl.h>

#define INSTR_NULLARY(f) \
  f(inv) f(nemu_trap) f(ecall) f(sret) f(sfence_vma) f(p_ret)
#define INSTR_UNARY(f) \
  f(p_li_0) f(p_li_1)
#define INSTR_BINARY(f) \
  f(lui) f(auipc) f(jal) \
  f(lw) f(sw) f(lh) f(lb) f(lhu) f(lbu) f(sh) f(sb) \
  f(c_j) f(c_jal) f(c_jr) \
  f(c_beqz) f(c_bnez) f(c_mv)
#define INSTR_TERNARY(f) \
  f(add) f(sll) f(srl) f(slt) f(sltu) f(xor) f(or) f(sub) f(sra) f(and) \
  f(addi) f(slli) f(srli) f(slti) f(sltui) f(xori) f(ori) f(srai) f(andi) \
  f(jalr) f(beq) f(bne) f(blt) f(bge) f(bltu) f(bgeu) \
  f(mul) f(mulh) f(mulhu) f(mulhsu) f(div) f(divu) f(rem) f(remu) \
  f(csrrw) f(csrrs) \
  f(c_li) f(c_addi) f(c_slli) f(c_srli) f(c_srai) f(c_andi) \
  f(c_add) f(c_and) f(c_or) f(c_xor) f(c_sub) \
  f(p_blez) f(p_bgez) f(p_bltz) f(p_bgtz) f(p_jr_imm)

def_all_EXEC_ID();
