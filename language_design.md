# Heluna 2.0 — Design Philosophy

## What Heluna Is
Heluna is a pure functional language designed for safe, composable JSON transformations. Every Heluna function takes a JSON record as input and produces a JSON record as output, with no side effects. Functions are wrapped in contracts that define their input and output schemas, validation rules, privacy constraints, and test cases.

Heluna is not a general-purpose programming language. It is a computation kernel — a small, focused engine that lives inside a host process. The host handles I/O, networking, databases, and orchestration. Heluna handles transformation and validation.

## Why Heluna Exists
Large language models are increasingly being used to generate code, but most programming languages were designed for humans writing code by hand. They carry decades of accumulated complexity — deep inheritance hierarchies, mutable state, implicit returns, operator overloading, null — that makes them difficult for LLMs to reason about reliably.

Heluna is designed from the ground up to be written collaboratively by humans and LLMs. A non-programmer should be able to describe what they want in plain English, and an LLM should be able to generate both the contract and the implementation in a single pass, with the contract serving as a verifiable specification of correctness.

## Core Design Decisions

### Everything Is JSON
All inputs, outputs, and intermediate values are JSON. There are no custom objects, no classes, no structs beyond what JSON already provides — strings, numbers, booleans, arrays, and records. This means the type system is immediately familiar to anyone who has worked with APIs, configuration files, or data pipelines.

### Contract First
Every Heluna function has a contract that defines what goes in, what comes out, what constraints must hold, and what tests verify correctness. The contract is not an afterthought — it is the primary artifact. Implementation flows from the contract, not the other way around.

This is inspired by Bertrand Meyer's Design by Contract from the Eiffel programming language. The contract serves as documentation, test harness, and security boundary all at once.

### One Function, One Signature
Every function takes a single JSON record as input and returns a single JSON record as output. There are no parameter lists, no overloading, no variadic arguments. This uniformity means there is exactly one pattern to learn, one pattern to generate, and one pattern to compose.

### Pure Functions, No Side Effects
Heluna functions cannot read files, make network calls, access databases, or mutate global state. The only thing a function can do is transform its input into its output. This makes every function deterministic, testable, and safe to run in any context.

Side effects belong to the host process. A RabbitMQ consumer, an HTTP server, a cron job — any of these can feed JSON into a Heluna container and act on the JSON that comes back.

### Data Security as a Language Feature
Most programming languages treat data security as an external concern — something handled by infrastructure, middleware, or developer discipline. Heluna makes it a first-class feature of the language itself.

#### Tagged Types
Every field in a contract can carry one or more semantic tags that describe what the data means, not just what shape it takes. A field declared as ssn as string tagged pii restricted tells the system two things beyond the type: this is personally identifiable information, and it must never leave the processing boundary.
Tags are user-definable. A healthcare contract might define phi for protected health information. A financial services contract might define material-nonpublic. The language provides the mechanism; the domain provides the vocabulary.
Fields can carry multiple tags, and tags apply to both input and output boundaries. Tags on the input side classify incoming data. Tags on the output side declare the expected classification of the result, enabling verification that a transformation didn't accidentally elevate something's sensitivity.

#### Tag Propagation
Tags are sticky by default. If you concatenate a restricted string with another string, the result is restricted. If you place a restricted value inside a record, that field is restricted. If a conditional could return a tagged value in any branch, the result carries that tag. The taint follows the data through every operation.
This means a malicious or careless implementation cannot launder sensitive data through simple transformations. $ssn + " " is still restricted. upper($name) is still PII. There is no implicit way to strip a tag.
#### Sanitizers
The only way to remove a tag from a value is through a declared sanitizer. Sanitizers are functions that the contract author explicitly blesses as producing output where the original sensitive value is irrecoverable. They are declared in the contract, not in the implementation:
sanitizers
  hash strips restricted pii,
  bracket-age strips restricted pii,
  extract-year strips restricted
end
This separation is critical. The person writing the function cannot invent new sanitizers — only the contract author can. A security reviewer can look at the contract alone and see exactly which functions are trusted to handle which categories of sensitive data. If someone declares identity as a sanitizer that strips restricted, that's an obvious red flag caught at review time, not at runtime.
#### Tag-Aware Rules
Contract rules can operate on tag categories rather than individual fields:
rules
  forbid tagged restricted in output
  forbid tagged pii in output
end
These rules automatically cover every field carrying those tags, including fields added in the future. A new restricted field added to the input schema tomorrow is immediately protected by existing rules without anyone remembering to write a new forbid line.
Defense in Depth
The tag system creates multiple layers of protection. Tags classify data at the boundary. Propagation tracks sensitivity through every computation. Sanitizers create explicit, auditable chokepoints where sensitive data is transformed. Rules enforce constraints against tag categories. And because Heluna functions are pure — no network calls, no file access, no side channels — there is no way for data to escape except through the output record, which the contract fully controls.
This means that even if an LLM generates a function with a subtle bug, or a malicious actor attempts to exfiltrate data through a cleverly constructed transformation, the tag system catches it. The data cannot reach the output without passing through a declared sanitizer, and the contract makes every trust decision visible and auditable.

### Readable Over Terse
Heluna uses English keywords where most languages use symbols. through instead of |>. and instead of &&. match...when...then...end instead of curly braces and arrows. result instead of implicit returns. The language reads almost like structured prose.

This is a deliberate choice for LLM collaboration. An LLM encountering Heluna for the first time in a context window can infer the meaning of most constructs from the keywords alone.

### Shared Syntax Between Logic and Validation
Pattern matching, boolean expressions, and the type system are identical in the language and in contracts. The same match...when...then...end that controls program flow also validates output fields. The same between 0 and 150 that guards a conditional also enforces a range constraint. One grammar serves both purposes, which means one grammar fits in one context window.

### No Null
There is no null in Heluna. Optional values are expressed through the maybe type, and absence is represented by nothing. Pattern matching makes handling optional values explicit — you must account for the nothing case. This eliminates an entire class of bugs where unexpected nulls cascade through a program.

### No Shadowing
Every variable binding within a function must have a unique name. You cannot reuse a name to mean something different later. This prevents subtle bugs — both from human programmers and from LLMs — where a variable silently changes meaning partway through a function.

### Small, Composable Pieces
Heluna encourages small functions that do one thing. If a transformation requires multiple steps, it should be broken into multiple functions, each with its own contract. Functions compose by passing records between them, and pipelines using through chain transformations cleanly without nesting.

### Designed for LLMs
The entire Heluna specification — grammar, standard library, and worked examples — is designed to fit within a single LLM context window alongside the user's actual work. Key properties that enable this:

* The language is small. There are roughly 40 reserved words and 25 core standard library functions. The complete grammar fits in a few hundred lines.
* The language is uniform. Every function looks the same. Every contract looks the same. There are no special cases or alternative syntaxes for the same concept.
* The language is explicit. Nothing is implicit — not returns, not types in contracts, not the handling of absent values. An LLM generating Heluna code is forced to be precise about what it means.
* The language is verifiable. Every generated function comes with a contract that can be checked automatically. If the LLM gets something wrong, the contract catches it.

### What Heluna Is Not
Heluna is not a replacement for Python, JavaScript, or any general-purpose language. It does not handle I/O, concurrency, or user interfaces. It is not designed for large applications or long-running processes.
Heluna is a small, safe, verifiable language for transforming data — designed to be written by humans and machines together, and to be trusted with sensitive information.


