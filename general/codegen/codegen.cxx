#include <tracey/codegen.hxx>

#include <functional>

#include <bit>
#include <cstdint>

#include <tracey/ast.hxx>

#include <cstddef>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace tracey
{

code_generator::code_generator(target_arch arch) : arch_(arch)
{
}

struct local_var
{
    std::string name;
    int size;
    bool is_array{false};
    type_kind type{type_kind::int_};
};

static void collect_vars(stmt *p_s, std::vector<local_var> &out, std::set<std::string> &seen,
                         std::map<std::string, type_kind> &vartype)
{
    if (!p_s)
        return;
    if (auto *b = dynamic_cast<block_stmt *>(p_s))
    {
        for (auto &st : b->statements)
            collect_vars(st.get(), out, seen, vartype);
        return;
    }
    if (auto *vd = dynamic_cast<var_decl_stmt *>(p_s))
    {
        if (seen.insert(vd->name).second)
        {
            int sz = (vd->array_size > 0) ? (int)(vd->array_size * 8) : 8;
            type_kind t = vd->type;
            if (vd->array_size > 0)
            {
                if (t == type_kind::int_)
                    t = type_kind::int_ptr;
                else if (t == type_kind::char_)
                    t = type_kind::char_ptr;
                else if (t == type_kind::float_)
                    t = type_kind::float_ptr;
                else if (t == type_kind::double_)
                    t = type_kind::double_ptr;
            }
            out.push_back({vd->name, sz, vd->array_size > 0, t});
            vartype[vd->name] = t;
        }
        return;
    }
    if (auto *is = dynamic_cast<if_stmt *>(p_s))
    {
        collect_vars(is->then_branch.get(), out, seen, vartype);
        collect_vars(is->else_branch.get(), out, seen, vartype);
        return;
    }
    if (auto *ws = dynamic_cast<while_stmt *>(p_s))
    {
        collect_vars(ws->body.get(), out, seen, vartype);
        return;
    }
}

static std::string new_label(int &counter, const std::string &prefix)
{
    return prefix + std::to_string(counter++);
}

void code_generator::generate(const program &p, const std::string &source_name, std::ostream &out,
                              int module_id, bool emit_runtime_helpers)
{
    if (arch_ != target_arch::aarch64)
    {
        out << "// codegen for this arch not implemented yet\n";
        return;
    }
    out << "    .text\n";
    out << "    .file \"" << source_name << "\"\n";

    std::vector<std::string> strtab;
    std::map<std::string, int> strindex;
    const std::string strprefix = "L" + std::to_string(module_id) + "str";
    auto getstr = [&](const std::string &s) -> int
    {
        auto it = strindex.find(s);
        if (it != strindex.end())
            return it->second;
        int idx = static_cast<int>(strtab.size());
        strindex[s] = idx;
        strtab.push_back(s);
        return idx;
    };

    for (auto &f : p.functions)
    {
        if (!f->body)
            continue;
        // collect locals
        std::vector<local_var> locals;
        std::set<std::string> seen;
        std::map<std::string, type_kind> vartype;
        for (auto &[t, name] : f->params)
        {
            if (seen.insert(name).second)
            {
                locals.push_back({name, 8, false, t});
                vartype[name] = t;
            }
        }
        collect_vars(f->body.get(), locals, seen, vartype);

        std::map<std::string, int> off;
        int cursor = 0;
        for (auto &lv : locals)
        {
            cursor += lv.size;
            // align to 8
            if (cursor % 8 != 0)
                cursor += 8 - (cursor % 8);
            off[lv.name] = -cursor;
        }

        int label_counter = 0;
        std::ostringstream body;
        // helper lambda to emit with indentation
        auto emit = [&](const std::string &s) { body << "    " << s << "\n"; };

        // We need recursive gen functions capturing off, label_counter, etc.
        // Use std::function? simpler recursive lambdas via auto with std::function
        // We'll implement as nested functions using lambda and recursion via std::function.
        // But lambdas capturing by reference can be recursive if declared std::function.
        // We'll write manual iterative? Use std::function.
        std::function<type_kind(expr *)> infer_type = [&](expr *p_e) -> type_kind
        {
            if (!p_e)
                return type_kind::void_;
            if (dynamic_cast<int_expr *>(p_e))
                return type_kind::int_;
            if (auto *re = dynamic_cast<real_expr *>(p_e))
                return re->is_float ? type_kind::float_ : type_kind::double_;
            if (dynamic_cast<bool_expr *>(p_e))
                return type_kind::bool_;
            if (dynamic_cast<string_expr *>(p_e))
                return type_kind::char_ptr;
            if (auto *id = dynamic_cast<ident_expr *>(p_e))
            {
                auto it = vartype.find(id->name);
                if (it != vartype.end())
                    return it->second;
                return type_kind::int_;
            }
            if (auto *ue = dynamic_cast<unary_expr *>(p_e))
            {
                type_kind t = infer_type(ue->operand.get());
                if (ue->op == unary_op::log_not)
                    return type_kind::bool_;
                return t;
            }
            if (auto *ix = dynamic_cast<index_expr *>(p_e))
            {
                type_kind bt = infer_type(ix->base.get());
                if (bt == type_kind::int_ptr)
                    return type_kind::int_;
                if (bt == type_kind::char_ptr)
                    return type_kind::char_;
                if (bt == type_kind::float_ptr)
                    return type_kind::float_;
                if (bt == type_kind::double_ptr)
                    return type_kind::double_;
                return type_kind::int_;
            }
            if (auto *ae = dynamic_cast<addr_expr *>(p_e))
            {
                type_kind t = infer_type(ae->operand.get());
                if (t == type_kind::int_)
                    return type_kind::int_ptr;
                if (t == type_kind::char_)
                    return type_kind::char_ptr;
                if (t == type_kind::float_)
                    return type_kind::float_ptr;
                if (t == type_kind::double_)
                    return type_kind::double_ptr;
                return type_kind::int_ptr;
            }
            if (auto *de = dynamic_cast<deref_expr *>(p_e))
            {
                type_kind t = infer_type(de->operand.get());
                if (t == type_kind::int_ptr)
                    return type_kind::int_;
                if (t == type_kind::char_ptr)
                    return type_kind::char_;
                if (t == type_kind::float_ptr)
                    return type_kind::float_;
                if (t == type_kind::double_ptr)
                    return type_kind::double_;
                return type_kind::int_;
            }
            if (auto *be = dynamic_cast<binary_expr *>(p_e))
            {
                if (be->op == binary_op::assign)
                    return infer_type(be->left.get());
                type_kind lt = infer_type(be->left.get());
                type_kind rt = infer_type(be->right.get());
                switch (be->op)
                {
                    case binary_op::eq:
                    case binary_op::ne:
                    case binary_op::lt:
                    case binary_op::gt:
                    case binary_op::le:
                    case binary_op::ge:
                    case binary_op::log_and:
                    case binary_op::log_or:
                        return type_kind::bool_;
                    default:
                        if (lt == type_kind::double_ || rt == type_kind::double_)
                            return type_kind::double_;
                        if (lt == type_kind::float_ || rt == type_kind::float_)
                            return type_kind::float_;
                        return type_kind::int_;
                }
            }
            if (auto *ce = dynamic_cast<call_expr *>(p_e))
            {
                (void)ce;
                return type_kind::int_;
            }
            return type_kind::int_;
        };

        std::function<void(expr *)> gen_expr = [&](expr *p_e)
        {
            if (auto *se = dynamic_cast<string_expr *>(p_e))
            {
                int idx = getstr(se->value);
                emit("adr x0, " + strprefix + std::to_string(idx));
                return;
            }
            if (auto *ie = dynamic_cast<int_expr *>(p_e))
            {
                emit("mov x0, #" + std::to_string(ie->value));
                return;
            }
            if (auto *be = dynamic_cast<bool_expr *>(p_e))
            {
                emit("mov x0, #" + std::to_string(be->value ? 1 : 0));
                return;
            }
            if (auto *re = dynamic_cast<real_expr *>(p_e))
            {
                uint64_t bits = 0;
                if (re->is_float)
                {
                    float fv = (float)re->value;
                    uint32_t b = std::bit_cast<uint32_t>(fv);
                    bits = b;
                }
                else
                {
                    bits = std::bit_cast<uint64_t>(re->value);
                }
                // load 64-bit immediate into x0 via movz/movk
                emit("mov x0, #" + std::to_string(bits & 0xFFFFU));
                emit("movk x0, #" + std::to_string((bits >> 16) & 0xFFFFU) + ", lsl #16");
                emit("movk x0, #" + std::to_string((bits >> 32) & 0xFFFFU) + ", lsl #32");
                emit("movk x0, #" + std::to_string((bits >> 48) & 0xFFFFU) + ", lsl #48");
                return;
            }
            if (auto *id = dynamic_cast<ident_expr *>(p_e))
            {
                auto it = off.find(id->name);
                int o = (it == off.end()) ? 0 : it->second;
                // arrays decay to their base address
                auto lit = std::find_if(locals.begin(), locals.end(),
                                        [&](const local_var &lv) { return lv.name == id->name; });
                bool is_array = (lit != locals.end() && lit->is_array);
                if (is_array)
                {
                    if (o < 0)
                        emit("sub x0, x29, #" + std::to_string(-o));
                    else
                        emit("add x0, x29, #" + std::to_string(o));
                }
                else
                {
                    emit("ldr x0, [x29, #" + std::to_string(o) + "]");
                }
                return;
            }
            if (auto *ix = dynamic_cast<index_expr *>(p_e))
            {
                gen_expr(ix->base.get());
                emit("sub sp, sp, #16");
                emit("str x0, [sp]");
                gen_expr(ix->index.get());
                emit("mov x1, x0");
                emit("ldr x0, [sp]");
                emit("add sp, sp, #16");
                emit("mov x2, #8");
                emit("mul x3, x1, x2");
                emit("add x0, x0, x3");
                emit("ldr x0, [x0]");
                return;
            }
            if (auto *ae = dynamic_cast<addr_expr *>(p_e))
            {
                if (auto *id = dynamic_cast<ident_expr *>(ae->operand.get()))
                {
                    auto it = off.find(id->name);
                    int o = (it == off.end()) ? 0 : it->second;
                    if (o < 0)
                        emit("sub x0, x29, #" + std::to_string(-o));
                    else
                        emit("add x0, x29, #" + std::to_string(o));
                    return;
                }
                gen_expr(ae->operand.get());
                return;
            }
            if (auto *de = dynamic_cast<deref_expr *>(p_e))
            {
                gen_expr(de->operand.get());
                emit("ldr x0, [x0]");
                return;
            }
            if (auto *ue = dynamic_cast<unary_expr *>(p_e))
            {
                gen_expr(ue->operand.get());
                if (ue->op == unary_op::neg)
                {
                    type_kind t = infer_type(ue->operand.get());
                    if (t == type_kind::double_)
                    {
                        emit("fmov d0, x0");
                        emit("fneg d0, d0");
                        emit("fmov x0, d0");
                    }
                    else if (t == type_kind::float_)
                    {
                        emit("fmov s0, w0");
                        emit("fneg s0, s0");
                        emit("fmov w0, s0");
                    }
                    else
                    {
                        emit("neg x0, x0");
                    }
                }
                else
                {
                    emit("cmp x0, #0");
                    emit("cset x0, eq");
                }
                return;
            }
            if (auto *ce = dynamic_cast<call_expr *>(p_e))
            {
                if (ce->callee == "print")
                {
                    if (!ce->args.empty())
                        gen_expr(ce->args[0].get());
                    if (ce->args.size() > 0 && dynamic_cast<string_expr *>(ce->args[0].get()))
                    {
                        emit("bl __tracey_print_str");
                    }
                    else
                    {
                        emit("bl __tracey_print_int");
                    }
                    return;
                }
                if (ce->callee == "printf")
                {
                    if (ce->args.empty())
                        return;
                    // format must be string literal for compile-time handling
                    auto *fe = dynamic_cast<string_expr *>(ce->args[0].get());
                    if (fe)
                    {
                        std::string fmt = fe->value;
                        // evaluate remaining args upfront? We'll evaluate lazily.
                        size_t argi = 1;
                        std::string seg;
                        for (size_t i = 0; i < fmt.size(); ++i)
                        {
                            char c = fmt[i];
                            if (c == '%')
                            {
                                if (!seg.empty())
                                {
                                    int idx = getstr(seg);
                                    emit("adr x0, " + strprefix + std::to_string(idx));
                                    emit("bl __tracey_print_str");
                                    seg.clear();
                                }
                                if (i + 1 < fmt.size())
                                {
                                    char spec = fmt[++i];
                                    if (spec == 'd')
                                    {
                                        if (argi < ce->args.size())
                                            gen_expr(ce->args[argi++].get());
                                        emit("bl __tracey_print_int");
                                    }
                                    else if (spec == 's')
                                    {
                                        if (argi < ce->args.size())
                                            gen_expr(ce->args[argi++].get());
                                        emit("bl __tracey_print_str");
                                    }
                                    else if (spec == '%')
                                    {
                                        seg.push_back('%');
                                    }
                                }
                            }
                            else
                            {
                                seg.push_back(c);
                            }
                        }
                        if (!seg.empty())
                        {
                            int idx = getstr(seg);
                            emit("adr x0, " + strprefix + std::to_string(idx));
                            emit("bl __tracey_print_str");
                        }
                    }
                    return;
                }
                // evaluate args
                for (auto &a : ce->args)
                {
                    gen_expr(a.get());
                    emit("sub sp, sp, #16");
                    emit("str x0, [sp]");
                }
                for (int i = (int)ce->args.size() - 1; i >= 0; --i)
                {
                    emit("ldr x" + std::to_string(i) + ", [sp]");
                    emit("add sp, sp, #16");
                }
                emit("bl " + ce->callee);
                return;
            }
            if (auto *be = dynamic_cast<binary_expr *>(p_e))
            {
                if (be->op == binary_op::assign)
                {
                    if (auto *id = dynamic_cast<ident_expr *>(be->left.get()))
                    {
                        gen_expr(be->right.get());
                        int o = off[id->name];
                        emit("str x0, [x29, #" + std::to_string(o) + "]");
                        return;
                    }
                    if (auto *ix = dynamic_cast<index_expr *>(be->left.get()))
                    {
                        gen_expr(ix->base.get());
                        emit("sub sp, sp, #16");
                        emit("str x0, [sp]");
                        gen_expr(ix->index.get());
                        emit("mov x1, x0");
                        emit("ldr x0, [sp]");
                        emit("add sp, sp, #16");
                        emit("mov x2, #8");
                        emit("mul x3, x1, x2");
                        emit("add x0, x0, x3");
                        emit("sub sp, sp, #16");
                        emit("str x0, [sp]");
                        gen_expr(be->right.get());
                        emit("ldr x1, [sp]");
                        emit("add sp, sp, #16");
                        emit("str x0, [x1]");
                        return;
                    }
                    if (auto *de = dynamic_cast<deref_expr *>(be->left.get()))
                    {
                        gen_expr(de->operand.get());
                        emit("sub sp, sp, #16");
                        emit("str x0, [sp]");
                        gen_expr(be->right.get());
                        emit("ldr x1, [sp]");
                        emit("add sp, sp, #16");
                        emit("str x0, [x1]");
                        return;
                    }
                    (void)be;
                    return;
                }
                gen_expr(be->left.get());
                emit("sub sp, sp, #16");
                emit("str x0, [sp]");
                gen_expr(be->right.get());
                emit("mov x1, x0");
                emit("ldr x0, [sp]");
                emit("add sp, sp, #16");
                // now x0=left, x1=right
                type_kind ltt = infer_type(be->left.get());
                type_kind rtt = infer_type(be->right.get());
                bool floating = (ltt == type_kind::float_ || ltt == type_kind::double_ ||
                                 rtt == type_kind::float_ || rtt == type_kind::double_);
                bool use_double = (ltt == type_kind::double_ || rtt == type_kind::double_);
                if (floating)
                {
                    if (use_double)
                    {
                        emit("fmov d0, x0");
                        emit("fmov d1, x1");
                    }
                    else
                    {
                        emit("fmov s0, w0");
                        emit("fmov s1, w1");
                    }
                    switch (be->op)
                    {
                        case binary_op::add:
                            if (use_double)
                                emit("fadd d0, d0, d1");
                            else
                                emit("fadd s0, s0, s1");
                            break;
                        case binary_op::sub:
                            if (use_double)
                                emit("fsub d0, d0, d1");
                            else
                                emit("fsub s0, s0, s1");
                            break;
                        case binary_op::mul:
                            if (use_double)
                                emit("fmul d0, d0, d1");
                            else
                                emit("fmul s0, s0, s1");
                            break;
                        case binary_op::div:
                            if (use_double)
                                emit("fdiv d0, d0, d1");
                            else
                                emit("fdiv s0, s0, s1");
                            break;
                        case binary_op::eq:
                            if (use_double)
                            {
                                emit("fcmp d0, d1");
                            }
                            else
                            {
                                emit("fcmp s0, s1");
                            }
                            emit("cset x0, eq");
                            // transfer not needed; x0 now int result. But we used d0; need skip
                            // fmov We'll jump to end by returning after setting a flag.
                            { /* done */
                            }
                            break;
                        case binary_op::ne:
                            if (use_double)
                                emit("fcmp d0, d1");
                            else
                                emit("fcmp s0, s1");
                            emit("cset x0, ne");
                            break;
                        case binary_op::lt:
                            if (use_double)
                                emit("fcmp d0, d1");
                            else
                                emit("fcmp s0, s1");
                            emit("cset x0, lt");
                            break;
                        case binary_op::gt:
                            if (use_double)
                                emit("fcmp d0, d1");
                            else
                                emit("fcmp s0, s1");
                            emit("cset x0, gt");
                            break;
                        case binary_op::le:
                            if (use_double)
                                emit("fcmp d0, d1");
                            else
                                emit("fcmp s0, s1");
                            emit("cset x0, le");
                            break;
                        case binary_op::ge:
                            if (use_double)
                                emit("fcmp d0, d1");
                            else
                                emit("fcmp s0, s1");
                            emit("cset x0, ge");
                            break;
                        default:
                            break;
                    }
                    // transfer result back to x0 if arithmetic (not comparison)
                    if (be->op == binary_op::add || be->op == binary_op::sub ||
                        be->op == binary_op::mul || be->op == binary_op::div)
                    {
                        if (use_double)
                            emit("fmov x0, d0");
                        else
                            emit("fmov w0, s0");
                    }
                    return;
                }
                switch (be->op)
                {
                    case binary_op::add:
                        emit("add x0, x0, x1");
                        break;
                    case binary_op::sub:
                        emit("sub x0, x0, x1");
                        break;
                    case binary_op::mul:
                        emit("mul x0, x0, x1");
                        break;
                    case binary_op::div:
                        emit("sdiv x0, x0, x1");
                        break;
                    case binary_op::mod:
                        emit("sdiv x9, x0, x1");
                        emit("msub x0, x9, x1, x0");
                        break;
                    case binary_op::eq:
                        emit("cmp x0, x1");
                        emit("cset x0, eq");
                        break;
                    case binary_op::ne:
                        emit("cmp x0, x1");
                        emit("cset x0, ne");
                        break;
                    case binary_op::lt:
                        emit("cmp x0, x1");
                        emit("cset x0, lt");
                        break;
                    case binary_op::gt:
                        emit("cmp x0, x1");
                        emit("cset x0, gt");
                        break;
                    case binary_op::le:
                        emit("cmp x0, x1");
                        emit("cset x0, le");
                        break;
                    case binary_op::ge:
                        emit("cmp x0, x1");
                        emit("cset x0, ge");
                        break;
                    case binary_op::log_and:
                        emit("cmp x0, #0");
                        emit("cset x0, ne");
                        emit("cmp x1, #0");
                        emit("cset x1, ne");
                        emit("and x0, x0, x1");
                        break;
                    case binary_op::log_or:
                        emit("cmp x0, #0");
                        emit("cset x0, ne");
                        emit("cmp x1, #0");
                        emit("cset x1, ne");
                        emit("orr x0, x0, x1");
                        break;
                    default:
                        break;
                }
                return;
            }
        };

        std::function<void(stmt *)> gen_stmt = [&](stmt *p_s)
        {
            if (!p_s)
                return;
            if (p_s->loc.line > 0)
                body << "    .loc " << p_s->loc.line << "\n";
            if (auto *b = dynamic_cast<block_stmt *>(p_s))
            {
                for (auto &st : b->statements)
                    gen_stmt(st.get());
                return;
            }
            if (auto *vd = dynamic_cast<var_decl_stmt *>(p_s))
            {
                if (vd->init)
                {
                    gen_expr(vd->init.get());
                    int o = off[vd->name];
                    emit("str x0, [x29, #" + std::to_string(o) + "]");
                }
                return;
            }
            if (auto *es = dynamic_cast<expr_stmt *>(p_s))
            {
                gen_expr(es->expression.get());
                return;
            }
            if (auto *is = dynamic_cast<if_stmt *>(p_s))
            {
                gen_expr(is->condition.get());
                emit("cmp x0, #0");
                std::string elseL = new_label(label_counter, ".Lelse");
                std::string endL = new_label(label_counter, ".Lendif");
                emit("beq " + elseL);
                gen_stmt(is->then_branch.get());
                emit("b " + endL);
                emit(elseL + ":");
                gen_stmt(is->else_branch.get());
                emit(endL + ":");
                return;
            }
            if (auto *ws = dynamic_cast<while_stmt *>(p_s))
            {
                std::string startL = new_label(label_counter, ".Lwhile");
                std::string endL = new_label(label_counter, ".Lendwhile");
                emit(startL + ":");
                gen_expr(ws->condition.get());
                emit("cmp x0, #0");
                emit("beq " + endL);
                gen_stmt(ws->body.get());
                emit("b " + startL);
                emit(endL + ":");
                return;
            }
            if (auto *rs = dynamic_cast<return_stmt *>(p_s))
            {
                if (rs->value)
                    gen_expr(rs->value.get());
                emit("b " + f->name + "_ret");
                return;
            }
        };

        int frame_size = cursor;
        if (frame_size % 16 != 0)
            frame_size += 16 - (frame_size % 16);

        out << "    .globl " << f->name << "\n";
        out << f->name << ":\n";
        out << "    stp x29, x30, [sp, #-16]!\n";
        out << "    mov x29, sp\n";
        if (frame_size > 0)
            out << "    sub sp, sp, #" << frame_size << "\n";
        // store params
        for (std::size_t i = 0; i < f->params.size() && i < 8; ++i)
        {
            out << "    str x" << i << ", [x29, #" << off[f->params[i].second] << "]\n";
        }

        // redirect body output: we built in body stringstream; but emit used body. Need output body
        // now. Instead gen_stmt directly to out? We used body stream. Let's just use out directly
        // by changing emit to out. Oops: emit lambda writes to body, not out. We'll instead make
        // emit write to out. Rework: we already defined emit to body. Let's flush body to out after
        // generation? But gen happens now. We'll regenerate? Instead change: make emit lambda
        // capture out by reference. Since too late, we can just output body.str() after calling
        // gen_stmt.
        if (f->loc.line > 0)
            body << "    .loc " << f->loc.line << "\n";
        gen_stmt(f->body.get());
        out << body.str();
        out << f->name << "_ret:\n";
        out << "    mov sp, x29\n";
        out << "    ldp x29, x30, [sp], #16\n";
        out << "    ret\n";
    }

    if (emit_runtime_helpers)
    {
        // Runtime helpers
        out << "    .globl __tracey_print_int\n";
        out << "__tracey_print_int:\n";
        out << "    sub sp, sp, #32\n";
        out << "    mov x1, #0\n";
        out << "    cmp x0, #0\n";
        out << "    b.ge Lpi_pos\n";
        out << "    neg x0, x0\n";
        out << "    mov x1, #1\n";
        out << "Lpi_pos:\n";
        out << "    add x2, sp, #31\n";
        out << "    mov w3, #0\n";
        out << "    strb w3, [x2]\n";
        out << "    mov x4, #0\n";
        out << "Lpi_loop:\n";
        out << "    mov x5, #10\n";
        out << "    udiv x6, x0, x5\n";
        out << "    msub x7, x6, x5, x0\n";
        out << "    add w7, w7, #48\n";
        out << "    sub x2, x2, #1\n";
        out << "    strb w7, [x2]\n";
        out << "    add x4, x4, #1\n";
        out << "    mov x0, x6\n";
        out << "    cbnz x0, Lpi_loop\n";
        out << "    cbz x1, Lpi_nosign\n";
        out << "    sub x2, x2, #1\n";
        out << "    mov w3, #45\n";
        out << "    strb w3, [x2]\n";
        out << "    add x4, x4, #1\n";
        out << "Lpi_nosign:\n";
        out << "    mov x8, #64\n";
        out << "    mov x0, #1\n";
        out << "    mov x1, x2\n";
        out << "    mov x2, x4\n";
        out << "    svc #0\n";
        out << "    add sp, sp, #32\n";
        out << "    ret\n";

        out << "    .globl __tracey_print_str\n";
        out << "__tracey_print_str:\n";
        out << "    mov x1, x0\n";
        out << "    mov x2, x0\n";
        out << "Lps_len:\n";
        out << "    ldrb w3, [x1]\n";
        out << "    cbz w3, Lps_done\n";
        out << "    add x1, x1, #1\n";
        out << "    b Lps_len\n";
        out << "Lps_done:\n";
        out << "    sub x2, x1, x2\n";
        out << "    mov x1, x0\n";
        out << "    mov x8, #64\n";
        out << "    mov x0, #1\n";
        out << "    svc #0\n";
        out << "    ret\n";
    }

    // String table
    out << "    .data\n";
    for (size_t i = 0; i < strtab.size(); ++i)
    {
        out << strprefix << i << ":\n";
        out << "    .asciz \"";
        for (char c : strtab[i])
        {
            if (c == '\\')
                out << "\\\\";
            else if (c == '"')
                out << "\\\"";
            else if (c == '\n')
                out << "\\n";
            else if (c == '\t')
                out << "\\t";
            else if (c == '\r')
                out << "\\r";
            else
                out << c;
        }
        out << "\"\n";
    }
}

} // namespace tracey
