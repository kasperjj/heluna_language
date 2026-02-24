# Heluna Language — LLM Context

This document contains everything you need to write correct Heluna code and use the toolchain. Read it fully before generating any Heluna.

---

## What Heluna Is

Heluna is a pure functional language for JSON transformations. Every function takes a single JSON record as input and returns a single JSON record as output. No side effects, no I/O, no mutable state. The host process handles the real world; Heluna handles the transformation.

There are three contract kinds:
- **Function contracts** — input schema, output schema, rules, tests, and a function body
- **Tag contracts** — reusable security vocabulary (tags only, no function)
- **Source contracts** — read-only external data dependencies (no function)

---

## Toolchain

Files use the `.heluna` extension. The toolchain processes a single file at a time:

```bash
bin/heluna-check  file.heluna            # Static analysis — prints "ok" or error with location
bin/heluna-fmt    file.heluna            # Canonical formatting to stdout
bin/heluna-compile file.heluna out.hlna  # Compile to binary packet
bin/heluna-lex    file.heluna            # Tokenize (debugging)
bin/heluna-parse  file.heluna            # Parse to AST (debugging)
```

**Workflow:** Write the `.heluna` file, run `heluna-check` to validate, run `heluna-fmt` to canonicalize formatting. The check tool catches: duplicate fields, undefined identifiers, tag coherence, sanitizer validity, rule field references, test case schema mismatches, scope errors (shadowing, undefined vars), and stdlib/function call correctness.

Build the toolchain with `make` (artifacts in `bin/`). Run all tests with `make test`.

---

## Complete Grammar

```ebnf
(* ── Lexical elements ──────────────────────────────────────── *)

Literal       ::= Integer | Float | String | Boolean | Nothing
Boolean       ::= 'true' | 'false'
Nothing       ::= 'nothing'
Integer       ::= Digit+
Float         ::= Digit+ '.' Digit+ (('e' | 'E') ('-' | '+')? Digit+)?
String        ::= '"' Character* '"'
Comment       ::= '#' (any character until end of line)
Identifier    ::= Letter (Letter | Digit | '-')*

(* Identifiers use hyphens, not underscores: full-name, date-of-birth *)

(* ── Types ─────────────────────────────────────────────────── *)

Type          ::= BaseType | MaybeType | ListType | RecordType
BaseType      ::= 'string' | 'integer' | 'float' | 'boolean'
MaybeType     ::= 'maybe' Type
ListType      ::= 'list' 'of' Type
RecordType    ::= 'record' FieldDecl (',' FieldDecl)* 'end'
FieldDecl     ::= Name 'as' Type ('tagged' Identifier+)?

(* ── Expressions ───────────────────────────────────────────── *)

(* Precedence (lowest to highest):
 *   through < or < and < not < compare < add/sub < mul/div/mod < unary neg < atom
 *)

ThroughExpr   ::= OrExpr ('through' (FunctionCall | FilterExpr | MapExpr))*
OrExpr        ::= AndExpr (('or' 'else' AndExpr) | ('or' AndExpr))*
AndExpr       ::= NotExpr ('and' NotExpr)*
NotExpr       ::= 'not' NotExpr | CompareExpr
CompareExpr   ::= ArithExpr (CompareOp ArithExpr | 'is' TypeKeyword)?
CompareOp     ::= '=' | '!=' | '<' | '>' | '<=' | '>='
TypeKeyword   ::= 'string' | 'integer' | 'float' | 'boolean'
               |  'nothing' | 'list' | 'record'
ArithExpr     ::= Term (('+' | '-') Term)*
Term          ::= Unary (('*' | '/' | '%' | 'mod') Unary)*
Unary         ::= '-' Unary | Atom

Atom          ::= Literal Accessor*
               | Identifier Accessor*
               | '$' Identifier Accessor*
               | FunctionCall Accessor*
               | ListLiteral Accessor*
               | RecordLiteral Accessor*
               | '(' Expression ')' Accessor*

Accessor      ::= '.' (Identifier | Integer | Keyword)

ListLiteral   ::= '[' (Expression (',' Expression)*)? ']'
RecordLiteral ::= '{' (Label (',' Label)*)? '}'
Label         ::= Name ':' Expression

(* ── Control flow ──────────────────────────────────────────── *)

LetExpr       ::= 'let' Identifier 'be' Expression
IfExpr        ::= 'if' BoolExpr 'then' Expression
                   ('else' 'if' BoolExpr 'then' Expression)*
                   'else' Expression 'end'
MatchExpr     ::= 'match' Expression WhenClause+ ('else' Expression)? 'end'
WhenClause    ::= 'when' Pattern ('and' Guard)? 'then' Expression
FilterExpr    ::= 'filter' Expression 'where' Identifier BoolExpr 'end'
MapExpr       ::= 'map' Expression 'as' Identifier 'do' Expression 'end'
LookupExpr    ::= 'lookup' Identifier 'where' LookupKey (',' LookupKey)* 'end'

(* ── Patterns ──────────────────────────────────────────────── *)

Pattern       ::= LiteralPattern | ListPattern | RecordPattern
               | RangePattern | WildcardPattern | BindingPattern
LiteralPattern   ::= Literal | '-' (Integer | Float)
ListPattern      ::= '[' (Pattern (',' Pattern)* (',' '..' Identifier?)?)? ']'
RecordPattern    ::= '{' (FieldPattern (',' FieldPattern)*)? '}'
FieldPattern     ::= Name ':' Pattern
RangePattern     ::= 'between' CompareExpr 'and' CompareExpr
BindingPattern   ::= Identifier
WildcardPattern  ::= '_'

(* ── Function definition ───────────────────────────────────── *)

FunctionDef   ::= 'define' Identifier 'with' 'input'
                   LetBinding*
                   'result' Expression 'end'
LetBinding    ::= 'let' Identifier 'be' Expression
FunctionCall  ::= Identifier '(' RecordLiteral ')'

(* ── Contracts ─────────────────────────────────────────────── *)

TagContract    ::= 'contract' Identifier UsesDecl? TagDecl 'end'

SourceContract ::= 'contract' Identifier UsesDecl? TagDecl?
                    SourceDecl KeyedByDecl ReturnsDecl 'end'

FunctionContract ::= 'contract' Identifier UsesDecl? TagDecl?
                      SanitizerDecl? SourcesDecl?
                      InputSpec OutputSpec RuleList? TestList? 'end'

UsesDecl      ::= 'uses' Identifier (',' Identifier)*
TagDecl       ::= 'tags' TagDef (',' TagDef)* 'end'
TagDef        ::= Identifier String?
SanitizerDecl ::= 'sanitizers' SanitizerDef+ 'end'
SanitizerDef  ::= Identifier ('using' Identifier)? 'strips' Identifier+
SourceDecl    ::= 'source' String
KeyedByDecl   ::= 'keyed-by' FieldDecl (',' FieldDecl)*
ReturnsDecl   ::= 'returns' Type
SourcesDecl   ::= 'sources' Identifier (',' Identifier)*
InputSpec     ::= 'input' FieldDecl (',' FieldDecl)* 'end'
OutputSpec    ::= 'output' FieldDecl (',' FieldDecl)* 'end'
RuleList      ::= 'rules' Rule+ 'end'
Rule          ::= ForbidRule | RequireRule | MatchRule
ForbidRule    ::= 'forbid' FieldRef 'in' 'output'
               | 'forbid' 'tagged' Identifier 'in' 'output'
RequireRule   ::= 'require' FieldRef BoolExpr ('else' 'reject' String)? 'end'
MatchRule     ::= 'match' FieldRef RuleWhenClause+
                   ('else' 'reject' String)? 'end'
RuleWhenClause ::= 'when' Pattern ('and' Guard)? 'then' Expression
FieldRef      ::= ('input' | 'output') Accessor*
TestList      ::= 'tests' TestCase+ 'end'
TestCase      ::= 'test' String 'given' RecordLiteral 'expect' RecordLiteral 'end'

(* ── Program ───────────────────────────────────────────────── *)

Program       ::= FunctionContract FunctionDef
               |  TagContract
               |  SourceContract
```

---

## Key Language Rules

- **No null.** Use `maybe` types and `nothing`. You must handle the `nothing` case explicitly via `match` or `or else`.
- **No shadowing.** Every `let` binding in a function must have a unique name.
- **No implicit returns.** Functions end with `result { ... } end`.
- **Input references use `$`.** `$field-name` accesses a field from the input record.
- **Hyphens in identifiers.** Use `full-name`, not `full_name` or `fullName`.
- **`if` always has `else`.** It's an expression, not a statement.
- **`+` on strings is concatenation.** `"hello" + " " + "world"`.
- **Equality is `=`, not `==`.** Inequality is `!=`.
- **Boolean operators are words.** `and`, `or`, `not` — never `&&`, `||`, `!`.
- **Comments use `#`.** They run to end of line.
- **Every function call takes a record.** `fn({ key: value })` — always one argument, always a record literal.
- **`through` is the pipe operator.** `expr through fn({})` passes `expr` as the `value` field.
- **Accessor syntax.** `record.field-name`, `list.0` (integer index), `fn({ x: 1 }).result`.

---

## Data Security

**Tags** classify fields: `patient-name as string tagged pii restricted`

**Propagation** is automatic. Tags follow data through all operations — concatenation, arithmetic, conditionals, record construction. You cannot strip tags through computation.

**Sanitizers** are the only way to remove tags. Declared in the contract, not the implementation:
```heluna
sanitizers
  hash using sha256 strips restricted pii phi
end
```
The `using` clause names the underlying stdlib function. When the sanitizer is called, the specified tags are stripped from its output.

**Rules** enforce constraints on output:
```heluna
rules
  forbid tagged restricted in output      # no restricted data in any output field
  forbid output.ssn in output             # specific field forbidden
  require input.quantity                   # validation with condition
    input.quantity > 0
    else reject "quantity must be positive"
  end
end
```

---

## Standard Library

Every stdlib function takes a single record and returns a value. When used with `through`, the left-hand value becomes the `value` field.

### String Functions

| Function | Input | Returns | Example |
|----------|-------|---------|---------|
| `upper` | `{ value: string }` | `string` | `upper({ value: "hello" })` → `"HELLO"` |
| `lower` | `{ value: string }` | `string` | `lower({ value: "Hello" })` → `"hello"` |
| `trim` | `{ value: string }` | `string` | `trim({ value: "  hi  " })` → `"hi"` |
| `trim-start` | `{ value: string }` | `string` | `trim-start({ value: "  hi  " })` → `"hi  "` |
| `trim-end` | `{ value: string }` | `string` | `trim-end({ value: "  hi  " })` → `"  hi"` |
| `substring` | `{ value: string, start: integer, end: integer }` | `string` | `substring({ value: "hello", start: 0, end: 3 })` → `"hel"` |
| `replace` | `{ value: string, find: string, replacement: string }` | `string` | `replace({ value: "ab", find: "b", replacement: "c" })` → `"ac"` |
| `split` | `{ value: string, delimiter: string }` | `list of string` | `split({ value: "a,b", delimiter: "," })` → `["a", "b"]` |
| `join` | `{ list: list of string, delimiter: string }` | `string` | `join({ list: ["a", "b"], delimiter: "-" })` → `"a-b"` |
| `starts-with` | `{ value: string, prefix: string }` | `boolean` | |
| `ends-with` | `{ value: string, suffix: string }` | `boolean` | |
| `contains` | `{ value: string, substring: string }` | `boolean` | |
| `length` | `{ value: string }` or `{ list: list }` | `integer` | `length({ value: "hi" })` → `2` |
| `pad-left` | `{ value: string, width: integer, fill: string }` | `string` | `pad-left({ value: "42", width: 5, fill: "0" })` → `"00042"` |
| `pad-right` | `{ value: string, width: integer, fill: string }` | `string` | |
| `regex-match` | `{ value: string, pattern: string }` | `boolean` | POSIX extended regex |
| `regex-replace` | `{ value: string, pattern: string, replacement: string }` | `string` | No backreferences |

### Numeric Functions

| Function | Input | Returns |
|----------|-------|---------|
| `abs` | `{ value: number }` | `number` |
| `ceil` | `{ value: number }` | `integer` |
| `floor` | `{ value: number }` | `integer` |
| `round` | `{ value: number }` | `integer` |
| `min` | `{ a: number, b: number }` | `number` |
| `max` | `{ a: number, b: number }` | `number` |
| `clamp` | `{ value: number, min: number, max: number }` | `number` |

### List Functions

| Function | Input | Returns |
|----------|-------|---------|
| `sort` | `{ list: list }` | `list` |
| `sort-by` | `{ list: list, field: string }` | `list` |
| `reverse` | `{ list: list }` | `list` |
| `unique` | `{ list: list }` | `list` |
| `flatten` | `{ list: list of list }` | `list` (one level) |
| `zip` | `{ a: list, b: list }` | `list of record { a, b }` |
| `range` | `{ start: integer, end: integer }` | `list of integer` (inclusive) |
| `slice` | `{ list: list, start: integer, end: integer }` | `list` (start inclusive, end exclusive) |

### Record Functions

| Function | Input | Returns |
|----------|-------|---------|
| `keys` | `{ record: record }` | `list of string` |
| `values` | `{ record: record }` | `list` |
| `merge` | `{ a: record, b: record }` | `record` (b overrides a) |
| `pick` | `{ record: record, fields: list of string }` | `record` |
| `omit` | `{ record: record, fields: list of string }` | `record` |

### Date/Time Functions

| Function | Input | Returns |
|----------|-------|---------|
| `parse-date` | `{ value: string, format: string }` | `record { year, month, day, hour, minute, second }` |
| `format-date` | `{ date: record, format: string }` | `string` |
| `date-diff` | `{ from: string, to: string, unit: string }` | `integer` |
| `date-add` | `{ date: string, amount: integer, unit: string }` | `string` |
| `now-date` | `{}` | `string` (logical timestamp, deterministic per execution) |

Format strings use strftime conventions (`%Y`, `%m`, `%d`, etc.). Date strings are ISO 8601 (`YYYY-MM-DDTHH:MM:SS`). Units for diff/add: `"seconds"`, `"minutes"`, `"hours"`, `"days"` (add also supports `"months"`, `"years"`).

### Encoding Functions

| Function | Input | Returns |
|----------|-------|---------|
| `base64-encode` | `{ value: string }` | `string` |
| `base64-decode` | `{ value: string }` | `string` |
| `url-encode` | `{ value: string }` | `string` |
| `url-decode` | `{ value: string }` | `string` |
| `json-encode` | `{ value: any }` | `string` |
| `json-parse` | `{ value: string }` | parsed value |

### Crypto Functions

| Function | Input | Returns |
|----------|-------|---------|
| `sha256` | `{ value: string }` | `string` (64-char hex) |
| `hmac-sha256` | `{ value: string, key: string }` | `string` (64-char hex) |
| `uuid` | `{}` | `string` (UUID v4) |

### Conversion Functions

| Function | Input | Returns |
|----------|-------|---------|
| `to-string` | `{ value: any }` | `string` |
| `to-float` | `{ value: integer \| float \| string }` | `float` |
| `to-integer` | `{ value: integer \| float \| string }` | `integer` (truncates) |

### Iteration

| Function | Input | Returns |
|----------|-------|---------|
| `fold` | `{ list: list, initial: any, fn: string }` | accumulated value |

Supported `fn` values: `"add"`, `"multiply"`.

---

## Complete Examples

### Function contract with lists, filter, fold

```heluna
contract process-scores
  input
    scores as list of integer
  end

  output
    passing as list of integer,
    average as float,
    count as integer
  end

  tests
    test "mixed scores"
      given { scores: [85, 42, 91, 55, 73, 68] }
      expect { passing: [85, 91, 73, 68], average: 79.25, count: 4 }
    end
  end
end

define process-scores with input
  let passing-scores be
    filter $scores where s s >= 60 end

  let total be fold({
    list: passing-scores,
    initial: 0.0,
    fn: "add"
  })

  let num-passing be length({ list: passing-scores })

  result {
    passing: passing-scores,
    average: total / to-float({ value: num-passing }),
    count: num-passing
  }
end
```

### Function contract with tags, sanitizers, and security rules

```heluna
contract patient-summary
  uses bracket-age

  tags
    pii "personally identifiable information",
    phi "protected health information",
    restricted "must not leave processing boundary"
  end

  sanitizers
    hash using sha256 strips restricted pii phi,
    bracket-age using bracket-age strips restricted pii
  end

  input
    patient-name as string tagged pii restricted,
    ssn as string tagged pii restricted,
    date-of-birth as string tagged phi restricted,
    diagnosis-code as string,
    visit-count as integer
  end

  output
    patient-hash as string,
    age-range as string,
    diagnosis-code as string,
    visit-count as integer
  end

  rules
    forbid tagged restricted in output
    forbid tagged pii in output
    forbid tagged phi in output
  end

  tests
    test "basic summary"
      given {
        patient-name: "Jane Doe",
        ssn: "123-45-6789",
        date-of-birth: "1985-03-15",
        diagnosis-code: "J06.9",
        visit-count: 3
      }
      expect {
        patient-hash: "5d0ea1a5d9bae23fdce4fa5fa15692b18d2e9049ca190471f8dc13299252487e",
        age-range: "35-44",
        diagnosis-code: "J06.9",
        visit-count: 3
      }
    end
  end
end

define patient-summary with input
  result {
    patient-hash: hash({ value: $patient-name + $ssn }),
    age-range: bracket-age({ date-of-birth: $date-of-birth }).value,
    diagnosis-code: $diagnosis-code,
    visit-count: $visit-count
  }
end
```

### Tag contract (reusable vocabulary)

```heluna
contract company-security
  tags
    pii "personally identifiable information",
    restricted "must not leave processing boundary"
  end
end
```

### Source contract (external data dependency)

```heluna
contract customers-source
  uses company-security

  tags
    pii "personally identifiable information",
    restricted "must not leave processing boundary"
  end

  source "customers"

  keyed-by customer-id as string

  returns record
    customer-id as string,
    name as string tagged pii,
    credit-limit as float tagged restricted
  end
end
```

### Function using a source with lookup

```heluna
contract enrich-order
  uses company-security

  tags
    pii "personally identifiable information",
    restricted "must not leave processing boundary"
  end

  sanitizers
    hash using sha256 strips restricted pii
  end

  sources customers-source

  input
    customer-id as string,
    order-total as float
  end

  output
    approved as boolean,
    customer-hash as string
  end

  rules
    forbid tagged restricted in output
  end

  tests
    test "approved order"
      given { customer-id: "C001", order-total: 50.0 }
      expect { approved: true, customer-hash: "abc" }
    end
  end
end

define enrich-order with input
  let customer be
    lookup customers-source
      where customer-id = $customer-id
    end

  let approved be match customer
    when nothing then false
    when c then $order-total <= c.credit-limit
  end

  result {
    approved: approved,
    customer-hash: hash({ value: $customer-id })
  }
end
```

### Pattern matching, maybe types, pipelines

```heluna
contract describe-value
  input
    value as maybe integer
  end

  output
    description as string
  end

  tests
    test "nothing"
      given { value: nothing }
      expect { description: "no value provided" }
    end

    test "negative"
      given { value: -5 }
      expect { description: "negative" }
    end

    test "small positive"
      given { value: 42 }
      expect { description: "between 1 and 100" }
    end

    test "large"
      given { value: 500 }
      expect { description: "large number: 500" }
    end
  end
end

define describe-value with input
  result {
    description: match $value
      when nothing then "no value provided"
      when between -999999 and -1 then "negative"
      when 0 then "zero"
      when between 1 and 100 then "between 1 and 100"
      when n and n > 100 then "large number: " + to-string({ value: n }).value
      else "unexpected"
    end
  }
end
```

---

## Common Patterns

**Default values for maybe fields:**
```heluna
let title be $title or else "Untitled"
```

**Type conversion before arithmetic:**
```heluna
let total be to-float({ value: $count }) * $price
```

**Pipeline chaining:**
```heluna
let clean be $email through trim({}) through lower({})
```

**Calling another function (declared via `uses`):**
```heluna
let result be other-function({ field: $value }).output-field
```

**Building a list with map:**
```heluna
let labels be map $items as item do
  item.last + ", " + item.first
end
```

**Filtering a list:**
```heluna
let adults be filter $people where p p.age >= 18 end
```

**Folding a list:**
```heluna
let sum be fold({ list: $numbers, initial: 0, fn: "add" })
```

**Record field access:**
```heluna
let name be $person.name
let first-item be $items.0
```
