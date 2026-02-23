/*
 * formatter.c — emit canonical Heluna source from an AST.
 *
 * Known limitation: comments are discarded during parsing, so the
 * formatter cannot preserve them.
 */

#include "heluna/formatter.h"
#include <string.h>

/* ── Indentation helper ─────────────────────────────────── */

static void indent(FILE *out, int depth) {
    for (int i = 0; i < depth; i++) fprintf(out, "  ");
}

/* ── Forward declarations ───────────────────────────────── */

static void fmt_expr(const AstExpr *e, FILE *out, int depth);
static void fmt_pattern(const AstPattern *p, FILE *out);
static void fmt_type(const AstType *t, FILE *out, int depth);

/* ── Helpers ────────────────────────────────────────────── */

static int is_block_expr(const AstExpr *e) {
    if (!e) return 0;
    switch (e->kind) {
    case EXPR_IF:
    case EXPR_MATCH:
    case EXPR_FILTER:
    case EXPR_MAP:
    case EXPR_LOOKUP:
        return 1;
    default:
        return 0;
    }
}

static const char *binop_str(AstBinOp op) {
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

/* ── Pattern formatter ──────────────────────────────────── */

static void fmt_pattern(const AstPattern *p, FILE *out) {
    if (!p) return;

    switch (p->kind) {
    case PAT_LITERAL:
        /* Delegate to expression formatter at depth 0 (inline) */
        fmt_expr(p->as.literal.value, out, 0);
        break;
    case PAT_WILDCARD:
        fprintf(out, "_");
        break;
    case PAT_BINDING:
        fprintf(out, "%s", p->as.binding.name);
        break;
    case PAT_RANGE:
        fprintf(out, "between ");
        fmt_expr(p->as.range.low, out, 0);
        fprintf(out, " and ");
        fmt_expr(p->as.range.high, out, 0);
        break;
    case PAT_LIST:
        fprintf(out, "[");
        for (const AstPatternElem *el = p->as.list.elements; el; el = el->next) {
            if (el != p->as.list.elements) fprintf(out, ", ");
            fmt_pattern(el->pattern, out);
        }
        if (p->as.list.rest_name) {
            if (p->as.list.elements) fprintf(out, ", ");
            fprintf(out, "..%s", p->as.list.rest_name);
        }
        fprintf(out, "]");
        break;
    case PAT_RECORD:
        fprintf(out, "{ ");
        for (const AstFieldPattern *fp = p->as.record.fields; fp; fp = fp->next) {
            if (fp != p->as.record.fields) fprintf(out, ", ");
            fprintf(out, "%s: ", fp->name);
            fmt_pattern(fp->pattern, out);
        }
        fprintf(out, " }");
        break;
    }
}

/* ── Type formatter ─────────────────────────────────────── */

static void fmt_type(const AstType *t, FILE *out, int depth) {
    if (!t) return;

    switch (t->kind) {
    case TYPE_STRING:  fprintf(out, "string");  break;
    case TYPE_INTEGER: fprintf(out, "integer"); break;
    case TYPE_FLOAT:   fprintf(out, "float");   break;
    case TYPE_BOOLEAN: fprintf(out, "boolean"); break;
    case TYPE_MAYBE:
        fprintf(out, "maybe ");
        fmt_type(t->as.maybe.inner, out, depth);
        break;
    case TYPE_LIST:
        fprintf(out, "list of ");
        fmt_type(t->as.list.inner, out, depth);
        break;
    case TYPE_RECORD:
        fprintf(out, "record\n");
        for (const AstFieldDecl *fd = t->as.record.fields; fd; fd = fd->next) {
            indent(out, depth + 1);
            fprintf(out, "%s as ", fd->name);
            fmt_type(fd->type, out, depth + 1);
            if (fd->tag_count > 0) {
                fprintf(out, " tagged");
                for (int i = 0; i < fd->tag_count; i++)
                    fprintf(out, " %s", fd->tags[i]);
            }
            if (fd->next) fprintf(out, ",");
            fprintf(out, "\n");
        }
        indent(out, depth);
        fprintf(out, "end");
        break;
    }
}

/* ── Field decl formatter ───────────────────────────────── */

static void fmt_field_decl(const AstFieldDecl *f, FILE *out, int depth) {
    fprintf(out, "%s as ", f->name);
    fmt_type(f->type, out, depth);
    if (f->tag_count > 0) {
        fprintf(out, " tagged");
        for (int i = 0; i < f->tag_count; i++)
            fprintf(out, " %s", f->tags[i]);
    }
}

/* ── Field ref formatter ────────────────────────────────── */

static void fmt_field_ref(const AstFieldRef *r, FILE *out) {
    fprintf(out, "%s", r->is_output ? "output" : "input");
    for (int i = 0; i < r->accessor_count; i++)
        fprintf(out, ".%s", r->accessors[i]);
}

/* ── Expression formatter ───────────────────────────────── */

static void fmt_expr(const AstExpr *e, FILE *out, int depth) {
    if (!e) return;

    switch (e->kind) {
    case EXPR_INTEGER:
        fprintf(out, "%lld", e->as.integer_val);
        break;
    case EXPR_FLOAT: {
        char buf[64];
        snprintf(buf, sizeof buf, "%g", e->as.float_val);
        /* Ensure the output contains a decimal point so it re-parses
           as a float, not an integer. */
        if (!strchr(buf, '.') && !strchr(buf, 'e') && !strchr(buf, 'E'))
            fprintf(out, "%s.0", buf);
        else
            fprintf(out, "%s", buf);
        break;
    }
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
        fmt_expr(e->as.binary.left, out, depth);
        fprintf(out, " %s ", binop_str(e->as.binary.op));
        fmt_expr(e->as.binary.right, out, depth);
        break;

    case EXPR_UNARY_NEG:
        fprintf(out, "-");
        fmt_expr(e->as.unary.operand, out, depth);
        break;

    case EXPR_NOT:
        fprintf(out, "not ");
        fmt_expr(e->as.not_expr.operand, out, depth);
        break;

    case EXPR_PAREN:
        fprintf(out, "(");
        fmt_expr(e->as.paren.inner, out, depth);
        fprintf(out, ")");
        break;

    case EXPR_IF:
        for (const AstIfBranch *br = e->as.if_expr.branches; br; br = br->next) {
            if (br == e->as.if_expr.branches) {
                /* First branch: if ... then */
                fprintf(out, "if ");
                fmt_expr(br->condition, out, depth);
                fprintf(out, " then ");
            } else if (br->condition) {
                /* else if ... then */
                fprintf(out, "\n");
                indent(out, depth);
                fprintf(out, "else if ");
                fmt_expr(br->condition, out, depth);
                fprintf(out, " then ");
            } else {
                /* final else */
                fprintf(out, "\n");
                indent(out, depth);
                fprintf(out, "else ");
            }
            if (is_block_expr(br->body)) {
                fprintf(out, "\n");
                indent(out, depth + 1);
            }
            fmt_expr(br->body, out, depth + 1);
        }
        fprintf(out, "\n");
        indent(out, depth);
        fprintf(out, "end");
        break;

    case EXPR_MATCH:
        fprintf(out, "match ");
        fmt_expr(e->as.match.subject, out, depth);
        for (const AstWhenClause *wc = e->as.match.clauses; wc; wc = wc->next) {
            fprintf(out, "\n");
            indent(out, depth + 1);
            fprintf(out, "when ");
            fmt_pattern(wc->pattern, out);
            if (wc->guard) {
                fprintf(out, " and ");
                fmt_expr(wc->guard, out, depth + 1);
            }
            fprintf(out, " then ");
            if (is_block_expr(wc->body)) {
                fprintf(out, "\n");
                indent(out, depth + 2);
            }
            fmt_expr(wc->body, out, depth + 2);
        }
        if (e->as.match.else_body) {
            fprintf(out, "\n");
            indent(out, depth + 1);
            fprintf(out, "else ");
            fmt_expr(e->as.match.else_body, out, depth + 2);
        }
        fprintf(out, "\n");
        indent(out, depth);
        fprintf(out, "end");
        break;

    case EXPR_LET: {
        if (is_block_expr(e->as.let.binding)) {
            fprintf(out, "let %s be\n", e->as.let.name);
            indent(out, depth + 1);
        } else {
            fprintf(out, "let %s be ", e->as.let.name);
        }
        fmt_expr(e->as.let.binding, out, depth + 1);
        fprintf(out, "\n");
        /* The body is either another let or the result expression */
        if (e->as.let.body) {
            /* Check if body is another let — if so, continue at same depth */
            if (e->as.let.body->kind == EXPR_LET) {
                fprintf(out, "\n");
                indent(out, depth);
                fmt_expr(e->as.let.body, out, depth);
            } else {
                /* This is the result expression */
                indent(out, depth);
                fprintf(out, "result ");
                if (is_block_expr(e->as.let.body)) {
                    fprintf(out, "\n");
                    indent(out, depth + 1);
                }
                fmt_expr(e->as.let.body, out, depth);
            }
        }
        break;
    }

    case EXPR_FILTER:
        fprintf(out, "filter ");
        fmt_expr(e->as.filter.list, out, depth);
        fprintf(out, " where %s ", e->as.filter.var_name);
        fmt_expr(e->as.filter.predicate, out, depth);
        fprintf(out, " end");
        break;

    case EXPR_MAP:
        fprintf(out, "map ");
        fmt_expr(e->as.map.list, out, depth);
        fprintf(out, " as %s do ", e->as.map.var_name);
        fmt_expr(e->as.map.body, out, depth);
        fprintf(out, " end");
        break;

    case EXPR_THROUGH:
        fmt_expr(e->as.through.left, out, depth);
        fprintf(out, " through ");
        fmt_expr(e->as.through.right, out, depth);
        break;

    case EXPR_CALL:
        fprintf(out, "%s(", e->as.call.name);
        fmt_expr(e->as.call.arg, out, depth);
        fprintf(out, ")");
        break;

    case EXPR_RECORD:
        if (!e->as.record.labels) {
            fprintf(out, "{}");
        } else {
            fprintf(out, "{\n");
            for (const AstLabel *l = e->as.record.labels; l; l = l->next) {
                indent(out, depth + 1);
                fprintf(out, "%s: ", l->name);
                fmt_expr(l->value, out, depth + 1);
                if (l->next) fprintf(out, ",");
                fprintf(out, "\n");
            }
            indent(out, depth);
            fprintf(out, "}");
        }
        break;

    case EXPR_LIST:
        fprintf(out, "[");
        for (const AstExpr *el = e->as.list.elements; el; el = el->next) {
            if (el != e->as.list.elements) fprintf(out, ", ");
            fmt_expr(el, out, depth);
        }
        fprintf(out, "]");
        break;

    case EXPR_ACCESS:
        fmt_expr(e->as.access.object, out, depth);
        fprintf(out, ".%s", e->as.access.field);
        break;

    case EXPR_LOOKUP:
        fprintf(out, "lookup %s\n", e->as.lookup.source_name);
        indent(out, depth + 1);
        fprintf(out, "where ");
        for (const AstLookupKey *lk = e->as.lookup.keys; lk; lk = lk->next) {
            if (lk != e->as.lookup.keys) {
                fprintf(out, ", ");
            }
            fprintf(out, "%s = ", lk->name);
            fmt_expr(lk->value, out, depth + 1);
        }
        fprintf(out, "\n");
        indent(out, depth);
        fprintf(out, "end");
        break;
    }
}

/* ── Contract formatter ─────────────────────────────────── */

static void fmt_contract(const AstContract *c, FILE *out) {
    fprintf(out, "contract %s\n", c->name);

    /* uses */
    if (c->uses_count > 0) {
        indent(out, 1);
        fprintf(out, "uses");
        for (int i = 0; i < c->uses_count; i++)
            fprintf(out, " %s", c->uses[i]);
        fprintf(out, "\n\n");
    }

    /* tags */
    if (c->tags) {
        indent(out, 1);
        fprintf(out, "tags\n");
        for (const AstTagDef *td = c->tags; td; td = td->next) {
            indent(out, 2);
            fprintf(out, "%s", td->name);
            if (td->description) fprintf(out, " %s", td->description);
            if (td->next) fprintf(out, ",");
            fprintf(out, "\n");
        }
        indent(out, 1);
        fprintf(out, "end\n\n");
    }

    /* Source contract: source, keyed-by, returns */
    if (c->kind == CONTRACT_SOURCE) {
        indent(out, 1);
        fprintf(out, "source %s\n\n", c->source_name);

        indent(out, 1);
        fprintf(out, "keyed-by ");
        for (const AstFieldDecl *fd = c->keyed_by; fd; fd = fd->next) {
            if (fd != c->keyed_by) fprintf(out, ", ");
            fmt_field_decl(fd, out, 1);
        }
        fprintf(out, "\n\n");

        indent(out, 1);
        fprintf(out, "returns ");
        fmt_type(c->returns_type, out, 1);
        fprintf(out, "\n");

        fprintf(out, "end\n");
        return;
    }

    /* Tag contract: just tags (already printed above) */
    if (c->kind == CONTRACT_TAG) {
        fprintf(out, "end\n");
        return;
    }

    /* Function contract continues below */

    /* sanitizers */
    if (c->sanitizers) {
        indent(out, 1);
        fprintf(out, "sanitizers\n");
        for (const AstSanitizerDef *sd = c->sanitizers; sd; sd = sd->next) {
            indent(out, 2);
            fprintf(out, "%s", sd->name);
            if (sd->impl_name)
                fprintf(out, " using %s", sd->impl_name);
            fprintf(out, " strips");
            for (int i = 0; i < sd->stripped_count; i++)
                fprintf(out, " %s", sd->stripped_tags[i]);
            if (sd->next) fprintf(out, ",");
            fprintf(out, "\n");
        }
        indent(out, 1);
        fprintf(out, "end\n\n");
    }

    /* sources */
    if (c->sources_count > 0) {
        indent(out, 1);
        fprintf(out, "sources");
        for (int i = 0; i < c->sources_count; i++) {
            if (i > 0) fprintf(out, ",");
            fprintf(out, " %s", c->sources_refs[i]);
        }
        fprintf(out, "\n\n");
    }

    /* input */
    indent(out, 1);
    fprintf(out, "input\n");
    for (const AstFieldDecl *fd = c->input; fd; fd = fd->next) {
        indent(out, 2);
        fmt_field_decl(fd, out, 2);
        if (fd->next) fprintf(out, ",");
        fprintf(out, "\n");
    }
    indent(out, 1);
    fprintf(out, "end\n\n");

    /* output */
    indent(out, 1);
    fprintf(out, "output\n");
    for (const AstFieldDecl *fd = c->output; fd; fd = fd->next) {
        indent(out, 2);
        fmt_field_decl(fd, out, 2);
        if (fd->next) fprintf(out, ",");
        fprintf(out, "\n");
    }
    indent(out, 1);
    fprintf(out, "end\n");

    /* rules */
    if (c->rules) {
        fprintf(out, "\n");
        indent(out, 1);
        fprintf(out, "rules\n");
        for (const AstRule *r = c->rules; r; r = r->next) {
            indent(out, 2);
            switch (r->kind) {
            case RULE_FORBID_FIELD:
                fprintf(out, "forbid ");
                fmt_field_ref(r->as.forbid_field.field_ref, out);
                fprintf(out, " in output\n");
                break;
            case RULE_FORBID_TAGGED:
                fprintf(out, "forbid tagged %s in output\n",
                        r->as.forbid_tagged.tag_name);
                break;
            case RULE_REQUIRE:
                fprintf(out, "require ");
                fmt_field_ref(r->as.require.field_ref, out);
                fprintf(out, "\n");
                indent(out, 3);
                fmt_expr(r->as.require.condition, out, 3);
                if (r->as.require.reject_msg) {
                    fprintf(out, "\n");
                    indent(out, 3);
                    fprintf(out, "else reject %s", r->as.require.reject_msg);
                }
                fprintf(out, "\n");
                indent(out, 2);
                fprintf(out, "end\n");
                break;
            case RULE_MATCH:
                fprintf(out, "match ");
                fmt_field_ref(r->as.match.field_ref, out);
                fprintf(out, "\n");
                for (const AstRuleWhenClause *wc = r->as.match.clauses;
                     wc; wc = wc->next) {
                    indent(out, 3);
                    fprintf(out, "when ");
                    fmt_pattern(wc->pattern, out);
                    fprintf(out, " then ");
                    fmt_expr(wc->body, out, 3);
                    fprintf(out, "\n");
                }
                if (r->as.match.reject_msg) {
                    indent(out, 3);
                    fprintf(out, "else reject %s\n", r->as.match.reject_msg);
                }
                indent(out, 2);
                fprintf(out, "end\n");
                break;
            }
        }
        indent(out, 1);
        fprintf(out, "end\n");
    }

    /* tests */
    if (c->tests) {
        fprintf(out, "\n");
        indent(out, 1);
        fprintf(out, "tests\n");
        for (const AstTestCase *tc = c->tests; tc; tc = tc->next) {
            indent(out, 2);
            fprintf(out, "test %s\n", tc->name);
            indent(out, 3);
            fprintf(out, "given ");
            fmt_expr(tc->given, out, 3);
            fprintf(out, "\n");
            indent(out, 3);
            fprintf(out, "expect ");
            fmt_expr(tc->expect, out, 3);
            fprintf(out, "\n");
            indent(out, 2);
            fprintf(out, "end\n");
        }
        indent(out, 1);
        fprintf(out, "end\n");
    }

    fprintf(out, "end\n");
}

/* ── Function formatter ─────────────────────────────────── */

static void fmt_function(const AstFunctionDef *fn, FILE *out) {
    fprintf(out, "\ndefine %s with input\n", fn->name);

    const AstExpr *body = fn->body;
    if (body->kind == EXPR_LET) {
        indent(out, 1);
        fmt_expr(body, out, 1);
        fprintf(out, "\n");
    } else {
        /* No let bindings, just a result */
        indent(out, 1);
        fprintf(out, "result ");
        if (is_block_expr(body)) {
            fprintf(out, "\n");
            indent(out, 2);
        }
        fmt_expr(body, out, 1);
        fprintf(out, "\n");
    }

    fprintf(out, "end\n");
}

/* ── Public API ─────────────────────────────────────────── */

void heluna_format(const AstProgram *prog, FILE *out) {
    if (!prog) return;
    if (prog->contract) fmt_contract(prog->contract, out);
    if (prog->function) fmt_function(prog->function, out);
}
