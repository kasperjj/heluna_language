# Introduction to Heluna

A practical guide for experienced programmers.

---

## What Is Heluna?

Heluna is a pure functional language for JSON transformations. If you've ever written a microservice whose entire job is "take JSON in, validate it, reshape it, send JSON out," that's exactly the problem Heluna solves — but with guarantees you can't get from Python, JavaScript, or Go.

Every Heluna function takes a single JSON record as input and returns a single JSON record as output. No exceptions. No side effects. No file I/O, no network calls, no mutable state. The host process (your HTTP server, your message queue consumer, your cron job) handles the messy real world. Heluna handles the transformation, and it does so safely.

The language was designed from the ground up for human-LLM collaboration. The entire specification fits in a single LLM context window. There are roughly 50 reserved words, 25 standard library functions, and one universal function signature. If that sounds small, that's the point.

---

## Your First Function

Let's start with something familiar: computing a full name from its parts.

```heluna
contract full-name
  input
    first-name as string,
    last-name as string
  end

  output
    full-name as string
  end

  tests
    test "basic concatenation"
      given { first-name: "Ada", last-name: "Lovelace" }
      expect { full-name: "Ada Lovelace" }
    end
  end
end

define full-name with input
  result {
    full-name: $first-name + " " + $last-name
  }
end
```

A few things to notice right away:

The **contract** comes first. It declares the input schema, the output schema, and a test case — all before a line of implementation exists. This is Design by Contract in the Eiffel tradition: the specification *is* the primary artifact.

The **function** has one shape: `define <name> with input ... result ... end`. Every Heluna function looks like this. There are no parameter lists, no overloading, no variadic arguments. The `$` prefix references input fields — `$first-name` means "the `first-name` field from the input record."

The **test** is embedded in the contract. Given a specific input record, expect a specific output record. There's no separate test file, no test framework, no assertion library. The contract *is* the test harness.

---

## Variables with `let`

Heluna uses `let ... be ...` for local bindings. Variables are immutable and cannot shadow each other — every name in a function must be unique.

```heluna
contract rectangle-area
  input
    width as float,
    height as float
  end

  output
    area as float,
    perimeter as float
  end

  tests
    test "3x4 rectangle"
      given { width: 3.0, height: 4.0 }
      expect { area: 12.0, perimeter: 14.0 }
    end
  end
end

define rectangle-area with input
  let w be $width
  let h be $height
  let computed-area be w * h
  let computed-perimeter be 2.0 * (w + h)

  result {
    area: computed-area,
    perimeter: computed-perimeter
  }
end
```

No `var`, no `const`, no reassignment. Once `computed-area` is bound, it stays that way. If you try to write `let w be 10.0` later in the same function, Heluna rejects it — no shadowing, ever.

---

## Conditionals with `if`

Heluna's `if` is an expression, not a statement. It always produces a value, and it always requires an `else` branch.

```heluna
contract ticket-price
  input
    age as integer
  end

  output
    price as float,
    category as string
  end

  tests
    test "child discount"
      given { age: 8 }
      expect { price: 5.0, category: "child" }
    end

    test "adult price"
      given { age: 35 }
      expect { price: 15.0, category: "adult" }
    end

    test "senior discount"
      given { age: 70 }
      expect { price: 10.0, category: "senior" }
    end
  end
end

define ticket-price with input
  let category be
    if $age < 12 then "child"
    else if $age >= 65 then "senior"
    else "adult"
    end

  let price be
    if category = "child" then 5.0
    else if category = "senior" then 10.0
    else 15.0
    end

  result {
    price: price,
    category: category
  }
end
```

Notice the boolean operators: `and`, `or`, `not` — English words, not symbols. The comparison operators stay familiar (`<`, `>=`, `=`, `!=`), but the logical connectives read like prose.

---

## Pattern Matching with `match`

Pattern matching is Heluna's most powerful control structure. It replaces switch statements, type checks, destructuring, and range guards with a single, uniform syntax.

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

Several pattern types appear here: `nothing` matches absent values (Heluna has no null — `maybe` and `nothing` replace it). `between ... and ...` matches ranges. A bare identifier like `n` binds the matched value so you can use it in a guard (`and n > 100`) or in the result expression. `_` would match anything without binding.

This same `match ... when ... then ... end` syntax appears in contracts too — the same grammar validates output fields and controls program flow.

---

## No Null — `maybe` and `nothing`

If you've dealt with `NullPointerException`, `TypeError: Cannot read properties of undefined`, or Go's zero-value footguns, this will feel refreshing. Heluna has no null. Optional values use `maybe`, and you must handle the absent case explicitly.

```heluna
contract greet
  input
    name as string,
    title as maybe string
  end

  output
    greeting as string
  end

  tests
    test "with title"
      given { name: "Chen", title: "Dr." }
      expect { greeting: "Hello, Dr. Chen" }
    end

    test "without title"
      given { name: "Chen", title: nothing }
      expect { greeting: "Hello, Chen" }
    end
  end
end

define greet with input
  let prefix be match $title
    when nothing then ""
    when t then t + " "
  end

  result {
    greeting: "Hello, " + prefix + $name
  }
end
```

You can't ignore the `nothing` case. If your match doesn't cover it, the compiler tells you. No silent nulls sneaking through.

### Type Testing with `is`

Sometimes you need a simple type check without the full weight of pattern matching. The `is` keyword tests whether a value is a specific type and returns a boolean.

```heluna
$value is integer    # true if $value is an integer
$value is nothing    # true if $value is absent
$value is string     # true if $value is a string
```

This is useful in conditional expressions:

```heluna
if $score is integer then $score * 2
else 0
end
```

The supported type keywords after `is` are: `string`, `integer`, `float`, `boolean`, `nothing`, `list`, and `record`.

### Fallback Values with `or else`

When working with `maybe` values, the most common pattern is "use this value, but if it's `nothing`, use a fallback instead." The `or else` expression handles this directly, without needing a `match`:

```heluna
$title or else "Untitled"
```

This evaluates the left-hand side. If the result is `nothing`, it evaluates and returns the right-hand side instead. Compare the `match` equivalent — `or else` is more concise for simple defaults:

```heluna
# These two are equivalent:
$title or else "Untitled"

match $title
  when nothing then "Untitled"
  when t then t
end
```

Use `match` when you need to transform the present value or handle multiple cases. Use `or else` when you just need a fallback.

---

## Arithmetic: The `mod` Operator

Heluna provides the standard arithmetic operators (`+`, `-`, `*`, `/`) and a `mod` keyword for the remainder operation. Like other Heluna operators, it uses an English word rather than a symbol.

```heluna
17 mod 5     # equals 2
10 mod 3     # equals 1
```

`mod` sits at the same precedence level as `*` and `/`, so it interacts naturally with other arithmetic:

```heluna
$total + $quantity mod 10
# parses as: $total + ($quantity mod 10)
```

---

## Working with Lists: `map`, `filter`, and `through`

Heluna provides `map`, `filter`, and the `through` keyword for chaining transformations on lists. If you're used to method chaining in JavaScript or Elixir's pipe operator, `through` will feel natural.

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

`filter ... where <binding> <condition> end` reads almost like SQL: "filter scores where each score is greater than or equal to 60."

Now let's see `map` and `through` working together in a pipeline:

```heluna
contract format-names
  input
    names as list of record first as string, last as string end
  end

  output
    labels as list of string
  end

  tests
    test "format list"
      given { names: [
        { first: "Ada", last: "Lovelace" },
        { first: "Alan", last: "Turing" }
      ]}
      expect { labels: ["Lovelace, Ada", "Turing, Alan"] }
    end
  end
end

define format-names with input
  result {
    labels: $names
      through map $names as person do
        person.last + ", " + person.first
      end
  }
end
```

The `through` keyword threads data left-to-right, eliminating nested function calls. You can chain multiple `through` steps — filters, maps, and function calls — into a readable pipeline.

---

## Data Security: Tags, Propagation, and Sanitizers

This is where Heluna diverges sharply from other languages. Data security isn't a library or a middleware concern — it's built into the type system.

Imagine you're building a function that takes a patient record and produces an analytics summary. The raw data contains protected health information. You need to guarantee — not hope, not document, *guarantee* — that the output is safe to send to a third-party analytics service.

```heluna
contract patient-summary

  tags
    pii "personally identifiable information",
    phi "protected health information",
    restricted "must not leave processing boundary"
  end

  sanitizers
    hash strips restricted pii phi,
    bracket-age strips restricted pii
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
        patient-hash: "a1b2c3",
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
    age-range: bracket-age({ date-of-birth: $date-of-birth }),
    diagnosis-code: $diagnosis-code,
    visit-count: $visit-count
  }
end
```

Here's what the contract enforces:

**Tags** classify fields at the boundary. `patient-name` is tagged `pii` and `restricted`. The runtime knows what this data *means*, not just its shape.

**Propagation** is automatic. If you wrote `$patient-name + " - notes"`, the result is still tagged `pii restricted`. You can't launder sensitive data through string concatenation, arithmetic, or conditionals. Tags are sticky.

**Sanitizers** are the only escape hatch. `hash` is declared to strip `restricted`, `pii`, and `phi`. The contract author decides which functions are trusted — the implementation author cannot invent new sanitizers.

**Rules** enforce constraints on the output as a whole. `forbid tagged restricted in output` means: if *any* field in the output still carries the `restricted` tag, the function is rejected. This applies to fields that exist today and any added tomorrow.

What happens if someone writes a buggy implementation that sneaks `$ssn` into the output without hashing it? The tag system catches it. The SSN carries `restricted`, the output rule forbids `restricted`, and the function fails validation. The data never leaves.

---

## Composing Functions

Real transformations rarely happen in one step. Heluna encourages small, single-purpose functions that compose by passing records between them.

```heluna
contract normalize-email
  input
    email as string
  end

  output
    email as string
  end

  tests
    test "lowercase and trim"
      given { email: "  Alice@Example.COM  " }
      expect { email: "alice@example.com" }
    end
  end
end

define normalize-email with input
  result {
    email: $email through trim({}) through lower({})
  }
end
```

```heluna
contract validate-user
  uses normalize-email

  input
    name as string,
    email as string,
    age as integer
  end

  output
    name as string,
    email as string,
    age-group as string
  end

  rules
    require output.age-group
      output.age-group = "minor" or output.age-group = "adult" or output.age-group = "senior"
      else reject "invalid age group"
    end
  end

  tests
    test "adult user"
      given { name: "Bob", email: "  BOB@test.com ", age: 30 }
      expect { name: "Bob", email: "bob@test.com", age-group: "adult" }
    end
  end
end

define validate-user with input
  let clean-email be normalize-email({ email: $email }).email

  let age-group be
    if $age < 18 then "minor"
    else if $age >= 65 then "senior"
    else "adult"
    end

  result {
    name: $name,
    email: clean-email,
    age-group: age-group
  }
end
```

The `uses` declaration in the contract tells the system that `validate-user` depends on `normalize-email`. The call syntax is always the same: `function-name({ field: value })` — it takes a record, returns a record.

---

## Contract Rules: Validation Without Code

Contract rules let you enforce constraints declaratively — no implementation logic required.

```heluna
contract create-order
  input
    quantity as integer,
    unit-price as float,
    discount as float
  end

  output
    total as float,
    quantity as integer
  end

  rules
    require input.quantity
      input.quantity > 0
      else reject "quantity must be positive"
    end

    require input.discount
      input.discount >= 0.0 and input.discount <= 1.0
      else reject "discount must be between 0 and 1"
    end

    require output.total
      output.total >= 0.0
      else reject "total must not be negative"
    end
  end

  tests
    test "simple order"
      given { quantity: 3, unit-price: 10.0, discount: 0.1 }
      expect { total: 27.0, quantity: 3 }
    end
  end
end

define create-order with input
  let subtotal be to-float({ value: $quantity }) * $unit-price
  let total be subtotal * (1.0 - $discount)

  result {
    total: total,
    quantity: $quantity
  }
end
```

The rules validate inputs *and* outputs. If someone calls this function with `quantity: -5`, the contract rejects the call before the function body even runs. If a bug in the implementation produces a negative total, the output rule catches it. The function author doesn't need to write defensive checks — the contract handles it.

---

## Reusable Tag Vocabularies: Tag Contracts

In the patient-summary example above, the tags `pii`, `phi`, and `restricted` were declared inline. But what if multiple contracts across your system need the same security vocabulary? You'd end up duplicating tag definitions everywhere.

Tag contracts solve this. A tag contract declares a reusable set of tags with no function — it exists purely to be referenced by other contracts via `uses`.

```heluna
contract company-security
  tags
    pii "personally identifiable information",
    restricted "must not leave processing boundary"
  end
end
```

That's the entire file. No input, no output, no function. Any contract that writes `uses company-security` gains access to those tag definitions and can reference them in field annotations, sanitizer declarations, and rules.

---

## External Data: Source Contracts and Lookup

Real transformations often need data beyond what's in the input record. A function might need to look up a customer record, check an inventory table, or reference a configuration set. Heluna handles this through **source contracts** and the **lookup** expression.

### Source Contracts

A source contract defines a read-only external data dependency — a named collection with a key and a return type.

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

The `source` declaration names the external collection. `keyed-by` declares which fields form the lookup key and their types. `returns` declares the shape of the data that comes back — including tags, so the security system knows what sensitivity the looked-up data carries.

Like tag contracts, source contracts have no function definition. They exist to be referenced.

### Using Sources in Function Contracts

A function contract declares which source contracts it depends on with the `sources` keyword. The function body can then use `lookup` to query them.

```heluna
contract enrich-order
  uses company-security

  tags
    pii "personally identifiable information",
    restricted "must not leave processing boundary"
  end

  sanitizers
    hash strips restricted pii
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

The `lookup` expression queries a declared source by key and returns a `maybe record` — the result is either `nothing` (no match found) or the record defined by the source contract's `returns` type. You must handle both cases, typically with `match`.

Notice how the source contract's tags flow through the lookup. The `credit-limit` field is tagged `restricted` in the source contract, so any value derived from `c.credit-limit` carries that tag. The `forbid tagged restricted in output` rule ensures no restricted data leaks into the output.

---

## How Heluna Fits Into Your System

Heluna is not a replacement for your application framework. It's a computation kernel that lives inside your host process. A typical integration looks like this:

1. Your HTTP server, message queue consumer, or scheduled job receives a request.
2. It constructs a JSON record matching the Heluna contract's input schema.
3. It invokes the Heluna function.
4. Heluna validates the input against the contract, runs the pure transformation, and validates the output.
5. Your host process receives the output JSON and does whatever comes next — stores it in a database, publishes it to a queue, returns it in an API response.

Heluna handles step 3 and 4. Everything else is your code, in whatever language you prefer.

---

## Quick Reference

| Concept | Heluna | What you might be used to |
|---|---|---|
| Variables | `let x be 42` | `const x = 42` / `let x = 42` |
| Conditionals | `if ... then ... else ... end` | `if/else` with braces or ternary |
| Pattern matching | `match ... when ... then ... end` | `switch`, `match`, `case` |
| Null handling | `maybe` type + `nothing` | `null`, `nil`, `None`, `Optional` |
| Fallback/coalesce | `expr or else default` | `??`, `\|\|`, `.unwrap_or()` |
| Type testing | `expr is integer` | `typeof`, `isinstance()` |
| Remainder | `a mod b` | `a % b` |
| List transform | `map list as x do ... end` | `.map(x => ...)` |
| List filter | `filter list where x ... end` | `.filter(x => ...)` |
| Pipeline | `expr through fn({})` | `expr \|> fn` / method chaining |
| Boolean logic | `and`, `or`, `not` | `&&`, `\|\|`, `!` |
| Input field access | `$field-name` | function parameters |
| Function call | `fn({ key: value })` | `fn(value)` |
| Explicit result | `result { ... }` | `return { ... }` |

---

## Where to Go Next

You now know enough Heluna to read any contract and follow any implementation. The key ideas to carry with you:

**Contract first.** Write the specification before the code. The contract defines what's valid, what's tested, and what's secure.

**Everything is JSON.** No classes, no structs, no custom types beyond JSON primitives, lists, and records.

**Pure functions only.** No side effects, no I/O, no mutable state. Your host process handles the real world.

**Security is structural.** Tags, propagation, and sanitizers make data protection auditable and automatic — not a matter of developer discipline.

**Small pieces that compose.** One function, one job, one contract. Chain them with `through` and `uses`.
