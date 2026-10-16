<<<<<<< HEAD
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
=======
# MiniDB

A database engine built from scratch in C++ to learn storage-engine internals from first principles.

This README is written to be re-read later. It records what exists, why it is shaped the way it is, what went wrong, and (where I have filled it in) how I fixed it.

- Status as of: 2026-10-16
- Known source files: `MiniDB/src/diskmanager.h`, `MiniDB/src/diskmanager.cpp`
- Longer Experiment 0 write-up: `minidb-experiment-0.md`

---

## 1. Status

| Component | State |
|---|---|
| `Hashmap<int>` (separate chaining) | Done. `forEach` added so entries can be traversed. |
| Experiment 0: flat-file persistence | `serializeToFile` and `deserializeFromFile` implemented. Round-trip harness, ASan run, and timing loop **not yet done**. |
| Phase 1: `DiskManager` | In progress. Constructor and destructor written and reviewed (section 7.2). `readPage`, `writePage`, `allocatePage`, `sync` are planned and not covered by this document. |

## 2. How I work on this project

The loop for every subsystem:

1. Pick the failure the subsystem exists to solve.
2. State the invariant that must hold.
3. Build the smallest experiment that faces the failure.
4. Write a failing test.
5. Read targeted material, then try my own fix first, then compare.

Rules that follow from it:
- No feature without a forcing limitation. No chapter is read before the problem it solves has been felt.
- Tests are a hand-rolled harness (plain asserts) before any external framework.
- Every new phase starts with a layout sketch using one concrete running example with real values, before any code.
- Build with `-fsanitize=address -g` while developing.

## 3. Roadmap

Experiment 0 (flat file) -> Phase 1 (DiskManager, fixed 4096-byte pages) -> Phase 2 (slotted pages / records) -> Phase 3 (heap table) -> Phase 4 (buffer pool) -> Phase 5 (B+ tree) -> Phase 6 (query execution) -> Phase 7 (SQL) -> later: transactions, concurrency, WAL / recovery.

Crash safety is a thread running through all of it, not a late add-on. The standing question is: *if the process dies in the middle of persisting a `put`, what is on disk after restart?*

### Main goal

Understand how a database engine works from first principles by building one, not to ship a product. MiniDB is a stepping stone in my low-level systems path toward GPU/TPU kernel and performance engineering: it trains reasoning about how data moves between memory and disk, what each layer guarantees, and what survives a failure.

### Milestone 1 (the finish line before B+ trees and SQL)

**A durable key-value engine built on pages that passes crash-injection tests at every write point.**

Done means:
- [ ] `DiskManager` complete: `readPage`, `writePage`, `allocatePage`, `sync`, with the constructor bugs in section 7.2 fixed.
- [ ] `put` / `get` work on top of pages (what sits between `DiskManager` and the key-value API is decided when the problem forces it).
- [ ] Invariant holds after restart: the old value or the fully committed new value, never corruption and never a thrown error from a valid file.
- [ ] Crash-injection harness kills the process at every write point of a `put` and the invariant holds each time.
- [ ] Section 8's questions answered in my own words and checked by experiment.

Open decision: whether power loss (as opposed to process crash) is in scope for Milestone 1 or is the first step after it. Process crash and power loss are separate failure models and cannot be tested the same way.

Everything after Milestone 1 (heap table, buffer pool, B+ tree, query execution, SQL, transactions) is built on a base whose durability I can already prove.

## 4. Architecture

Planned layering (each layer only knows the one below it):

```
SQL
 -> query execution
   -> B+ tree / heap table
     -> buffer pool
       -> slotted pages (records inside a page)
         -> DiskManager (raw 4096-byte pages by page_id)
           -> file
```

What exists today is two separate paths:

```
Experiment 0:  Hashmap<int> --serializeToFile--> one flat file (rewritten in full each time)
Phase 1:       DiskManager --pread/pwrite--> file made of 4096-byte pages
```

**Boundary rule:** `DiskManager` knows pages and `page_id`s. It does not know about keys, records, or `Hashmap`. Page contents are opaque bytes at this layer. Record awareness belongs to Phase 2.

### Page addressing

```
offset = page_id * PAGE_SIZE        (PAGE_SIZE = 4096)

page_id  byte range
-------  ---------------
0        [0,     4096)
1        [4096,  8192)
2        [8192,  12288)
3        [12288, 16384)
```

Reading page 3 means reading exactly 4096 bytes at offset 12288. Writing page 3 touches only those bytes, and pages 0-2 and 4+ are never read or written. In-place update is the whole point of the phase.

Limit: `page_id_t` is `uint32_t`, so at most 2^32 pages x 4096 bytes = 16 TiB per file.

## 5. On-disk formats

### 5.1 Experiment 0 flat file (little-endian)

```
[ num_entries : uint32_t ]
>>>>>>> f388899 (New update -> Diskmanager initialization)
repeated num_entries times:
[ key_len : uint32_t ][ key_bytes : key_len ][ value : int32_t ]
```

<<<<<<< HEAD
- `key_bytes` has **no null terminator**; the length prefix delimits it.
- `value` has **no length prefix**: `int` is fixed at 4 bytes. If `value_type` ever becomes variable-length (e.g. `string`), the format and serializer must change.
- `num_entries` is `uint32_t` (4 bytes), not `size_t` (8 bytes on x86-64). `map.size()` is narrowed with an explicit `static_cast`.

### Worked example

`Hashmap<int>` containing `"cat" -> 3` and `"dog" -> 7`:
=======
Running example, `{"cat" -> 3, "dog" -> 7}`:
>>>>>>> f388899 (New update -> Diskmanager initialization)

```
Offset  Bytes  Field              Value
------  -----  -----------------  ----------------------
0       4      num_entries        02 00 00 00   (= 2)
<<<<<<< HEAD

4       4      entry[0].key_len   03 00 00 00   (= 3)
8       3      entry[0].key       63 61 74      ('c' 'a' 't')
11      4      entry[0].value     03 00 00 00   (= 3)

15      4      entry[1].key_len   03 00 00 00   (= 3)
19      3      entry[1].key       64 6F 67      ('d' 'o' 'g')
22      4      entry[1].value     07 00 00 00   (= 7)
=======
4       4      entry[0].key_len   03 00 00 00
8       3      entry[0].key       63 61 74      ('cat')
11      4      entry[0].value     03 00 00 00
15      4      entry[1].key_len   03 00 00 00
19      3      entry[1].key       64 6F 67      ('dog')
22      4      entry[1].value     07 00 00 00
>>>>>>> f388899 (New update -> Diskmanager initialization)
------
26 bytes total
```

<<<<<<< HEAD
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
=======
Locating entry k requires parsing entries 0..k-1, because offsets depend on cumulative key lengths. That is why this format cannot support in-place update.

### 5.2 Page file (Phase 1)

- The file is exactly `N * 4096` bytes. No header page yet, no free list yet.
- Invariant checked on open: `file_size % 4096 == 0`.
- Page contents are opaque.

## 6. Decision log

Each entry: what I chose, what I rejected, why, what it costs, and when to revisit.

### D1. Start with a naive flat file (Experiment 0)
- **Chosen:** serialize the whole `Hashmap<int>` to one length-prefixed binary file.
- **Rejected:** going straight to pages.
- **Why:** the cost of whole-file rewrite should be felt, not just understood. It is the forcing problem for pages.
- **Cost:** every persist is O(n). One `put` followed by one serialize of 50,000 entries (~1 MB) writes ~1 MB to save a few bytes of new information, roughly 50,000:1 write amplification.
- **Revisit:** after the timing loop shows the cost on my machine (results go in section 9).

### D2. Format details
- **Chosen:** `uint32_t` count header, `uint32_t` key length, raw key bytes with no null terminator, raw `int32_t` value with no length prefix, little-endian.
- **Why no value length prefix:** `int` is fixed 4 bytes. If the value type ever becomes variable-length, the format and serializer must change.
- **Why `uint32_t` count, not `size_t`:** `size_t` is 8 bytes here and would silently shift every offset. The narrowing is done with an explicit `static_cast`.
- **Cost:** truncates above ~4 billion entries. Irrelevant at current scale.

### D3. Traversal via `forEach(callback)`
- **Chosen:** a templated `forEach` on `Hashmap`.
- **Rejected:** `entries()` returning a snapshot `vector<pair<...>>`, and a `std::function` parameter.
- **Why:** no allocation or copy, and the lambda can be inlined at the call site.
- **Cost:** opaque template signature. No early exit (nothing needs it yet). No ordering guarantee: bucket order follows the hash, and chains are newest-first because `put` prepends.

### D4. Serialization as free functions
- **Chosen:** `serializeToFile` and `deserializeFromFile` are free functions that use `forEach`, `size()`, and `put()`.
- **Why:** `Hashmap` should not know file I/O exists.

### D5. Fail loudly with exceptions and explicit stream checks
- **Chosen:** check `is_open()` after opening and `good()` after writing, and throw on failure. In `deserializeFromFile`, compare `gcount()` to the requested length after each read.
- **Why:** `ofstream` does not throw on open failure and `istream::read` does not throw on a short read. They set a failure flag and later operations silently do nothing, so a truncated or corrupt file would otherwise produce a silently wrong result.

### D6. POSIX file descriptors, not `fstream`, for DiskManager
- **Chosen:** `open` / `pread` / `pwrite` / `fsync` / `close` on an `int fd_`.
- **Original plan (wrong):** `fstream` opened `in | out | binary`. I replaced it because `fstream` has no `fsync`, so durability could not be expressed, and the class would have to be rewritten once it mattered.
- **Why POSIX:** `pread`/`pwrite` take an explicit offset, so there is no shared seek position to get wrong. `fsync` is available.
- **Cost:** I now own a raw resource. This directly caused the constructor and copy-semantics bugs in section 7.2.

### D7. Fixed 4096-byte pages
- **Chosen:** `PAGE_SIZE = 4096`.
- **Why:** it is typically the OS virtual-memory page size and a common filesystem block size on x86-64 Linux, so a database page maps cleanly onto units the OS already uses.
- **Revisit:** when the buffer pool and page layout exist and I can measure.

### D8. Append-only `allocatePage()`
- **Chosen:** `return num_pages_++`.
- **Rejected:** a free list.
- **Why:** nothing deletes yet, so nothing reclaims space. Building it now would be speculative.

### D9. Refuse to open a file whose size is not a multiple of `PAGE_SIZE`
- **Chosen:** throw rather than truncate or repair.
- **Why:** a ragged size means the file is not what this format expects, and guessing would hide corruption. What to do about a half-extended file after a crash is a recovery question for later.

### D10. Order: pages first, with conditions
- **Context:** my earlier plan was to fix the flat file first (temp file + rename, fsync ordering), then move to pages. I chose to start pages first.
- **Conditions I accepted:** run the timing loop first so the O(n) cost is felt, and point the crash-injection harness at `writePage` once pages work.
- **What I give up:** the simple-case crash lesson (rename-based atomic replace). The torn-page problem (half a page new, half old) must be met directly instead.

## 7. Bug log

### 7.1 Fixed: first draft of `serializeToFile`

| # | Bug | Why it was wrong | Fix | Lesson |
|---|---|---|---|---|
| E1 | `out.close()` inside the `forEach` lambda | The stream closed after the first entry. Every later `write` silently failed, and a 1-entry test hides it. | Removed. `ofstream` closes itself at scope exit (RAII). | Test with 3+ entries and compare file size to the formula `4 + sum(4 + key_len + 4)`. |
| E2 | `write(&key_bytes, ...)` where `key_bytes = key.cbegin()` | `&key_bytes` is the address of the iterator object, not the string's buffer. `write` then read `key.size()` bytes from that stack address. Undefined behavior. | Use `key.data()`. | An iterator is a class type, not a pointer. ASan catches this class of bug immediately. |
| E3 | Header written as `uint64_t` | The format says `uint32_t`. Every downstream offset shifts by 4 bytes. | Reverted to `uint32_t` with an explicit cast. | A deviation from the format spec must be a decision, not an accident of which type I reached for. |
| E4 | No `is_open()` / `good()` checks | A failed open or write produced a silently empty or truncated file. | Added both checks. | `ofstream` fails quietly by default. |

### 7.2 Open: `DiskManager` constructor and destructor

I am fixing these myself. For each one, fill in **How I fixed it** after solving it, and tick the box.

Sources: B1-B6 came from an external AI review of an earlier revision. B7 came from Claude reading the current code. The review said the destructor only printed a message. The current code already calls `close(fd_)` there, so that part is partly fixed (B2).

#### B1. `throw e.what();` in both catch blocks
- **Problem:** `e.what()` is a `const char*`. Throwing it throws a *different type* than the original exception, so a caller catching `std::system_error` will not catch it. The pointer also refers into the original exception object, which is destroyed when the handler exits, so it dangles.
- **Concept to learn:** throwing a new exception vs rethrowing the one currently being handled. Also, catching by `const&` when only inspecting.
- **Status:** [ ]
- **How I fixed it:** _TODO_

#### B2. Destructor
- **Done:** it now calls `close(fd_)` and sets `fd_ = -1`.
- **Remaining:** (a) decide what happens if `close()` fails. Destructors are implicitly `noexcept`, so throwing from one terminates the program. (b) remove the diagnostic `cout`.
- **Status:** [ ]
- **How I fixed it:** _TODO_

#### B3. Page count truncates before dividing
- **Problem:** `(uint32_t) file_size / PAGE_SIZE` — the cast binds tighter than `/`, so the 64-bit file size is cut to 32 bits first. A 5 GiB file reads as 1 GiB (262,144 pages instead of 1,310,720). A file of exactly 4 GiB reads as **0 pages**, and the next `allocatePage()` would return page 0 and overwrite real data.
- **Concept to learn:** do the arithmetic in a wide type, then check the result fits in `uint32_t` before narrowing. Page *count* and `page_id_t` are different quantities, and allocation must not wrap at 2^32.
- **Status:** [ ]
- **How I fixed it:** _TODO_

#### B4. Cleanup does not cover every exception
- **Problem:** only `std::system_error` and `std::runtime_error` are caught. Any other exception after `open()` succeeds (for example `std::bad_alloc` while building an error message) leaves the descriptor open, and the destructor does not run for a constructor that throws.
- **Two approaches:** (1) a cleanup-and-rethrow path that every exit goes through; (2) a small RAII wrapper that owns the fd and closes it in its destructor. Learn (1) to see why (2) exists.
- **Status:** [ ]
- **How I fixed it:** _TODO_

#### B5. `open()` failure loses the reason
- **Problem:** `throw std::runtime_error("failed to open file at " + path)` says nothing about *why* (permissions, missing directory, ...). POSIX reports the reason through `errno`.
- **Concept to learn:** `std::system_error` carries an `errno`. Save `errno` into a local immediately after the failing call, because building a message string (which can allocate) happens before the exception is constructed. The `fstat` path has the same hazard: `errno` is read in the same expression as the string concatenation.
- **Status:** [ ]
- **How I fixed it:** _TODO_

#### B6. Copying a `DiskManager`
- **Problem:** the class owns a raw descriptor. The default copy copies the integer, so two objects will each try to `close` the same fd.
- **Concept to learn:** Rule of Five. The decision is whether copying should be forbidden, or whether move should transfer ownership. This is the same bug class as the shallow copy I already hit in `Vector` and the one flagged in `Hashmap`.
- **Status:** [ ]
- **How I fixed it:** _TODO_

#### B7. Minor
- Debug `std::cout` output in the constructor and destructor.
- `file_size % PAGE_SIZE` mixes a signed `off_t` with an unsigned `size_t`. It is correct today only because a file size is never negative.
- **Status:** [ ]

### 7.3 Open: `Hashmap` / Experiment 0

- **Rule of Five gap.** `Hashmap` has a destructor but no copy or move constructor, so the compiler-generated copy is a shallow pointer copy. `return map;` in `deserializeFromFile` returns a named local by value. NRVO will almost certainly elide the copy, but for a named local it is permitted, not guaranteed. If it does not fire, two objects own one `buckets` array and the destructors double-free. Plan: do not fix preemptively; run the harness under ASan and let it show whether it fires.
- **Symbol corruption.** `getvalue_typealue` and the comments `value_typeoid` / `value_typeisualize` come from an editor text-replace of `V` with `value_type`. It compiles but should be cleaned up.
- **Weak hash.** `hashfunction` sums character codes mod `SIZE_`, so anagrams collide and chains get long.
- **Double lookup.** `containsKey()` then `get()` hashes and walks the chain twice.

## 8. Questions I must answer myself

These are deliberately unanswered here. Write the answer, then check it with an experiment.

1. `writePage` returns successfully. Where does that data physically sit, and what does `sync()` change about it?
2. If the process dies between `write()` and `fsync()`, what state is the file in? Answer separately for **(a) process crash** and **(b) power loss**.
   - My answer: _TODO_
3. What does a file size that is not a multiple of 4096 tell me about how the previous run ended?
   - My answer: _TODO_
4. What is a torn page, and which of the two failure models in question 2 can cause it?
   - My answer: _TODO_

## 9. Tests and measurements

### Experiment 0 round-trip harness (not yet written)
1. Empty map: round-trip gives `empty() == true`; the file is exactly 4 bytes.
2. Single entry.
3. Enough `put()` calls to force at least one `reharsh()` before serializing.
4. Same `size()` and identical key/value set before and after (bucket layout may differ).
5. Truncated file (cut mid-header, mid-key, mid-value) must throw, not return a partial map.

### Timing loop (the forcing problem)
Insert one entry, call `serializeToFile`, time it with `std::chrono::steady_clock`, repeat, and print time against n.
- Expected: per-call time grows roughly linearly with n; cumulative time grows roughly quadratically.
- My measurements: _TODO_

### DiskManager tests
1. Write distinct patterns to pages 0-9, overwrite only page 5, check every other page is unchanged and the file is exactly `10 * 4096` bytes.
2. Close and reopen. `num_pages_` and the contents must survive.
3. Time `writePage` against page count. It should stay flat, the opposite of the serialize curve.
4. Constructor error paths: nonexistent directory, file with a ragged size, and a failing `fstat`. Each must throw the right exception type and leave no descriptor open.

### Crash-injection (after pages work)
`fork()` a child that runs the write path and kill it at each write point. The parent then reads the file and asserts the invariant: **the old image or the new image, never anything else**. This only simulates *process* crash. Power loss cannot be simulated by killing a process and is a separate failure model.

## 10. Glossary

- **RAII:** a resource is acquired in a constructor and released in the destructor, so cleanup runs on every exit path.
- **NRVO:** the compiler constructs a returned local directly in the caller's storage, skipping a copy. Permitted for named locals, guaranteed only for temporaries (C++17).
- **Rule of Five:** if a class manages a resource, decide its destructor, copy constructor, copy assignment, move constructor, and move assignment together.
- **`pread` / `pwrite`:** read/write at an explicit offset without touching the shared file position.
- **`fsync`:** asks the OS to push a file's data to stable storage and returns when it has.
- **Torn page:** a page write interrupted partway, leaving part old bytes and part new bytes.
- **Write amplification:** bytes physically written divided by bytes of new information.

## 11. Reading

CS:APP 10.1-10.4 (Unix I/O) is the assigned reading for this layer. Per the working method, read it after the failure it explains has been felt, not before.
>>>>>>> f388899 (New update -> Diskmanager initialization)
