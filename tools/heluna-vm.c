/*
 * heluna-vm: execute a compiled Heluna VM packet (.hlna) with JSON input.
 *
 * Usage: heluna-vm <packet.hlna> <input.json | '{"key":"value"}'>
 *
 * Pipeline: read packet → load → parse input JSON → execute → emit output JSON.
 */

#include "heluna/vm.h"
#include "heluna/json.h"
#include "heluna/arena.h"
#include "heluna/errors.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *read_binary(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "heluna-vm: cannot open '%s'\n", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *buf = malloc((size_t)size);
    if (!buf) {
        fclose(f);
        fprintf(stderr, "heluna-vm: out of memory\n");
        return NULL;
    }

    size_t nread = fread(buf, 1, (size_t)size, f);
    fclose(f);
    *out_size = nread;
    return buf;
}

static char *read_text(const char *path) {
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

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr,
            "usage: heluna-vm <packet.hlna> <input.json | '{...}'>\n");
        return 1;
    }

    const char *packet_path = argv[1];
    const char *input_arg   = argv[2];

    /* Read packet */
    size_t pkt_size = 0;
    uint8_t *pkt_data = read_binary(packet_path, &pkt_size);
    if (!pkt_data) return 1;

    Arena *arena = arena_create(64 * 1024);

    /* Load packet */
    HelunaError load_err = {0};
    VmPacket *packet = vm_load_packet(pkt_data, pkt_size, arena, &load_err);
    if (!packet) {
        fprintf(stderr, "heluna-vm: load error: %s\n", load_err.message);
        arena_destroy(arena);
        free(pkt_data);
        return 1;
    }

    /* Parse input JSON — either inline string or file */
    const char *json_str = input_arg;
    char *json_file_buf = NULL;
    if (input_arg[0] != '{' && input_arg[0] != '[') {
        json_file_buf = read_text(input_arg);
        if (!json_file_buf) {
            fprintf(stderr, "heluna-vm: cannot read input '%s'\n", input_arg);
            arena_destroy(arena);
            free(pkt_data);
            return 1;
        }
        json_str = json_file_buf;
    }

    HelunaError parse_err = {0};
    HVal *input = json_parse(arena, json_str, &parse_err);
    free(json_file_buf);

    if (!input) {
        fprintf(stderr, "heluna-vm: JSON parse error: %s\n",
                parse_err.message);
        arena_destroy(arena);
        free(pkt_data);
        return 1;
    }

    /* Execute */
    Vm vm;
    vm_init(&vm, packet, arena);
    HVal *result = vm_execute(&vm, input);

    if (vm.had_error || !result) {
        fprintf(stderr, "heluna-vm: runtime error: %s\n", vm.error.message);
        arena_destroy(arena);
        free(pkt_data);
        return 1;
    }

    /* Emit output */
    json_emit(result, stdout);
    printf("\n");

    arena_destroy(arena);
    free(pkt_data);
    return 0;
}
