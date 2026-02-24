# Heluna

**A pure functional language for safe JSON transformations — designed to be written by humans and LLMs together.**

---

Today's LLMs can generate impressive code, but they struggle with the accumulated complexity of traditional languages — mutable state, implicit returns, null, deep inheritance. That's a limitation of current models, and it won't last forever. LLMs are getting better fast.

Heluna is designed for what comes next: a world where the human focuses on *what* the transformation should do — the contract — and the LLM handles *how*. The contract declares the input schema, output schema, validation rules, security constraints, and test cases. The LLM generates the implementation. And because the contract is machine-verifiable, you don't have to trust the LLM's code — you trust the contract, and the contract proves the code is correct.

### The idea in 30 seconds

Every Heluna function takes JSON in and produces JSON out. No exceptions, no side effects, no I/O. The specification comes first — a **contract** declares the input schema, output schema, validation rules, security constraints, and test cases before a single line of implementation exists.

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

### What makes it different

**Contract-first development.** The contract *is* the documentation, the test harness, and the security boundary. Write the spec, then fill in the implementation.

**Data security as a language feature.** Fields carry semantic tags (`pii`, `restricted`, `phi`) that propagate automatically through every operation. You can't launder sensitive data through string concatenation — tags are sticky. The only way to remove them is through explicitly declared sanitizers. If tagged data reaches the output without being sanitized, the contract rejects it.

**No null, ever.** Optional values use `maybe` and `nothing`. You must handle absence explicitly — no silent nulls sneaking through.

**Readable over terse.** `match...when...then...end` instead of braces and arrows. `and`/`or`/`not` instead of `&&`/`||`/`!`. `through` instead of `|>`. The language reads like structured prose.

**Built for LLMs.** ~50 keywords, ~25 stdlib functions, one universal function signature. The entire spec fits in a single context window. A non-programmer can describe what they want, and an LLM can generate both the contract and the implementation — with the contract serving as a verifiable proof of correctness.

### What Heluna is *not*

Heluna is not a general-purpose programming language. It has no I/O, no concurrency, no mutable state. It's a **computation kernel** — a small, focused engine that lives inside your host process. Your HTTP server, message queue, or cron job handles the messy real world. Heluna handles the transformation, and it does so safely.

---

See [`language_introduction.md`](language_introduction.md) for a practical walkthrough with examples, and [`language_design.md`](language_design.md) for the design rationale.
