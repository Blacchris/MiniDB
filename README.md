# MiniDB — Experiment 0: Flat-File Persistence for `Hashmap<int>`

**Status:** Implementation complete. Test harness, ASan run, and timing experiment pending (planned before Phase 1).

---

## 1. Purpose

Make the existing in-memory `Hashmap<int>` (string keys, `int` values, separate chaining) survive process exit, using the simplest possible mechanism: a length-prefixed binary flat file.

This is a deliberately naive design. Its job is to expose, by direct experience, the limitations that justify Phase 1 (fixed-size pages and a DiskManager). No feature here exists without a forcing reason.

## 2. Scope

**In scope**
- `serializeToFile(map, path)`: write every live entry to one file.
- `deserializeFromFile(path)`: rebuild a `Hashmap<int>` by replaying `put()`.
- Binary, length-prefixed layout, little-endian.
- Round-trip correctness as the only success criterion.

**Deliberately out of scope** (each is a limitation that motivates a later phase)
- Random access into the file (Phase 1).
- Crash safety / atomicity (WAL, much later).
- Space reuse, fixed-size records (Phases 2–3).
- Generic value types. Target is one concrete instantiation, `Hashmap<int>`.

## 3. Changes to `Hashmap`

### 3.1 Why a change was required
The class exposed only single-key operations (`put`, `get`, `containsKey`). There was no way to enumerate stored entries, so serialization was impossible without reaching into `buckets` from outside. This was the forcing limitation.

### 3.2 `forEach`

```cpp
// forEach: visits every live key/value pair exactly once.
// No ordering guarantee — bucket order is by hashfunction(key),
// and within a bucket, chain order is most-recently-inserted-first
// (put() prepends). Do not rely on insertion order or sorted order.
template<typename Func>
void forEach(Func callback) const {
    for (size_t i = 0; i < SIZE_; ++i) {
        Entry* current = buckets[i];
        while (current != nullptr) {
            callback(current->key, current->value);
            current = current->next;
        }
    }
}
```

Design decisions:
- **Templated callable** rather than `std::function`: the lambda can be inlined at the call site, with no heap allocation or indirect call.
- **Callback takes `const string&, const value_type&`**: no copies just to read.
- **`const` method**: it only reads. Inside a const method the `buckets` pointer is const but the `Entry` nodes it points to are not, so traversal works without `mutable`.
- **No early exit**: no caller needs it yet.
- **No ordering guarantee**, stated in the comment so no caller assumes one.

## 4. On-Disk Format

All integers little-endian.

```
[ num_entries : uint32_t ]                      header, 4 bytes
repeated num_entries times:
[ key_len : uint32_t ][ key_bytes : key_len ][ value : int32_t ]
```

- `key_bytes` has **no null terminator**; the length prefix delimits it.
- `value` has **no length prefix**: `int` is fixed at 4 bytes. If `value_type` ever becomes variable-length (e.g. `string`), the format and serializer must change.
- `num_entries` is `uint32_t` (4 bytes), not `size_t` (8 bytes on x86-64). `map.size()` is narrowed with an explicit `static_cast`.

### Worked example

`Hashmap<int>` containing `"cat" -> 3` and `"dog" -> 7`:

```
Offset  Bytes  Field              Value
------  -----  -----------------  ----------------------
0       4      num_entries        02 00 00 00   (= 2)

4       4      entry[0].key_len   03 00 00 00   (= 3)
8       3      entry[0].key       63 61 74      ('c' 'a' 't')
11      4      entry[0].value     03 00 00 00   (= 3)

15      4      entry[1].key_len   03 00 00 00   (= 3)
19      3      entry[1].key       64 6F 67      ('d' 'o' 'g')
22      4      entry[1].value     07 00 00 00   (= 7)
------
26 bytes total
```

Total size formula: `4 + sum over entries of (4 + key_len + 4)`.

Entry boundaries exist only because each key's length is known before its bytes are read. Locating entry *k* requires parsing entries 0 through *k-1*.

## 5. Implementation

### 5.1 `serializeToFile`

```cpp
void serializeToFile(const Hashmap<int>& map, const string& path) {
    ofstream out(path, ios::binary);
    if (!out.is_open()) {
        throw std::runtime_error("serializeToFile: failed to open " + path);
    }

    uint32_t num_entries = static_cast<uint32_t>(map.size());
    out.write(reinterpret_cast<const char*>(&num_entries), sizeof(num_entries));

    map.forEach([&out](const string& key, const int& val) {
        uint32_t key_len = static_cast<uint32_t>(key.size());
        out.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
        out.write(key.data(), key_len);
        out.write(reinterpret_cast<const char*>(&val), sizeof(val));
    });

    if (!out.good()) {
        throw std::runtime_error("serializeToFile: write failed for " + path);
    }
    // ofstream closes on scope exit (RAII).
}
```

Notes:
- `ios::binary` prevents text-mode newline translation from corrupting the data.
- `ofstream` does not throw on open failure; it enters a failed state and every later `write()` silently no-ops. Hence the `is_open()` and `good()` checks.
- `key.data()` is the string's real contiguous buffer; `reinterpret_cast` is used only for writing the byte representation of POD integers.

### 5.2 `deserializeFromFile`

```cpp
Hashmap<int> deserializeFromFile(const string& path) {
    ifstream in(path, ios::binary);
    if (!in.is_open()) {
        throw std::runtime_error("deserializeFromFile: failed to open " + path);
    }

    uint32_t num_entries = 0;
    in.read(reinterpret_cast<char*>(&num_entries), sizeof(num_entries));
    if (!in.good()) {
        throw std::runtime_error("deserializeFromFile: failed to read header");
    }

    Hashmap<int> map;

    for (uint32_t i = 0; i < num_entries; ++i) {
        uint32_t key_len = 0;
        in.read(reinterpret_cast<char*>(&key_len), sizeof(key_len));
        if (!in.good()) {
            throw std::runtime_error("deserializeFromFile: truncated key_len at entry " + std::to_string(i));
        }

        string key(key_len, '\0');
        in.read(&key[0], key_len);
        if (in.gcount() != static_cast<std::streamsize>(key_len)) {
            throw std::runtime_error("deserializeFromFile: truncated key bytes at entry " + std::to_string(i));
        }

        int val = 0;
        in.read(reinterpret_cast<char*>(&val), sizeof(val));
        if (in.gcount() != sizeof(val)) {
            throw std::runtime_error("deserializeFromFile: truncated value at entry " + std::to_string(i));
        }

        map.put(key, val);
    }

    return map;
}
```

Notes:
- `istream::read` does not throw on a short read; it sets failbit. Comparing `gcount()` to the requested length is what turns a truncated or corrupted file into an error instead of a silently wrong map.
- `&key[0]` is a writable pointer into the string's contiguous storage (guaranteed since C++11).
- The map is rebuilt through the public `put()` API only. Bucket layout and chain order in the rebuilt map may differ from the original; correctness is defined by key/value set equality, not physical layout.

## 6. Review Findings (first draft of `serializeToFile`)

| # | Severity | Issue | Resolution |
|---|----------|-------|------------|
| 1 | Fatal | `out.close()` inside the `forEach` lambda. The stream closed after the first entry; every later `write()` silently failed. | Removed; RAII closes on scope exit. |
| 2 | Undefined behavior | `&key_bytes`, where `key_bytes = key.cbegin()`, is the address of the *iterator object*, not the character buffer. `write()` then read `key.size()` bytes from that stack address, running past the iterator into adjacent memory. | Replaced with `key.data()`. |
| 3 | Spec deviation | Header written as `uint64_t` (8 bytes) instead of the locked `uint32_t` (4 bytes), shifting every downstream offset. | Reverted to `uint32_t` with explicit cast. |
| 4 | Missing checks | No `is_open()` / `good()` verification. | Added. |

Takeaway for #2: an iterator is a class type, not a pointer. `&iterator` and `&buffer[0]` are unrelated addresses. ASan or Valgrind would have flagged this immediately.

## 7. Known Issues (open)

1. **Rule of Five gap in `Hashmap` (live risk).** `Hashmap` has a user-declared destructor but no copy or move constructor, so the compiler generates a shallow, raw-pointer copy. `return map;` in `deserializeFromFile` returns a named local by value. NRVO will almost certainly elide the copy, but for a named local it is *permitted*, not *guaranteed*. If it does not fire, both objects own the same `buckets` array and the destructors double-free. Do not fix preemptively: run the harness under ASan and let the result decide.
2. **Symbol corruption from a bad find-and-replace.** `getvalue_typealue` (a method) and comments reading `value_typeoid clear()` / `value_typeisualize` are artifacts of a naive text substitution of `V`. Self-consistent, so it compiles, but it should be cleaned up.
3. **Weak hash function.** `hashfunction` sums character codes mod `SIZE_`; anagrams collide. Expect long chains in `inspect()`.
4. **Double lookup.** `containsKey()` followed by `get()` hashes and walks the chain twice.
5. **Narrowing assumption.** `num_entries` truncates above ~4 billion entries. Irrelevant at current scale.

## 8. Test Plan (pending)

Hand-rolled asserts, no framework. Build with `-fsanitize=address -g`.

1. Empty map: round-trip yields `empty() == true`; file is exactly 4 bytes.
2. Single entry: same key, same value.
3. Enough `put()` calls to force at least one `reharsh()` before serializing, confirming `forEach` is correct after a rehash.
4. Full comparison: same `size()` and identical key/value set before and after (not bucket layout).
5. Error paths: truncated file (cut mid-key, mid-value, mid-header) must throw, not return a partial map.

## 9. The Forcing Problem (to be measured)

`serializeToFile` has one operation: rewrite the entire file from entry 0. Because entry *k*'s offset depends on the sizes of entries 0..k-1, there is no "update one record" path.

At roughly 20 bytes per entry, 50,000 entries is about 1 MB. One `put()` followed by one `serializeToFile` writes ~1 MB to persist a few bytes of new information, about a 50,000:1 write amplification. In-memory `put()` is O(1) amortized; durability on top of it is O(n).

The round-trip tests call serialize once per test and will not expose this. A workload will:

- Throwaway timing loop: insert entries one at a time (or in small batches), call `serializeToFile` after each, and time each call with `std::chrono::steady_clock`.
- Expected result: per-call time grows roughly linearly with n; cumulative time over N insert cycles grows roughly quadratically.

That measurement is the problem Phase 1 answers.

## 10. Next: Phase 1 — DiskManager

Fixed 4096-byte pages addressed by index: `offset = page_id * PAGE_SIZE`. Updating a page touches exactly one page, at a cost independent of total entry count.

```cpp
class DiskManager {
public:
    static constexpr size_t PAGE_SIZE = 4096;
    using page_id_t = uint32_t;

    void readPage(page_id_t id, char* dest);
    void writePage(page_id_t id, const char* src);
    page_id_t allocatePage();

private:
    fstream file_;
    page_id_t num_pages_;
};
```

Decisions to lock before coding:
- `fstream` opened with `ios::in | ios::out | ios::binary` (read and write on one handle for seek-based access).
- `allocatePage()` is append-only (`return num_pages_++`); no free list until deletes exist.
- Page contents are opaque bytes; `DiskManager` knows nothing about keys, records, or `Hashmap`.
- Decide whether the constructor creates a missing file or requires it to exist.
