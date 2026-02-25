# Heluna Virtual Machine & Packet Format Specification

**Version 1.0 — Draft**

---

## 1. Overview

This document specifies the Heluna packet format and virtual machine execution model. A Heluna packet is the output of the Heluna compiler — a self-contained, cryptographically signed binary artifact that any conforming VM can execute to perform JSON transformations.

The specification is intentionally **semantic rather than mechanical**. It defines what operations must produce, not how a VM must produce them. A conforming VM is one that, given any valid packet and valid input JSON, produces the correct output JSON with all tag constraints satisfied. How it achieves this — through interpretation, JIT compilation, parallel execution, lazy evaluation, or any other strategy — is entirely the implementer's choice.

### 1.1 Design Principles

**Compile once, run anywhere.** A packet compiled by the Heluna compiler can be executed by any conforming VM, regardless of the host language or platform. A Go service, a Rust edge proxy, a Python data pipeline, and a Java API gateway all consume the same packet.

**The packet is a trust boundary.** The Ed25519 signature guarantees that the bytecode has not been tampered with since compilation. The contract's security rules (tag constraints, sanitizer declarations, forbid rules) were enforced at compile time. The host process trusts the signing key; the signature guarantees the bytecode that passed those checks is the bytecode being executed.

**Fat packets, zero runtime resolution.** A packet contains all inlined dependencies, the constant pool, contract metadata, and the full instruction stream. There is no linker phase, no dependency fetching, no version conflicts at runtime. One file, one artifact, atomic deployment.

**Simple VMs are easy; fast VMs are possible.** The required sections of the packet are minimal — a simple VM can be implemented in an afternoon. Optional metadata sections emitted by the compiler give sophisticated VMs the information they need to optimize aggressively, without burdening simple implementations.

**The compiler is generous; the VM is free.** The packet format includes optional sections containing compiler analysis — dependency graphs, type layouts, liveness ranges, branch hints. A simple VM ignores all of these. A fast VM exploits whichever ones it understands. The compiler invests complexity once; every VM benefits.

### 1.2 Execution Model Summary

A Heluna function executes as a directed acyclic graph of operations over a flat memory region called the **scratchpad**. The scratchpad is an array of JSON value slots, pre-allocated at the size declared in the packet. The compiler assigns every input field, intermediate value, and output field a fixed offset in the scratchpad.

Every execution occurs at a single **logical timestamp** — a frozen moment in time provided by the host process. All time-related functions return values consistent with this instant. This ensures temporal consistency even when the VM executes instructions in parallel, speculatively, or out of order. The logical timestamp is also available for future external lookup queries, allowing external sources to provide data as it existed at that moment.

Execution proceeds as:

1. The host provides an input JSON record and a logical timestamp.
2. The VM allocates the scratchpad.
3. Input JSON fields are mapped to their assigned scratchpad offsets (eagerly or lazily — the VM decides).
4. Instructions execute, reading from and writing to scratchpad offsets.
5. Output JSON is constructed from the designated output scratchpad offsets.

There is no call stack, no heap, no recursion, and no dynamic memory allocation during execution. The scratchpad is the only mutable state, and its size is fixed before the first instruction runs.

### 1.3 Language Properties That Enable This Model

The Heluna language guarantees several properties that the VM specification depends on:

- **Pure functions, no side effects.** Functions cannot perform I/O, access external state, or mutate anything outside the scratchpad. Every function is deterministic.
- **No recursion.** Functions cannot call themselves, directly or indirectly. This guarantees termination and eliminates the need for a call stack.
- **No shadowing.** Every binding has a unique name. The compiler maps each to a unique scratchpad offset with no ambiguity.
- **Bounded iteration.** The only looping constructs — `map`, `filter`, `fold` — iterate over finite input lists. Iteration count is determined by input data, never by computation.
- **All expressions, no statements.** Every construct (`if`, `match`, `let`) produces a value. There are no side-effecting statements.

Together, these guarantee that every Heluna function is a finite, acyclic dataflow graph with statically known memory requirements.

---

## 2. Packet Format

A Heluna packet is a binary file consisting of a fixed header followed by a directory of typed sections. Sections are either required (every VM must process them) or optional (VMs may ignore them).

### 2.1 Header

The header is exactly **88 bytes**, fixed across all format versions.

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 4 | `magic` | `0x484C4E41` — ASCII "HLNA" |
| 4 | 2 | `format_version` | Packet format version (currently `1`) |
| 6 | 2 | `min_spec_version` | Minimum stdlib/opcode spec version required |
| 8 | 4 | `total_size` | Total packet size in bytes |
| 12 | 2 | `section_count` | Number of entries in the section directory |
| 14 | 2 | `flags` | Reserved, must be zero |
| 16 | 8 | `key_fingerprint` | First 8 bytes of SHA-256 of the signing public key |
| 24 | 64 | `signature` | Ed25519 signature over all bytes following the header |

**Byte order:** All multi-byte integers are stored in little-endian format.

**Signature scope:** The signature covers every byte from the end of the header (offset 88) to the end of the packet. The header itself is not signed — the `total_size` field allows the VM to know how much data the signature covers.

**Verification sequence:**
1. Read the header.
2. Verify `magic` matches `0x484C4E41`.
3. Look up the signing public key using `key_fingerprint`.
4. Verify the Ed25519 `signature` over bytes `[88, total_size)`.
5. If verification fails, reject the packet. Do not parse any sections.

### 2.2 Section Directory

Immediately following the header, the section directory contains `section_count` entries of **10 bytes** each:

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 2 | `section_type` | Section type identifier |
| 2 | 4 | `section_offset` | Byte offset from start of packet |
| 6 | 4 | `section_length` | Section length in bytes |

Sections may appear in any order. A VM locates sections by scanning the directory for the desired `section_type`. Unknown section types are silently skipped.

### 2.3 Section Types

#### Required Sections

Every conforming VM must process these sections. A packet missing any required section is invalid.

| Type ID | Name | Description |
|---------|------|-------------|
| `0x0001` | `CONTRACT` | Input/output schemas, tag definitions, rules |
| `0x0002` | `CONSTANTS` | Constant pool — all literal values referenced by bytecode |
| `0x0003` | `STDLIB_DEPS` | List of stdlib function IDs used by this packet |
| `0x0004` | `BYTECODE` | Instruction stream |

#### Optional Sections

A VM may ignore any optional section. Ignoring optional sections must not affect correctness — only performance. A packet may omit any optional section.

| Type ID | Name | Description |
|---------|------|-------------|
| `0x0101` | `TESTS` | Embedded test cases as input/output JSON pairs |
| `0x0102` | `SCRATCHPAD_TYPES` | Type of each scratchpad slot |
| `0x0103` | `SCRATCHPAD_LIVENESS` | First-write and last-read instruction index per slot |
| `0x0104` | `SCRATCHPAD_ZONES` | Partition map: input, output, constant, working regions |
| `0x0105` | `DEPENDENCY_GRAPH` | Per-instruction dependency list |
| `0x0106` | `BRANCH_HINTS` | Probability and hot-path annotations for jump instructions |
| `0x0107` | `INPUT_ACCESS_ORDER` | Sequence in which input fields are first referenced |
| `0x0108` | `RECORD_TEMPLATES` | Pre-defined record shapes for fast output construction |
| `0x0109` | `CONSTANT_SLOTS` | Scratchpad slots that hold compile-time deterministic values |
| `0x010A` | `LIVE_FIELDS` | Set of input fields actually referenced by bytecode |

---

## 3. Required Sections

### 3.1 CONTRACT Section

The contract section encodes the input schema, output schema, tag definitions, sanitizer declarations, and validation rules. This section allows the VM to validate input before execution and validate output after execution, without examining bytecode.

#### 3.1.1 Contract Header

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 2 | `name_length` | Length of contract name in bytes |
| 2 | var | `name` | Contract name as UTF-8 string |
| var | 2 | `scratchpad_size` | Total number of scratchpad slots required |
| var | 2 | `input_field_count` | Number of input fields |
| var | 2 | `output_field_count` | Number of output fields |
| var | 2 | `tag_count` | Number of tag definitions |
| var | 2 | `sanitizer_count` | Number of sanitizer declarations |
| var | 2 | `rule_count` | Number of validation rules |

#### 3.1.2 Tag Definitions

For each tag (`tag_count` entries):

| Size | Field | Description |
|------|-------|-------------|
| 1 | `tag_bit_index` | Bit position in the tag bitfield (0–63) |
| 2 | `name_length` | Length of tag name |
| var | `name` | Tag name as UTF-8 |
| 2 | `description_length` | Length of description (0 if none) |
| var | `description` | Tag description as UTF-8 |

Tags are represented as bits in a 64-bit bitfield. Each scratchpad slot carries a `uint64` tag bitfield alongside its value. Tag propagation is performed by bitwise OR of operand tag bits onto the result.

#### 3.1.3 Field Declarations

For each input and output field:

| Size | Field | Description |
|------|-------|-------------|
| 2 | `name_length` | Length of field name |
| var | `name` | Field name as UTF-8 |
| 1 | `type_id` | Type identifier (see type table below) |
| var | `type_detail` | Additional type information for compound types |
| 8 | `tag_bits` | Tag bitfield — which tags this field carries |
| 2 | `scratchpad_offset` | Assigned scratchpad slot |

**Type identifiers:**

| ID | Type | Detail |
|----|------|--------|
| `0x01` | `string` | None |
| `0x02` | `integer` | None |
| `0x03` | `float` | None |
| `0x04` | `boolean` | None |
| `0x05` | `nothing` | None |
| `0x06` | `maybe` | 1 byte: inner type ID (+ detail if compound) |
| `0x07` | `list` | 1 byte: element type ID (+ detail if compound) |
| `0x08` | `record` | 2 bytes: field count, then field declarations |

#### 3.1.4 Sanitizer Declarations

For each sanitizer:

| Size | Field | Description |
|------|-------|-------------|
| 2 | `name_length` | Length of sanitizer name |
| var | `name` | Sanitizer name as UTF-8 |
| 2 | `stdlib_func_id` | Stdlib function ID that implements this sanitizer |
| 8 | `strips_tags` | Tag bitfield — which tags this sanitizer strips |

#### 3.1.5 Validation Rules

Rules are encoded as typed entries. Each rule begins with a 1-byte rule type:

| Rule Type | ID | Encoding |
|-----------|----|----------|
| Forbid field | `0x01` | `field_ref` + scope (`input`/`output`) |
| Forbid tagged | `0x02` | `tag_bits` + scope |
| Require | `0x03` | `field_ref` + encoded boolean expression + reject message |
| Match | `0x04` | `field_ref` + encoded match clauses + reject message |

Rule expressions use a compact bytecode-like encoding of boolean expressions, sharing the same comparison and logical operators as the main instruction set.

### 3.2 CONSTANTS Section

The constant pool stores all literal values referenced by `LOAD_CONST` instructions. Each entry is prefixed with a type tag and length.

| Size | Field | Description |
|------|-------|-------------|
| 1 | `type_id` | Type of this constant (same type IDs as field declarations) |
| 4 | `data_length` | Length of the serialized value |
| var | `data` | Serialized value |

Constant pool indices are zero-based. A `LOAD_CONST` instruction references constants by their index in this section.

**Serialization formats:**
- **string**: raw UTF-8 bytes
- **integer**: 8-byte signed little-endian
- **float**: 8-byte IEEE 754 double, little-endian
- **boolean**: 1 byte (`0x00` = false, `0x01` = true)
- **nothing**: 0 bytes (type tag alone is sufficient)

### 3.3 STDLIB_DEPS Section

A list of stdlib function IDs used by this packet. The VM must check that it supports all listed functions before beginning execution.

| Size | Field | Description |
|------|-------|-------------|
| 2 | `count` | Number of stdlib functions used |
| 2 each | `func_ids` | Array of stdlib function IDs |

If the VM does not support any listed function ID, it must reject the packet with an error identifying the unsupported function. This enables fast failure with a clear diagnostic rather than a runtime crash mid-execution.

### 3.4 BYTECODE Section

The instruction stream. Every instruction is exactly **8 bytes** with no exceptions.

| Byte | Field | Description |
|------|-------|-------------|
| 0 | `opcode` | Operation code (8 bits) |
| 1 | `flags` | Instruction flags (8 bits) |
| 2–3 | `dest` | Destination scratchpad offset (16 bits, little-endian) |
| 4–5 | `operand1` | First operand (16 bits, little-endian) |
| 6–7 | `operand2` | Second operand (16 bits, little-endian) |

**Instruction alignment:** Instruction N begins at byte offset `N × 8` within the bytecode section. Jump targets are instruction indices, not byte offsets.

**Flags byte layout:**

| Bits | Field | Description |
|------|-------|-------------|
| 0–2 | `type_hint` | Expected result type (see type hint table) |
| 3–4 | `tag_mode` | Tag propagation mode |
| 5–7 | `reserved` | Must be zero; VMs must ignore these bits |

**Type hint values:**

| Value | Type |
|-------|------|
| `0` | unspecified |
| `1` | string |
| `2` | integer |
| `3` | float |
| `4` | boolean |
| `5` | list |
| `6` | record |
| `7` | nothing |

Type hints are advisory. The compiler emits them to assist VM optimization. A VM may use them to select typed code paths, pre-allocate typed storage, or ignore them entirely. A conforming VM must produce correct results regardless of type hint values.

**Tag mode values:**

| Value | Mode | Behavior |
|-------|------|----------|
| `0` | `PROPAGATE` | Result tag bits = bitwise OR of all operand tag bits |
| `1` | `CLEAR` | Result tag bits = `0x0000000000000000` |
| `2` | `SET` | Result tag bits are set explicitly (used with sanitizers) |
| `3` | reserved | |

`PROPAGATE` is the default and handles the common case. `CLEAR` is used for sanitizer outputs where the contract has declared that specific tags are stripped. `SET` allows the compiler to assign specific tag bits when needed.

---

## 4. Opcodes

Every instruction is exactly 8 bytes. If an operation needs more information than fits in a single instruction, the compiler emits a multi-instruction sequence. There are no variable-width instructions and no exceptions to this rule.

### 4.1 Opcode Naming Convention

Opcodes are named by category and operation: `CATEGORY_OPERATION`. Operand semantics are consistent: `dest` is always where the result goes, `operand1` and `operand2` are inputs. For instructions that don't use all three operands, unused fields must be zero.

### 4.2 Opcode Design Principle

An operation is an opcode if and only if a competent developer can implement it in a couple of lines of code in C, Go, Java, and Python without importing any libraries. If the operation requires an algorithm, a library, Unicode awareness, or any meaningful implementation complexity, it belongs in the standard library and is invoked via `STDLIB_CALL`.

### 4.3 Scratchpad & Constants

| Opcode | ID | dest | operand1 | operand2 | Description |
|--------|----|------|----------|----------|-------------|
| `LOAD_CONST` | `0x01` | slot | const_idx | — | Load constant pool entry `const_idx` into `slot` |
| `LOAD_FIELD` | `0x02` | slot | field_idx | — | Load input field `field_idx` into `slot` |
| `LOAD_NOTHING` | `0x03` | slot | — | — | Load `nothing` into `slot` |
| `COPY` | `0x04` | dest | source | — | Copy value from `source` to `dest` |

`LOAD_FIELD` makes input field values available to the instruction stream. The VM may materialize the value eagerly (parsing the JSON field immediately) or lazily (deferring until the slot is read by another instruction). Both strategies are conforming.

### 4.4 Arithmetic

| Opcode | ID | dest | operand1 | operand2 | Description |
|--------|----|------|----------|----------|-------------|
| `ADD` | `0x10` | slot | left | right | `slot = left + right` |
| `SUB` | `0x11` | slot | left | right | `slot = left - right` |
| `MUL` | `0x12` | slot | left | right | `slot = left * right` |
| `DIV` | `0x13` | slot | left | right | `slot = left / right` |
| `MOD` | `0x14` | slot | left | right | `slot = left % right` |
| `NEGATE` | `0x15` | slot | source | — | `slot = -source` |

Arithmetic operations work on integer and float values. When one operand is integer and the other is float, the integer is promoted to float and the result is float. Integer division truncates toward zero. Division by zero is a runtime error.

Tag propagation: result tag bits are the bitwise OR of both operand tag bits.

### 4.5 Comparison

| Opcode | ID | dest | operand1 | operand2 | Description |
|--------|----|------|----------|----------|-------------|
| `EQ` | `0x20` | slot | left | right | `slot = (left == right)` |
| `NEQ` | `0x21` | slot | left | right | `slot = (left != right)` |
| `LT` | `0x22` | slot | left | right | `slot = (left < right)` |
| `GT` | `0x23` | slot | left | right | `slot = (left > right)` |
| `LTE` | `0x24` | slot | left | right | `slot = (left <= right)` |
| `GTE` | `0x25` | slot | left | right | `slot = (left >= right)` |

Comparison operations produce boolean values. Numeric comparisons follow standard ordering. String comparisons use lexicographic Unicode codepoint ordering. Comparing values of incompatible types is a runtime error, except that `EQ` and `NEQ` return `false` and `true` respectively for mismatched types.

Tag propagation: result tag bits are the bitwise OR of both operand tag bits.

### 4.6 Boolean Logic

| Opcode | ID | dest | operand1 | operand2 | Description |
|--------|----|------|----------|----------|-------------|
| `AND` | `0x30` | slot | left | right | `slot = left and right` |
| `OR` | `0x31` | slot | left | right | `slot = left or right` |
| `NOT` | `0x32` | slot | source | — | `slot = not source` |

Operands must be boolean values. Non-boolean operands are a runtime error.

Tag propagation: result tag bits are the bitwise OR of all operand tag bits.

### 4.7 String

| Opcode | ID | dest | operand1 | operand2 | Description |
|--------|----|------|----------|----------|-------------|
| `STR_CONCAT` | `0x40` | slot | left | right | `slot = left + right` (string concatenation) |

String concatenation is the only string opcode. All other string operations (uppercase, lowercase, trim, substring, replace, split, etc.) require Unicode-aware implementations and belong in the standard library.

Tag propagation: result tag bits are the bitwise OR of both operand tag bits.

### 4.8 Type Testing

| Opcode | ID | dest | operand1 | operand2 | Description |
|--------|----|------|----------|----------|-------------|
| `IS_STRING` | `0x50` | slot | source | — | `slot = (source is string)` |
| `IS_INT` | `0x51` | slot | source | — | `slot = (source is integer)` |
| `IS_FLOAT` | `0x52` | slot | source | — | `slot = (source is float)` |
| `IS_BOOL` | `0x53` | slot | source | — | `slot = (source is boolean)` |
| `IS_NOTHING` | `0x54` | slot | source | — | `slot = (source is nothing)` |
| `IS_LIST` | `0x55` | slot | source | — | `slot = (source is list)` |
| `IS_RECORD` | `0x56` | slot | source | — | `slot = (source is record)` |

Type testing produces boolean values. These are primarily used in pattern matching compilation.

Tag propagation: result tag bits are copied from the source operand.

### 4.9 Type Conversion

| Opcode | ID | dest | operand1 | operand2 | Description |
|--------|----|------|----------|----------|-------------|
| `TO_STRING` | `0x58` | slot | source | — | Convert value to string representation |
| `TO_INT` | `0x59` | slot | source | — | Convert value to integer |
| `TO_FLOAT` | `0x5A` | slot | source | — | Convert value to float |
| `TO_BOOL` | `0x5B` | slot | source | — | Convert value to boolean |

Conversion from float to integer truncates toward zero. Conversion from string to numeric types follows JSON number parsing rules. Boolean conversion follows: `0`, `0.0`, `""`, `nothing` → `false`; all other values → `true`. Conversion failure is a runtime error.

Tag propagation: result tag bits are copied from the source operand.

### 4.10 Record Operations

| Opcode | ID | dest | operand1 | operand2 | Description |
|--------|----|------|----------|----------|-------------|
| `RECORD_NEW` | `0x60` | slot | — | — | Create empty record in `slot` |
| `RECORD_SET` | `0x61` | record | key | value | Set field `key` to `value` in `record` |
| `RECORD_GET` | `0x62` | slot | record | key | Get field `key` from `record` into `slot` |
| `RECORD_HAS` | `0x63` | slot | record | key | `slot = (record has field key)` |

For `RECORD_SET` and `RECORD_GET`, the `key` operand is a scratchpad offset containing a string value (the field name), typically loaded from the constant pool.

`RECORD_SET` modifies the record in place on the scratchpad. Since Heluna functions are pure, each record value is used linearly — the compiler guarantees no aliasing issues.

Tag propagation for `RECORD_SET`: the record's tag bits are OR'd with the value's tag bits. For `RECORD_GET`: the result tag bits are the OR of the record's tag bits and the individual field's tag bits.

### 4.11 List Operations

| Opcode | ID | dest | operand1 | operand2 | Description |
|--------|----|------|----------|----------|-------------|
| `LIST_NEW` | `0x70` | slot | — | — | Create empty list in `slot` |
| `LIST_APPEND` | `0x71` | list | value | — | Append `value` to `list` |
| `LIST_GET` | `0x72` | slot | list | index | Get element at `index` from `list` |
| `LIST_LENGTH` | `0x73` | slot | list | — | `slot = number of elements in list` |

`LIST_APPEND` modifies the list in place on the scratchpad, similar to `RECORD_SET`.

Tag propagation for `LIST_APPEND`: the list's tag bits are OR'd with the value's tag bits. For `LIST_GET`: the result tag bits are the OR of the list's tag bits and the individual element's tag bits.

### 4.12 Control Flow

| Opcode | ID | dest | operand1 | operand2 | Description |
|--------|----|------|----------|----------|-------------|
| `JUMP` | `0x80` | target | — | — | Unconditional jump to instruction `target` |
| `JUMP_IF` | `0x81` | target | cond | — | Jump to `target` if `cond` is true |
| `JUMP_IF_NOT` | `0x82` | target | cond | — | Jump to `target` if `cond` is false |

Jump targets are instruction indices (not byte offsets). The `dest` field is repurposed as the jump target. `cond` must be a boolean value.

Note: the `dest` field in jump instructions specifies the target instruction index, not a scratchpad destination. This is the one case where `dest` does not refer to a scratchpad offset.

### 4.13 Nothing Handling

| Opcode | ID | dest | operand1 | operand2 | Description |
|--------|----|------|----------|----------|-------------|
| `COALESCE` | `0x85` | slot | maybe | default | If `maybe` is not nothing, `slot = maybe`; else `slot = default` |

This is the runtime equivalent of Heluna's maybe/nothing handling. Pattern matching on maybe types often compiles to `IS_NOTHING` + `JUMP_IF` sequences, but `COALESCE` handles the common case of "use this value or fall back to a default" in a single instruction.

Tag propagation: result tag bits are those of whichever operand is selected.

### 4.14 Iteration

Iteration instructions bracket a block of body instructions that execute for each element of a list.

| Opcode | ID | dest | operand1 | operand2 | Description |
|--------|----|------|----------|----------|-------------|
| `ITER_SETUP` | `0x90` | element | source | body_length | Begin iteration over `source` list, placing each element at `element` slot |
| `ITER_COLLECT` | `0x91` | result | slot_a | slot_b | End iteration, collect results into `result` |

**`ITER_SETUP` flags byte:**

| Bits | Field | Values |
|------|-------|--------|
| 0–1 | `mode` | `0` = MAP, `1` = FILTER, `2` = FOLD, `3` = MAP_FILTER |
| 2 | `parallel` | `0` = INDEPENDENT, `1` = SEQUENTIAL |
| 3–4 | `complexity` | `0` = TRIVIAL (1–3 instructions), `1` = LIGHT (4–10), `2` = HEAVY (10+) |
| 5–7 | reserved | Must be zero |

**Semantics by mode:**

- **MAP**: Execute the body for each element. `ITER_COLLECT` gathers the value at `slot_a` from each iteration into a new list at `result`. `slot_b` is unused.
- **FILTER**: Execute the body for each element. `ITER_COLLECT` checks the boolean at `slot_a` (predicate) and, if true, includes the value at `slot_b` in the result list at `result`.
- **FOLD**: Execute the body for each element sequentially. `slot_a` is the accumulator slot, which must be initialized before `ITER_SETUP`. After all iterations, `ITER_COLLECT` copies the final accumulator value to `result`. `slot_b` is unused.
- **MAP_FILTER**: Execute the body for each element. `ITER_COLLECT` checks the boolean at `slot_a` (predicate) and, if true, includes the value at `slot_b` in the result list at `result`. This combines map and filter in a single pass, avoiding intermediate list construction.

**Parallelism hint:**

- **INDEPENDENT** (`0`): Body iterations have no dependencies on each other. The VM may execute them in any order, concurrently, or in parallel. This is always the case for MAP, FILTER, and MAP_FILTER modes.
- **SEQUENTIAL** (`1`): Body iterations depend on previous iterations (through the accumulator). The VM must execute them in order. This is always the case for FOLD mode.

The parallelism hint is technically derivable from the mode, but is provided explicitly so VM implementers can check a single bit rather than maintaining a mode-to-parallelism table.

**Complexity hint:** Indicates how many instructions the iteration body contains. A VM may use this to choose between parallelism strategies — vectorization for trivial bodies, thread pools for heavy bodies, sequential execution for everything. This is purely advisory.

**Body scope:** The instructions between `ITER_SETUP` and `ITER_COLLECT` form the iteration body. The compiler guarantees that the body is a self-contained block — no jumps into or out of the body from external instructions.

### 4.15 Standard Library Dispatch

| Opcode | ID | dest | operand1 | operand2 | Description |
|--------|----|------|----------|----------|-------------|
| `STDLIB_CALL` | `0xA0` | slot | func_id | args | Call stdlib function `func_id` with args record at `args`, result to `slot` |

`func_id` is a stdlib function ID (see §6). `args` is a scratchpad offset containing a record value with the function's expected arguments.

The calling convention matches Heluna's function call syntax: every stdlib function takes a single record as input and produces a single value as output. This uniformity means the VM's stdlib dispatch is a simple switch on `func_id`.

Tag propagation depends on the `tag_mode` flag in the instruction:
- For normal stdlib calls: `PROPAGATE` — result tags are the OR of all argument tags.
- For sanitizer calls: `CLEAR` or `SET` — the compiler sets the appropriate mode based on the contract's sanitizer declarations.

### 4.16 Tag Operations

| Opcode | ID | dest | operand1 | operand2 | Description |
|--------|----|------|----------|----------|-------------|
| `TAG_SET` | `0xB0` | slot | tags_const | — | Set tag bits on `slot` to the value in constant `tags_const` |
| `TAG_CHECK` | `0xB1` | slot | source | tags_const | `slot = true` if `source` carries all bits in `tags_const` |

These are used in edge cases where the compiler needs explicit tag control beyond the automatic propagation handled by the `tag_mode` flag. Most instructions never need these — the flags byte handles the common case.

### 4.17 Superinstructions

Superinstructions fuse common multi-instruction sequences into single opcodes,
eliminating dispatch overhead. All superinstructions are semantically equivalent
to their expanded sequences — a conforming VM may execute the expanded form instead.

#### RECORD_GET_C (0xC1)
| Byte | Field | Value |
|------|-------|-------|
| 0 | opcode | 0xC1 |
| 1 | flags | type_hint ∣ tag_mode |
| 2–3 | dest | scratchpad slot for result |
| 4–5 | operand1 | scratchpad slot of record |
| 6–7 | operand2 | constant pool index of key string |

`values[dest] = values[operand1].get(constants[operand2])`

Equivalent to: `LOAD_CONST tmp, operand2; RECORD_GET dest, operand1, tmp`
Tag behavior: propagate from record slot.

#### RECORD_SET_C (0xC2)
| Byte | Field | Value |
|------|-------|-------|
| 0 | opcode | 0xC2 |
| 1 | flags | type_hint ∣ tag_mode |
| 2–3 | dest | scratchpad slot of record (modified in place) |
| 4–5 | operand1 | constant pool index of key string |
| 6–7 | operand2 | scratchpad slot of value |

`values[dest].set(constants[operand1], values[operand2])`

Equivalent to: `LOAD_CONST tmp, operand1; RECORD_SET dest, tmp, operand2`
Tag behavior: dest tags |= operand2 tags.

#### RECORD_NEW_SET_C (0xC3)
| Byte | Field | Value |
|------|-------|-------|
| 0 | opcode | 0xC3 |
| 1 | flags | type_hint ∣ tag_mode |
| 2–3 | dest | scratchpad slot for new record |
| 4–5 | operand1 | constant pool index of key string |
| 6–7 | operand2 | scratchpad slot of value |

`values[dest] = new_record(); values[dest].set(constants[operand1], values[operand2])`

Equivalent to: `RECORD_NEW dest; LOAD_CONST tmp, operand1; RECORD_SET dest, tmp, operand2`
Tag behavior: propagate from value slot.

#### STDLIB_CALL_1 (0xC4)
| Byte | Field | Value |
|------|-------|-------|
| 0 | opcode | 0xC4 |
| 1 | flags | type_hint ∣ tag_mode |
| 2–3 | dest | scratchpad slot for result |
| 4–5 | operand1 | stdlib function ID |
| 6–7 | operand2 | scratchpad slot of value argument |

`values[dest] = stdlib_call(operand1, {value: values[operand2]})`

Equivalent to: `RECORD_NEW tmp; LOAD_CONST k, "value"; RECORD_SET tmp, k, operand2; STDLIB_CALL dest, operand1, tmp`
Tag behavior: determined by flags tag_mode (typically PROPAGATE from value slot, or CLEAR for sanitizers).

#### CMP_JUMP_EQ (0xC5)
| Byte | Field | Value |
|------|-------|-------|
| 0 | opcode | 0xC5 |
| 1 | flags | type_hint ∣ tag_mode |
| 2–3 | dest | jump target (instruction index) |
| 4–5 | operand1 | scratchpad slot of left operand |
| 6–7 | operand2 | scratchpad slot of right operand |

Jump to `dest` if `values[operand1] != values[operand2]` (i.e., when EQ is **false**).

Equivalent to: `EQ tmp, operand1, operand2; JUMP_IF_NOT dest, tmp`
Tag behavior: no value written, no tag changes.

#### CMP_JUMP_NEQ (0xC6)
Same encoding as CMP_JUMP_EQ. Jump to `dest` if `values[operand1] == values[operand2]` (i.e., when NEQ is **false**).

Equivalent to: `NEQ tmp, operand1, operand2; JUMP_IF_NOT dest, tmp`

#### CMP_JUMP_LT (0xC7)
Same encoding. Jump to `dest` if `values[operand1] >= values[operand2]` (i.e., when LT is **false**).

Equivalent to: `LT tmp, operand1, operand2; JUMP_IF_NOT dest, tmp`

#### CMP_JUMP_GT (0xC8)
Same encoding. Jump to `dest` if `values[operand1] <= values[operand2]` (i.e., when GT is **false**).

Equivalent to: `GT tmp, operand1, operand2; JUMP_IF_NOT dest, tmp`

#### CMP_JUMP_LTE (0xC9)
Same encoding. Jump to `dest` if `values[operand1] > values[operand2]` (i.e., when LTE is **false**).

Equivalent to: `LTE tmp, operand1, operand2; JUMP_IF_NOT dest, tmp`

#### CMP_JUMP_GTE (0xCA)
Same encoding. Jump to `dest` if `values[operand1] < values[operand2]` (i.e., when GTE is **false**).

Equivalent to: `GTE tmp, operand1, operand2; JUMP_IF_NOT dest, tmp`

#### IS_NOTHING_JUMP (0xCB)
| Byte | Field | Value |
|------|-------|-------|
| 0 | opcode | 0xCB |
| 1 | flags | unused |
| 2–3 | dest | jump target (instruction index) |
| 4–5 | operand1 | scratchpad slot to test |
| 6–7 | operand2 | unused |

Jump to `dest` if `values[operand1]` is nothing.

Equivalent to: `IS_NOTHING tmp, operand1; JUMP_IF dest, tmp`
Tag behavior: no value written, no tag changes.

### 4.18 Opcode Table Summary

| Range | Category | Count |
|-------|----------|-------|
| `0x01–0x04` | Scratchpad & constants | 4 |
| `0x10–0x15` | Arithmetic | 6 |
| `0x20–0x25` | Comparison | 6 |
| `0x30–0x32` | Boolean logic | 3 |
| `0x40` | String | 1 |
| `0x50–0x56` | Type testing | 7 |
| `0x58–0x5B` | Type conversion | 4 |
| `0x60–0x63` | Record operations | 4 |
| `0x70–0x73` | List operations | 4 |
| `0x80–0x82` | Control flow | 3 |
| `0x85` | Nothing handling | 1 |
| `0x90–0x91` | Iteration | 2 |
| `0xA0` | Standard library dispatch | 1 |
| `0xB0–0xB1` | Tag operations | 2 |
| `0xC1–0xCB` | Superinstructions | 11 |
| | **Total** | **59** |

Opcode IDs `0xCC–0xFF` are reserved for future use. A VM encountering an unknown opcode must halt execution with an error.

---

## 5. Scratchpad

The scratchpad is a flat array of **slots**, where each slot holds a JSON value and its associated tag bitfield. The number of slots is declared in the contract section's `scratchpad_size` field.

### 5.1 Slot Structure

Each scratchpad slot logically contains:

- A **value** — one of: string, integer, float, boolean, nothing, list, or record
- A **tag bitfield** — a 64-bit unsigned integer where each bit corresponds to a tag defined in the contract

The physical representation of slots is entirely up to the VM implementation. A simple VM might use a tagged union with a `uint64` alongside it. A fast VM might use typed arrays informed by the optional `SCRATCHPAD_TYPES` section. A lazy VM might represent input slots as deferred references into the raw JSON input buffer.

### 5.2 Slot Addressing

All instructions reference slots by their zero-based offset in the scratchpad. The compiler assigns offsets at compile time. The scratchpad layout follows a logical partitioning:

- **Input region**: slots assigned to input fields, populated from the input JSON
- **Constant region**: slots that hold values loaded from the constant pool
- **Working region**: slots for intermediate computation results
- **Output region**: slots that form the output record

This partitioning is logical, not enforced by the VM. The optional `SCRATCHPAD_ZONES` section makes the boundaries explicit for VMs that can exploit them.

### 5.3 Tag Propagation

The default tag propagation rule is: when an instruction combines two or more values, the result's tag bits are the bitwise OR of all operand tag bits. This ensures that tags are sticky — sensitive data cannot be laundered through computation.

The `tag_mode` field in the instruction flags byte allows the compiler to override this default:

- **PROPAGATE** (default): `result_tags = operand1_tags | operand2_tags`
- **CLEAR**: `result_tags = 0` — used for sanitizer outputs
- **SET**: `result_tags` are set from an explicit value — used for specific tag assignment

The contract's `forbid` rules are checked against output slot tag bits after execution completes. If any output slot carries a forbidden tag, execution fails.

---

## 6. Standard Library

The standard library is a versioned set of functions that every conforming VM must support (for the spec version it claims). Stdlib functions are invoked via the `STDLIB_CALL` opcode with a function ID.

Every stdlib function takes a single record argument and produces a single value. Stdlib functions are pure — they have no side effects and produce deterministic results for the same inputs.

### 6.1 Versioning

The packet header's `min_spec_version` declares the minimum spec version required. Each spec version defines a set of stdlib function IDs. A VM supporting spec version N must support all stdlib functions defined in versions 1 through N.

New spec versions may add new function IDs but must not change the semantics of existing ones. This guarantees forward compatibility — a packet compiled against spec 1.0 runs correctly on a VM supporting spec 2.0.

### 6.2 Function Table

The following table defines the initial stdlib function set (spec version 1.0).

#### String Functions

| ID | Name | Input Record | Result | Description |
|----|------|-------------|--------|-------------|
| `0x0001` | `upper` | `{ value: string }` | string | Convert to uppercase |
| `0x0002` | `lower` | `{ value: string }` | string | Convert to lowercase |
| `0x0003` | `trim` | `{ value: string }` | string | Remove leading/trailing whitespace |
| `0x0004` | `trim-start` | `{ value: string }` | string | Remove leading whitespace |
| `0x0005` | `trim-end` | `{ value: string }` | string | Remove trailing whitespace |
| `0x0006` | `substring` | `{ value: string, start: integer, end: integer }` | string | Extract substring by codepoint index |
| `0x0007` | `replace` | `{ value: string, find: string, replacement: string }` | string | Replace all occurrences |
| `0x0008` | `split` | `{ value: string, delimiter: string }` | list of string | Split string by delimiter |
| `0x0009` | `join` | `{ list: list of string, delimiter: string }` | string | Join strings with delimiter |
| `0x000A` | `starts-with` | `{ value: string, prefix: string }` | boolean | Test if string starts with prefix |
| `0x000B` | `ends-with` | `{ value: string, suffix: string }` | boolean | Test if string ends with suffix |
| `0x000C` | `contains` | `{ value: string, search: string }` | boolean | Test if string contains substring |
| `0x000D` | `length` | `{ value: string }` | integer | Length in Unicode codepoints |
| `0x000E` | `pad-left` | `{ value: string, width: integer, fill: string }` | string | Pad to width with fill character on left |
| `0x000F` | `pad-right` | `{ value: string, width: integer, fill: string }` | string | Pad to width with fill character on right |
| `0x0010` | `regex-match` | `{ value: string, pattern: string }` | maybe record | Match regex, return captured groups or nothing |
| `0x0011` | `regex-replace` | `{ value: string, pattern: string, replacement: string }` | string | Replace regex matches |

#### Numeric Functions

| ID | Name | Input Record | Result | Description |
|----|------|-------------|--------|-------------|
| `0x0020` | `abs` | `{ value: number }` | number | Absolute value |
| `0x0021` | `ceil` | `{ value: float }` | integer | Round up to nearest integer |
| `0x0022` | `floor` | `{ value: float }` | integer | Round down to nearest integer |
| `0x0023` | `round` | `{ value: float }` | integer | Round to nearest integer (half up) |
| `0x0024` | `min` | `{ a: number, b: number }` | number | Smaller of two values |
| `0x0025` | `max` | `{ a: number, b: number }` | number | Larger of two values |
| `0x0026` | `clamp` | `{ value: number, low: number, high: number }` | number | Clamp value to range |

#### List Functions

| ID | Name | Input Record | Result | Description |
|----|------|-------------|--------|-------------|
| `0x0030` | `sort` | `{ list: list }` | list | Sort in ascending order |
| `0x0031` | `sort-by` | `{ list: list of record, field: string }` | list of record | Sort records by field value |
| `0x0032` | `reverse` | `{ list: list }` | list | Reverse element order |
| `0x0033` | `unique` | `{ list: list }` | list | Remove duplicate values |
| `0x0034` | `flatten` | `{ list: list of list }` | list | Flatten one level of nesting |
| `0x0035` | `zip` | `{ a: list, b: list }` | list of record | Pair elements from two lists |
| `0x0036` | `range` | `{ start: integer, end: integer }` | list of integer | Generate integer range (inclusive) |
| `0x0037` | `slice` | `{ list: list, start: integer, end: integer }` | list | Extract sublist by index |

#### Record Functions

| ID | Name | Input Record | Result | Description |
|----|------|-------------|--------|-------------|
| `0x0040` | `keys` | `{ record: record }` | list of string | Get all field names |
| `0x0041` | `values` | `{ record: record }` | list | Get all field values |
| `0x0042` | `merge` | `{ a: record, b: record }` | record | Merge two records (b overwrites a) |
| `0x0043` | `pick` | `{ record: record, fields: list of string }` | record | Keep only named fields |
| `0x0044` | `omit` | `{ record: record, fields: list of string }` | record | Remove named fields |

#### Date/Time Functions

| ID | Name | Input Record | Result | Description |
|----|------|-------------|--------|-------------|
| `0x0050` | `parse-date` | `{ value: string, format: string }` | record | Parse date string into components |
| `0x0051` | `format-date` | `{ date: record, format: string }` | string | Format date components into string |
| `0x0052` | `date-diff` | `{ a: string, b: string, unit: string }` | integer | Difference between two dates in given unit |
| `0x0053` | `date-add` | `{ date: string, amount: integer, unit: string }` | string | Add duration to date |
| `0x0054` | `now-date` | `{}` | string | Execution timestamp as ISO 8601 string |

Note: `now-date` returns the logical timestamp provided by the host process at execution start. Multiple calls within the same execution always return the same value. This ensures temporal consistency across parallel or speculative execution.

#### Encoding Functions

| ID | Name | Input Record | Result | Description |
|----|------|-------------|--------|-------------|
| `0x0060` | `base64-encode` | `{ value: string }` | string | Encode as Base64 |
| `0x0061` | `base64-decode` | `{ value: string }` | string | Decode from Base64 |
| `0x0062` | `url-encode` | `{ value: string }` | string | Percent-encode for URLs |
| `0x0063` | `url-decode` | `{ value: string }` | string | Decode percent-encoded string |
| `0x0064` | `json-encode` | `{ value: any }` | string | Serialize value to JSON string |
| `0x0065` | `json-parse` | `{ value: string }` | any | Parse JSON string to value |

#### Cryptographic Functions

| ID | Name | Input Record | Result | Description |
|----|------|-------------|--------|-------------|
| `0x0070` | `sha256` | `{ value: string }` | string | SHA-256 hash as hex string |
| `0x0071` | `hmac-sha256` | `{ value: string, key: string }` | string | HMAC-SHA256 as hex string |
| `0x0072` | `uuid` | `{}` | string | Generate UUID v4 |

Note: `uuid` generates a fresh UUID on each call. This is acceptable because UUIDs are used for identification, not computation, and Heluna's purity guarantees prevent the non-determinism from affecting the transformation logic.

---

## 7. Optional Sections

Optional sections contain compiler analysis that a VM may use for optimization. A VM must produce correct results whether or not these sections are present. A packet may include any combination of optional sections.

### 7.1 TESTS

Embedded test cases from the contract, serialized as input/output JSON pairs.

| Size | Field | Description |
|------|-------|-------------|
| 2 | `test_count` | Number of test cases |

For each test:

| Size | Field | Description |
|------|-------|-------------|
| 2 | `name_length` | Length of test name |
| var | `name` | Test name as UTF-8 |
| 4 | `input_length` | Length of input JSON |
| var | `input_json` | Input as serialized JSON |
| 4 | `output_length` | Length of expected output JSON |
| var | `output_json` | Expected output as serialized JSON |

VMs can use these for self-verification and compliance testing.

### 7.2 SCRATCHPAD_TYPES

Type of each scratchpad slot, as determined by the compiler.

| Size | Field | Description |
|------|-------|-------------|
| 2 | `slot_count` | Number of entries (equal to `scratchpad_size`) |
| 1 each | `types` | Array of type IDs, one per slot |

A VM may use this to allocate typed backing stores — integer slots in a flat int array, string slots in a string pool, etc. — for cache-friendly layout and to avoid boxing overhead.

### 7.3 SCRATCHPAD_LIVENESS

First-write and last-read instruction index for each scratchpad slot.

| Size | Field | Description |
|------|-------|-------------|
| 2 | `slot_count` | Number of entries |

For each slot:

| Size | Field | Description |
|------|-------|-------------|
| 2 | `first_write` | Instruction index of first write |
| 2 | `last_read` | Instruction index of last read |

A memory-conscious VM can reuse physical memory for slots with non-overlapping lifetimes, reducing the actual memory footprint below the declared scratchpad size.

### 7.4 SCRATCHPAD_ZONES

Partition map dividing the scratchpad into functional regions.

| Size | Field | Description |
|------|-------|-------------|
| 2 | `zone_count` | Number of zones |

For each zone:

| Size | Field | Description |
|------|-------|-------------|
| 1 | `zone_type` | `0` = input, `1` = output, `2` = constant, `3` = working |
| 2 | `start_offset` | First slot in zone (inclusive) |
| 2 | `end_offset` | Last slot in zone (inclusive) |

A VM may use this to place zones in different memory regions. The input zone can back to a lazy JSON parser. The output zone can back to a serialization buffer. The constant zone is read-only after initialization.

For parallel iteration, the working zone boundaries tell the VM which slots need to be replicated per worker thread.

### 7.5 DEPENDENCY_GRAPH

Per-instruction dependency information, enabling out-of-order or parallel execution.

| Size | Field | Description |
|------|-------|-------------|
| 2 | `instruction_count` | Number of entries |

For each instruction:

| Size | Field | Description |
|------|-------|-------------|
| 1 | `dep_count` | Number of instructions this depends on |
| 2 each | `deps` | Array of instruction indices |

A simple VM ignores this entirely. A JIT or parallel VM uses it to schedule instructions optimally without performing its own dependency analysis.

### 7.6 BRANCH_HINTS

Probability and hot-path annotations for jump instructions.

| Size | Field | Description |
|------|-------|-------------|
| 2 | `hint_count` | Number of entries |

For each hinted jump:

| Size | Field | Description |
|------|-------|-------------|
| 2 | `instruction_index` | Index of the jump instruction |
| 1 | `probability` | Probability of taking the jump (0–255, where 128 = 50%) |
| 1 | `hot_path` | `0` = cold, `1` = hot |

A JIT compiler uses this for branch layout optimization — placing the likely path in the fall-through position.

### 7.7 INPUT_ACCESS_ORDER

The sequence in which input fields are first referenced by the bytecode.

| Size | Field | Description |
|------|-------|-------------|
| 2 | `field_count` | Number of entries |
| 2 each | `field_indices` | Array of input field indices in order of first access |

A lazy parsing VM can use this to prioritize which fields to materialize first, or to speculatively parse upcoming fields in a background thread while executing current instructions.

### 7.8 RECORD_TEMPLATES

Pre-defined record shapes for fast output construction.

| Size | Field | Description |
|------|-------|-------------|
| 2 | `template_count` | Number of templates |

For each template:

| Size | Field | Description |
|------|-------|-------------|
| 2 | `template_id` | Template identifier referenced by bytecode |
| 2 | `field_count` | Number of fields in the record |

For each field in the template:

| Size | Field | Description |
|------|-------|-------------|
| 2 | `name_const_idx` | Constant pool index of the field name |
| 2 | `source_slot` | Scratchpad slot that provides the field's value |
| 1 | `flags` | `0x01` = passthrough (value is unmodified from input) |

A VM can use templates to construct output records in a single pass. The `passthrough` flag indicates that the value at `source_slot` is an unmodified input field — a VM with lazy parsing can copy raw bytes from the input JSON buffer directly to the output without ever materializing the intermediate value.

### 7.9 CONSTANT_SLOTS

Scratchpad slots whose values are fully determined at compile time.

| Size | Field | Description |
|------|-------|-------------|
| 2 | `slot_count` | Number of entries |
| 2 each | `slots` | Array of scratchpad offsets |

A JIT compiler can eliminate the instructions that produce these values and inline the constants directly.

### 7.10 LIVE_FIELDS

The set of input fields actually referenced by the bytecode.

| Size | Field | Description |
|------|-------|-------------|
| 2 | `field_count` | Number of live fields |
| 2 each | `field_indices` | Array of input field indices |

A VM can skip parsing input fields not in this set. For contracts with many input fields where the function only uses a few, this can significantly reduce input processing time.

---

## 8. Execution Lifecycle

This section describes the complete lifecycle of a packet execution, from loading to output.

### 8.1 Loading and Verification

1. Read the 88-byte header.
2. Verify `magic` = `0x484C4E41`.
3. Check `format_version` is supported.
4. Locate the signing public key using `key_fingerprint`.
5. Verify the Ed25519 signature over bytes `[88, total_size)`.
6. If any check fails, reject with an appropriate error.

### 8.2 Section Parsing

1. Read the section directory.
2. Verify all four required sections are present (`CONTRACT`, `CONSTANTS`, `STDLIB_DEPS`, `BYTECODE`).
3. Read `STDLIB_DEPS` and verify the VM supports all listed function IDs.
4. Read `CONTRACT` to obtain the scratchpad size, input/output schemas, and rules.
5. Read `CONSTANTS` to build the constant pool.
6. Load the `BYTECODE` section.
7. Optionally read any optional sections the VM supports.

### 8.3 Input Validation

1. Receive input JSON and **logical timestamp** from the host process.
2. Validate the input against the contract's input schema — verify field presence, types, and tag assignments.
3. Evaluate input `require` rules. If any fail, reject with the rule's error message.
4. Map input field values to their assigned scratchpad offsets (eagerly or lazily).

### 8.4 Execution

1. Allocate the scratchpad (informed by optional sections if available).
2. Initialize input slots, load required constants, and register the logical timestamp for time-related stdlib functions.
3. Execute the instruction stream sequentially (or in dependency order, or in parallel — the VM decides).
4. On runtime error (type mismatch, division by zero, out-of-bounds access), halt with a descriptive error.

### 8.5 Output Validation

1. Construct the output record from the designated output scratchpad offsets.
2. Check output tag bits against `forbid` rules. If any output slot carries a forbidden tag, reject.
3. Evaluate output `require` rules. If any fail, reject with the rule's error message.
4. Validate output types against the contract's output schema.

### 8.6 Output Delivery

1. Serialize the output record as JSON.
2. Return to the host process.

---

## 9. Conformance

### 9.1 Conformance Levels

**Level 1 — Minimal:** The VM supports all required sections, all opcodes, and all stdlib functions for a given spec version. It executes packets correctly but makes no use of optional sections.

**Level 2 — Optimizing:** The VM supports Level 1 and additionally uses one or more optional sections to improve performance.

**Level 3 — Full:** The VM supports all optional sections defined in the spec version.

All conformance levels must produce identical outputs for identical inputs and packets. The difference is purely in performance characteristics.

### 9.2 Compliance Testing

A conformance test suite consists of packets with known inputs and expected outputs. A VM is conforming if it produces the correct output for every test case. The test suite includes:

- **Opcode tests**: Verify each opcode's behavior across type combinations and edge cases.
- **Stdlib tests**: Verify each stdlib function with comprehensive inputs, including Unicode edge cases, boundary values, and error conditions.
- **Tag tests**: Verify tag propagation, sanitizer behavior, and forbid rule enforcement.
- **Integration tests**: Full packets exercising real-world transformation patterns.

For VMs claiming Level 2 or Level 3 conformance, the test suite additionally verifies that optional section support does not alter outputs — the same packets are run with and without optional sections, and outputs must be identical.

### 9.3 Error Handling

A conforming VM must report errors in the following categories:

- **Packet errors**: Invalid magic, unsupported format version, signature verification failure, missing required section, unsupported stdlib function.
- **Input errors**: Schema validation failure, input rule rejection.
- **Runtime errors**: Type mismatch, division by zero, index out of bounds, missing record field.
- **Output errors**: Tag constraint violation (forbid rule), output rule rejection, output schema mismatch.

The specific error message format is not prescribed. The VM must clearly indicate the error category and provide sufficient detail for the host process to diagnose the issue.

---

## 10. Security Considerations

### 10.1 Signature Verification

The Ed25519 signature is the packet's primary integrity guarantee. A VM must never execute a packet whose signature fails verification. The host process is responsible for managing trusted signing keys.

### 10.2 Tag System

The tag system provides defense in depth against data leakage:

- **Tags classify data** at the input boundary.
- **Propagation tracks sensitivity** through every computation (bitwise OR on every operation).
- **Sanitizers create auditable chokepoints** where sensitive data is transformed.
- **Forbid rules enforce constraints** at the output boundary.

Because Heluna functions are pure — no I/O, no network access, no side channels — data can only leave through the output record. The tag system ensures the output record contains no improperly tagged data.

### 10.3 Resource Limits

The VM should enforce resource limits to prevent abuse:

- **Scratchpad size**: The VM may impose a maximum scratchpad size and reject packets exceeding it.
- **Instruction count**: The VM may impose a maximum instruction count per execution.
- **List size**: The VM may impose a maximum list length to prevent memory exhaustion during iteration.
- **Execution time**: The VM may impose a wall-clock timeout.

Specific limits are not prescribed by this specification. They are deployment decisions made by the host process.

### 10.4 Guaranteed Termination

Because Heluna prohibits recursion and all iteration is bounded by input list sizes, every Heluna function is guaranteed to terminate. The resource limits in §10.3 are defense-in-depth against maliciously crafted packets, not against the language's own semantics.

---

## Appendix A: Opcode Quick Reference

```
SCRATCHPAD & CONSTANTS
  0x01  LOAD_CONST      dest  const_idx  —
  0x02  LOAD_FIELD       dest  field_idx  —
  0x03  LOAD_NOTHING     dest  —          —
  0x04  COPY             dest  source     —

ARITHMETIC
  0x10  ADD              dest  left       right
  0x11  SUB              dest  left       right
  0x12  MUL              dest  left       right
  0x13  DIV              dest  left       right
  0x14  MOD              dest  left       right
  0x15  NEGATE           dest  source     —

COMPARISON
  0x20  EQ               dest  left       right
  0x21  NEQ              dest  left       right
  0x22  LT               dest  left       right
  0x23  GT               dest  left       right
  0x24  LTE              dest  left       right
  0x25  GTE              dest  left       right

BOOLEAN LOGIC
  0x30  AND              dest  left       right
  0x31  OR               dest  left       right
  0x32  NOT              dest  source     —

STRING
  0x40  STR_CONCAT       dest  left       right

TYPE TESTING
  0x50  IS_STRING        dest  source     —
  0x51  IS_INT           dest  source     —
  0x52  IS_FLOAT         dest  source     —
  0x53  IS_BOOL          dest  source     —
  0x54  IS_NOTHING       dest  source     —
  0x55  IS_LIST          dest  source     —
  0x56  IS_RECORD        dest  source     —

TYPE CONVERSION
  0x58  TO_STRING        dest  source     —
  0x59  TO_INT           dest  source     —
  0x5A  TO_FLOAT         dest  source     —
  0x5B  TO_BOOL          dest  source     —

RECORD OPERATIONS
  0x60  RECORD_NEW       dest  —          —
  0x61  RECORD_SET       record key       value
  0x62  RECORD_GET       dest  record     key
  0x63  RECORD_HAS       dest  record     key

LIST OPERATIONS
  0x70  LIST_NEW         dest  —          —
  0x71  LIST_APPEND      list  value      —
  0x72  LIST_GET         dest  list       index
  0x73  LIST_LENGTH      dest  list       —

CONTROL FLOW
  0x80  JUMP             target —         —
  0x81  JUMP_IF          target cond      —
  0x82  JUMP_IF_NOT      target cond      —

NOTHING HANDLING
  0x85  COALESCE         dest  maybe      default

ITERATION
  0x90  ITER_SETUP       element source   body_length
  0x91  ITER_COLLECT     result  slot_a   slot_b

STDLIB DISPATCH
  0xA0  STDLIB_CALL      dest  func_id   args

TAG OPERATIONS
  0xB0  TAG_SET          slot  tags_const —
  0xB1  TAG_CHECK        dest  source     tags_const

SUPERINSTRUCTIONS
  0xC1  RECORD_GET_C       dest = record[const_key]
  0xC2  RECORD_SET_C       record[const_key] = value
  0xC3  RECORD_NEW_SET_C   dest = { const_key: value }
  0xC4  STDLIB_CALL_1      dest = stdlib(func, {value: slot})
  0xC5  CMP_JUMP_EQ        jump if !(op1 == op2)
  0xC6  CMP_JUMP_NEQ       jump if !(op1 != op2)
  0xC7  CMP_JUMP_LT        jump if !(op1 < op2)
  0xC8  CMP_JUMP_GT        jump if !(op1 > op2)
  0xC9  CMP_JUMP_LTE       jump if !(op1 <= op2)
  0xCA  CMP_JUMP_GTE       jump if !(op1 >= op2)
  0xCB  IS_NOTHING_JUMP    jump if op1 is nothing
```

---

## Appendix B: Stdlib Function Quick Reference

```
STRING                          NUMERIC
  0x0001  upper                   0x0020  abs
  0x0002  lower                   0x0021  ceil
  0x0003  trim                    0x0022  floor
  0x0004  trim-start              0x0023  round
  0x0005  trim-end                0x0024  min
  0x0006  substring               0x0025  max
  0x0007  replace                 0x0026  clamp
  0x0008  split
  0x0009  join                  LIST
  0x000A  starts-with             0x0030  sort
  0x000B  ends-with               0x0031  sort-by
  0x000C  contains                0x0032  reverse
  0x000D  length                  0x0033  unique
  0x000E  pad-left                0x0034  flatten
  0x000F  pad-right               0x0035  zip
  0x0010  regex-match             0x0036  range
  0x0011  regex-replace           0x0037  slice

RECORD                          DATE/TIME
  0x0040  keys                    0x0050  parse-date
  0x0041  values                  0x0051  format-date
  0x0042  merge                   0x0052  date-diff
  0x0043  pick                    0x0053  date-add
  0x0044  omit                    0x0054  now-date

ENCODING                        CRYPTO
  0x0060  base64-encode           0x0070  sha256
  0x0061  base64-decode           0x0071  hmac-sha256
  0x0062  url-encode              0x0072  uuid
  0x0063  url-decode
  0x0064  json-encode
  0x0065  json-parse
```
