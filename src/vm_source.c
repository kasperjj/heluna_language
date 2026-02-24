/*
 * vm_source.c — Source resolvers for the Heluna VM.
 *
 * Currently supports:
 *   - "file" type: reads a JSON file, finds a record by key
 *
 * HTTP sources require CURL=1 at build time.
 */

#include "heluna/vm.h"
#include "heluna/json.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ── Value constructors ──────────────────────────────────── */

static HVal *make_nothing(Arena *a) {
    HVal *v = arena_calloc(a, sizeof(HVal));
    v->kind = VAL_NOTHING;
    return v;
}

/* ── Config parsing helpers ──────────────────────────────── */

/* Get a string field from a parsed JSON config record */
static const char *config_get_string(HVal *config, const char *key) {
    if (!config || config->kind != VAL_RECORD) return NULL;
    for (HField *f = config->as.record_fields; f; f = f->next) {
        if (strcmp(f->name, key) == 0 && f->value &&
            f->value->kind == VAL_STRING) {
            return f->value->as.string_val;
        }
    }
    return NULL;
}

/* ── File resolver ───────────────────────────────────────── */

static char *read_file_text(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = malloc((size_t)size + 1);
    if (!buf) { fclose(f); return NULL; }

    size_t nread = fread(buf, 1, (size_t)size, f);
    buf[nread] = '\0';
    fclose(f);
    return buf;
}

/* Get string field from an HVal record */
static const char *record_string(HVal *rec, const char *name) {
    if (!rec || rec->kind != VAL_RECORD) return NULL;
    for (HField *f = rec->as.record_fields; f; f = f->next) {
        if (strcmp(f->name, name) == 0 && f->value &&
            f->value->kind == VAL_STRING) {
            return f->value->as.string_val;
        }
    }
    return NULL;
}

static HVal *resolve_file(Vm *vm, int source_idx, VmSource *src,
                           HVal *config, HVal *keys) {
    Arena *arena = vm->arena;

    /* Check cache */
    HVal *cached_data = vm->source_cache[source_idx];

    if (!cached_data) {
        /* Read and parse the file */
        const char *path = config_get_string(config, "path");
        if (!path) return NULL;

        char *text = read_file_text(path);
        if (!text) return NULL;

        HelunaError err = {0};
        cached_data = json_parse(arena, text, &err);
        free(text);

        if (!cached_data) return NULL;
        vm->source_cache[source_idx] = cached_data;
    }

    /* Navigate to collection if specified */
    HVal *data = cached_data;
    const char *collection = config_get_string(config, "collection");
    if (collection && data->kind == VAL_RECORD) {
        for (HField *f = data->as.record_fields; f; f = f->next) {
            if (strcmp(f->name, collection) == 0) {
                data = f->value;
                break;
            }
        }
    }

    if (!data || data->kind != VAL_LIST) return NULL;

    /* Find matching record by keyed_by field */
    const char *key_field = src->keyed_by;
    if (!key_field) return NULL;

    /* Get the key value from the keys record */
    const char *key_value = record_string(keys, key_field);
    if (!key_value) return NULL;

    /* Iterate through list to find match */
    for (HVal *item = data->as.list_head; item; item = item->next) {
        const char *item_key = record_string(item, key_field);
        if (item_key && strcmp(item_key, key_value) == 0) {
            return item;
        }
    }

    return NULL;
}

/* ── HTTP resolver (conditional) ─────────────────────────── */

#ifdef HELUNA_CURL
#include <curl/curl.h>

/* TODO: Implement HTTP resolver */
static HVal *resolve_http(Vm *vm, int source_idx, VmSource *src,
                           HVal *config, HVal *keys) {
    (void)vm; (void)source_idx; (void)src; (void)config; (void)keys;
    return NULL;
}
#endif

/* ── Public dispatch ─────────────────────────────────────── */

HVal *vm_source_resolve(Vm *vm, int source_idx, HVal *keys) {
    if (source_idx < 0 || source_idx >= vm->packet->source_count) {
        return NULL;
    }

    VmSource *src = &vm->packet->sources[source_idx];
    Arena *arena = vm->arena;

    /* Parse config JSON */
    if (!src->config_json) return NULL;
    HelunaError err = {0};
    HVal *config = json_parse(arena, src->config_json, &err);
    if (!config) return NULL;

    const char *type = config_get_string(config, "type");
    if (!type) return NULL;

    if (strcmp(type, "file") == 0) {
        return resolve_file(vm, source_idx, src, config, keys);
    }

#ifdef HELUNA_CURL
    if (strcmp(type, "http") == 0 || strcmp(type, "https") == 0) {
        return resolve_http(vm, source_idx, src, config, keys);
    }
#endif

    return make_nothing(arena);
}
