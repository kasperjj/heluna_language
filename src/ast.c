#include "heluna/ast.h"
#include <stdio.h>
#include <string.h>

/* ── Indentation helpers ─────────────────────────────────── */

static void indent(FILE *out, int depth) {
    for (int i = 0; i < depth; i++) fprintf(out, "  ");
}

/* ── Forward declarations ────────────────────────────────── */

static void print_expr(const AstExpr *e, FILE *out, int depth);
static void print_pattern(const AstPattern *p, FILE *out, int depth);
static void print_type(const AstType *t, FILE *out, int depth);

/* ── Binary operator names ───────────────────────────────── */

static const char *binop_name(AstBinOp op) {
    switch (op) {
    case BIN_ADD: return "+";
    case BIN_SUB: return "-";
    case BIN_MUL: return "*";
    case BIN_DIV: return "/";
    case BIN_MOD: return "%";
    case BIN_EQ:  return "=";
    case BIN_NEQ: return "!=";
    case BIN_LT:  return "<";
    case BIN_GT:  return ">";
    case BIN_LTE: return "<=";
    case BIN_GTE: return ">=";
    case BIN_AND: return "and";
    case BIN_OR:  return "or";
    }
    return "?";
}

/* ── Expression printer ──────────────────────────────────── */

static void print_expr(const AstExpr *e, FILE *out, int depth) {
    if (!e) { fprintf(out, "nil"); return; }

    switch (e->kind) {
    case EXPR_INTEGER:
        fprintf(out, "%lld", e->as.integer_val);
        break;
    case EXPR_FLOAT:
        fprintf(out, "%g", e->as.float_val);
        break;
    case EXPR_STRING:
        fprintf(out, "%.*s", e->as.string_val.length, e->as.string_val.value);
        break;
    case EXPR_TRUE:
        fprintf(out, "true");
        break;
    case EXPR_FALSE:
        fprintf(out, "false");
        break;
    case EXPR_NOTHING:
        fprintf(out, "nothing");
        break;
    case EXPR_IDENT:
        fprintf(out, "%s", e->as.ident.name);
        break;
    case EXPR_INPUT_REF:
        fprintf(out, "$%s", e->as.input_ref.name);
        break;
    case EXPR_BINARY:
        fprintf(out, "(%s ", binop_name(e->as.binary.op));
        print_expr(e->as.binary.left, out, depth);
        fprintf(out, " ");
        print_expr(e->as.binary.right, out, depth);
        fprintf(out, ")");
        break;
    case EXPR_UNARY_NEG:
        fprintf(out, "(- ");
        print_expr(e->as.unary.operand, out, depth);
        fprintf(out, ")");
        break;
    case EXPR_NOT:
        fprintf(out, "(not ");
        print_expr(e->as.not_expr.operand, out, depth);
        fprintf(out, ")");
        break;
    case EXPR_IF:
        fprintf(out, "(if");
        for (const AstIfBranch *br = e->as.if_expr.branches; br; br = br->next) {
            fprintf(out, "\n");
            indent(out, depth + 1);
            if (br->condition) {
                fprintf(out, "(branch ");
                print_expr(br->condition, out, depth + 1);
                fprintf(out, "\n");
                indent(out, depth + 2);
                print_expr(br->body, out, depth + 2);
                fprintf(out, ")");
            } else {
                fprintf(out, "(else\n");
                indent(out, depth + 2);
                print_expr(br->body, out, depth + 2);
                fprintf(out, ")");
            }
        }
        fprintf(out, ")");
        break;
    case EXPR_MATCH:
        fprintf(out, "(match ");
        print_expr(e->as.match.subject, out, depth);
        for (const AstWhenClause *wc = e->as.match.clauses; wc; wc = wc->next) {
            fprintf(out, "\n");
            indent(out, depth + 1);
            fprintf(out, "(when ");
            print_pattern(wc->pattern, out, depth + 1);
            if (wc->guard) {
                fprintf(out, " :guard ");
                print_expr(wc->guard, out, depth + 1);
            }
            fprintf(out, "\n");
            indent(out, depth + 2);
            print_expr(wc->body, out, depth + 2);
            fprintf(out, ")");
        }
        if (e->as.match.else_body) {
            fprintf(out, "\n");
            indent(out, depth + 1);
            fprintf(out, "(else ");
            print_expr(e->as.match.else_body, out, depth + 1);
            fprintf(out, ")");
        }
        fprintf(out, ")");
        break;
    case EXPR_LET:
        fprintf(out, "(let %s\n", e->as.let.name);
        indent(out, depth + 1);
        print_expr(e->as.let.binding, out, depth + 1);
        fprintf(out, "\n");
        indent(out, depth + 1);
        print_expr(e->as.let.body, out, depth + 1);
        fprintf(out, ")");
        break;
    case EXPR_FILTER:
        fprintf(out, "(filter ");
        print_expr(e->as.filter.list, out, depth);
        fprintf(out, " %s\n", e->as.filter.var_name);
        indent(out, depth + 1);
        print_expr(e->as.filter.predicate, out, depth + 1);
        fprintf(out, ")");
        break;
    case EXPR_MAP:
        fprintf(out, "(map ");
        print_expr(e->as.map.list, out, depth);
        fprintf(out, " %s\n", e->as.map.var_name);
        indent(out, depth + 1);
        print_expr(e->as.map.body, out, depth + 1);
        fprintf(out, ")");
        break;
    case EXPR_THROUGH:
        fprintf(out, "(through\n");
        indent(out, depth + 1);
        print_expr(e->as.through.left, out, depth + 1);
        fprintf(out, "\n");
        indent(out, depth + 1);
        print_expr(e->as.through.right, out, depth + 1);
        fprintf(out, ")");
        break;
    case EXPR_CALL:
        fprintf(out, "(%s ", e->as.call.name);
        print_expr(e->as.call.arg, out, depth);
        fprintf(out, ")");
        break;
    case EXPR_RECORD:
        fprintf(out, "{");
        for (const AstLabel *l = e->as.record.labels; l; l = l->next) {
            fprintf(out, "\n");
            indent(out, depth + 1);
            fprintf(out, "(%s: ", l->name);
            print_expr(l->value, out, depth + 1);
            fprintf(out, ")");
        }
        if (e->as.record.labels) {
            fprintf(out, "\n");
            indent(out, depth);
        }
        fprintf(out, "}");
        break;
    case EXPR_LIST:
        fprintf(out, "[");
        for (const AstExpr *elem = e->as.list.elements; elem; elem = elem->next) {
            if (elem != e->as.list.elements) fprintf(out, " ");
            print_expr(elem, out, depth);
        }
        fprintf(out, "]");
        break;
    case EXPR_ACCESS:
        fprintf(out, "(. ");
        print_expr(e->as.access.object, out, depth);
        fprintf(out, " %s)", e->as.access.field);
        break;
    case EXPR_PAREN:
        fprintf(out, "(paren ");
        print_expr(e->as.paren.inner, out, depth);
        fprintf(out, ")");
        break;
    }
}

/* ── Pattern printer ─────────────────────────────────────── */

static void print_pattern(const AstPattern *p, FILE *out, int depth) {
    if (!p) { fprintf(out, "nil"); return; }

    switch (p->kind) {
    case PAT_LITERAL:
        print_expr(p->as.literal.value, out, depth);
        break;
    case PAT_WILDCARD:
        fprintf(out, "_");
        break;
    case PAT_BINDING:
        fprintf(out, "%s", p->as.binding.name);
        break;
    case PAT_RANGE:
        fprintf(out, "(between ");
        print_expr(p->as.range.low, out, depth);
        fprintf(out, " ");
        print_expr(p->as.range.high, out, depth);
        fprintf(out, ")");
        break;
    case PAT_LIST:
        fprintf(out, "[");
        for (const AstPatternElem *e = p->as.list.elements; e; e = e->next) {
            if (e != p->as.list.elements) fprintf(out, " ");
            print_pattern(e->pattern, out, depth);
        }
        if (p->as.list.rest_name) {
            if (p->as.list.elements) fprintf(out, " ");
            fprintf(out, "..%s", p->as.list.rest_name);
        }
        fprintf(out, "]");
        break;
    case PAT_RECORD:
        fprintf(out, "{");
        for (const AstFieldPattern *fp = p->as.record.fields; fp; fp = fp->next) {
            if (fp != p->as.record.fields) fprintf(out, " ");
            fprintf(out, "%s: ", fp->name);
            print_pattern(fp->pattern, out, depth);
        }
        fprintf(out, "}");
        break;
    }
}

/* ── Type printer ────────────────────────────────────────── */

static void print_type(const AstType *t, FILE *out, int depth) {
    if (!t) { fprintf(out, "nil"); return; }

    switch (t->kind) {
    case TYPE_STRING:  fprintf(out, "string");  break;
    case TYPE_INTEGER: fprintf(out, "integer"); break;
    case TYPE_FLOAT:   fprintf(out, "float");   break;
    case TYPE_BOOLEAN: fprintf(out, "boolean"); break;
    case TYPE_MAYBE:
        fprintf(out, "(maybe ");
        print_type(t->as.maybe.inner, out, depth);
        fprintf(out, ")");
        break;
    case TYPE_LIST:
        fprintf(out, "(list ");
        print_type(t->as.list.inner, out, depth);
        fprintf(out, ")");
        break;
    case TYPE_RECORD:
        fprintf(out, "(record");
        for (const AstFieldDecl *fd = t->as.record.fields; fd; fd = fd->next) {
            fprintf(out, "\n");
            indent(out, depth + 1);
            fprintf(out, "(%s ", fd->name);
            print_type(fd->type, out, depth + 1);
            if (fd->tag_count > 0) {
                fprintf(out, " :tagged");
                for (int i = 0; i < fd->tag_count; i++)
                    fprintf(out, " %s", fd->tags[i]);
            }
            fprintf(out, ")");
        }
        fprintf(out, ")");
        break;
    }
}

/* ── Field decl printer ──────────────────────────────────── */

static void print_field_decl(const AstFieldDecl *fd, FILE *out, int depth) {
    fprintf(out, "(field \"%s\" ", fd->name);
    print_type(fd->type, out, depth);
    if (fd->tag_count > 0) {
        fprintf(out, " :tagged");
        for (int i = 0; i < fd->tag_count; i++)
            fprintf(out, " %s", fd->tags[i]);
    }
    fprintf(out, ")");
}

/* ── Field ref printer ───────────────────────────────────── */

static void print_field_ref(const AstFieldRef *ref, FILE *out) {
    fprintf(out, "%s", ref->is_output ? "output" : "input");
    for (int i = 0; i < ref->accessor_count; i++)
        fprintf(out, ".%s", ref->accessors[i]);
}

/* ── Program printer ─────────────────────────────────────── */

void ast_print(const AstProgram *prog, FILE *out) {
    if (!prog) { fprintf(out, "(nil)\n"); return; }

    fprintf(out, "(program\n");

    /* Contract */
    const AstContract *c = prog->contract;
    if (c) {
        indent(out, 1);
        fprintf(out, "(contract \"%s\"\n", c->name);

        /* uses */
        if (c->uses_count > 0) {
            indent(out, 2);
            fprintf(out, "(uses");
            for (int i = 0; i < c->uses_count; i++)
                fprintf(out, " %s", c->uses[i]);
            fprintf(out, ")\n");
        }

        /* tags */
        if (c->tags) {
            indent(out, 2);
            fprintf(out, "(tags\n");
            for (const AstTagDef *td = c->tags; td; td = td->next) {
                indent(out, 3);
                fprintf(out, "(%s", td->name);
                if (td->description) fprintf(out, " %s", td->description);
                fprintf(out, ")\n");
            }
            indent(out, 2);
            fprintf(out, ")\n");
        }

        /* sanitizers */
        if (c->sanitizers) {
            indent(out, 2);
            fprintf(out, "(sanitizers\n");
            for (const AstSanitizerDef *sd = c->sanitizers; sd; sd = sd->next) {
                indent(out, 3);
                fprintf(out, "(%s strips", sd->name);
                for (int i = 0; i < sd->stripped_count; i++)
                    fprintf(out, " %s", sd->stripped_tags[i]);
                fprintf(out, ")\n");
            }
            indent(out, 2);
            fprintf(out, ")\n");
        }

        /* input */
        indent(out, 2);
        fprintf(out, "(input\n");
        for (const AstFieldDecl *fd = c->input; fd; fd = fd->next) {
            indent(out, 3);
            print_field_decl(fd, out, 3);
            fprintf(out, "\n");
        }
        indent(out, 2);
        fprintf(out, ")\n");

        /* output */
        indent(out, 2);
        fprintf(out, "(output\n");
        for (const AstFieldDecl *fd = c->output; fd; fd = fd->next) {
            indent(out, 3);
            print_field_decl(fd, out, 3);
            fprintf(out, "\n");
        }
        indent(out, 2);
        fprintf(out, ")\n");

        /* rules */
        if (c->rules) {
            indent(out, 2);
            fprintf(out, "(rules\n");
            for (const AstRule *r = c->rules; r; r = r->next) {
                indent(out, 3);
                switch (r->kind) {
                case RULE_FORBID_FIELD:
                    fprintf(out, "(forbid ");
                    print_field_ref(r->as.forbid_field.field_ref, out);
                    fprintf(out, " in output)");
                    break;
                case RULE_FORBID_TAGGED:
                    fprintf(out, "(forbid tagged %s in output)", r->as.forbid_tagged.tag_name);
                    break;
                case RULE_REQUIRE:
                    fprintf(out, "(require ");
                    print_field_ref(r->as.require.field_ref, out);
                    fprintf(out, "\n");
                    indent(out, 4);
                    print_expr(r->as.require.condition, out, 4);
                    if (r->as.require.reject_msg) {
                        fprintf(out, "\n");
                        indent(out, 4);
                        fprintf(out, ":reject %s", r->as.require.reject_msg);
                    }
                    fprintf(out, ")");
                    break;
                case RULE_MATCH:
                    fprintf(out, "(match ");
                    print_field_ref(r->as.match.field_ref, out);
                    for (const AstRuleWhenClause *wc = r->as.match.clauses; wc; wc = wc->next) {
                        fprintf(out, "\n");
                        indent(out, 4);
                        fprintf(out, "(when ");
                        print_pattern(wc->pattern, out, 4);
                        fprintf(out, " ");
                        print_expr(wc->body, out, 4);
                        fprintf(out, ")");
                    }
                    if (r->as.match.reject_msg) {
                        fprintf(out, "\n");
                        indent(out, 4);
                        fprintf(out, ":reject %s", r->as.match.reject_msg);
                    }
                    fprintf(out, ")");
                    break;
                }
                fprintf(out, "\n");
            }
            indent(out, 2);
            fprintf(out, ")\n");
        }

        /* tests */
        if (c->tests) {
            indent(out, 2);
            fprintf(out, "(tests\n");
            for (const AstTestCase *tc = c->tests; tc; tc = tc->next) {
                indent(out, 3);
                fprintf(out, "(test %s\n", tc->name);
                indent(out, 4);
                fprintf(out, "(given ");
                print_expr(tc->given, out, 4);
                fprintf(out, ")\n");
                indent(out, 4);
                fprintf(out, "(expect ");
                print_expr(tc->expect, out, 4);
                fprintf(out, "))\n");
            }
            indent(out, 2);
            fprintf(out, ")\n");
        }

        indent(out, 1);
        fprintf(out, ")\n");
    }

    /* Function def */
    const AstFunctionDef *fn = prog->function;
    if (fn) {
        indent(out, 1);
        fprintf(out, "(define \"%s\"\n", fn->name);
        indent(out, 2);
        print_expr(fn->body, out, 2);
        fprintf(out, ")\n");
    }

    fprintf(out, ")\n");
}
