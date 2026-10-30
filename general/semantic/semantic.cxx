#include <tracey/semantic.hxx>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace tracey {

void semantic_analyzer::error(const std::string& msg, const source_loc& loc) {
    semantic_error e; e.message = msg; e.line = loc.line; e.column = loc.column;
    errors_.push_back(e); had_error_ = true;
}

void semantic_analyzer::push_scope() { scopes_.emplace_back(); }
void semantic_analyzer::pop_scope() { if (!scopes_.empty()) scopes_.pop_back(); }

bool semantic_analyzer::declare(const std::string& name, symbol sym, const source_loc& loc) {
    if (scopes_.empty()) push_scope();
    for (auto& [n,s] : scopes_.back()) {
        if (n == name) {
            if (s.is_function && sym.is_function && s.type == sym.type && s.arity == sym.arity) {
                return true; // compatible redeclaration
            }
            error("redeclaration of '" + name + "'", loc);
            return false;
        }
    }
    scopes_.back().emplace_back(name, sym);
    return true;
}

bool semantic_analyzer::lookup(const std::string& name, symbol& out) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        for (auto& [n,s] : *it) {
            if (n == name) { out = s; return true; }
        }
    }
    return false;
}

type_kind semantic_analyzer::check_expr(expr* e, const function_decl* fn) {
    if (!e) return type_kind::void_;
    if (auto* ie = dynamic_cast<int_expr*>(e)) { (void)ie; return type_kind::int_; }
    if (auto* be = dynamic_cast<bool_expr*>(e)) { (void)be; return type_kind::bool_; }
    if (auto* re = dynamic_cast<real_expr*>(e)) { return re->is_float ? type_kind::float_ : type_kind::double_; }
    if (auto* se = dynamic_cast<string_expr*>(e)) { (void)se; return type_kind::char_ptr; }
    if (auto* ie = dynamic_cast<index_expr*>(e)) {
        type_kind bt = check_expr(ie->base.get(), fn);
        type_kind it = check_expr(ie->index.get(), fn);
        if (it != type_kind::int_) error("array index must be int", ie->loc);
        if (bt == type_kind::int_ptr) return type_kind::int_;
        if (bt == type_kind::char_ptr) return type_kind::char_;
        if (bt == type_kind::float_ptr) return type_kind::float_;
        if (bt == type_kind::double_ptr) return type_kind::double_;
        error("subscript requires array or pointer", ie->loc);
        return type_kind::int_;
    }
    if (auto* ae = dynamic_cast<addr_expr*>(e)) {
        type_kind t = check_expr(ae->operand.get(), fn);
        if (t == type_kind::int_) return type_kind::int_ptr;
        if (t == type_kind::char_) return type_kind::char_ptr;
        if (t == type_kind::float_) return type_kind::float_ptr;
        if (t == type_kind::double_) return type_kind::double_ptr;
        error("cannot take address", ae->loc);
        return type_kind::int_ptr;
    }
    if (auto* de = dynamic_cast<deref_expr*>(e)) {
        type_kind t = check_expr(de->operand.get(), fn);
        if (t == type_kind::int_ptr) return type_kind::int_;
        if (t == type_kind::char_ptr) return type_kind::char_;
        if (t == type_kind::float_ptr) return type_kind::float_;
        if (t == type_kind::double_ptr) return type_kind::double_;
        error("cannot dereference non-pointer", de->loc);
        return type_kind::int_;
    }
    if (auto* id = dynamic_cast<ident_expr*>(e)) {
        symbol s;
        if (!lookup(id->name, s)) { error("undefined identifier '" + id->name + "'", id->loc); return type_kind::int_; }
        if (s.is_function) { error("'" + id->name + "' is a function", id->loc); return type_kind::int_; }
        return s.type;
    }
    if (auto* ue = dynamic_cast<unary_expr*>(e)) {
        type_kind t = check_expr(ue->operand.get(), fn);
        if (ue->op == unary_op::neg) {
            if (t == type_kind::int_) return type_kind::int_;
            if (t == type_kind::float_) return type_kind::float_;
            if (t == type_kind::double_) return type_kind::double_;
            error("unary '-' requires numeric", ue->loc);
            return type_kind::int_;
        }
        if (t != type_kind::bool_) error("'!' requires bool", ue->loc);
        return type_kind::bool_;
    }
    if (auto* be = dynamic_cast<binary_expr*>(e)) {
        if (be->op == binary_op::assign) {
            if (auto* id = dynamic_cast<ident_expr*>(be->left.get())) {
                symbol s;
                if (!lookup(id->name, s) || s.is_function) { error("undefined variable '" + id->name + "'", be->loc); (void)check_expr(be->right.get(), fn); return type_kind::int_; }
                type_kind rt = check_expr(be->right.get(), fn);
                if (rt != s.type) error("type mismatch in assignment", be->loc);
                return s.type;
            }
            if (auto* ix = dynamic_cast<index_expr*>(be->left.get())) {
                type_kind bt = check_expr(ix->base.get(), fn);
                type_kind it = check_expr(ix->index.get(), fn);
                if (it != type_kind::int_) error("array index must be int", ix->loc);
                type_kind elem = type_kind::int_;
                if (bt == type_kind::char_ptr) elem = type_kind::char_;
                else if (bt == type_kind::float_ptr) elem = type_kind::float_;
                else if (bt == type_kind::double_ptr) elem = type_kind::double_;
                else if (bt != type_kind::int_ptr) error("subscript requires array or pointer", ix->loc);
                type_kind rt = check_expr(be->right.get(), fn);
                if (rt != elem) error("type mismatch in assignment", be->loc);
                return elem;
            }
            if (auto* de = dynamic_cast<deref_expr*>(be->left.get())) {
                type_kind pt = check_expr(de->operand.get(), fn);
                type_kind elem = type_kind::int_;
                if (pt == type_kind::char_ptr) elem = type_kind::char_;
                else if (pt == type_kind::float_ptr) elem = type_kind::float_;
                else if (pt == type_kind::double_ptr) elem = type_kind::double_;
                else if (pt != type_kind::int_ptr) error("cannot dereference non-pointer", de->loc);
                type_kind rt = check_expr(be->right.get(), fn);
                if (rt != elem) error("type mismatch in assignment", de->loc);
                return elem;
            }
            error("invalid assignment target", be->loc);
            (void)check_expr(be->right.get(), fn);
            return type_kind::int_;
        }
        type_kind lt = check_expr(be->left.get(), fn);
        type_kind rt = check_expr(be->right.get(), fn);
        switch (be->op) {
            case binary_op::add: case binary_op::sub: case binary_op::mul:
            case binary_op::div:
                if (lt == type_kind::double_ || rt == type_kind::double_) {
                    if ((lt == type_kind::double_ || lt == type_kind::int_ || lt == type_kind::float_) &&
                        (rt == type_kind::double_ || rt == type_kind::int_ || rt == type_kind::float_)) {
                        return type_kind::double_;
                    }
                    error("arithmetic requires numeric operands", be->loc);
                    return type_kind::double_;
                }
                if (lt == type_kind::float_ || rt == type_kind::float_) {
                    if ((lt == type_kind::float_ || lt == type_kind::int_) &&
                        (rt == type_kind::float_ || rt == type_kind::int_)) {
                        return type_kind::float_;
                    }
                    error("arithmetic requires numeric operands", be->loc);
                    return type_kind::float_;
                }
                if (lt != type_kind::int_ || rt != type_kind::int_) error("arithmetic requires int operands", be->loc);
                return type_kind::int_;
            case binary_op::mod:
                if (lt != type_kind::int_ || rt != type_kind::int_) error("modulo requires int operands", be->loc);
                return type_kind::int_;
            case binary_op::eq: case binary_op::ne:
                if (lt != rt) error("comparison requires same types", be->loc);
                return type_kind::bool_;
            case binary_op::lt: case binary_op::gt: case binary_op::le: case binary_op::ge:
                if ((lt == type_kind::int_ || lt == type_kind::float_ || lt == type_kind::double_) &&
                    (rt == type_kind::int_ || rt == type_kind::float_ || rt == type_kind::double_)) {
                    return type_kind::bool_;
                }
                error("relational requires numeric operands", be->loc);
                return type_kind::bool_;
            case binary_op::log_and: case binary_op::log_or:
                if (lt != type_kind::bool_ || rt != type_kind::bool_) error("logical requires bool operands", be->loc);
                return type_kind::bool_;
            default: return type_kind::int_;
        }
    }
    if (auto* ce = dynamic_cast<call_expr*>(e)) {
        if (ce->callee == "print") {
            for (auto& a : ce->args) check_expr(a.get(), fn);
            return type_kind::void_;
        }
        if (ce->callee == "printf") {
            for (auto& a : ce->args) check_expr(a.get(), fn);
            return type_kind::int_;
        }
        symbol s;
        if (!lookup(ce->callee, s) || !s.is_function) { error("undefined function '" + ce->callee + "'", ce->loc); for (auto& a : ce->args) check_expr(a.get(), fn); return type_kind::int_; }
        if ((int)ce->args.size() != s.arity) error("wrong number of arguments to '" + ce->callee + "'", ce->loc);
        for (auto& a : ce->args) check_expr(a.get(), fn);
        return s.type;
    }
    return type_kind::void_;
}

void semantic_analyzer::check_stmt(stmt* s, const function_decl* fn, type_kind expected_return) {
    if (!s) return;
    if (auto* bs = dynamic_cast<block_stmt*>(s)) {
        push_scope();
        for (auto& st : bs->statements) check_stmt(st.get(), fn, expected_return);
        pop_scope();
        return;
    }
    if (auto* vd = dynamic_cast<var_decl_stmt*>(s)) {
        type_kind vt = vd->type;
        if (vt == type_kind::void_) { error("variable cannot have type void", vd->loc); return; }
        if (vd->array_size > 0) {
            if (vt == type_kind::int_) vt = type_kind::int_ptr;
            else if (vt == type_kind::char_) vt = type_kind::char_ptr;
            else if (vt == type_kind::float_) vt = type_kind::float_ptr;
            else if (vt == type_kind::double_) vt = type_kind::double_ptr;
        }
        symbol sym{vt, false, 0};
        if (!declare(vd->name, sym, vd->loc)) return;
        if (vd->init) {
            type_kind t = check_expr(vd->init.get(), fn);
            if (t != vt) error("type mismatch in initializer", vd->loc);
        }
        return;
    }
    if (auto* es = dynamic_cast<expr_stmt*>(s)) {
        check_expr(es->expression.get(), fn);
        return;
    }
    if (auto* is = dynamic_cast<if_stmt*>(s)) {
        type_kind t = check_expr(is->condition.get(), fn);
        if (t != type_kind::bool_) error("if condition must be bool", is->loc);
        check_stmt(is->then_branch.get(), fn, expected_return);
        if (is->else_branch) check_stmt(is->else_branch.get(), fn, expected_return);
        return;
    }
    if (auto* ws = dynamic_cast<while_stmt*>(s)) {
        type_kind t = check_expr(ws->condition.get(), fn);
        if (t != type_kind::bool_) error("while condition must be bool", ws->loc);
        check_stmt(ws->body.get(), fn, expected_return);
        return;
    }
    if (auto* rs = dynamic_cast<return_stmt*>(s)) {
        if (rs->value) {
            type_kind t = check_expr(rs->value.get(), fn);
            if (t != expected_return) error("return type mismatch", rs->loc);
        } else {
            if (expected_return != type_kind::void_) error("expected return value", rs->loc);
        }
        return;
    }
}

void semantic_analyzer::check_function(function_decl* f) {
    push_scope();
    for (auto& [t,name] : f->params) {
        if (t == type_kind::void_) error("parameter cannot have type void", f->loc);
        declare(name, symbol{t,false,0}, f->loc);
    }
    if (f->body) check_stmt(f->body.get(), f, f->return_type);
    pop_scope();
}

bool semantic_analyzer::analyze(program& p) {
    scopes_.clear();
    had_error_ = false;
    errors_.clear();
    push_scope(); // global
    // declare functions
    for (auto& f : p.functions) {
        declare(f->name, symbol{f->return_type, true, (int)f->params.size()}, f->loc);
    }
    for (auto& f : p.functions) {
        check_function(f.get());
    }
    // ensure main exists? optional
    pop_scope();
    return !had_error_;
}

} // namespace tracey
