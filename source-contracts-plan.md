# Source Contracts & External Queries — Implementation Plan

## Motivation

Heluna functions are pure transformations, but real-world transformations frequently need reference data that lives outside the input record: looking up a customer by ID, resolving a product SKU to a price tier, fetching a configuration document by key. The naive solution — forcing the host to pre-load everything into the input record — couples the host tightly to the function's implementation details and bloats input schemas with data the function merely reads through. Source contracts solve this by treating external read-only data dependencies as first-class contract boundaries. A source contract declares the external collection's name, its key shape, its return schema, and the security tags on its fields — exactly the same way a function contract declares inputs, outputs, and tags. This means the contract remains the single artifact a reviewer reads to understand every trust decision: what data enters the system, what sensitivity it carries, and what rules prevent it from leaking. At runtime, the host fulfills source queries using the logical timestamp already provided at execution start, making lookups semantically deterministic — "give me this record as it existed at this instant." The function remains pure; the host acts as a deterministic data provider. And because source contracts are just contracts, they compose with the existing `uses` mechanism for tag vocabularies and dependency resolution, they inline into fat packets for zero-runtime-resolution deployment, and the tag propagation system tracks sensitivity through query results exactly as it does through input fields. No new concepts — just a new kind of boundary that the existing contract model already knows how to govern.

## Concept

Contracts are now polymorphic. A contract can be one of three kinds, determined by its body:

- **Tag contract** — contains only `tags ... end`. Defines a reusable security vocabulary.
- **Source contract** — contains `source`, `keyed-by`, and `returns`. Defines a read-only external data dependency.
- **Function contract** — contains `input`, `output`, and optionally `rules`/`tests`. What already exists.

All three use the `contract` keyword. The kind is inferred from which clauses appear. All three can `uses` other contracts to import tag definitions.

## Language Syntax Changes

### Tag contract
```heluna
contract company-security
  tags
    pii "personally identifiable information",
    restricted "must not leave processing boundary"
  end
end
```

### Source contract
```heluna
contract customers
  uses company-security

  source "customers"

  keyed-by customer-id as string

  returns record
    customer-id as string,
    name as string tagged pii,
    credit-limit as float tagged restricted
  end
end
```

### Function contract referencing sources
```heluna
contract enrich-order
  uses company-security
  sources customers

  input
    customer-id as string,
    order-total as float
  end

  output
    approved as boolean,
    tier as string
  end

  rules
    forbid tagged restricted in output
  end
end
```

### Lookup expression in function body
```heluna
define enrich-order with input
  let customer be lookup customers
    where customer-id = $customer-id
  end

  result {
    approved: match customer
      when nothing then false
      when c then $order-total <= c.credit-limit
    end,
    tier: match customer
      when nothing then "unknown"
      when c then c.tier
    end
  }
end
```

`lookup` always returns `maybe record` — the record might not exist. The programmer must handle `nothing`.

## Grammar Additions

```
Contract       ::= 'contract' Identifier
                     UsesDecl?
                     (TagContract | SourceContract | FunctionContract)
                     'end'

TagContract    ::= TagDecl

SourceContract ::= TagDecl?
                    SourceDecl
                    KeyedByDecl
                    ReturnsDecl

FunctionContract ::= TagDecl?
                      SanitizerDecl?
                      SourcesDecl?
                      InputSpec
                      OutputSpec
                      RuleList?
                      TestList?

SourceDecl     ::= 'source' String
KeyedByDecl    ::= 'keyed-by' FieldDecl (',' FieldDecl)*
ReturnsDecl    ::= 'returns' Type
SourcesDecl    ::= 'sources' Identifier (',' Identifier)*

LookupExpr     ::= 'lookup' Identifier 'where' LookupKey (',' LookupKey)* 'end'
LookupKey      ::= Identifier '=' Expression
```

Add `LookupExpr` as a variant of `Expression`.

New reserved words: `source`, `sources`, `keyed-by`, `returns`, `lookup`, `where` (already used in filter).

## AST Changes

Add new AST node types:

- `AST_CONTRACT_TAG` — contract body is just tags
- `AST_CONTRACT_SOURCE` — contract body has source/keyed-by/returns
- `AST_CONTRACT_FUNCTION` — existing contract type (input/output/rules/tests)
- `AST_SOURCE_DECL` — the `source "name"` clause
- `AST_KEYED_BY` — key field declarations
- `AST_RETURNS` — return type declaration
- `AST_SOURCES_REF` — the `sources x, y` reference list in function contracts
- `AST_LOOKUP` — lookup expression node with source name, key bindings

## Lexer Changes

New tokens: `TOKEN_SOURCE`, `TOKEN_SOURCES`, `TOKEN_KEYED_BY`, `TOKEN_RETURNS`, `TOKEN_LOOKUP`.

`where` is already a token (used in `filter`). Reuse it.

## Parser Changes

1. In `parse_contract()`: after parsing `uses`, peek at the next token to determine contract kind:
   - See `tags` and nothing else → tag contract
   - See `source` → source contract
   - See `input` or `sources` or `sanitizers` → function contract
2. Add `parse_source_contract()` — parses `source`, `keyed-by`, `returns`
3. Add `parse_sources_decl()` — parses the `sources x, y` reference list
4. Add `parse_lookup_expr()` — triggered when expression parser sees `lookup` token

## VM / Packet Changes (future, not needed for interpreter)

- New packet section `SOURCES` (`0x0005`, required if sources are used) listing source schemas, key shapes, tag bits
- New opcode `QUERY` (`0xC0`) — `dest: result_slot, operand1: source_idx, operand2: keys_record_slot`
- Host callback interface: VM calls host with `(source_name, key_record, logical_timestamp)` → host returns JSON record or nothing
- Result lands in scratchpad with tag bits from the source contract's `returns` declaration

## Implementation Order

1. **Lexer** — add new tokens
2. **AST** — add new node types
3. **Parser** — contract kind detection, source contract parsing, `sources` decl, `lookup` expression
4. **Evaluator** — implement lookup as a host callback (for the interpreter, the host provides a callback function that the evaluator calls with source name + key record + timestamp)
5. **Tag unification** — when resolving `uses`, merge tag definitions and verify no conflicts (same name = same bit position)
6. **Tests** — sample `.heluna` files exercising all three contract kinds and lookup in function bodies
