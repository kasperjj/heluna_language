# Heluna Standard Library Reference

## Function Index

| Category | Functions |
|----------|-----------|
| **String** | `upper` `lower` `trim` `trim-start` `trim-end` `substring` `replace` `split` `join` `starts-with` `ends-with` `contains` `length` `pad-left` `pad-right` `regex-match` `regex-replace` |
| **Numeric** | `abs` `ceil` `floor` `round` `min` `max` `clamp` |
| **List** | `sort` `sort-by` `reverse` `unique` `flatten` `zip` `range` `slice` |
| **Record** | `keys` `values` `merge` `pick` `omit` |
| **Date/Time** | `parse-date` `format-date` `date-diff` `date-add` `now-date` |
| **Encoding** | `base64-encode` `base64-decode` `url-encode` `url-decode` `json-encode` `json-parse` |
| **Crypto** | `sha256` `hmac-sha256` `uuid` |
| **Conversion** | `to-string` `to-float` `to-integer` |
| **Iteration** | `fold` |

---

Every stdlib function follows the same calling convention: it takes a single record argument and returns a value.

```heluna
upper({ value: "hello" })           # returns "HELLO"
length({ list: [1, 2, 3] })         # returns 3
```

When used in a pipeline with `through`, the left-hand value is injected as the `value` field:

```heluna
$name through upper({})              # equivalent to upper({ value: $name })
```

---

## String Functions

### upper

Converts a string to uppercase.

**Input:** `{ value: string }`
**Returns:** `string`

```heluna
upper({ value: "hello" })           # "HELLO"
```

### lower

Converts a string to lowercase.

**Input:** `{ value: string }`
**Returns:** `string`

```heluna
lower({ value: "Hello World" })     # "hello world"
```

### trim

Removes leading and trailing whitespace.

**Input:** `{ value: string }`
**Returns:** `string`

```heluna
trim({ value: "  hello  " })        # "hello"
```

### trim-start

Removes leading whitespace only.

**Input:** `{ value: string }`
**Returns:** `string`

```heluna
trim-start({ value: "  hello  " })  # "hello  "
```

### trim-end

Removes trailing whitespace only.

**Input:** `{ value: string }`
**Returns:** `string`

```heluna
trim-end({ value: "  hello  " })    # "  hello"
```

### substring

Extracts a substring between codepoint indices.

**Input:** `{ value: string, start: integer, end: integer }`
**Returns:** `string`

```heluna
substring({ value: "hello world", start: 0, end: 5 })   # "hello"
substring({ value: "hello world", start: 6, end: 11 })  # "world"
```

Negative `start` is clamped to 0. `end` beyond the string length is clamped. If `start > end`, returns an empty string.

### replace

Replaces all occurrences of a substring.

**Input:** `{ value: string, find: string, replacement: string }`
**Returns:** `string`

```heluna
replace({ value: "hello world", find: "world", replacement: "there" })
# "hello there"
```

An empty `find` string returns the original unchanged.

### split

Splits a string by a delimiter.

**Input:** `{ value: string, delimiter: string }`
**Returns:** `list of string`

```heluna
split({ value: "a,b,c", delimiter: "," })      # ["a", "b", "c"]
split({ value: "hello", delimiter: "" })         # ["h", "e", "l", "l", "o"]
```

### join

Joins a list of strings with a delimiter.

**Input:** `{ list: list of string, delimiter: string }`
**Returns:** `string`

```heluna
join({ list: ["a", "b", "c"], delimiter: ", " })  # "a, b, c"
join({ list: ["hello", "world"], delimiter: " " }) # "hello world"
```

### starts-with

Tests whether a string starts with a prefix.

**Input:** `{ value: string, prefix: string }`
**Returns:** `boolean`

```heluna
starts-with({ value: "hello", prefix: "hel" })    # true
starts-with({ value: "hello", prefix: "world" })   # false
```

### ends-with

Tests whether a string ends with a suffix.

**Input:** `{ value: string, suffix: string }`
**Returns:** `boolean`

```heluna
ends-with({ value: "hello.txt", suffix: ".txt" })  # true
```

### contains

Tests whether a string contains a substring.

**Input:** `{ value: string, substring: string }`
**Returns:** `boolean`

```heluna
contains({ value: "hello world", substring: "world" })  # true
```

### length

Returns the length of a string (in codepoints) or a list (in elements).

**Input:** `{ value: string }` or `{ list: list }`
**Returns:** `integer`

```heluna
length({ value: "hello" })       # 5
length({ list: [10, 20, 30] })   # 3
```

If both fields are present, `list` takes priority.

### pad-left

Pads a string to a target width by prepending fill characters.

**Input:** `{ value: string, width: integer, fill: string }`
**Returns:** `string`

```heluna
pad-left({ value: "42", width: 5, fill: "0" })   # "00042"
pad-left({ value: "hello", width: 3, fill: "x" }) # "hello" (already >= width)
```

### pad-right

Pads a string to a target width by appending fill characters.

**Input:** `{ value: string, width: integer, fill: string }`
**Returns:** `string`

```heluna
pad-right({ value: "hi", width: 5, fill: "." })  # "hi..."
```

### regex-match

Tests whether a string matches an extended POSIX regular expression.

**Input:** `{ value: string, pattern: string }`
**Returns:** `boolean`

```heluna
regex-match({ value: "hello123", pattern: "^[a-z]+[0-9]+$" })  # true
regex-match({ value: "hello", pattern: "^[0-9]+$" })            # false
```

Returns an error if the pattern is invalid.

### regex-replace

Replaces all matches of a POSIX regex with a fixed string.

**Input:** `{ value: string, pattern: string, replacement: string }`
**Returns:** `string`

```heluna
regex-replace({ value: "abc 123 def", pattern: "[0-9]+", replacement: "NUM" })
# "abc NUM def"
```

No backreference support — the replacement is used literally.

---

## Numeric Functions

### abs

Returns the absolute value.

**Input:** `{ value: number }`
**Returns:** `number` (same type as input)

```heluna
abs({ value: -5 })     # 5
abs({ value: -3.14 })  # 3.14
```

### ceil

Rounds up to the nearest integer.

**Input:** `{ value: number }`
**Returns:** `integer`

```heluna
ceil({ value: 3.2 })   # 4
ceil({ value: -1.8 })  # -1
```

### floor

Rounds down to the nearest integer.

**Input:** `{ value: number }`
**Returns:** `integer`

```heluna
floor({ value: 3.9 })   # 3
floor({ value: -1.2 })  # -2
```

### round

Rounds to the nearest integer (half up).

**Input:** `{ value: number }`
**Returns:** `integer`

```heluna
round({ value: 3.5 })   # 4
round({ value: 3.4 })   # 3
```

### min

Returns the smaller of two values.

**Input:** `{ a: number, b: number }`
**Returns:** `number`

```heluna
min({ a: 3, b: 7 })         # 3
min({ a: 1.5, b: 2.5 })     # 1.5
```

### max

Returns the larger of two values.

**Input:** `{ a: number, b: number }`
**Returns:** `number`

```heluna
max({ a: 3, b: 7 })         # 7
max({ a: 1.5, b: 2.5 })     # 2.5
```

### clamp

Clamps a value to a range.

**Input:** `{ value: number, min: number, max: number }`
**Returns:** `number`

```heluna
clamp({ value: 15, min: 0, max: 10 })    # 10
clamp({ value: -5, min: 0, max: 10 })    # 0
clamp({ value: 5, min: 0, max: 10 })     # 5
```

---

## List Functions

### sort

Sorts a list in ascending order.

**Input:** `{ list: list }`
**Returns:** `list`

```heluna
sort({ list: [3, 1, 2] })           # [1, 2, 3]
sort({ list: ["c", "a", "b"] })     # ["a", "b", "c"]
```

### sort-by

Sorts a list of records by a named field.

**Input:** `{ list: list of record, field: string }`
**Returns:** `list of record`

```heluna
sort-by({ list: [{ name: "Bob", age: 30 }, { name: "Ada", age: 25 }], field: "name" })
# [{ name: "Ada", age: 25 }, { name: "Bob", age: 30 }]
```

### reverse

Reverses the order of elements.

**Input:** `{ list: list }`
**Returns:** `list`

```heluna
reverse({ list: [1, 2, 3] })        # [3, 2, 1]
```

### unique

Removes duplicate values, keeping the first occurrence.

**Input:** `{ list: list }`
**Returns:** `list`

```heluna
unique({ list: [1, 2, 2, 3, 1] })   # [1, 2, 3]
```

### flatten

Flattens one level of nested lists.

**Input:** `{ list: list of list }`
**Returns:** `list`

```heluna
flatten({ list: [[1, 2], [3, 4], [5]] })  # [1, 2, 3, 4, 5]
```

Non-list elements are kept as-is. Only flattens one level deep.

### zip

Pairs elements from two lists into records.

**Input:** `{ a: list, b: list }`
**Returns:** `list of record`

```heluna
zip({ a: [1, 2, 3], b: ["x", "y", "z"] })
# [{ a: 1, b: "x" }, { a: 2, b: "y" }, { a: 3, b: "z" }]
```

Stops at the shorter list.

### range

Generates an inclusive integer range.

**Input:** `{ start: integer, end: integer }`
**Returns:** `list of integer`

```heluna
range({ start: 1, end: 5 })         # [1, 2, 3, 4, 5]
range({ start: 3, end: 1 })         # [3, 2, 1]
```

### slice

Extracts a sublist by index range.

**Input:** `{ list: list, start: integer, end: integer }`
**Returns:** `list`

```heluna
slice({ list: [10, 20, 30, 40, 50], start: 1, end: 4 })  # [20, 30, 40]
```

The `start` index is inclusive, `end` is exclusive.

---

## Record Functions

### keys

Extracts all field names from a record.

**Input:** `{ record: record }`
**Returns:** `list of string`

```heluna
keys({ record: { name: "Ada", age: 25 } })    # ["name", "age"]
```

### values

Extracts all field values from a record.

**Input:** `{ record: record }`
**Returns:** `list`

```heluna
values({ record: { name: "Ada", age: 25 } })  # ["Ada", 25]
```

### merge

Merges two records. Fields from the second record override the first.

**Input:** `{ a: record, b: record }`
**Returns:** `record`

```heluna
merge({ a: { x: 1, y: 2 }, b: { y: 3, z: 4 } })
# { x: 1, y: 3, z: 4 }
```

### pick

Keeps only the named fields from a record.

**Input:** `{ record: record, fields: list of string }`
**Returns:** `record`

```heluna
pick({ record: { name: "Ada", age: 25, email: "ada@example.com" }, fields: ["name", "email"] })
# { name: "Ada", email: "ada@example.com" }
```

### omit

Removes the named fields from a record.

**Input:** `{ record: record, fields: list of string }`
**Returns:** `record`

```heluna
omit({ record: { name: "Ada", age: 25, email: "ada@example.com" }, fields: ["email"] })
# { name: "Ada", age: 25 }
```

---

## Date/Time Functions

All date/time functions use ISO 8601 format (`YYYY-MM-DDTHH:MM:SS`) for string representations.

### parse-date

Parses a date string into a record of components.

**Input:** `{ value: string, format: string }`
**Returns:** `record`

```heluna
parse-date({ value: "2024-03-15", format: "%Y-%m-%d" })
# { year: 2024, month: 3, day: 15, hour: 0, minute: 0, second: 0 }
```

The `format` string uses `strftime` conventions (e.g., `%Y` for four-digit year, `%m` for month, `%d` for day).

### format-date

Formats a date record into a string.

**Input:** `{ date: record, format: string }`
**Returns:** `string`

The date record must contain `year`, `month`, `day`, `hour`, `minute`, `second` fields.

```heluna
format-date({
  date: { year: 2024, month: 3, day: 15, hour: 0, minute: 0, second: 0 },
  format: "%Y-%m-%d"
})
# "2024-03-15"
```

### date-diff

Calculates the difference between two ISO 8601 date strings.

**Input:** `{ from: string, to: string, unit: string }`
**Returns:** `integer`

```heluna
date-diff({
  from: "2024-01-01T00:00:00",
  to: "2024-01-02T00:00:00",
  unit: "hours"
})
# 24
```

Supported units: `"seconds"`, `"minutes"`, `"hours"`, `"days"`.

### date-add

Adds a duration to an ISO 8601 date string.

**Input:** `{ date: string, amount: integer, unit: string }`
**Returns:** `string`

```heluna
date-add({ date: "2024-01-15T00:00:00", amount: 10, unit: "days" })
# "2024-01-25T00:00:00Z"
```

Supported units: `"seconds"`, `"minutes"`, `"hours"`, `"days"`, `"months"`, `"years"`.

### now-date

Returns the current execution timestamp.

**Input:** `{}`
**Returns:** `string`

```heluna
now-date({})                         # "2024-01-01T00:00:00Z"
```

Returns the logical timestamp provided by the host at execution start. Multiple calls within the same execution return the same value.

---

## Encoding Functions

### base64-encode

Encodes a string as Base64.

**Input:** `{ value: string }`
**Returns:** `string`

```heluna
base64-encode({ value: "hello" })    # "aGVsbG8="
```

### base64-decode

Decodes a Base64 string.

**Input:** `{ value: string }`
**Returns:** `string`

```heluna
base64-decode({ value: "aGVsbG8=" }) # "hello"
```

### url-encode

Percent-encodes a string for use in URLs.

**Input:** `{ value: string }`
**Returns:** `string`

```heluna
url-encode({ value: "hello world" }) # "hello%20world"
```

Unreserved characters (alphanumeric, `-`, `_`, `.`, `~`) are not encoded.

### url-decode

Decodes a percent-encoded string.

**Input:** `{ value: string }`
**Returns:** `string`

```heluna
url-decode({ value: "hello%20world" })  # "hello world"
```

Also converts `+` to space (form encoding convention).

### json-encode

Serializes any value to a JSON string.

**Input:** `{ value: any }`
**Returns:** `string`

```heluna
json-encode({ value: { name: "Ada", age: 25 } })
# "{\"name\":\"Ada\",\"age\":25}"
```

### json-parse

Parses a JSON string into a value.

**Input:** `{ value: string }`
**Returns:** the parsed value

```heluna
json-parse({ value: "{\"x\":1}" })   # { x: 1 }
```

Returns an error if the JSON is invalid.

---

## Cryptographic Functions

### sha256

Computes the SHA-256 hash of a string.

**Input:** `{ value: string }`
**Returns:** `string` (64-character lowercase hex)

```heluna
sha256({ value: "hello" })
# "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824"
```

### hmac-sha256

Computes an HMAC-SHA256 message authentication code.

**Input:** `{ value: string, key: string }`
**Returns:** `string` (64-character lowercase hex)

```heluna
hmac-sha256({ value: "hello", key: "secret" })
# "88aab3ede8d3adf94d26ab90d3bafd4a2083070c3bcce9c014ee04a443847c0b"
```

### uuid

Generates a random UUID v4.

**Input:** `{}`
**Returns:** `string` (36-character UUID)

```heluna
uuid({})                             # "a1b2c3d4-e5f6-4a7b-8c9d-0e1f2a3b4c5d"
```

Each call produces a unique value.

---

## Conversion Functions

### to-string

Converts a value to its string representation.

**Input:** `{ value: any }`
**Returns:** `string`

```heluna
to-string({ value: 42 })            # "42"
to-string({ value: 3.14 })          # "3.14"
to-string({ value: true })          # "true"
to-string({ value: nothing })       # "nothing"
```

### to-float

Converts a value to a float.

**Input:** `{ value: integer | float | string }`
**Returns:** `float`

```heluna
to-float({ value: 42 })             # 42.0
to-float({ value: "3.14" })         # 3.14
```

### to-integer

Converts a value to an integer (truncates floats).

**Input:** `{ value: integer | float | string }`
**Returns:** `integer`

```heluna
to-integer({ value: 3.9 })          # 3
to-integer({ value: "42" })         # 42
```

---

## Iteration

### fold

Reduces a list to a single value using a named operation.

**Input:** `{ list: list, initial: any, fn: string }`
**Returns:** the accumulated value

```heluna
fold({ list: [1, 2, 3, 4], initial: 0, fn: "add" })        # 10
fold({ list: [1, 2, 3, 4], initial: 1, fn: "multiply" })    # 24
```

Supported `fn` values: `"add"`, `"multiply"`.

If any element is a float, the result is promoted to float. Returns an integer only if the initial value and all list elements are integers.
