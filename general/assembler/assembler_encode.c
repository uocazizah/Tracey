#include "assembler_internal.h"

enum asm_encode_status asm_encode_line(struct asm_emit *e, const char *mnemonic,
                                               struct asm_slice operands)
{
    struct asm_slice ops[ASM_MAX_OPERANDS];
    size_t op_count;
    size_t i;
    uint32_t insn;
    int rd;
    int rn;
    int rm;
    int ra;
    int cc;
    int imm;
    int shift;

    for (i = 0; i < sizeof asm_exact_forms / sizeof asm_exact_forms[0]; i++)
    {
        if (strcmp(mnemonic, asm_exact_forms[i].mnemonic) == 0 &&
            asm_slice_eq(operands, asm_exact_forms[i].operands))
        {
            asm_emit_word(e, asm_exact_forms[i].encoding);
            return ASM_ENCODE_OK;
        }
    }

    if (strcmp(mnemonic, "movk") == 0)
    {
        op_count = asm_split_operands(operands, ops, ASM_MAX_OPERANDS);
        if (op_count < 2)
            return asm_fail_operands(e, mnemonic);
        rd = asm_regnum(ops[0]);
        if (!asm_parse_int(asm_after_char(ops[1], '#'), &imm))
            return asm_fail_operands(e, mnemonic);
        shift = 0;
        if (op_count >= 3)
        {
            size_t hp = asm_slice_find_char(ops[2], '#');

            if (hp != ASM_NPOS &&
                !asm_parse_int(asm_slice_sub(ops[2], hp + 1, ASM_NPOS), &shift))
                return asm_fail_operands(e, mnemonic);
        }
        insn = 0xF2800000u | (((uint32_t)(shift / 16) & 0x3u) << 21) |
               (((uint32_t)imm & 0xFFFFu) << 5) | ((uint32_t)rd & 0x1Fu);
        asm_emit_word(e, insn);
        return ASM_ENCODE_OK;
    }

    if (strcmp(mnemonic, "fmov") == 0)
    {
        op_count = asm_split_operands(operands, ops, ASM_MAX_OPERANDS);
        if (op_count < 2)
            return asm_fail_msg(e, "Invalid floating-point move instruction");
        /* Destination in an FP register, source in a general register. */
        if (ops[0].n > 0 && ops[0].p[0] == 's' && ops[1].n > 0 && ops[1].p[0] == 'w')
        {
            if (!asm_parse_int(asm_slice_sub(ops[0], 1, ASM_NPOS), &rd) ||
                !asm_parse_int(asm_slice_sub(ops[1], 1, ASM_NPOS), &rn))
                return asm_fail_msg(e, "Invalid floating-point move instruction");
            insn = 0x1E270000u | (((uint32_t)rn & 0x1Fu) << 5) | ((uint32_t)rd & 0x1Fu);
            asm_emit_word(e, insn);
            return ASM_ENCODE_OK;
        }
        if (ops[0].n > 0 && ops[0].p[0] == 'd' && ops[1].n > 0 && ops[1].p[0] == 'x')
        {
            if (!asm_parse_int(asm_slice_sub(ops[0], 1, ASM_NPOS), &rd) ||
                !asm_parse_int(asm_slice_sub(ops[1], 1, ASM_NPOS), &rn))
                return asm_fail_msg(e, "Invalid floating-point move instruction");
            insn = 0x9E670000u | (((uint32_t)rn & 0x1Fu) << 5) | ((uint32_t)rd & 0x1Fu);
            asm_emit_word(e, insn);
            return ASM_ENCODE_OK;
        }
        /* Destination in a general register, source in an FP register. */
        if (ops[0].n > 0 && ops[0].p[0] == 'w' && ops[1].n > 0 && ops[1].p[0] == 's')
        {
            if (!asm_parse_int(asm_slice_sub(ops[0], 1, ASM_NPOS), &rd) ||
                !asm_parse_int(asm_slice_sub(ops[1], 1, ASM_NPOS), &rn))
                return asm_fail_msg(e, "Invalid floating-point move instruction");
            insn = 0x1E260000u | (((uint32_t)rn & 0x1Fu) << 5) | ((uint32_t)rd & 0x1Fu);
            asm_emit_word(e, insn);
            return ASM_ENCODE_OK;
        }
        if (ops[0].n > 0 && ops[0].p[0] == 'x' && ops[1].n > 0 && ops[1].p[0] == 'd')
        {
            if (!asm_parse_int(asm_slice_sub(ops[0], 1, ASM_NPOS), &rd) ||
                !asm_parse_int(asm_slice_sub(ops[1], 1, ASM_NPOS), &rn))
                return asm_fail_msg(e, "Invalid floating-point move instruction");
            insn = 0x9E660000u | (((uint32_t)rn & 0x1Fu) << 5) | ((uint32_t)rd & 0x1Fu);
            asm_emit_word(e, insn);
            return ASM_ENCODE_OK;
        }
        return asm_fail_msg(e, "Invalid floating-point move instruction");
    }

    if (strcmp(mnemonic, "fneg") == 0)
    {
        bool is_double;

        op_count = asm_split_operands(operands, ops, ASM_MAX_OPERANDS);
        if (op_count < 2)
            return asm_fail_operands(e, mnemonic);
        if (!asm_parse_int(asm_slice_sub(ops[0], 1, ASM_NPOS), &rd) ||
            !asm_parse_int(asm_slice_sub(ops[1], 1, ASM_NPOS), &rn))
            return asm_fail_operands(e, mnemonic);
        is_double = ops[0].n > 0 && ops[0].p[0] == 'd';
        insn = (is_double ? 0x1E614000u : 0x1E214000u) | (((uint32_t)rn & 0x1Fu) << 5) |
               ((uint32_t)rd & 0x1Fu);
        asm_emit_word(e, insn);
        return ASM_ENCODE_OK;
    }

    if (strcmp(mnemonic, "fcmp") == 0)
    {
        bool is_double;

        op_count = asm_split_operands(operands, ops, ASM_MAX_OPERANDS);
        if (op_count < 2)
            return asm_fail_operands(e, mnemonic);
        if (!asm_parse_int(asm_slice_sub(ops[0], 1, ASM_NPOS), &rn) ||
            !asm_parse_int(asm_slice_sub(ops[1], 1, ASM_NPOS), &rm))
            return asm_fail_operands(e, mnemonic);
        is_double = ops[0].n > 0 && ops[0].p[0] == 'd';
        insn = (is_double ? 0x1E602000u : 0x1E202000u) | (((uint32_t)rm & 0x1Fu) << 16) |
               (((uint32_t)rn & 0x1Fu) << 5);
        asm_emit_word(e, insn);
        return ASM_ENCODE_OK;
    }

    if (strcmp(mnemonic, "fadd") == 0 || strcmp(mnemonic, "fsub") == 0 ||
        strcmp(mnemonic, "fmul") == 0 || strcmp(mnemonic, "fdiv") == 0)
    {
        uint32_t opbase = 0;
        bool is_double;

        op_count = asm_split_operands(operands, ops, ASM_MAX_OPERANDS);
        if (op_count < 3)
            return asm_fail_operands(e, mnemonic);
        if (!asm_parse_int(asm_slice_sub(ops[0], 1, ASM_NPOS), &rd) ||
            !asm_parse_int(asm_slice_sub(ops[1], 1, ASM_NPOS), &rn) ||
            !asm_parse_int(asm_slice_sub(ops[2], 1, ASM_NPOS), &rm))
            return asm_fail_operands(e, mnemonic);
        is_double = ops[0].n > 0 && ops[0].p[0] == 'd';
        if (strcmp(mnemonic, "fadd") == 0)
            opbase = is_double ? 0x1E602800u : 0x1E202800u;
        else if (strcmp(mnemonic, "fsub") == 0)
            opbase = is_double ? 0x1E603800u : 0x1E203800u;
        else if (strcmp(mnemonic, "fmul") == 0)
            opbase = is_double ? 0x1E600800u : 0x1E200800u;
        else
            opbase = is_double ? 0x1E601800u : 0x1E201800u;
        insn = opbase | (((uint32_t)rm & 0x1Fu) << 16) | (((uint32_t)rn & 0x1Fu) << 5) |
               ((uint32_t)rd & 0x1Fu);
        asm_emit_word(e, insn);
        return ASM_ENCODE_OK;
    }

    if (strcmp(mnemonic, "and") == 0 || strcmp(mnemonic, "orr") == 0 || strcmp(mnemonic, "eor") == 0)
    {
        uint32_t base;

        op_count = asm_split_operands(operands, ops, ASM_MAX_OPERANDS);
        if (op_count < 3)
            return asm_fail_operands(e, mnemonic);
        if (!asm_parse_int(asm_slice_sub(ops[0], 1, ASM_NPOS), &rd) ||
            !asm_parse_int(asm_slice_sub(ops[1], 1, ASM_NPOS), &rn) ||
            !asm_parse_int(asm_slice_sub(ops[2], 1, ASM_NPOS), &rm))
            return asm_fail_operands(e, mnemonic);
        base = strcmp(mnemonic, "and") == 0 ? 0x8A000000u :
               strcmp(mnemonic, "orr") == 0 ? 0xAA000000u : 0xCA000000u;
        insn = base | (((uint32_t)rm & 0x1Fu) << 16) | (((uint32_t)rn & 0x1Fu) << 5) |
               ((uint32_t)rd & 0x1Fu);
        asm_emit_word(e, insn);
        return ASM_ENCODE_OK;
    }

    if (strcmp(mnemonic, "lsl") == 0 || strcmp(mnemonic, "lsr") == 0 ||
        strcmp(mnemonic, "asr") == 0)
    {
        uint32_t base;
        bool is_64;
        int width;

        op_count = asm_split_operands(operands, ops, ASM_MAX_OPERANDS);
        if (op_count < 3)
            return asm_fail_operands(e, mnemonic);

        /*
         * The destination register prefix selects the operand width: an
         * "x" register operates on 64 bits, a "w" register on 32 bits.
         * Shift immediates and register forms are encoded differently for
         * the two widths, so the prefix must be inspected before choosing
         * an encoding.
         */
        is_64 = !(ops[0].n > 0 && ops[0].p[0] == 'w');
        width = is_64 ? 64 : 32;

        rd = asm_regnum(ops[0]);
        rn = asm_regnum(ops[1]);
        if (rd < 0 || rn < 0)
            return asm_fail_operands(e, mnemonic);

        /*
         * Immediate shift form: the third operand begins with '#'. Encode
         * as UBFM for logical shifts (lsl/lsr) and SBFM for arithmetic
         * shifts (asr). The immediate is validated against the operand
         * width; out-of-range amounts are rejected.
         */
        if (ops[2].n > 0 && ops[2].p[0] == '#')
        {
            uint32_t immr;
            uint32_t imms;

            if (!asm_parse_int(asm_slice_sub(ops[2], 1, ASM_NPOS), &imm))
                return asm_fail_operands(e, mnemonic);
            if (imm < 0 || imm >= width)
                return asm_fail_operands(e, mnemonic);

            if (strcmp(mnemonic, "lsl") == 0)
            {
                immr = (uint32_t)(width - imm);
                imms = (uint32_t)(width - 1 - imm);
            }
            else
            {
                immr = (uint32_t)imm;
                imms = (uint32_t)(width - 1);
            }

            if (strcmp(mnemonic, "asr") == 0)
                base = is_64 ? 0x93400000u : 0x13000000u;
            else
                base = is_64 ? 0xD3400000u : 0x53000000u;

            insn = base | ((immr & 0x3Fu) << 16) | ((imms & 0x3Fu) << 10) |
                   (((uint32_t)rn & 0x1Fu) << 5) | ((uint32_t)rd & 0x1Fu);
            asm_emit_word(e, insn);
            return ASM_ENCODE_OK;
        }

        /*
         * Register shift form: the third operand names a register holding
         * the shift amount.
         */
        rm = asm_regnum(ops[2]);
        if (rm < 0)
            return asm_fail_operands(e, mnemonic);
        if (strcmp(mnemonic, "lsl") == 0)
            base = is_64 ? 0x9AC02000u : 0x1AC02000u;
        else if (strcmp(mnemonic, "lsr") == 0)
            base = is_64 ? 0x9AC02400u : 0x1AC02400u;
        else
            base = is_64 ? 0x9AC02800u : 0x1AC02800u;
        insn = base | (((uint32_t)rm & 0x1Fu) << 16) | (((uint32_t)rn & 0x1Fu) << 5) |
               ((uint32_t)rd & 0x1Fu);
        asm_emit_word(e, insn);
        return ASM_ENCODE_OK;
    }

    if (strcmp(mnemonic, "mvn") == 0)
    {
        op_count = asm_split_operands(operands, ops, ASM_MAX_OPERANDS);
        if (op_count < 2)
            return asm_fail_operands(e, mnemonic);
        if (!asm_parse_int(asm_slice_sub(ops[0], 1, ASM_NPOS), &rd) ||
            !asm_parse_int(asm_slice_sub(ops[1], 1, ASM_NPOS), &rm))
            return asm_fail_operands(e, mnemonic);
        /* MVN Xd, Xm is ORN Xd, XZR, Xm. */
        insn = 0xAA2003E0u | (((uint32_t)rm & 0x1Fu) << 16) | ((uint32_t)rd & 0x1Fu);
        asm_emit_word(e, insn);
        return ASM_ENCODE_OK;
    }

    if (strcmp(mnemonic, "mov") == 0)
    {
        op_count = asm_split_operands(operands, ops, ASM_MAX_OPERANDS);
        if (op_count != 2)
            return asm_fail_msg(e, "Invalid move instruction");
        if (ops[1].n > 0 && ops[1].p[0] == '#')
        {
            /* MOVZ: sf=1, hw=0, imm16, Rd. A 64-bit move of a small value. */
            rd = asm_regnum(ops[0]);
            if (!asm_parse_int(asm_slice_sub(ops[1], 1, ASM_NPOS), &imm))
                return asm_fail_msg(e, "Invalid move instruction");
            insn = 0xD2800000u | ((uint32_t)rd & 0x1Fu) | (((uint32_t)imm & 0xFFFFu) << 5);
        }
        else
        {
            rd = asm_regnum(ops[0]);
            rn = asm_regnum(ops[1]);
            if (asm_slice_eq(ops[0], "sp") || asm_slice_eq(ops[1], "sp"))
            {
                /* MOV involving the stack pointer must be encoded as ADD. */
                insn = 0x91000000u | ((uint32_t)rd & 0x1Fu) | (((uint32_t)rn & 0x1Fu) << 5);
            }
            else
            {
                /* ORR Rd, XZR, Rm. */
                insn = 0xAA0003E0u | (((uint32_t)rn & 0x1Fu) << 16) | ((uint32_t)rd & 0x1Fu);
            }
        }
        asm_emit_word(e, insn);
        return ASM_ENCODE_OK;
    }

    if (strcmp(mnemonic, "add") == 0 || strcmp(mnemonic, "sub") == 0)
    {
        bool is_sub = strcmp(mnemonic, "sub") == 0;

        op_count = asm_split_operands(operands, ops, ASM_MAX_OPERANDS);
        if (op_count != 3)
            return asm_fail_msg(e, "Invalid add/subtract instruction");
        if (ops[2].n > 0 && ops[2].p[0] == '#')
        {
            rd = asm_regnum(ops[0]);
            rn = asm_regnum(ops[1]);
            if (!asm_parse_int(asm_slice_sub(ops[2], 1, ASM_NPOS), &imm))
                return asm_fail_msg(e, "Invalid add/subtract instruction");
            insn = (is_sub ? 0xD1000000u : 0x91000000u) | ((uint32_t)rd & 0x1Fu) |
                   (((uint32_t)rn & 0x1Fu) << 5) | (((uint32_t)imm & 0xFFFu) << 10);
        }
        else
        {
            rd = asm_regnum(ops[0]);
            rn = asm_regnum(ops[1]);
            rm = asm_regnum(ops[2]);
            insn = (is_sub ? 0xCB000000u : 0x8B000000u) | ((uint32_t)rd & 0x1Fu) |
                   (((uint32_t)rn & 0x1Fu) << 5) | (((uint32_t)rm & 0x1Fu) << 16);
        }
        asm_emit_word(e, insn);
        return ASM_ENCODE_OK;
    }

    if (strcmp(mnemonic, "udiv") == 0)
    {
        op_count = asm_split_operands(operands, ops, ASM_MAX_OPERANDS);
        if (op_count < 3)
            return asm_fail_operands(e, mnemonic);
        rd = asm_regnum(ops[0]);
        rn = asm_regnum(ops[1]);
        rm = asm_regnum(ops[2]);
        insn = 0x9AC00800u | ((uint32_t)rd & 0x1Fu) | (((uint32_t)rn & 0x1Fu) << 5) |
               (((uint32_t)rm & 0x1Fu) << 16);
        asm_emit_word(e, insn);
        return ASM_ENCODE_OK;
    }

    if (strcmp(mnemonic, "mul") == 0)
    {
        op_count = asm_split_operands(operands, ops, ASM_MAX_OPERANDS);
        if (op_count < 3)
            return asm_fail_operands(e, mnemonic);
        rd = asm_regnum(ops[0]);
        rn = asm_regnum(ops[1]);
        rm = asm_regnum(ops[2]);
        /* MUL 64-bit is MADD with Ra = XZR. */
        insn = 0x9B007C00u | ((uint32_t)rd & 0x1Fu) | (((uint32_t)rn & 0x1Fu) << 5) |
               (((uint32_t)rm & 0x1Fu) << 16);
        asm_emit_word(e, insn);
        return ASM_ENCODE_OK;
    }

    if (strcmp(mnemonic, "sdiv") == 0)
    {
        op_count = asm_split_operands(operands, ops, ASM_MAX_OPERANDS);
        if (op_count < 3)
            return asm_fail_operands(e, mnemonic);
        rd = asm_regnum(ops[0]);
        rn = asm_regnum(ops[1]);
        rm = asm_regnum(ops[2]);
        insn = 0x9AC00C00u | ((uint32_t)rd & 0x1Fu) | (((uint32_t)rn & 0x1Fu) << 5) |
               (((uint32_t)rm & 0x1Fu) << 16);
        asm_emit_word(e, insn);
        return ASM_ENCODE_OK;
    }

    if (strcmp(mnemonic, "msub") == 0)
    {
        op_count = asm_split_operands(operands, ops, ASM_MAX_OPERANDS);
        if (op_count < 4)
            return asm_fail_operands(e, mnemonic);
        rd = asm_regnum(ops[0]);
        rn = asm_regnum(ops[1]);
        rm = asm_regnum(ops[2]);
        ra = asm_regnum(ops[3]);
        insn = 0x9B008000u | ((uint32_t)rd & 0x1Fu) | (((uint32_t)rn & 0x1Fu) << 5) |
               (((uint32_t)ra & 0x1Fu) << 10) | (((uint32_t)rm & 0x1Fu) << 16);
        asm_emit_word(e, insn);
        return ASM_ENCODE_OK;
    }

    if (strcmp(mnemonic, "cmp") == 0)
    {
        op_count = asm_split_operands(operands, ops, ASM_MAX_OPERANDS);
        if (op_count < 2)
            return asm_fail_operands(e, mnemonic);
        if (op_count == 2 && ops[1].n > 0 && ops[1].p[0] == '#')
        {
            /* SUBS XZR, Xn, #imm. */
            rn = asm_regnum(ops[0]);
            if (!asm_parse_int(asm_slice_sub(ops[1], 1, ASM_NPOS), &imm))
                return asm_fail_operands(e, mnemonic);
            insn = 0xF100001Fu | (((uint32_t)rn & 0x1Fu) << 5) |
                   (((uint32_t)imm & 0xFFFu) << 10);
        }
        else
        {
            /* SUBS XZR, Xn, Xm. */
            rn = asm_regnum(ops[0]);
            rm = asm_regnum(ops[1]);
            insn = 0xEB00001Fu | (((uint32_t)rn & 0x1Fu) << 5) |
                   (((uint32_t)rm & 0x1Fu) << 16);
        }
        asm_emit_word(e, insn);
        return ASM_ENCODE_OK;
    }

    if (strcmp(mnemonic, "cset") == 0)
    {
        int inverted;

        op_count = asm_split_operands(operands, ops, ASM_MAX_OPERANDS);
        if (op_count < 2)
            return asm_fail_operands(e, mnemonic);
        rd = asm_regnum(ops[0]);
        cc = asm_condcode(ops[1]);
        /* CSINC Xd, XZR, XZR, invert(cond). */
        inverted = cc ^ 1;
        insn = 0x9A9F07E0u | ((uint32_t)rd & 0x1Fu) | ((uint32_t)inverted << 12);
        asm_emit_word(e, insn);
        return ASM_ENCODE_OK;
    }

    if (strcmp(mnemonic, "neg") == 0)
    {
        op_count = asm_split_operands(operands, ops, ASM_MAX_OPERANDS);
        if (op_count < 2)
            return asm_fail_operands(e, mnemonic);
        rd = asm_regnum(ops[0]);
        rn = asm_regnum(ops[1]);
        /* SUB Xd, XZR, Xm. */
        insn = 0xCB000000u | ((uint32_t)rd & 0x1Fu) | (31u << 5) |
               (((uint32_t)rn & 0x1Fu) << 16);
        asm_emit_word(e, insn);
        return ASM_ENCODE_OK;
    }

    if (strcmp(mnemonic, "ret") == 0)
    {
        asm_emit_word(e, 0xD65F03C0u);
        return ASM_ENCODE_OK;
    }

    if (strcmp(mnemonic, "br") == 0 || strcmp(mnemonic, "blr") == 0)
    {
        uint32_t base = strcmp(mnemonic, "blr") == 0 ? 0xD63F0000u : 0xD61F0000u;

        op_count = asm_split_operands(operands, ops, ASM_MAX_OPERANDS);
        if (op_count < 1)
            return asm_fail_operands(e, mnemonic);
        rn = asm_regnum(ops[0]);
        asm_emit_word(e, base | (((uint32_t)rn & 0x1Fu) << 5));
        return ASM_ENCODE_OK;
    }

    if (strncmp(mnemonic, "b.", 2) == 0)
    {
        struct asm_slice target = asm_slice_trim(operands);

        cc = asm_condcode(asm_slice_cstr(mnemonic + 2));
        if (cc < 0)
            return asm_fail_msg(e, "Invalid branch condition");
        return asm_emit_branch(e, 0x54000000u | ((uint32_t)cc & 0xFu), target,
                               ASM_RELOC_CONDBR19);
    }

    if (strcmp(mnemonic, "cbz") == 0 || strcmp(mnemonic, "cbnz") == 0)
    {
        size_t comma = asm_slice_find_char(operands, ',');
        struct asm_slice rstr = asm_slice_trim(asm_slice_sub(operands, 0, comma));
        struct asm_slice target = asm_slice_trim(
            asm_slice_sub(operands, comma == ASM_NPOS ? 0 : comma + 1, ASM_NPOS));
        uint32_t base = strcmp(mnemonic, "cbnz") == 0 ? 0xB5000000u : 0xB4000000u;

        rn = asm_regnum(rstr);
        return asm_emit_branch(e, base | ((uint32_t)rn & 0x1Fu), target, ASM_RELOC_CONDBR19);
    }

    if (strcmp(mnemonic, "ldrb") == 0 || strcmp(mnemonic, "strb") == 0)
    {
        size_t lb = asm_slice_find_char(operands, '[');
        size_t rb = asm_slice_find_char(operands, ']');
        struct asm_slice rtstr = asm_slice_trim(asm_slice_sub(operands, 0, lb));
        struct asm_slice inside = asm_slice_trim(asm_bracket_inside(operands, lb, rb));
        int rt = asm_regnum(rtstr);
        int base = asm_regnum(inside);
        int offset = 0;
        size_t comma = asm_slice_find_char(inside, ',');

        if (comma != ASM_NPOS)
        {
            base = asm_regnum(asm_slice_sub(inside, 0, comma));
            /* An unparsable displacement leaves the offset at zero, which is
               what the original (conditionally parsed) code did. */
            if (!asm_parse_filtered_int(asm_slice_sub(inside, comma + 1, ASM_NPOS), &offset))
                offset = 0;
        }
        if (strcmp(mnemonic, "ldrb") == 0)
        {
            insn = 0x39400000u | (((uint32_t)offset & 0xFFFu) << 10) |
                   (((uint32_t)base & 0x1Fu) << 5) | ((uint32_t)rt & 0x1Fu);
        }
        else
        {
            uint32_t uimm = (uint32_t)offset & 0x1FFu;

            insn = 0x38000000u | (uimm << 12) | (((uint32_t)base & 0x1Fu) << 5) |
                   ((uint32_t)rt & 0x1Fu);
        }
        asm_emit_word(e, insn);
        return ASM_ENCODE_OK;
    }

    if (strcmp(mnemonic, "ldrh") == 0 || strcmp(mnemonic, "strh") == 0)
    {
        size_t lb = asm_slice_find_char(operands, '[');
        size_t rb = asm_slice_find_char(operands, ']');
        struct asm_slice rtstr = asm_slice_trim(asm_slice_sub(operands, 0, lb));
        struct asm_slice inside = asm_slice_trim(asm_bracket_inside(operands, lb, rb));
        int rt = asm_regnum(rtstr);
        int base = -1;
        int offset = 0;
        size_t comma = asm_slice_find_char(inside, ',');

        if (comma != ASM_NPOS)
        {
            base = asm_regnum(asm_slice_sub(inside, 0, comma));
            if (!asm_parse_filtered_int(asm_slice_sub(inside, comma + 1, ASM_NPOS), &offset))
                offset = 0;
        }
        else
        {
            base = asm_regnum(inside);
        }
        if (strcmp(mnemonic, "ldrh") == 0)
        {
            insn = 0x78400000u | (((uint32_t)offset & 0x1FFu) << 12) |
                   (((uint32_t)base & 0x1Fu) << 5) | ((uint32_t)rt & 0x1Fu);
        }
        else
        {
            insn = 0x78000000u | (((uint32_t)offset & 0x1FFu) << 12) |
                   (((uint32_t)base & 0x1Fu) << 5) | ((uint32_t)rt & 0x1Fu);
        }
        asm_emit_word(e, insn);
        return ASM_ENCODE_OK;
    }

    if (strcmp(mnemonic, "b") == 0 || strcmp(mnemonic, "beq") == 0 ||
        strcmp(mnemonic, "bl") == 0)
    {
        struct asm_slice target = asm_slice_trim(operands);
        uint32_t word;
        int type;

        if (strcmp(mnemonic, "bl") == 0)
        {
            word = 0x94000000u;
            type = (int)TRACEY_ELF_R_AARCH64_CALL26;
        }
        else if (strcmp(mnemonic, "b") == 0)
        {
            word = 0x14000000u;
            type = (int)TRACEY_ELF_R_AARCH64_JUMP26;
        }
        else
        {
            /* beq is the conditional form emitted by the code generator. */
            word = 0x54000000u;
            type = ASM_RELOC_CONDBR19;
        }
        return asm_emit_branch(e, word, target, type);
    }

    if (strcmp(mnemonic, "stp") == 0 || strcmp(mnemonic, "ldp") == 0)
    {
        size_t lb = asm_slice_find_char(operands, '[');
        size_t rb = asm_slice_find_char(operands, ']');
        struct asm_slice regs = asm_slice_trim(asm_slice_sub(operands, 0, lb));
        struct asm_slice ro[ASM_MAX_OPERANDS];
        struct asm_slice inside = asm_slice_trim(asm_bracket_inside(operands, lb, rb));
        size_t ro_count = asm_split_operands(regs, ro, ASM_MAX_OPERANDS);
        size_t first_lb = asm_slice_find_char(operands, '[');
        size_t first_rb = asm_slice_find_char(operands, ']');
        /* A post-index operand list has a comma after the closing bracket.
           The pre-index form is not detected: stp always encodes the
           pre-index base and ldp always encodes the post-index base, which is
           exactly what the previous implementation produced. */
        bool post = asm_slice_find_from(operands, ']', first_lb) != ASM_NPOS &&
                    asm_slice_find_from(operands, ',', first_rb) != ASM_NPOS;
        int rt;
        int rt2;
        int base = -1;
        int offset = 0;
        int imm7;
        uint32_t uimm;

        if (ro_count < 2)
            return asm_fail_operands(e, mnemonic);
        rt = asm_regnum(ro[0]);
        rt2 = asm_regnum(ro[1]);
        {
            size_t comma = asm_slice_find_char(inside, ',');

            if (comma == ASM_NPOS)
            {
                base = asm_regnum(inside);
            }
            else
            {
                struct asm_slice immediate =
                    asm_slice_trim(asm_slice_sub(inside, comma + 1, ASM_NPOS));

                base = asm_regnum(asm_slice_sub(inside, 0, comma));
                if (!asm_parse_int(asm_slice_sub(immediate, 1, ASM_NPOS), &offset))
                    return asm_fail_operands(e, mnemonic);
            }
        }
        if (post)
        {
            /* The displacement of the post-index form follows the closing
               bracket and is not part of the bracketed operand. */
            size_t rbp = asm_slice_find_from(operands, ']', rb);
            size_t after = (rbp == ASM_NPOS) ? 0 : rbp + 1;
            struct asm_slice rest = asm_slice_trim(asm_slice_sub(operands, after, ASM_NPOS));

            if (asm_slice_find_char(rest, '#') != ASM_NPOS &&
                !asm_parse_filtered_int(rest, &offset))
                offset = 0;
        }
        /* imm7 is a signed multiple of eight in the range -64..63. */
        imm7 = offset / 8;
        if (imm7 < -64)
            imm7 = -64;
        if (imm7 > 63)
            imm7 = 63;
        uimm = (uint32_t)imm7 & 0x7Fu;
        if (strcmp(mnemonic, "stp") == 0)
        {
            insn = 0xA9800000u | (uimm << 15) | (((uint32_t)rt2 & 0x1Fu) << 10) |
                   (((uint32_t)base & 0x1Fu) << 5) | ((uint32_t)rt & 0x1Fu);
        }
        else
        {
            insn = 0xA8C00000u | (uimm << 15) | (((uint32_t)rt2 & 0x1Fu) << 10) |
                   (((uint32_t)base & 0x1Fu) << 5) | ((uint32_t)rt & 0x1Fu);
        }
        asm_emit_word(e, insn);
        return ASM_ENCODE_OK;
    }

    if (strcmp(mnemonic, "ldr") == 0 || strcmp(mnemonic, "str") == 0)
    {
        bool is_store = strcmp(mnemonic, "str") == 0;
        size_t lb = asm_slice_find_char(operands, '[');
        size_t rb = asm_slice_find_char(operands, ']');
        struct asm_slice rtstr = asm_slice_trim(asm_slice_sub(operands, 0, lb));
        struct asm_slice inside = asm_slice_trim(asm_bracket_inside(operands, lb, rb));
        int rt = asm_regnum(rtstr);
        int base = -1;
        int offset = 0;
        int imm9;
        /* '!' after the closing bracket marks pre-index writeback, a comma
           after it marks post-index writeback. */
        bool pre = asm_slice_find_from(operands, '!', rb) != ASM_NPOS;
        bool post = asm_slice_find_from(operands, ',', rb) != ASM_NPOS;

        {
            size_t comma = asm_slice_find_char(inside, ',');

            if (comma == ASM_NPOS)
            {
                base = asm_regnum(inside);
            }
            else
            {
                base = asm_regnum(asm_slice_sub(inside, 0, comma));
                if (!asm_parse_filtered_int(asm_slice_sub(inside, comma + 1, ASM_NPOS),
                                            &offset))
                    return asm_fail_operands(e, mnemonic);
            }
        }
        {
            size_t rbp = asm_slice_find_from(operands, ']', rb);
            size_t after = (rbp == ASM_NPOS) ? 0 : rbp + 1;
            struct asm_slice rest = asm_slice_trim(asm_slice_sub(operands, after, ASM_NPOS));
            int post_offset;

            if (post && asm_parse_filtered_int(rest, &post_offset))
                offset = post_offset;
        }
        /* The 9-bit signed displacement is placed at bits 12..20. The
           original encoder used the same base for the pre-index and the
           plain signed-offset forms; that quirk is preserved here. */
        imm9 = offset & 0x1FF;
        if (pre)
        {
            insn = (is_store ? (rtstr.p[0] == 'w' ? 0xB8000000u : 0xF8000000u)
                           : (rtstr.p[0] == 'w' ? 0xB8400000u : 0xF8400000u)) | ((uint32_t)imm9 << 12) |
                   ((uint32_t)base << 5) | (uint32_t)rt;
        }
        else if (post)
        {
            insn = (is_store ? (rtstr.p[0] == 'w' ? 0xB8000C00u : 0xF8000C00u)
                           : (rtstr.p[0] == 'w' ? 0xB8400C00u : 0xF8400C00u)) | ((uint32_t)imm9 << 12) |
                   ((uint32_t)base << 5) | (uint32_t)rt;
        }
        else
        {
            insn = (is_store ? (rtstr.p[0] == 'w' ? 0xB8000000u : 0xF8000000u)
                           : (rtstr.p[0] == 'w' ? 0xB8400000u : 0xF8400000u)) | ((uint32_t)imm9 << 12) |
                   ((uint32_t)base << 5) | (uint32_t)rt;
        }
        asm_emit_word(e, insn);
        return ASM_ENCODE_OK;
    }

    if (strcmp(mnemonic, "adr") == 0)
    {
        const struct asm_label *label;
        int64_t delta;

        op_count = asm_split_operands(operands, ops, ASM_MAX_OPERANDS);
        if (op_count < 2)
            return asm_fail_operands(e, mnemonic);
        rd = asm_regnum(ops[0]);
        {
            struct asm_slice target = asm_slice_trim(ops[1]);

            label = asm_label_find(e->p_labels, target);
            if (label == NULL)
            {
                asm_set_error(e->p_result, "Undefined assembler label: %.*s", (int)target.n,
                              target.p);
                return ASM_ENCODE_ERROR;
            }
            delta = (int64_t)label->offset - (int64_t)e->off;
        }
        /* ADR: immlo occupies bits 29..30, immhi bits 5..23. */
        insn = 0x10000000u | ((uint32_t)(delta & 0x3) << 29) |
               ((uint32_t)((delta >> 2) & 0x7FFFF) << 5) | ((uint32_t)rd & 0x1Fu);
        asm_emit_word(e, insn);
        return ASM_ENCODE_OK;
    }

    if (strcmp(mnemonic, "svc") == 0)
    {
        asm_emit_word(e, 0xD4000001u);
        return ASM_ENCODE_OK;
    }

    asm_set_error(e->p_result, "Unknown assembler instruction: %s", mnemonic);
    return ASM_ENCODE_ERROR;
}

/*
 * tracey_assemble_aarch64 - assemble one buffer of AArch64 source text.
 *
 * The input is copied so that lines can be terminated in place; the caller's
 * buffer is never modified or retained. Assembly runs in two passes: the
 * first records label offsets and the DWARF file list, the second encodes
 * instructions and fills in line table entries.
 *
 * On any error the result is left with ok == false and a diagnostic in
 * error; partially built output is released before returning.
 */
