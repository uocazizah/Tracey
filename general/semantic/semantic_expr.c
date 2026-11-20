#include "semantic_internal.h"


enum tracey_type_kind tracey_semantic_check_expr(struct tracey_semantic_analyzer *sa,
                                                        struct tracey_expr *e,
                                                        const struct tracey_function_decl *fn)
{
    (void)fn;
    if (e == NULL)
        return TRACEY_TYPE_VOID;

    switch (e->kind)
    {
        case TRACEY_EXPR_INT:
        {
            enum tracey_type_kind t = e->as.int_expr.type;
            if (t == TRACEY_TYPE_VOID)
                t = TRACEY_TYPE_INT;
            return t;
        }
        case TRACEY_EXPR_BOOL:
            return TRACEY_TYPE_BOOL;
        case TRACEY_EXPR_REAL:
            return e->as.real_expr.is_float ? TRACEY_TYPE_FLOAT : TRACEY_TYPE_DOUBLE;
        case TRACEY_EXPR_STRING:
            return TRACEY_TYPE_CHAR_PTR;
        case TRACEY_EXPR_CHAR:
            return TRACEY_TYPE_CHAR;
        case TRACEY_EXPR_IDENT:
        {
            struct tracey_semantic_symbol sym;
            if (!tracey_semantic_lookup(sa, e->as.ident_expr.name, &sym))
            {
                char buf[256];
                snprintf(buf, sizeof(buf), "Undefined identifier '%s'", e->as.ident_expr.name);
                tracey_semantic_error(sa, buf, e->loc);
                return TRACEY_TYPE_INT;
            }
            if (sym.is_function)
            {
                /* A bare function name decays to a function pointer value,
                 * which can be assigned to or passed as a function-pointer
                 * variable. */
                return TRACEY_TYPE_FUNC_PTR;
            }
            return sym.type;
        }
        case TRACEY_EXPR_INDEX:
        {
            enum tracey_type_kind bt = tracey_semantic_check_expr(sa, e->as.index_expr.base, fn);
            enum tracey_type_kind it = tracey_semantic_check_expr(sa, e->as.index_expr.index, fn);
            if (it != TRACEY_TYPE_INT)
                tracey_semantic_error(sa, "Array index must be int", e->loc);
            if (bt == TRACEY_TYPE_INT_PTR) return TRACEY_TYPE_INT;
            if (bt == TRACEY_TYPE_CHAR_PTR) return TRACEY_TYPE_CHAR;
            if (bt == TRACEY_TYPE_FLOAT_PTR) return TRACEY_TYPE_FLOAT;
            if (bt == TRACEY_TYPE_STRUCT) return TRACEY_TYPE_STRUCT;
            if (bt == TRACEY_TYPE_DOUBLE_PTR) return TRACEY_TYPE_DOUBLE;
            tracey_semantic_error(sa, "Subscript requires array or pointer", e->loc);
            return TRACEY_TYPE_INT;
        }
        case TRACEY_EXPR_INIT_LIST:
            return TRACEY_TYPE_VOID;
        case TRACEY_EXPR_MEMBER:
        {
            struct tracey_expr *base = e->as.member_expr.base;
            int base_sid;
            const struct tracey_struct_member *m;

            (void)tracey_semantic_check_expr(sa, base, fn);
            base_sid = tracey_semantic_expr_struct_id(sa, base);
            if (base_sid < 0)
            {
                tracey_semantic_error(sa, "Member access requires a struct value", e->loc);
                return TRACEY_TYPE_INT;
            }
            m = tracey_semantic_member(sa, base_sid, e->as.member_expr.field);
            if (m == NULL)
            {
                char buf[256];
                const char *tag = (sa->program != NULL)
                                      ? sa->program->structs[base_sid].name : "";

                snprintf(buf, sizeof(buf), "Struct '%s' has no member named '%s'",
                         tag ? tag : "",
                         e->as.member_expr.field ? e->as.member_expr.field : "");
                tracey_semantic_error(sa, buf, e->loc);
                return TRACEY_TYPE_INT;
            }
            return m->type;
        }
        case TRACEY_EXPR_ADDR:
        {
            enum tracey_type_kind t = tracey_semantic_check_expr(sa, e->as.addr_expr.operand, fn);
            if (t == TRACEY_TYPE_INT) return TRACEY_TYPE_INT_PTR;
            if (t == TRACEY_TYPE_CHAR) return TRACEY_TYPE_CHAR_PTR;
            if (t == TRACEY_TYPE_FLOAT) return TRACEY_TYPE_FLOAT_PTR;
            if (t == TRACEY_TYPE_DOUBLE) return TRACEY_TYPE_DOUBLE_PTR;
            if (t == TRACEY_TYPE_STRUCT) return TRACEY_TYPE_STRUCT_PTR;
            tracey_semantic_error(sa, "Cannot take address", e->loc);
            return TRACEY_TYPE_INT_PTR;
        }
        case TRACEY_EXPR_DEREF:
        {
            enum tracey_type_kind t = tracey_semantic_check_expr(sa, e->as.deref_expr.operand, fn);
            if (t == TRACEY_TYPE_INT_PTR) return TRACEY_TYPE_INT;
            if (t == TRACEY_TYPE_CHAR_PTR) return TRACEY_TYPE_CHAR;
            if (t == TRACEY_TYPE_SHORT_PTR) return TRACEY_TYPE_SHORT;
            if (t == TRACEY_TYPE_LONG_PTR) return TRACEY_TYPE_LONG;
            if (t == TRACEY_TYPE_SIZE_T_PTR) return TRACEY_TYPE_SIZE_T;
            if (t == TRACEY_TYPE_FLOAT_PTR) return TRACEY_TYPE_FLOAT;
            if (t == TRACEY_TYPE_DOUBLE_PTR) return TRACEY_TYPE_DOUBLE;
            if (t == TRACEY_TYPE_STRUCT_PTR) return TRACEY_TYPE_STRUCT;
            tracey_semantic_error(sa, "Cannot dereference non-pointer", e->loc);
            return TRACEY_TYPE_INT;
        }
        case TRACEY_EXPR_UNARY:
        {
            enum tracey_type_kind t = tracey_semantic_check_expr(sa, e->as.unary_expr.operand, fn);
            if (e->as.unary_expr.op == TRACEY_UNARY_NEG)
            {
                if (tracey_is_numeric(t))
                    return t;
                tracey_semantic_error(sa, "Unary '-' requires numeric", e->loc);
                return TRACEY_TYPE_INT;
            }
            if (e->as.unary_expr.op == TRACEY_UNARY_LOG_NOT)
            {
                if (t != TRACEY_TYPE_BOOL)
                    tracey_semantic_error(sa, "'!' requires bool", e->loc);
                return TRACEY_TYPE_BOOL;
            }
            if (e->as.unary_expr.op == TRACEY_UNARY_PRE_INC ||
                e->as.unary_expr.op == TRACEY_UNARY_PRE_DEC ||
                e->as.unary_expr.op == TRACEY_UNARY_POST_INC ||
                e->as.unary_expr.op == TRACEY_UNARY_POST_DEC)
            {
                if (!tracey_is_integer(t))
                    tracey_semantic_error(sa, "Increment/decrement requires integer", e->loc);
                /* operand must be assignable */
                if (!(e->as.unary_expr.operand->kind == TRACEY_EXPR_IDENT ||
                      e->as.unary_expr.operand->kind == TRACEY_EXPR_INDEX ||
                      e->as.unary_expr.operand->kind == TRACEY_EXPR_DEREF ||
                      e->as.unary_expr.operand->kind == TRACEY_EXPR_MEMBER))
                    tracey_semantic_error(sa, "Increment/decrement requires assignable lvalue", e->loc);
                return t;
            }
            if (e->as.unary_expr.op == TRACEY_UNARY_BIT_NOT)
            {
                if (tracey_is_integer(t))
                    return t;
                tracey_semantic_error(sa, "Unary '~' requires integer", e->loc);
                return TRACEY_TYPE_INT;
            }
            return TRACEY_TYPE_INT;
        }
        case TRACEY_EXPR_BINARY:
        {
            if (e->as.binary_expr.op == TRACEY_BINARY_ASSIGN)
            {
                struct tracey_expr *lhs = e->as.binary_expr.left;
                if (lhs && lhs->kind == TRACEY_EXPR_IDENT)
                {
                    struct tracey_semantic_symbol sym;
                    if (!tracey_semantic_lookup(sa, lhs->as.ident_expr.name, &sym) || sym.is_function)
                    {
                        char buf[256];
                        snprintf(buf, sizeof(buf), "Undefined variable '%s'", lhs->as.ident_expr.name);
                        tracey_semantic_error(sa, buf, e->loc);
                        (void)tracey_semantic_check_expr(sa, e->as.binary_expr.right, fn);
                        return TRACEY_TYPE_INT;
                    }
                    {
                        struct tracey_expr *rhs = e->as.binary_expr.right;
                        if (sym.type == TRACEY_TYPE_FUNC_PTR && rhs != NULL &&
                            rhs->kind == TRACEY_EXPR_IDENT)
                        {
                            struct tracey_semantic_symbol rhs_sym;

                            if (tracey_semantic_lookup(sa, rhs->as.ident_expr.name, &rhs_sym) &&
                                rhs_sym.is_function && sa->program != NULL)
                            {
                                struct tracey_function_decl *fdecl = NULL;
                                size_t fi;

                                for (fi = 0; fi < sa->program->function_count; fi++)
                                {
                                    if (sa->program->functions[fi] != NULL &&
                                        sa->program->functions[fi]->name != NULL &&
                                        strcmp(sa->program->functions[fi]->name, rhs->as.ident_expr.name) == 0)
                                    {
                                        fdecl = sa->program->functions[fi];
                                        break;
                                    }
                                }
                                if (fdecl != NULL && sym.fn_sig != NULL &&
                                    fdecl->return_type == sym.fn_sig->return_type &&
                                    fdecl->param_count == sym.fn_sig->param_count)
                                {
                                    size_t pi;
                                    bool ok = true;

                                    for (pi = 0; pi < fdecl->param_count; pi++)
                                    {
                                        if (fdecl->params[pi].type != sym.fn_sig->param_types[pi])
                                        {
                                            ok = false;
                                            break;
                                        }
                                    }
                                    if (ok)
                                        return sym.type;
                                }
                            }
                        }
                        {
                            enum tracey_type_kind rt = tracey_semantic_check_expr(sa, rhs, fn);
                            if (!tracey_types_assignable(sym.type, rt) &&
                                !tracey_semantic_is_narrowing_conversion(sym.type, rt))
                                tracey_semantic_error(sa, "Type mismatch in assignment", e->loc);
                            else if (tracey_semantic_is_narrowing_conversion(sym.type, rt))
                                tracey_semantic_warning(sa, "Narrowing conversion in assignment", e->loc);
                            return sym.type;
                        }
                    }
                }
                if (lhs && lhs->kind == TRACEY_EXPR_INDEX)
                {
                    enum tracey_type_kind bt = tracey_semantic_check_expr(sa, lhs->as.index_expr.base, fn);
                    enum tracey_type_kind it = tracey_semantic_check_expr(sa, lhs->as.index_expr.index, fn);
                    enum tracey_type_kind elem = TRACEY_TYPE_INT;
                    if (it != TRACEY_TYPE_INT)
                        tracey_semantic_error(sa, "Array index must be int", lhs->loc);
                    if (bt == TRACEY_TYPE_CHAR_PTR) elem = TRACEY_TYPE_CHAR;
                    else if (bt == TRACEY_TYPE_FLOAT_PTR) elem = TRACEY_TYPE_FLOAT;
                    else if (bt == TRACEY_TYPE_DOUBLE_PTR) elem = TRACEY_TYPE_DOUBLE;
                    else if (bt != TRACEY_TYPE_INT_PTR)
                        tracey_semantic_error(sa, "Subscript requires array or pointer", lhs->loc);
                    {
                        enum tracey_type_kind rt = tracey_semantic_check_expr(sa, e->as.binary_expr.right, fn);
                        if (rt != elem && !tracey_semantic_is_narrowing_conversion(elem, rt))
                            tracey_semantic_error(sa, "Type mismatch in assignment", e->loc);
                        else if (tracey_semantic_is_narrowing_conversion(elem, rt))
                            tracey_semantic_warning(sa, "Narrowing conversion in assignment", e->loc);
                    }
                    return elem;
                }
                if (lhs && lhs->kind == TRACEY_EXPR_DEREF)
                {
                    enum tracey_type_kind pt = tracey_semantic_check_expr(sa, lhs->as.deref_expr.operand, fn);
                    enum tracey_type_kind elem = TRACEY_TYPE_INT;
                    if (pt == TRACEY_TYPE_CHAR_PTR) elem = TRACEY_TYPE_CHAR;
                    else if (pt == TRACEY_TYPE_FLOAT_PTR) elem = TRACEY_TYPE_FLOAT;
                    else if (pt == TRACEY_TYPE_DOUBLE_PTR) elem = TRACEY_TYPE_DOUBLE;
                    else if (pt != TRACEY_TYPE_INT_PTR)
                        tracey_semantic_error(sa, "Cannot dereference non-pointer", lhs->loc);
                    {
                        enum tracey_type_kind rt = tracey_semantic_check_expr(sa, e->as.binary_expr.right, fn);
                        if (rt != elem && !tracey_semantic_is_narrowing_conversion(elem, rt))
                            tracey_semantic_error(sa, "Type mismatch in assignment", lhs->loc);
                        else if (tracey_semantic_is_narrowing_conversion(elem, rt))
                            tracey_semantic_warning(sa, "Narrowing conversion in assignment", lhs->loc);
                    }
                    return elem;
                }
                if (lhs && lhs->kind == TRACEY_EXPR_MEMBER)
                {
                    enum tracey_type_kind lhs_t = tracey_semantic_check_expr(sa, lhs, fn);
                    enum tracey_type_kind rt = tracey_semantic_check_expr(sa, e->as.binary_expr.right, fn);
                    if (!tracey_types_assignable(lhs_t, rt) &&
                        !tracey_semantic_is_narrowing_conversion(lhs_t, rt))
                        tracey_semantic_error(sa, "Type mismatch in assignment", e->loc);
                    else if (tracey_semantic_is_narrowing_conversion(lhs_t, rt))
                        tracey_semantic_warning(sa, "Narrowing conversion in assignment", e->loc);
                    return lhs_t;
                }
                tracey_semantic_error(sa, "Invalid assignment target", e->loc);
                (void)tracey_semantic_check_expr(sa, e->as.binary_expr.right, fn);
                return TRACEY_TYPE_INT;
            }
            {
                enum tracey_type_kind lt = tracey_semantic_check_expr(sa, e->as.binary_expr.left, fn);
                enum tracey_type_kind rt = tracey_semantic_check_expr(sa, e->as.binary_expr.right, fn);
                switch (e->as.binary_expr.op)
                {
                    case TRACEY_BINARY_ASSIGN:
                    case TRACEY_BINARY_ADD_ASSIGN:
                    case TRACEY_BINARY_SUB_ASSIGN:
                    case TRACEY_BINARY_MUL_ASSIGN:
                    case TRACEY_BINARY_DIV_ASSIGN:
                    case TRACEY_BINARY_MOD_ASSIGN:
                    case TRACEY_BINARY_SHL_ASSIGN:
                    case TRACEY_BINARY_SHR_ASSIGN:
                    case TRACEY_BINARY_AND_ASSIGN:
                    case TRACEY_BINARY_OR_ASSIGN:
                    case TRACEY_BINARY_XOR_ASSIGN:
                    {
                        struct tracey_expr *lhs = e->as.binary_expr.left;
                        if (lhs && lhs->kind == TRACEY_EXPR_IDENT)
                        {
                            struct tracey_semantic_symbol sym;
                            if (!tracey_semantic_lookup(sa, lhs->as.ident_expr.name, &sym) || sym.is_function)
                            {
                                char buf[256];
                                snprintf(buf, sizeof(buf), "Undefined variable '%s'", lhs->as.ident_expr.name);
                                tracey_semantic_error(sa, buf, e->loc);
                                (void)tracey_semantic_check_expr(sa, e->as.binary_expr.right, fn);
                                return TRACEY_TYPE_INT;
                            }
                            {
                                enum tracey_type_kind rt2 = tracey_semantic_check_expr(sa, e->as.binary_expr.right, fn);
                                if (!tracey_types_assignable(sym.type, rt2) &&
                                    !tracey_semantic_is_narrowing_conversion(sym.type, rt2))
                                    tracey_semantic_error(sa, "Type mismatch in assignment", e->loc);
                                else if (tracey_semantic_is_narrowing_conversion(sym.type, rt2))
                                    tracey_semantic_warning(sa, "Narrowing conversion in assignment", e->loc);
                                return sym.type;
                            }
                        }
                        if (lhs && lhs->kind == TRACEY_EXPR_INDEX)
                        {
                            enum tracey_type_kind bt = tracey_semantic_check_expr(sa, lhs->as.index_expr.base, fn);
                            enum tracey_type_kind it = tracey_semantic_check_expr(sa, lhs->as.index_expr.index, fn);
                            enum tracey_type_kind elem = TRACEY_TYPE_INT;
                            if (it != TRACEY_TYPE_INT)
                                tracey_semantic_error(sa, "Array index must be int", lhs->loc);
                            if (bt == TRACEY_TYPE_CHAR_PTR) elem = TRACEY_TYPE_CHAR;
                            else if (bt == TRACEY_TYPE_FLOAT_PTR) elem = TRACEY_TYPE_FLOAT;
                            else if (bt == TRACEY_TYPE_DOUBLE_PTR) elem = TRACEY_TYPE_DOUBLE;
                            else if (bt == TRACEY_TYPE_LONG_PTR) elem = TRACEY_TYPE_LONG;
                            else if (bt == TRACEY_TYPE_UNSIGNED_LONG_PTR) elem = TRACEY_TYPE_UNSIGNED_LONG;
                            else if (bt == TRACEY_TYPE_LONG_LONG_PTR) elem = TRACEY_TYPE_LONG_LONG;
                            else if (bt == TRACEY_TYPE_UNSIGNED_LONG_LONG_PTR) elem = TRACEY_TYPE_UNSIGNED_LONG_LONG;
                            else if (bt == TRACEY_TYPE_SHORT_PTR) elem = TRACEY_TYPE_SHORT;
                            else if (bt == TRACEY_TYPE_UNSIGNED_SHORT_PTR) elem = TRACEY_TYPE_UNSIGNED_SHORT;
                            else if (bt == TRACEY_TYPE_SIZE_T_PTR) elem = TRACEY_TYPE_SIZE_T;
                            else if (bt != TRACEY_TYPE_INT_PTR)
                                tracey_semantic_error(sa, "Subscript requires array or pointer", lhs->loc);
                            {
                                enum tracey_type_kind rt2 = tracey_semantic_check_expr(sa, e->as.binary_expr.right, fn);
                                if (!tracey_types_assignable(elem, rt2) &&
                                    !tracey_semantic_is_narrowing_conversion(elem, rt2))
                                    tracey_semantic_error(sa, "Type mismatch in assignment", e->loc);
                                else if (tracey_semantic_is_narrowing_conversion(elem, rt2))
                                    tracey_semantic_warning(sa, "Narrowing conversion in assignment", e->loc);
                            }
                            return elem;
                        }
                        if (lhs && lhs->kind == TRACEY_EXPR_DEREF)
                        {
                            enum tracey_type_kind pt = tracey_semantic_check_expr(sa, lhs->as.deref_expr.operand, fn);
                            enum tracey_type_kind elem = tracey_pointer_element_type(pt);
                            if (!tracey_is_pointer(pt))
                                tracey_semantic_error(sa, "Cannot dereference non-pointer", lhs->loc);
                            {
                                enum tracey_type_kind rt2 = tracey_semantic_check_expr(sa, e->as.binary_expr.right, fn);
                                if (!tracey_types_assignable(elem, rt2) &&
                                    !tracey_semantic_is_narrowing_conversion(elem, rt2))
                                    tracey_semantic_error(sa, "Type mismatch in assignment", e->loc);
                                else if (tracey_semantic_is_narrowing_conversion(elem, rt2))
                                    tracey_semantic_warning(sa, "Narrowing conversion in assignment", e->loc);
                            }
                            return elem;
                        }
                        if (lhs && lhs->kind == TRACEY_EXPR_MEMBER)
                        {
                            enum tracey_type_kind lhs_t = tracey_semantic_check_expr(sa, lhs, fn);
                            enum tracey_type_kind rt = tracey_semantic_check_expr(sa, e->as.binary_expr.right, fn);
                            if (!tracey_types_assignable(lhs_t, rt) &&
                                !tracey_semantic_is_narrowing_conversion(lhs_t, rt))
                                tracey_semantic_error(sa, "Type mismatch in assignment", e->loc);
                            else if (tracey_semantic_is_narrowing_conversion(lhs_t, rt))
                                tracey_semantic_warning(sa, "Narrowing conversion in assignment", e->loc);
                            return lhs_t;
                        }
                        tracey_semantic_error(sa, "Invalid assignment target", e->loc);
                        (void)tracey_semantic_check_expr(sa, e->as.binary_expr.right, fn);
                        return TRACEY_TYPE_INT;
                    }
                    case TRACEY_BINARY_ADD:
                    case TRACEY_BINARY_SUB:
                    case TRACEY_BINARY_MUL:
                    case TRACEY_BINARY_DIV:
                        if (lt == TRACEY_TYPE_DOUBLE || rt == TRACEY_TYPE_DOUBLE)
                        {
                            if (tracey_is_numeric(lt) && tracey_is_numeric(rt))
                                return TRACEY_TYPE_DOUBLE;
                            tracey_semantic_error(sa, "Arithmetic requires numeric operands", e->loc);
                            return TRACEY_TYPE_DOUBLE;
                        }
                        if (lt == TRACEY_TYPE_FLOAT || rt == TRACEY_TYPE_FLOAT)
                        {
                            if (tracey_is_numeric(lt) && tracey_is_numeric(rt))
                                return TRACEY_TYPE_FLOAT;
                            tracey_semantic_error(sa, "Arithmetic requires numeric operands", e->loc);
                            return TRACEY_TYPE_FLOAT;
                        }
                        if (tracey_is_integer(lt) && tracey_is_integer(rt))
                        {
                            /* Promote to the wider integer type. */
                            if (lt == TRACEY_TYPE_LONG_LONG || rt == TRACEY_TYPE_LONG_LONG)
                                return TRACEY_TYPE_LONG_LONG;
                            if (lt == TRACEY_TYPE_LONG || rt == TRACEY_TYPE_LONG)
                                return TRACEY_TYPE_LONG;
                            if (lt == TRACEY_TYPE_SIZE_T || rt == TRACEY_TYPE_SIZE_T)
                                return TRACEY_TYPE_SIZE_T;
                            return TRACEY_TYPE_INT;
                        }
                        tracey_semantic_error(sa, "Arithmetic requires numeric operands", e->loc);
                        return TRACEY_TYPE_INT;
                    case TRACEY_BINARY_MOD:
                        if (tracey_is_integer(lt) && tracey_is_integer(rt))
                            return TRACEY_TYPE_INT;
                        tracey_semantic_error(sa, "Modulo requires integer operands", e->loc);
                        return TRACEY_TYPE_INT;
                    case TRACEY_BINARY_EQ:
                    case TRACEY_BINARY_NE:
                        if (lt != rt)
                            tracey_semantic_error(sa, "Comparison requires same types", e->loc);
                        return TRACEY_TYPE_BOOL;
                    case TRACEY_BINARY_LT:
                    case TRACEY_BINARY_GT:
                    case TRACEY_BINARY_LE:
                    case TRACEY_BINARY_GE:
                        if (tracey_is_numeric(lt) && tracey_is_numeric(rt))
                            return TRACEY_TYPE_BOOL;
                        tracey_semantic_error(sa, "Relational requires numeric operands", e->loc);
                        return TRACEY_TYPE_BOOL;
                    case TRACEY_BINARY_LOG_AND:
                    case TRACEY_BINARY_LOG_OR:
                        if (lt != TRACEY_TYPE_BOOL || rt != TRACEY_TYPE_BOOL)
                            tracey_semantic_error(sa, "Logical requires bool operands", e->loc);
                        return TRACEY_TYPE_BOOL;
                    case TRACEY_BINARY_BIT_AND:
                    case TRACEY_BINARY_BIT_OR:
                    case TRACEY_BINARY_BIT_XOR:
                        if (tracey_is_integer(lt) && tracey_is_integer(rt))
                        {
                            if (lt == TRACEY_TYPE_LONG_LONG || rt == TRACEY_TYPE_LONG_LONG)
                                return TRACEY_TYPE_LONG_LONG;
                            if (lt == TRACEY_TYPE_LONG || rt == TRACEY_TYPE_LONG)
                                return TRACEY_TYPE_LONG;
                            return TRACEY_TYPE_INT;
                        }
                        tracey_semantic_error(sa, "Bitwise requires integer operands", e->loc);
                        return TRACEY_TYPE_INT;
                    case TRACEY_BINARY_SHIFT_LEFT:
                    case TRACEY_BINARY_SHIFT_RIGHT:
                        if (tracey_is_integer(lt) && tracey_is_integer(rt))
                            return lt;
                        tracey_semantic_error(sa, "Shift requires integer operands", e->loc);
                        return TRACEY_TYPE_INT;
                    default:
                        return TRACEY_TYPE_INT;
                }
            }
        }
        case TRACEY_EXPR_CALL:
        {
            const char *name = e->as.call_expr.callee;
            size_t i;
            if (name && strcmp(name, "print") == 0)
            {
                for (i = 0; i < e->as.call_expr.arg_count; i++)
                    tracey_semantic_check_expr(sa, e->as.call_expr.args[i], fn);
                return TRACEY_TYPE_VOID;
            }
            if (name && strcmp(name, "input_int") == 0)
            {
                if (e->as.call_expr.arg_count != 0)
                    tracey_semantic_error(sa, "Wrong number of arguments to 'input_int'", e->loc);
                return TRACEY_TYPE_INT;
            }
            if (name && strcmp(name, "input_str") == 0)
            {
                if (e->as.call_expr.arg_count != 2)
                    tracey_semantic_error(sa, "Wrong number of arguments to 'input_str'", e->loc);
                if (e->as.call_expr.arg_count >= 1)
                {
                    enum tracey_type_kind t = tracey_semantic_check_expr(sa, e->as.call_expr.args[0], fn);
                    if (t != TRACEY_TYPE_CHAR_PTR)
                        tracey_semantic_error(sa, "Input_str requires char* buffer", e->as.call_expr.args[0]->loc);
                }
                if (e->as.call_expr.arg_count >= 2)
                {
                    enum tracey_type_kind t = tracey_semantic_check_expr(sa, e->as.call_expr.args[1], fn);
                    if (t != TRACEY_TYPE_INT)
                        tracey_semantic_error(sa, "Input_str size must be int", e->as.call_expr.args[1]->loc);
                }
                return TRACEY_TYPE_INT;
            }
            if (name && strcmp(name, "puts") == 0)
            {
                if (e->as.call_expr.arg_count != 1)
                    tracey_semantic_error(sa, "Wrong number of arguments to 'puts'", e->loc);
                if (e->as.call_expr.arg_count >= 1)
                {
                    enum tracey_type_kind t = tracey_semantic_check_expr(sa, e->as.call_expr.args[0], fn);
                    if (t != TRACEY_TYPE_CHAR_PTR)
                        tracey_semantic_error(sa, "puts requires char*", e->as.call_expr.args[0]->loc);
                }
                return TRACEY_TYPE_INT;
            }
            if (name && strcmp(name, "putchar") == 0)
            {
                if (e->as.call_expr.arg_count != 1)
                    tracey_semantic_error(sa, "Wrong number of arguments to 'putchar'", e->loc);
                if (e->as.call_expr.arg_count >= 1)
                    tracey_semantic_check_expr(sa, e->as.call_expr.args[0], fn);
                return TRACEY_TYPE_INT;
            }
            if (name && strcmp(name, "getchar") == 0)
            {
                if (e->as.call_expr.arg_count != 0)
                    tracey_semantic_error(sa, "Wrong number of arguments to 'getchar'", e->loc);
                return TRACEY_TYPE_INT;
            }
            if (name && strcmp(name, "fget") == 0)
            {
                if (e->as.call_expr.arg_count != 1)
                    tracey_semantic_error(sa, "Wrong number of arguments to 'fget'", e->loc);
                if (e->as.call_expr.arg_count >= 1)
                {
                    enum tracey_type_kind t = tracey_semantic_check_expr(sa, e->as.call_expr.args[0], fn);
                    if (t != TRACEY_TYPE_CHAR_PTR)
                        tracey_semantic_error(sa, "fget requires char* buffer", e->as.call_expr.args[0]->loc);
                }
                return TRACEY_TYPE_CHAR_PTR;
            }
            if (name && strcmp(name, "fgets") == 0)
            {
                if (e->as.call_expr.arg_count != 2)
                    tracey_semantic_error(sa, "Wrong number of arguments to 'fgets'", e->loc);
                if (e->as.call_expr.arg_count >= 1)
                {
                    enum tracey_type_kind t = tracey_semantic_check_expr(sa, e->as.call_expr.args[0], fn);
                    if (t != TRACEY_TYPE_CHAR_PTR)
                        tracey_semantic_error(sa, "fgets requires char* buffer", e->as.call_expr.args[0]->loc);
                }
                if (e->as.call_expr.arg_count >= 2)
                {
                    enum tracey_type_kind t = tracey_semantic_check_expr(sa, e->as.call_expr.args[1], fn);
                    if (t != TRACEY_TYPE_INT)
                        tracey_semantic_error(sa, "fgets size must be int", e->as.call_expr.args[1]->loc);
                }
                return TRACEY_TYPE_CHAR_PTR;
            }
            if (name && (strcmp(name, "scanf") == 0 || strcmp(name, "scanfs") == 0))
            {
                if (e->as.call_expr.arg_count < 2)
                    tracey_semantic_error(sa, "Wrong number of arguments", e->loc);
                if (e->as.call_expr.arg_count >= 1)
                {
                    enum tracey_type_kind t = tracey_semantic_check_expr(sa, e->as.call_expr.args[0], fn);
                    if (t != TRACEY_TYPE_CHAR_PTR)
                        tracey_semantic_error(sa, "Format must be char*", e->as.call_expr.args[0]->loc);
                }
                for (size_t ai = 1; ai < e->as.call_expr.arg_count; ai++)
                    tracey_semantic_check_expr(sa, e->as.call_expr.args[ai], fn);
                return TRACEY_TYPE_INT;
            }
            if (name && strcmp(name, "printf") == 0)
            {
                for (i = 0; i < e->as.call_expr.arg_count; i++)
                    tracey_semantic_check_expr(sa, e->as.call_expr.args[i], fn);
                return TRACEY_TYPE_INT;
            }
            {
                struct tracey_semantic_symbol sym;
                if (tracey_semantic_lookup(sa, name, &sym) && sym.type == TRACEY_TYPE_FUNC_PTR &&
                    !sym.is_function)
                {
                    struct tracey_func_sig *sig = sym.fn_sig;

                    if (sig == NULL || (int)e->as.call_expr.arg_count != (int)sig->param_count)
                    {
                        char buf[256];
                        snprintf(buf, sizeof(buf), "Wrong number of arguments to '%s'", name);
                        tracey_semantic_error(sa, buf, e->loc);
                    }
                    for (i = 0; i < e->as.call_expr.arg_count; i++)
                    {
                        enum tracey_type_kind at = tracey_semantic_check_expr(sa, e->as.call_expr.args[i], fn);
                        if (sig != NULL && i < sig->param_count && !tracey_types_assignable(sig->param_types[i], at))
                            tracey_semantic_error(sa, "Type mismatch in function pointer call", e->loc);
                    }
                    return sig != NULL ? sig->return_type : TRACEY_TYPE_INT;
                }
                if (!tracey_semantic_lookup(sa, name, &sym) || !sym.is_function)
                {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "Undefined function '%s'", name ? name : "");
                    tracey_semantic_error(sa, buf, e->loc);
                    for (i = 0; i < e->as.call_expr.arg_count; i++)
                        tracey_semantic_check_expr(sa, e->as.call_expr.args[i], fn);
                    return TRACEY_TYPE_INT;
                }
                if ((int)e->as.call_expr.arg_count != sym.arity)
                {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "Wrong number of arguments to '%s'", name);
                    tracey_semantic_error(sa, buf, e->loc);
                }
                for (i = 0; i < e->as.call_expr.arg_count; i++)
                    tracey_semantic_check_expr(sa, e->as.call_expr.args[i], fn);
                return sym.type;
            }
        }
        case TRACEY_EXPR_TERNARY:
        {
            enum tracey_type_kind ct = tracey_semantic_check_expr(sa, e->as.ternary_expr.condition, fn);
            if (ct != TRACEY_TYPE_BOOL)
                tracey_semantic_error(sa, "Ternary condition must be bool", e->loc);
            enum tracey_type_kind tt = tracey_semantic_check_expr(sa, e->as.ternary_expr.then_branch, fn);
            enum tracey_type_kind et = tracey_semantic_check_expr(sa, e->as.ternary_expr.else_branch, fn);
            if (tt != et)
                tracey_semantic_error(sa, "Ternary branches must have same type", e->loc);
            return tt;
        }
        case TRACEY_EXPR_SIZEOF:
            if (!e->as.sizeof_expr.is_type && e->as.sizeof_expr.operand != NULL)
                (void)tracey_semantic_check_expr(sa, e->as.sizeof_expr.operand, fn);
            return TRACEY_TYPE_INT;
        default:
            return TRACEY_TYPE_VOID;
    }
}

