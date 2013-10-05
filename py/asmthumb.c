#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "misc.h"
#include "machine.h"
#include "asmthumb.h"

#define UNSIGNED_FIT8(x) (((x) & 0xffffff00) == 0)
#define UNSIGNED_FIT16(x) (((x) & 0xffff0000) == 0)
#define SIGNED_FIT8(x) (((x) & 0xffffff80) == 0) || (((x) & 0xffffff80) == 0xffffff80)
#define SIGNED_FIT9(x) (((x) & 0xffffff00) == 0) || (((x) & 0xffffff00) == 0xffffff00)
#define SIGNED_FIT12(x) (((x) & 0xfffff800) == 0) || (((x) & 0xfffff800) == 0xfffff800)

struct _asm_thumb_t {
    int pass;
    uint code_offset;
    uint code_size;
    byte *code_base;
    byte dummy_data[8];

    int next_label;
    int max_num_labels;
    int *label_offsets;
    int num_locals;
    uint push_reglist;
    uint stack_adjust;
};

asm_thumb_t *asm_thumb_new() {
    asm_thumb_t *as;

    as = m_new(asm_thumb_t, 1);
    as->pass = 0;
    as->code_offset = 0;
    as->code_size = 0;
    as->code_base = NULL;
    as->label_offsets = NULL;
    as->num_locals = 0;

    return as;
}

void asm_thumb_free(asm_thumb_t *as, bool free_code) {
    if (free_code) {
        m_free(as->code_base);
    }
    /*
    if (as->label != NULL) {
        int i;
        for (i = 0; i < as->label->len; ++i)
        {
            Label *lab = &g_array_index(as->label, Label, i);
            if (lab->unresolved != NULL)
                g_array_free(lab->unresolved, true);
        }
        g_array_free(as->label, true);
    }
    */
    m_free(as);
}

void asm_thumb_start_pass(asm_thumb_t *as, int pass) {
    as->pass = pass;
    as->code_offset = 0;
    as->next_label = 1;
    if (pass == ASM_THUMB_PASS_1) {
        as->max_num_labels = 0;
    } else {
        if (pass == ASM_THUMB_PASS_2) {
            memset(as->label_offsets, -1, as->max_num_labels * sizeof(int));
        }
    }
}

void asm_thumb_end_pass(asm_thumb_t *as) {
    if (as->pass == ASM_THUMB_PASS_1) {
        // calculate number of labels need
        if (as->next_label > as->max_num_labels) {
            as->max_num_labels = as->next_label;
        }
        as->label_offsets = m_new(int, as->max_num_labels);
    } else if (as->pass == ASM_THUMB_PASS_2) {
        // calculate size of code in bytes
        as->code_size = as->code_offset;
        as->code_base = m_new(byte, as->code_size);
        printf("code_size: %u\n", as->code_size);
    }

    /*
    // check labels are resolved
    if (as->label != NULL)
    {
        int i;
        for (i = 0; i < as->label->len; ++i)
            if (g_array_index(as->label, Label, i).unresolved != NULL)
                return false;
    }
    */
}

// all functions must go through this one to emit bytes
static byte *asm_thumb_get_cur_to_write_bytes(asm_thumb_t *as, int num_bytes_to_write) {
    //printf("emit %d\n", num_bytes_to_write);
    if (as->pass < ASM_THUMB_PASS_3) {
        as->code_offset += num_bytes_to_write;
        return as->dummy_data;
    } else {
        assert(as->code_offset + num_bytes_to_write <= as->code_size);
        byte *c = as->code_base + as->code_offset;
        as->code_offset += num_bytes_to_write;
        return c;
    }
}

uint asm_thumb_get_code_size(asm_thumb_t *as) {
    return as->code_size;
}

void *asm_thumb_get_code(asm_thumb_t *as) {
    // need to set low bit to indicate that it's thumb code
    return (void *)(((machine_uint_t)as->code_base) | 1);
}

/*
static void asm_thumb_write_byte_1(asm_thumb_t *as, byte b1) {
    byte *c = asm_thumb_get_cur_to_write_bytes(as, 1);
    c[0] = b1;
}
*/

static void asm_thumb_write_op16(asm_thumb_t *as, uint op) {
    byte *c = asm_thumb_get_cur_to_write_bytes(as, 2);
    // little endian
    c[0] = op;
    c[1] = op >> 8;
}

static void asm_thumb_write_op32(asm_thumb_t *as, uint op1, uint op2) {
    byte *c = asm_thumb_get_cur_to_write_bytes(as, 4);
    // little endian, op1 then op2
    c[0] = op1;
    c[1] = op1 >> 8;
    c[2] = op2;
    c[3] = op2 >> 8;
}

/*
#define IMM32_L0(x) ((x) & 0xff)
#define IMM32_L1(x) (((x) >> 8) & 0xff)
#define IMM32_L2(x) (((x) >> 16) & 0xff)
#define IMM32_L3(x) (((x) >> 24) & 0xff)

static void asm_thumb_write_word32(asm_thumb_t *as, int w32) {
    byte *c = asm_thumb_get_cur_to_write_bytes(as, 4);
    c[0] = IMM32_L0(w32);
    c[1] = IMM32_L1(w32);
    c[2] = IMM32_L2(w32);
    c[3] = IMM32_L3(w32);
}
*/

// rlolist is a bit map indicating desired lo-registers
#define OP_PUSH_RLIST(rlolist)      (0xb400 | (rlolist))
#define OP_PUSH_RLIST_LR(rlolist)   (0xb400 | 0x0100 | (rlolist))
#define OP_POP_RLIST(rlolist)       (0xbc00 | (rlolist))
#define OP_POP_RLIST_PC(rlolist)    (0xbc00 | 0x0100 | (rlolist))

#define OP_ADD_SP(num_words) (0xb000 | (num_words))
#define OP_SUB_SP(num_words) (0xb080 | (num_words))

void asm_thumb_entry(asm_thumb_t *as, int num_locals) {
    // work out what to push and how many extra space to reserve on stack
    // so that we have enough for all locals and it's aligned an 8-byte boundary
    uint reglist;
    uint stack_adjust;
    if (num_locals < 0) {
        num_locals = 0;
    }
    // don't ppop r0 because it's used for return value
    switch (num_locals) {
        case 0:
            reglist = 0xf2;
            stack_adjust = 0;
            break;

        case 1:
            reglist = 0xf2;
            stack_adjust = 0;
            break;

        case 2:
            reglist = 0xfe;
            stack_adjust = 0;
            break;

        case 3:
            reglist = 0xfe;
            stack_adjust = 0;
            break;

        default:
            reglist = 0xfe;
            stack_adjust = ((num_locals - 3) + 1) & (~1);
            break;
    }
    asm_thumb_write_op16(as, OP_PUSH_RLIST_LR(reglist));
    if (stack_adjust > 0) {
        asm_thumb_write_op16(as, OP_SUB_SP(stack_adjust));
    }
    as->push_reglist = reglist;
    as->stack_adjust = stack_adjust;
    as->num_locals = num_locals;
}

void asm_thumb_exit(asm_thumb_t *as) {
    if (as->stack_adjust > 0) {
        asm_thumb_write_op16(as, OP_ADD_SP(as->stack_adjust));
    }
    asm_thumb_write_op16(as, OP_POP_RLIST_PC(as->push_reglist));
}

int asm_thumb_label_new(asm_thumb_t *as) {
    return as->next_label++;
}

void asm_thumb_label_assign(asm_thumb_t *as, int label) {
    if (as->pass > ASM_THUMB_PASS_1) {
        assert(label < as->max_num_labels);
        if (as->pass == ASM_THUMB_PASS_2) {
            // assign label offset
            assert(as->label_offsets[label] == -1);
            as->label_offsets[label] = as->code_offset;
        } else if (as->pass == ASM_THUMB_PASS_3) {
            // ensure label offset has not changed from PASS_2 to PASS_3
            //printf("l%d: (at %d=%ld)\n", label, as->label_offsets[label], as->code_offset);
            assert(as->label_offsets[label] == as->code_offset);
        }
    }
}

// the i8 value will be zero extended into the r32 register!
void asm_thumb_mov_reg_i8(asm_thumb_t *as, uint rlo_dest, int i8) {
    assert(rlo_dest < REG_R8);
    // movs rlo_dest, #i8
    asm_thumb_write_op16(as, 0x2000 | (rlo_dest << 8) | i8);
}

// if loading lo half, the i16 value will be zero extended into the r32 register!
void asm_thumb_mov_i16_to_reg(asm_thumb_t *as, int i16, uint reg_dest, bool load_hi_half) {
    assert(reg_dest < REG_R15);
    uint op;
    if (load_hi_half) {
        // movt reg_dest, #i16
        op = 0xf2c0;
    } else {
        // movw reg_dest, #i16
        op = 0xf240;
    }
    asm_thumb_write_op32(as, op | ((i16 >> 1) & 0x0400) | ((i16 >> 12) & 0xf), ((i16 << 4) & 0x7000) | (reg_dest << 8) | (i16 & 0xff));
}

void asm_thumb_mov_reg_i32(asm_thumb_t *as, uint reg_dest, machine_uint_t i32) {
    // movw, movt does it in 8 bytes
    // ldr [pc, #], dw does it in 6 bytes, but we might not reach to end of code for dw

    asm_thumb_mov_i16_to_reg(as, i32, reg_dest, false);
    asm_thumb_mov_i16_to_reg(as, i32 >> 16, reg_dest, true);
}

void asm_thumb_mov_reg_i32_optimised(asm_thumb_t *as, uint reg_dest, int i32) {
    if (reg_dest < 8 && UNSIGNED_FIT8(i32)) {
        asm_thumb_mov_reg_i8(as, reg_dest, i32);
    } else if (UNSIGNED_FIT16(i32)) {
        asm_thumb_mov_i16_to_reg(as, i32, reg_dest, false);
    } else {
        asm_thumb_mov_reg_i32(as, reg_dest, i32);
    }
}

void asm_thumb_mov_reg_reg(asm_thumb_t *as, uint reg_dest, uint reg_src) {
    uint op_lo;
    if (reg_src < 8) {
        op_lo = reg_src << 3;
    } else {
        op_lo = 0x40 | ((reg_src - 8) << 3);
    }
    if (reg_dest < 8) {
        op_lo |= reg_dest;
    } else {
        op_lo |= 0x80 | (reg_dest - 8);
    }
    asm_thumb_write_op16(as, 0x4600 | op_lo);
}

#define OP_STR_TO_SP_OFFSET(rlo_dest, word_offset) (0x9000 | ((rlo_dest) << 8) | ((word_offset) & 0x00ff))
#define OP_LDR_FROM_SP_OFFSET(rlo_dest, word_offset) (0x9800 | ((rlo_dest) << 8) | ((word_offset) & 0x00ff))

void asm_thumb_mov_local_reg(asm_thumb_t *as, int local_num, uint rlo_src) {
    assert(rlo_src < REG_R8);
    int word_offset = as->num_locals - local_num - 1;
    assert(as->pass < ASM_THUMB_PASS_3 || word_offset >= 0);
    asm_thumb_write_op16(as, OP_STR_TO_SP_OFFSET(rlo_src, word_offset));
}

void asm_thumb_mov_reg_local(asm_thumb_t *as, uint rlo_dest, int local_num) {
    assert(rlo_dest < REG_R8);
    int word_offset = as->num_locals - local_num - 1;
    assert(as->pass < ASM_THUMB_PASS_3 || word_offset >= 0);
    asm_thumb_write_op16(as, OP_LDR_FROM_SP_OFFSET(rlo_dest, word_offset));
}

void asm_thumb_mov_reg_local_addr(asm_thumb_t *as, uint reg_dest, int local_num) {
    assert(0);
    // see format 12, load address
    asm_thumb_write_op16(as, 0x0000);
}

#define OP_ADD_REG_REG_REG(rlo_dest, rlo_src_a, rlo_src_b) (0x1800 | ((rlo_src_b) << 6) | ((rlo_src_a) << 3) | (rlo_dest))

void asm_thumb_add_reg_reg_reg(asm_thumb_t *as, uint rlo_dest, uint rlo_src_a, uint rlo_src_b) {
    asm_thumb_write_op16(as, OP_ADD_REG_REG_REG(rlo_dest, rlo_src_a, rlo_src_b));
}

#define OP_CMP_REG_REG(rlo_a, rlo_b) (0x4280 | ((rlo_b) << 3) | (rlo_a))

void asm_thumb_cmp_reg_reg(asm_thumb_t *as, uint rlo_a, uint rlo_b) {
    asm_thumb_write_op16(as, OP_CMP_REG_REG(rlo_a, rlo_b));
}

void asm_thumb_ite_ge(asm_thumb_t *as) {
    asm_thumb_write_op16(as, 0xbfac);
}

#define OP_B(byte_offset) (0xe000 | (((byte_offset) >> 1) & 0x07ff))
// this could be wrong, because it should have a range of +/- 16MiB...
#define OP_BW_HI(byte_offset) (0xf000 | (((byte_offset) >> 12) & 0x07ff))
#define OP_BW_LO(byte_offset) (0xb800 | (((byte_offset) >> 1) & 0x07ff))

void asm_thumb_b_label(asm_thumb_t *as, int label) {
    if (as->pass > ASM_THUMB_PASS_1) {
        int dest = as->label_offsets[label];
        int rel = dest - as->code_offset;
        rel -= 4; // account for instruction prefetch, PC is 4 bytes ahead of this instruction
        if (dest >= 0 && rel <= -4) {
            // is a backwards jump, so we know the size of the jump on the first pass
            // calculate rel assuming 12 bit relative jump
            if (SIGNED_FIT12(rel)) {
                asm_thumb_write_op16(as, OP_B(rel));
            } else {
                goto large_jump;
            }
        } else {
            // is a forwards jump, so need to assume it's large
            large_jump:
            asm_thumb_write_op32(as, OP_BW_HI(rel), OP_BW_LO(rel));
        }
    }
}

#define OP_CMP_REG_IMM(rlo, i8) (0x2800 | ((rlo) << 8) | (i8))
// all these bit arithmetics need coverage testing!
#define OP_BEQ(byte_offset) (0xd000 | (((byte_offset) >> 1) & 0x00ff))
#define OP_BEQW_HI(byte_offset) (0xf000 | (((byte_offset) >> 10) & 0x0400) | (((byte_offset) >> 14) & 0x003f))
#define OP_BEQW_LO(byte_offset) (0x8000 | ((byte_offset) & 0x2000) | (((byte_offset) >> 1) & 0x0fff))

void asm_thumb_cmp_reg_bz_label(asm_thumb_t *as, uint rlo, int label) {
    assert(rlo < REG_R8);

    // compare reg with 0
    asm_thumb_write_op16(as, OP_CMP_REG_IMM(rlo, 0));

    // branch if equal
    if (as->pass > ASM_THUMB_PASS_1) {
        int dest = as->label_offsets[label];
        int rel = dest - as->code_offset;
        rel -= 4; // account for instruction prefetch, PC is 4 bytes ahead of this instruction
        if (dest >= 0 && rel <= -4) {
            // is a backwards jump, so we know the size of the jump on the first pass
            // calculate rel assuming 12 bit relative jump
            if (SIGNED_FIT9(rel)) {
                asm_thumb_write_op16(as, OP_BEQ(rel));
            } else {
                goto large_jump;
            }
        } else {
            // is a forwards jump, so need to assume it's large
            large_jump:
            asm_thumb_write_op32(as, OP_BEQW_HI(rel), OP_BEQW_LO(rel));
        }
    }
}

#define OP_BLX(reg) (0x4780 | ((reg) << 3))
#define OP_SVC(arg) (0xdf00 | (arg))
#define OP_LDR_FROM_BASE_OFFSET(rlo_dest, rlo_base, word_offset) (0x6800 | (((word_offset) << 6) & 0x07c0) | ((rlo_base) << 3) | (rlo_dest))

void asm_thumb_bl_ind(asm_thumb_t *as, void *fun_ptr, uint fun_id, uint reg_temp) {
    /* TODO make this use less bytes
    uint rlo_base = REG_R3;
    uint rlo_dest = REG_R7;
    uint word_offset = 4;
    asm_thumb_write_op16(as, 0x0000);
    asm_thumb_write_op16(as, 0x6800 | (word_offset << 6) | (rlo_base << 3) | rlo_dest); // ldr rlo_dest, [rlo_base, #offset]
    asm_thumb_write_op16(as, 0x4780 | (REG_R9 << 3)); // blx reg
    */

    if (0) {
        // load ptr to function into register using immediate, then branch
        // not relocatable
        asm_thumb_mov_reg_i32(as, reg_temp, (machine_uint_t)fun_ptr);
        asm_thumb_write_op16(as, OP_BLX(reg_temp));
    } else if (1) {
        asm_thumb_write_op16(as, OP_LDR_FROM_BASE_OFFSET(reg_temp, REG_R7, fun_id));
        asm_thumb_write_op16(as, OP_BLX(reg_temp));
    } else {
        // use SVC
        asm_thumb_write_op16(as, OP_SVC(fun_id));
    }
}