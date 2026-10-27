#include <tracey/codegen.hxx>

#include <functional>

#include <tracey/ast.hxx>

#include <cstddef>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace tracey {

code_generator::code_generator(target_arch arch) : arch_(arch) {}

static void collect_vars(stmt* s, std::vector<std::string>& out, std::set<std::string>& seen) {
    if (!s) return;
    if (auto* b = dynamic_cast<block_stmt*>(s)) {
        for (auto& st : b->statements) collect_vars(st.get(), out, seen);
        return;
    }
    if (auto* vd = dynamic_cast<var_decl_stmt*>(s)) {
        if (seen.insert(vd->name).second) out.push_back(vd->name);
        return;
    }
    if (auto* is = dynamic_cast<if_stmt*>(s)) { collect_vars(is->then_branch.get(), out, seen); collect_vars(is->else_branch.get(), out, seen); return; }
    if (auto* ws = dynamic_cast<while_stmt*>(s)) { collect_vars(ws->body.get(), out, seen); return; }
}

static std::string new_label(int& counter, const std::string& prefix) {
    return prefix + std::to_string(counter++);
}

void code_generator::generate(const program& p, std::ostream& out) {
    if (arch_ != target_arch::aarch64) {
        out << "// codegen for this arch not implemented yet\n";
        return;
    }
    out << "    .text\n";
    for (auto& f : p.functions) {
        // collect locals
        std::vector<std::string> locals;
        std::set<std::string> seen;
        for (auto& [t,name] : f->params) { if (seen.insert(name).second) locals.push_back(name); }
        collect_vars(f->body.get(), locals, seen);

        std::map<std::string,int> off;
        for (std::size_t i=0;i<locals.size();++i) off[locals[i]] = -(int)((i+1)*8);

        int label_counter = 0;
        std::ostringstream body;
        // helper lambda to emit with indentation
        auto emit = [&](const std::string& s){ body << "    " << s << "\n"; };

        // We need recursive gen functions capturing off, label_counter, etc.
        // Use std::function? simpler recursive lambdas via auto with std::function
        // We'll implement as nested functions using lambda and recursion via std::function.
        // But lambdas capturing by reference can be recursive if declared std::function.
        // We'll write manual iterative? Use std::function.
        std::function<void(expr*)> gen_expr = [&](expr* e) {
            if (auto* ie = dynamic_cast<int_expr*>(e)) {
                emit("mov x0, #" + std::to_string(ie->value));
                return;
            }
            if (auto* be = dynamic_cast<bool_expr*>(e)) {
                emit("mov x0, #" + std::to_string(be->value ? 1 : 0));
                return;
            }
            if (auto* id = dynamic_cast<ident_expr*>(e)) {
                auto it = off.find(id->name);
                int o = (it==off.end()) ? 0 : it->second;
                emit("ldr x0, [x29, #" + std::to_string(o) + "]");
                return;
            }
            if (auto* ue = dynamic_cast<unary_expr*>(e)) {
                gen_expr(ue->operand.get());
                if (ue->op == unary_op::neg) emit("neg x0, x0");
                else { emit("cmp x0, #0"); emit("cset x0, eq"); }
                return;
            }
            if (auto* ce = dynamic_cast<call_expr*>(e)) {
                // evaluate args
                for (auto& a : ce->args) {
                    gen_expr(a.get());
                    emit("str x0, [sp, #-16]!");
                }
                for (int i = (int)ce->args.size()-1; i>=0; --i) {
                    emit("ldr x" + std::to_string(i) + ", [sp], #16");
                }
                emit("bl " + ce->callee);
                return;
            }
            if (auto* be = dynamic_cast<binary_expr*>(e)) {
                if (be->op == binary_op::assign) {
                    gen_expr(be->right.get());
                    auto* id = dynamic_cast<ident_expr*>(be->left.get());
                    int o = off[id->name];
                    emit("str x0, [x29, #" + std::to_string(o) + "]");
                    return;
                }
                gen_expr(be->left.get());
                emit("str x0, [sp, #-16]!");
                gen_expr(be->right.get());
                emit("mov x1, x0");
                emit("ldr x0, [sp], #16");
                // now x0=left, x1=right
                switch (be->op) {
                    case binary_op::add: emit("add x0, x0, x1"); break;
                    case binary_op::sub: emit("sub x0, x0, x1"); break;
                    case binary_op::mul: emit("mul x0, x0, x1"); break;
                    case binary_op::div: emit("sdiv x0, x0, x1"); break;
                    case binary_op::mod:
                        emit("sdiv x9, x0, x1");
                        emit("msub x0, x9, x1, x0");
                        break;
                    case binary_op::eq: emit("cmp x0, x1"); emit("cset x0, eq"); break;
                    case binary_op::ne: emit("cmp x0, x1"); emit("cset x0, ne"); break;
                    case binary_op::lt: emit("cmp x0, x1"); emit("cset x0, lt"); break;
                    case binary_op::gt: emit("cmp x0, x1"); emit("cset x0, gt"); break;
                    case binary_op::le: emit("cmp x0, x1"); emit("cset x0, le"); break;
                    case binary_op::ge: emit("cmp x0, x1"); emit("cset x0, ge"); break;
                    case binary_op::log_and:
                        emit("cmp x0, #0"); emit("cset x0, ne");
                        emit("cmp x1, #0"); emit("cset x1, ne");
                        emit("and x0, x0, x1");
                        break;
                    case binary_op::log_or:
                        emit("cmp x0, #0"); emit("cset x0, ne");
                        emit("cmp x1, #0"); emit("cset x1, ne");
                        emit("orr x0, x0, x1");
                        break;
                    default: break;
                }
                return;
            }
        };

        std::function<void(stmt*)> gen_stmt = [&](stmt* s) {
            if (!s) return;
            if (auto* b = dynamic_cast<block_stmt*>(s)) {
                for (auto& st : b->statements) gen_stmt(st.get());
                return;
            }
            if (auto* vd = dynamic_cast<var_decl_stmt*>(s)) {
                if (vd->init) {
                    gen_expr(vd->init.get());
                    int o = off[vd->name];
                    emit("str x0, [x29, #" + std::to_string(o) + "]");
                }
                return;
            }
            if (auto* es = dynamic_cast<expr_stmt*>(s)) {
                gen_expr(es->expression.get());
                return;
            }
            if (auto* is = dynamic_cast<if_stmt*>(s)) {
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
            if (auto* ws = dynamic_cast<while_stmt*>(s)) {
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
            if (auto* rs = dynamic_cast<return_stmt*>(s)) {
                if (rs->value) gen_expr(rs->value.get());
                emit("b " + f->name + "_ret");
                return;
            }
        };

        int frame_size = (int)locals.size() * 8;
        if (frame_size % 16 != 0) frame_size += 8;

        out << "    .globl " << f->name << "\n";
        out << f->name << ":\n";
        out << "    stp x29, x30, [sp, #-16]!\n";
        out << "    mov x29, sp\n";
        if (frame_size > 0) out << "    sub sp, sp, #" << frame_size << "\n";
        // store params
        for (std::size_t i=0; i<f->params.size() && i<8; ++i) {
            out << "    str x" << i << ", [x29, #" << off[f->params[i].second] << "]\n";
        }

        // redirect body output: we built in body stringstream; but emit used body. Need output body now.
        // Instead gen_stmt directly to out? We used body stream. Let's just use out directly by changing emit to out.
        // Oops: emit lambda writes to body, not out. We'll instead make emit write to out.
        // Rework: we already defined emit to body. Let's flush body to out after generation? But gen happens now.
        // We'll regenerate? Instead change: make emit lambda capture out by reference.
        // Since too late, we can just output body.str() after calling gen_stmt.
        gen_stmt(f->body.get());
        out << body.str();
        out << f->name << "_ret:\n";
        out << "    mov sp, x29\n";
        out << "    ldp x29, x30, [sp], #16\n";
        out << "    ret\n";
    }
}

} // namespace tracey
