# Plan: Upgrade outer sysop to atomic + skip redundant per-page inner sysop in file_alloc on bulk COPY FROM STDIN fast path

Branch: `cubvec/m1-f4-cp`
Mode: DELIBERATE (consensus / RALPLAN-DR) — revised per Critic ITERATE verdict
Scope: bulk-insert (BU-lock) path only — all other callers must remain bit-identical.

---

## 1. Context (verified from source — revised)

### 1.1 Outer sysop in `copy_session::flush_batch` is PLAIN, not atomic (decisive)
File: `src/loaddb/copy_session.cpp:353-394`.

On the fast path (`has_BU_lock && HA_DISABLED()`):
```
log_sysop_start (thread_p);            // line 357 — PLAIN start, NOT atomic (decisive)
  locator_multi_insert_force (...);    // allocates many heap pages + inserts
log_sysop_attach_to_outer (thread_p);  // line 392 — defers sysop completion to outer txn
// on error: log_sysop_abort (thread_p) at line 389
```
Verified facts:
- `log_sysop_start` at `copy_session.cpp:357` is plain — it does NOT emit `LOG_SYSOP_ATOMIC_START` and does NOT set `tdes->rcv.atomic_sysop_start_lsa` (see `log_manager.c:3665-3698`).
- `log_sysop_attach_to_outer` (`log_manager.c:4097-4136`) merges the nested sysop's postpone/undo records into the parent, but provides NO recovery-time atomicity guarantee — on crash, there is no `atomic_sysop_start_lsa` marker for `log_recovery_abort_atomic_sysop` (`log_recovery.c:4267-4376`) to key off.
- Therefore, today's outer sysop on the COPY fast path gives user-visible rollback coverage, but it does NOT satisfy the crash-recovery atomicity contract that `log_sysop_start_atomic` provides.

### 1.2 Inner sysop inside `file_alloc` is ATOMIC (verified)
File: `src/storage/file_manager.c:5466-5487`.
```
log_sysop_start_atomic (thread_p);            // inner sysop — emits LOG_SYSOP_ATOMIC_START
  file_perm_alloc (...);                      // may call file_perm_expand (4690) -> nested sysop COMMITTED
  /* optionally */ file_numerable_add_page (...);
  /* optionally */ f_init via pgbuf_fix NEW_PAGE + f_init (e.g., heap_vpid_init_new)
log_sysop_end_logical_undo (thread_p, RVFL_ALLOC, NULL, UNDO_DATA_SIZE, undo_log_data);
  // logical undo: on rollback, RVFL_ALLOC -> file_rv_dealloc_on_undo -> file_rv_dealloc_internal
```
Key facts:
- Inner sysop is `log_sysop_start_atomic` (stronger than plain). The comment at `file_manager.c:5186-5187` explicitly requires: *"this should always be called under a system operation. the system operation should always be committed before file header page is unfixed."*
- The sysop end is a *logical undo* (`RVFL_ALLOC`) — its recovery handler deallocates by VFID+VPID.
- Cost attribution (revised — binary-verifiable):
  - `fa{sysop}` probe (`g_copy_probe_ns_fa_sysop`) measures ONLY the `log_sysop_start_atomic` call at line 5470 (prior-LSA pressure from emitting `LOG_SYSOP_ATOMIC_START`).
  - The per-page tail record — `log_sysop_end_logical_undo (RVFL_ALLOC)` at ~5570 — is NOT currently under `fa{sysop}`; it is amortized inside `m_ns_flush`.
  - **A new `fa{sysop_tail}` probe around `log_sysop_end_logical_undo` is REQUIRED (§7.4, §12 step 1)** so acceptance criteria can be binary-verified rather than tautological (the skip trivially zeroes `fa{sysop}` by construction; what we need to verify is that the tail-record cost ALSO goes to ~0, which is where the real win lives).

### 1.3 Decisive discovery — `file_perm_expand` opens a COMMITTED nested sysop
File: `src/storage/file_manager.c:4687-4750`.
```
log_sysop_start (thread_p);                       // line 4690 — plain
  disk_reserve_sectors (...);                     // reserves sectors, emits redo/undo
  file_extdata_append (extdata_part_ftab, ...);   // updates file header partial table
  fhead->n_sector_total += expand_size_in_sectors;
  fhead->n_sector_empty = expand_size_in_sectors;
  fhead->n_sector_partial = expand_size_in_sectors;
  fhead->n_page_free = expand_size_in_sectors * DISK_SECTOR_NPAGES;
  fhead->n_page_total += fhead->n_page_free;
  log_append_undoredo_data2 (thread_p, RVFL_EXPAND, ...);   // line 4728 — emitted INSIDE this nested sysop
  pgbuf_set_dirty (thread_p, page_fhead, DONT_FREE);
log_sysop_commit (thread_p);                      // line 4748 — COMMITS the nested sysop (durable)
```
This is the dispositive case for recovery correctness:
- `RVFL_EXPAND` and the file-header bookkeeping increments (`n_sector_total`, `n_page_free`, `n_page_total`) are emitted inside a *nested sysop that is committed by `log_sysop_commit`*.
- Once `log_sysop_commit` runs, those records are NO LONGER reachable by the parent's per-record undo walk on transaction rollback.
- The ONLY mechanism that can roll these records back on a crash is `log_recovery_abort_atomic_sysop` (`log_recovery.c:4267-4376`), which keys off the parent's `atomic_sysop_start_lsa` set by `log_sysop_start_atomic` (`log_manager.c:3665-3698`; analysis-phase handler at `log_recovery.c:1471-1491`).
- Today's correctness chain:
  - `file_alloc`'s inner `log_sysop_start_atomic` (line 5470) installs `atomic_sysop_start_lsa`.
  - `file_perm_expand`'s committed nested sysop at 4690/4748 is therefore covered: on crash before `file_alloc`'s inner commit, recovery walks back to `atomic_sysop_start_lsa` and undoes everything — including the expansion bookkeeping.
- If we simply skip `file_alloc`'s inner `log_sysop_start_atomic` without upgrading the outer sysop, the `file_perm_expand` commit becomes durable with no parent atomic marker. A crash between the expansion commit and the end of the COPY batch leaves `fhead->n_sector_total` / `n_page_free` INCONSISTENT with actually-allocated user pages after recovery.

### 1.4 Why the inner sysop's logical-undo tail (`RVFL_ALLOC`) IS redundant once the outer sysop is atomic
- `file_perm_alloc`'s own per-record redo/undo (bitmap flips, partial-table updates on the header page) are reachable by the parent atomic sysop's recovery undo walk.
- `file_perm_expand`'s committed nested sysop is reachable via `atomic_sysop_start_lsa` on crash (provided the PARENT is atomic).
- The per-page `RVFL_ALLOC` logical undo is therefore a shortcut for *user-visible rollback only* — it collapses per-record undos into one `file_dealloc`. Under the outer atomic sysop, transaction rollback still reaches the same records via per-record undos, and crash recovery is covered by the parent's `atomic_sysop_start_lsa`.
- Net: the per-page `log_sysop_start_atomic` + `log_sysop_end_logical_undo` pair is redundant IF AND ONLY IF the outer sysop is itself atomic.

### 1.5 Callers of `file_alloc` (complete, verified — 13 sites)
- `src/storage/heap_file.c` (2) — only `heap_alloc_new_page` is on the COPY path
- `src/storage/system_catalog.c`, `src/storage/overflow_file.c`, `src/storage/extendible_hash.c` (4), `src/storage/external_sort.c`, `src/storage/btree_load.c`, `src/storage/btree.c`
- `src/storage/index_hnsw/hnsw_storage.cpp`
- `src/query/vacuum.c` (3), `src/query/query_manager.c`, `src/query/query_hash_scan.c` (6)
- `src/storage/file_manager.c` internal (`file_alloc_multiple`, `file_alloc_sticky_first_page`, `file_tracker_register_internal`)

Only `heap_alloc_new_page` (through `locator_multi_insert_force` on the BU-lock fast path) receives the new `skip_inner_sysop=true`. All others keep the default.

### 1.6 BU_LOCK lifetime and visibility (resolves Open Question #2)
Verified from source:
- **Acquisition**: BU_LOCK is acquired on the class OID at COPY session setup via `xlocator_find_class_oid (..., BU_LOCK)`. Evidence:
  - CAS-side COPY path (our path): `src/communication/network_interface_sr.cpp:12247-12250` — comment explicitly reads "Acquire BU_LOCK on the class so the COPY session can use the bulk-insert fast path (page-image WAL via locator_multi_insert_force). Mirrors loaddb; the lock is released at transaction commit."
  - loaddb mirror: `src/loaddb/load_server_loader.cpp:126`, `:137`, `:158`, `:276`, with post-acquire assertion `lock_has_lock_on_object (&class_oid, oid_Root_class_oid, BU_LOCK)` at `:601` and again per batch at `:728`.
- **No mid-batch release path**: a code-wide search for `BU_LOCK` (9 files, all cross-checked) shows it is only ever read via `lock_has_lock_on_object` / `granted_mode == BU_LOCK` asserts. There is no `lock_unlock_object (..., BU_LOCK)` in any COPY-path function; every `flush_batch` call re-asserts the lock. In `lock_manager.c:2911-2917`, `class_entry->granted_mode == BU_LOCK` explicitly *disallows lock escalation* (the class is pinned at BU_LOCK for the duration the entry lives).
- **Release**: BU_LOCK is released only at transaction commit/abort by the normal `lock_unlock_all` path on the outer txn — not by any flush-batch / sysop-attach machinery.
- **`log_sysop_attach_to_outer` does NOT touch locks**: it merges postpone/undo records into the parent (`log_manager.c:4097-4136`). Lock state is independent of sysop nesting state.
- **Visibility answer**: BU_LOCK is held from class resolution through outer-txn commit. Concurrent readers cannot observe pages allocated by `locator_multi_insert_force` as *attached-but-uncommitted-by-outer-txn* in a way that differs from today: the same visibility contract applies before and after this plan. No hazard.

**Close Open Question #2: VERIFIED — BU_LOCK held to outer commit/abort; no visibility hazard introduced by Synthesis A.**

### 1.7 HA interaction (resolves Missing Item X3)
The fast path that this plan modifies is gated by `has_BU_lock && HA_DISABLED()` at `copy_session.cpp:353`. The `skip_inner_sysop` flag and the outer atomic-sysop upgrade are invisible when HA is enabled — the code falls through to the slow path unchanged. No replica / log-shipping protocol change is required or possible.

---

## 2. Principles (6)

1. **Correctness first**: crash recovery + user rollback must remain sound. The `file_perm_expand` committed-nested-sysop case is the crux — it requires a parent `atomic_sysop_start_lsa`.
2. **Compensate before skipping**: never drop the inner atomic sysop unless the parent sysop is upgraded to atomic. Weakening without compensation is a correctness regression.
3. **Minimal API surface**: one optional parameter on `file_alloc` + `heap_alloc_new_page`; default preserves current behavior byte-for-byte.
4. **Localized opt-in**: only the bulk-insert path (BU-lock + HA disabled) ever sets the flag AND the outer sysop-upgrade.
5. **Observable**: existing ns probes (`fa{sysop}`, `fa{perm_alloc}`) plus a new `fa{sysop_tail}` probe around `log_sysop_end_logical_undo` give binary-verifiable acceptance — the win is dominated by the tail record, not the start-atomic call.
6. **Reversible**: both changes revert by a one-line edit each.

## 3. Decision Drivers (top 3)

1. **Correctness under crash recovery** — Synthesis A closes the `file_perm_expand` gap by upgrading the outer sysop to atomic. This is the core correctness fix; everything else is secondary.
2. **Win size** — `fa{sysop}` is 3.906 s of 24.4 s flush. The target is the per-page `log_sysop_end_logical_undo` tail record (amortized, ~175 µs/page), not the once-per-batch atomic start. The new `fa{sysop_tail}` probe (§7.4, §12 step 1) disambiguates these before enabling the skip.
3. **Blast radius** — 2 isolated edits: (a) `copy_session.cpp:357` outer sysop upgrade, (b) `file_alloc` new skip flag. Neither changes any other caller's behavior.

## 4. Viable Options (refreshed under Synthesis A framing)

### Option A — **SYNTHESIS A (CHOSEN)**: Upgrade outer sysop to atomic + add `skip_inner_sysop` opt-in to `file_alloc`
Two coupled edits:
1. `copy_session.cpp:357` — replace `log_sysop_start (thread_p)` with `log_sysop_start_atomic (thread_p)`. This installs `atomic_sysop_start_lsa` on `tdes->rcv`, which covers `file_perm_expand`'s committed nested sysop against crash recovery.
2. `file_alloc` — add `bool skip_inner_sysop = false`. When true, skip `log_sysop_start_atomic` + `log_sysop_end_logical_undo`. An **always-on runtime guard** (not just a DEBUG assert) refuses the skip with `ER_FATAL_ERROR_SEVERITY` if the caller is not inside an atomic outer sysop (§5.3).

**Note on helper**: `log_check_atomic_sysop_is_started` does not currently exist in the codebase. It must be introduced in `log_manager.c`/`.h` alongside `log_check_system_op_is_started` at `log_manager.c:4187` (§5.6, §12 step 2).

- Pros: fully correct for the `file_perm_expand` case — parent atomic sysop covers all nested committed sysops uniformly.
- Pros: eliminates both the per-page `log_sysop_start_atomic` cost AND the per-page `log_sysop_end_logical_undo` cost (single `RVFL_ALLOC` record per page).
- Pros: parent already in place; the upgrade at line 357 is a single-token change.
- Cons: transaction rollback now walks more per-record undos (bounded by the work done). This is acceptable; abort path is rare.
- Cons: requires introducing `log_check_atomic_sysop_is_started` (§5.6).

### Option B — Upgrade outer sysop to atomic only (no file_alloc change)
Just the `copy_session.cpp:357` switch. Keep `file_alloc`'s inner atomic sysop unchanged.

- Pros: correctness-only patch; guarantees crash consistency across the COPY batch.
- Pros: useful as a *validation* milestone before adding the skip flag (see §8 validation step).
- Cons: does not reclaim the 3.9 s `fa{sysop}`. Not the perf win; partial fix.
- Invalidation: not the final deliverable, but explicitly staged as the §8 validation step to separate correctness impact from the skip-flag perf impact.

### Option C — Batch multiple allocs inside a single inner sysop (amortize cost)
`file_alloc_batch_begin` / `file_alloc_batch_end` pair; `locator_multi_insert_force` opens one sysop covering all 22,302 allocs, emitting a single `RVFL_ALLOC`-equivalent record at the batch end.

- Pros: preserves atomic semantics inside `file_alloc`; amortizes sysop cost to ~0.
- Pros: no change to per-caller correctness contract.
- Cons: adds new API and state (batch handle, batch-end bookkeeping).
- Cons: the batch-end logical undo must accept a VPID list, which requires a new undo record type and a new `file_rv_dealloc_list` handler. Non-trivial recovery-path refactor.
- Cons: `file_alloc_multiple` already exists and still invokes the inner per-page sysop today; making it skip would regress other callers.
- Invalidation: larger diff and new recovery-path surface for no additional win over Synthesis A. Park as a follow-up if Synthesis A is measured insufficient.

### Option D — Skip inner sysop without upgrading outer sysop (REJECTED — unsafe)
Only add the `skip_inner_sysop` flag without touching `copy_session.cpp:357`.

- **Invalidation rationale (decisive, from Architect)**: `file_perm_expand` opens a nested sysop and COMMITS it at line 4748, emitting `RVFL_EXPAND` at line 4728 inside that committed nested sysop. With the outer sysop still plain, there is no `atomic_sysop_start_lsa` to anchor `log_recovery_abort_atomic_sysop`. A crash between `file_perm_expand`'s commit and the outer batch end leaves `fhead->n_sector_total`, `n_page_free`, `n_page_total` inconsistent with user-visible page allocations. **This option is unsafe and rejected.**

### Option E — Do nothing, optimize `perm_alloc` or `init_page` instead (REJECTED)
Chase the 8 s in `perm_alloc` or 3.9 s in `init_page` directly.

- Invalidation: deeper refactors touching file-header layout and `heap_vpid_init_new` semantics. The 3.9 s sysop win is available today with Synthesis A. Revisit as a follow-up.

### Chosen: **Option A (Synthesis A)**

---

## 5. Concrete diff plan (pseudocode)

### 5.1 `src/loaddb/copy_session.cpp` — upgrade outer sysop to atomic (CORE CORRECTNESS FIX)
Line 357:
```
-      log_sysop_start (thread_p);
+      log_sysop_start_atomic (thread_p);
```
Rationale: installs `tdes->rcv.atomic_sysop_start_lsa` before `locator_multi_insert_force`. This makes every nested sysop emitted during the batch — including `file_perm_expand`'s committed nested sysop (`file_manager.c:4690/4748`) — recoverable via `log_recovery_abort_atomic_sysop` (`log_recovery.c:4267-4376`) in the event of a crash before the outer batch's `log_sysop_attach_to_outer`.

**Durability boundary (explicit)**: this single-token change has two distinct effects, which must NOT be conflated:
- For `file_perm_alloc`'s own log records: these are emitted inside the outer batch. On user rollback (`log_sysop_abort` at line 389), they are undone via the normal per-record undo walk. This is unchanged by the `log_sysop_start_atomic` upgrade — atomicity is a *crash-recovery* guarantee, not a rollback semantics change.
- For `file_perm_expand`'s committed nested sysop records (including `RVFL_EXPAND` and file-header bookkeeping at `file_manager.c:4714-4729`): these are NOT reachable by the parent's per-record undo walk (the commit at 4748 closes them out). They are reachable ONLY via `atomic_sysop_start_lsa` on crash recovery. The outer sysop upgrade is the mechanism that extends crash-recovery coverage to these records. **This asymmetry is the reason Synthesis A is required; it is not optional.**

### 5.2 `src/storage/file_manager.h`
```
-extern int file_alloc (THREAD_ENTRY *, const VFID *, FILE_INIT_PAGE_FUNC, void *,
-                       VPID *, PAGE_PTR *);
+extern int file_alloc (THREAD_ENTRY *, const VFID *, FILE_INIT_PAGE_FUNC, void *,
+                       VPID *, PAGE_PTR *, bool skip_inner_sysop = false);
```
`.c` files compile as C++17 (`c_to_cpp.sh`), so a default-arg works. If cleaner, use an overload wrapper keeping the 6-arg signature calling the 7-arg with `false`.

### 5.3 `src/storage/file_manager.c` — `file_alloc`
```
int file_alloc (..., bool skip_inner_sysop /* default false */)
{
  ...
  if (FILE_IS_TEMPORARY (fhead)) { ... /* unchanged */ }
  else
    {
      if (!skip_inner_sysop)
        {
          long long __fa_ts = file_alloc_probe_now_ns ();
          log_sysop_start_atomic (thread_p);
          g_copy_probe_ns_fa_sysop += file_alloc_probe_now_ns () - __fa_ts;
          is_sysop_started = true;
        }
      else
        {
          /* ALWAYS-ON runtime guard (release build too).
           * Caller MUST hold an ATOMIC outer sysop because file_perm_alloc may
           * call file_perm_expand which opens-and-COMMITS a nested sysop that
           * emits RVFL_EXPAND. Only atomic_sysop_start_lsa covers that committed
           * nested sysop for crash recovery (log_recovery.c:4267-4376). If the
           * caller's outer sysop is plain, we REFUSE the skip — do not fall
           * through silently; the file header would be at risk on crash. */
          LOG_TDES *tdes = LOG_FIND_CURRENT_TDES (thread_p);
          if (tdes == NULL
              || tdes->topops.last < 0
              || LSA_ISNULL (&tdes->rcv.atomic_sysop_start_lsa))
            {
              assert (false);       /* trip debug builds immediately */
              er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
                      ER_GENERIC_ERROR, 1,
                      "file_alloc: skip_inner_sysop requires an atomic outer sysop");
              error_code = ER_FAILED;
              goto exit;            /* preserves existing exit-path page unfix */
            }
        }
      error_code = file_perm_alloc (...);
      ...
      VFID_COPY (...); VPID_COPY (...);   /* still compose undo_log_data for exit */
    }
  ...
exit:
  /* Existing exit path at file_manager.c:5587-5590 is PRESERVED: on error,
   * the allocated page is unfixed via the existing pgbuf_set_dirty +
   * pgbuf_unfix sequence. No buffer-pool leak on the skip_inner_sysop=true
   * error path because page_ptr is set to NULL by this function only on
   * success; errors propagate up and the caller never receives a stale
   * fixed pointer. The outer atomic sysop's abort/undo reclaims the alloc. */
  if (is_sysop_started) { /* commit/abort as before */ }
  /* When skip_inner_sysop=true and error_code != NO_ERROR:
   * DO NOT call file_dealloc here. Caller's outer atomic sysop abort covers it. */
  ...
}
```
Acceptance: when `skip_inner_sysop == false`, control flow is character-identical to today (verify by diff). When `true`, the two `log_sysop_*` calls are elided; `file_perm_alloc` still runs (including any `file_perm_expand`); `f_init` still runs; `file_numerable_add_page` still runs. The guard fires in *release builds too* on any mis-use.

### 5.4 `src/storage/heap_file.c` — `heap_alloc_new_page`
```
int heap_alloc_new_page (..., bool skip_inner_sysop = false)
{
  ...
  error_code = file_alloc (thread_p, &hfid->vfid, heap_vpid_init_new,
                           &new_page_chain, new_page_vpid, &page_ptr,
                           skip_inner_sysop);
  ...
}
```
Signature update in `heap_file.h`. Existing non-COPY caller inside `heap_vpid_alloc` keeps default `false`.

### 5.5 `src/transaction/locator_sr.c` — `locator_multi_insert_force`
```
// At the alloc call site (near line 13835 where has_BU_lock is in scope):
bool skip_inner_sysop = has_BU_lock;  // fast path only; outer atomic sysop already upgraded
error_code = heap_alloc_new_page (thread_p, hfid, *class_oid, &home_hint_p,
                                  &new_page_vpid, skip_inner_sysop);
```
Rationale: `has_BU_lock` already gates the same block that COPY uses. The outer atomic sysop guarantee is upheld by `copy_session.cpp:357`'s upgrade (§5.1).

### 5.6 `log_check_atomic_sysop_is_started` — INTRODUCE (does not exist today)
The helper does not exist in the codebase (verified). Introduce it in `src/transaction/log_manager.c` alongside `log_check_system_op_is_started` at `log_manager.c:4187`, with the public declaration added to `src/transaction/log_manager.h`:
```
bool
log_check_atomic_sysop_is_started (THREAD_ENTRY * thread_p)
{
  LOG_TDES *tdes = LOG_FIND_CURRENT_TDES (thread_p);
  return tdes != NULL
         && tdes->topops.last >= 0
         && !LSA_ISNULL (&tdes->rcv.atomic_sysop_start_lsa);
}
```
Used by:
- `file_alloc`'s DEBUG `assert (log_check_atomic_sysop_is_started (thread_p))` (complements the always-on runtime guard in §5.3);
- Unit tests under `unit_tests/` to validate setup.

### 5.7 Other `heap_alloc_new_page` / `file_alloc` callers
All 13 `file_alloc` callers: no change (default arg = false). Both `heap_alloc_new_page` callers: only `locator_multi_insert_force` changes; the other (vacuum / heap extend path) keeps default `false`.

### 5.8 Undo / rollback correctness argument (final form)
- Outer sysop is now `log_sysop_start_atomic` (§5.1) — it installs `atomic_sysop_start_lsa`.
- Transaction rollback (user-visible): `log_sysop_abort` (`copy_session.cpp:389`) walks the outer sysop's undo chain. All per-record undos from `file_perm_alloc`, `file_numerable_add_page`, and `f_init` (e.g., `heap_vpid_init_new`) are directly reachable. `file_perm_expand`'s committed nested sysop is NOT reached by this walk — but this is identical to today's behavior (file expansion is durable by design, per the comment at `file_manager.c:4687-4689`).
- Crash recovery: WAL replays redo. The outer txn is incomplete → recovery analysis observes the atomic_sysop_start_lsa marker (`log_recovery.c:1471-1491`). `log_recovery_abort_atomic_sysop` (`log_recovery.c:4267-4376`) walks back to `atomic_sysop_start_lsa` and undoes every record emitted since — INCLUDING `file_perm_expand`'s committed nested sysop's `RVFL_EXPAND` and file-header bookkeeping. File header remains consistent with user-visible page allocations.
- The `skip_inner_sysop=true` branch drops only the per-page `LOG_SYSOP_ATOMIC_START` + `LOG_SYSOP_END_LOGICAL_UNDO (RVFL_ALLOC)` pair. Crash recovery coverage is preserved by the outer atomic marker; rollback coverage is preserved by per-record undos walked on `log_sysop_abort`.

---

## 6. Pre-mortem (DELIBERATE)

### Scenario 1 — Crash between `file_perm_expand`'s inner-sysop commit and the batch's outer sysop attach (the decisive window)
- The batch has been allocating pages; at some point `file_perm_alloc` triggered `file_perm_expand`, which reserved sectors, updated `fhead->n_sector_total` / `n_page_free`, emitted `RVFL_EXPAND` at `file_manager.c:4728`, and committed its nested sysop at `file_manager.c:4748`.
- Server crashes AFTER that commit but BEFORE `copy_session::flush_batch`'s `log_sysop_attach_to_outer` at line 392.
- With plain outer sysop (today or Option D): no `atomic_sysop_start_lsa` exists. Recovery redoes the `RVFL_EXPAND` but has no mechanism to undo it. `n_sector_total` / `n_page_free` remain bumped while the user-page allocations that depended on them are gone (outer txn undo walks per-record undos, reclaiming bitmap bits — but cannot reach the post-commit nested-sysop records). **File header inconsistent with actual user-visible page state.**
- With Synthesis A: the outer sysop is atomic; `atomic_sysop_start_lsa` is set. Recovery's `log_recovery_abort_atomic_sysop` walks back to that LSA and undoes `RVFL_EXPAND` (via its registered undo handler) as well as every other record since. File header consistent post-recovery.
- **Mitigation (testing)**: the standard `FI_TEST_LOG_MANAGER_RANDOM_EXIT_AT_END_SYSTEMOP` fires at *sysop end*, which is the WRONG window. A dedicated fault-injection point is needed: `FI_TEST_FILE_PERM_EXPAND_AFTER_COMMIT`, injected *after* `log_sysop_commit` at `file_manager.c:4748` returns. Unit/integration test then: run a COPY that forces at least one `file_perm_expand`, trigger the injection to exit, restart the server, and verify `fhead->n_sector_total`, `fhead->n_sector_partial`, `fhead->n_sector_empty`, `fhead->n_page_free`, `fhead->n_page_total` are consistent with the file-tracker / sector-bitmap actual state.

### Scenario 2 — COPY ROLLBACK mid-batch
- User issues ROLLBACK after 150k rows streamed.
- `copy_session::abort` → outer transaction abort → normal rollback machinery walks the outer sysop's undo chain.
- With `skip_inner_sysop=true`, the undo chain now contains per-record undos from `file_perm_alloc` / `file_numerable_add_page` / `f_init` directly (no `RVFL_ALLOC` shortcut). Rollback reclaims bitmap bits, partial-table entries, and user-page contents.
- `file_perm_expand`'s committed nested sysop records are NOT rolled back on user ROLLBACK — by design (`file_manager.c:4687-4689` explicitly chooses durable expansion so the file does not re-expand).
- **Mitigation**: integration test — COPY 10k rows, force abort at row 5k via injected invalid row, verify `file_header.n_page_user` returns to baseline AND sector-map bits are freed. Expansion-related counters (`n_sector_total`) may legitimately stay bumped — document this.

### Scenario 3 — Non-BU-lock caller accidentally gets `skip=true` via refactor error
- A future contributor threads `skip_inner_sysop=true` through a new path without holding an *atomic* outer sysop.
- On crash: `file_perm_expand`'s committed nested sysop would be orphaned → file header inconsistency (Scenario 1 class of bug).
- **Mitigation (revised — always-on)**: the `skip_inner_sysop` branch in `file_alloc` contains an **always-on runtime guard** (§5.3) that checks `tdes->rcv.atomic_sysop_start_lsa != NULL_LSA` AND `tdes->topops.last >= 0`. On violation it calls `er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ...)` and returns `ER_FAILED` — **refusing the skip in release builds too**, not just DEBUG asserting. The debug-only `assert (log_check_atomic_sysop_is_started (thread_p))` complements this (early trip in debug runs). Doc comment on the flag in `file_manager.h` warns: "caller MUST hold an *atomic* outer sysop; `log_sysop_start` is NOT sufficient". Narrow use to one call site.

---

## 7. Expanded Test Plan (DELIBERATE)

### 7.1 Unit (`unit_tests/storage/` or a new subdir)
- `file_alloc_skip_inner_sysop_off`: pre-existing behavior — log record stream contains `LOG_SYSOP_ATOMIC_START` + `LOG_SYSOP_END_LOGICAL_UNDO (RVFL_ALLOC)` per alloc. Assert via tdes log tail walker.
- `file_alloc_skip_inner_sysop_on_with_atomic_outer`: open an outer sysop via `log_sysop_start_atomic`, call `file_alloc (..., true)`, verify:
  - (a) no `LOG_SYSOP_ATOMIC_START` / `LOG_SYSOP_END_LOGICAL_UNDO` emitted by `file_alloc` itself;
  - (b) `tdes->rcv.atomic_sysop_start_lsa` remains the outer one;
  - (c) page is allocated and file header consistent;
  - (d) outer `log_sysop_abort` reclaims the page; `file_header.n_page_user` returns to baseline.
- `file_alloc_skip_inner_sysop_on_without_atomic_outer`: outer is plain `log_sysop_start`. Verify the always-on runtime guard (§5.3) returns `ER_FAILED` AND `er_errid()` matches the fatal-severity code. In DEBUG the `assert` trips first; in RELEASE the guard return path is exercised.
- `file_alloc_skip_inner_sysop_wal_record_spy`: mock/spy test — with skip OFF vs skip ON under an atomic outer, capture the log record stream via `logtb_get_next_tran_log_record`-style walker and confirm the ONLY differences are the absence of the per-page `LOG_SYSOP_ATOMIC_START` and `LOG_SYSOP_END_LOGICAL_UNDO (RVFL_ALLOC)` records. All other records (bitmap flips, partial-table updates, `heap_vpid_init_new` REDO) are byte-identical in both streams. (This replaces the earlier `file_alloc_skip_inner_sysop_on_triggers_expand` unit slot — forcing `file_perm_expand` in a unit test is demoted to §7.2 integration.)

### 7.2 Integration (existing CUBRID test harness)
- `copy_bulk_abort_mid_batch.sql`: COPY 10k rows, force abort at row 5k via injected invalid row. Verify:
  - target heap file `n_page_user` equals baseline before COPY (no leak);
  - no orphan entries in `file_tracker` for this VFID;
  - `n_sector_total` may legitimately stay bumped (expansion is durable) — document this in the test.
- `copy_bulk_happy_path.sql`: COPY 290k rows, verify `SELECT COUNT(*)` matches.
- `copy_bulk_triggers_expand.sql` (NEW, replaces unit-level expand test): COPY enough rows under a fresh heap that `file_perm_expand` is invoked at least twice (volume tuned to the file manager's expand heuristic). Verify `fhead->n_sector_total` and `fhead->n_page_total` are bumped by the expected amount AND the post-COPY row count is correct.
- `copy_bulk_crash_after_expand.sql` (NEW, DELIBERATE-specific): with the new `FI_TEST_FILE_PERM_EXPAND_AFTER_COMMIT` injection, COPY with 2× expansion-forcing volume, trigger crash after second expansion's commit, restart server. Verify file header's `n_sector_total`, `n_sector_empty`, `n_sector_partial`, `n_page_free`, `n_page_total` are consistent with the sector bitmap + partial/full tables. This is the acceptance test for §8 criterion 5.
- `copy_bulk_sa_mode.sql` (NEW, Missing Item X4): same `copy_bulk_happy_path.sql` scenario, but built/run under `SA_MODE` (standalone — `cubridsa` library, `sa/` target). Verify the `skip_inner_sysop` branch compiles in `SA_MODE` (no `SERVER_MODE`-gated symbol leakage), and the post-COPY row count + file-header counters match the `SERVER_MODE` baseline.

### 7.3 e2e (annb benchmark + existing probes)
- Run the existing annb 290k `INT, VECTOR(256)` benchmark.
- Expect under full Synthesis A:
  - `fa{sysop}` in `/tmp/copy_counter.log` is <100 ms (trivially — the skip branch elides the measured call);
  - `fa{sysop_tail}` (new probe, §7.4) drops to <100 ms — this is the **binding** per-probe criterion;
  - Total wall time improves by ≥3 s vs baseline;
  - Row count identical to baseline (290,000).
- Abort-on-abort recovery timing: re-run the 290k COPY under a user ROLLBACK immediately before batch end. Record wall-clock for abort; compare to baseline abort time (see §8 criterion 9).

### 7.4 Observability (REQUIRED — upgraded from "consider")
- **MUST add** a nanosecond probe around `log_sysop_end_logical_undo` at `file_manager.c:~5570`, exposed as `fa{sysop_tail}` in `/tmp/copy_counter.log`. This probe is a blocker for the §8 validation step; without it criterion 1 is tautological.
- Keep existing ns probes (`g_copy_probe_ns_fa_*`) untouched — they show `fa{sysop}` ≈ 0 under the skip path by construction.
- Temporary counter `g_copy_probe_skip_inner_sysop_count` to confirm the skip branch is taken (remove before merge).
- New counter `g_copy_probe_wal_records_skipped` incrementing by the WAL record count elided per skipped alloc (2 records: LOG_SYSOP_ATOMIC_START + LOG_SYSOP_END_LOGICAL_UNDO). Used for §8 criterion 8.

---

## 8. Acceptance Criteria (binary-verifiable end-to-end)

1. **`fa{sysop_tail}` (new probe) drops from its baseline (to be captured in step 1a of §12) to <100 ms** on a 290k-row annb COPY under full Synthesis A. This replaces the tautological prior criterion. Rationale: the skip by construction zeroes `fa{sysop}`, so the binding perf signal is the tail-record probe.
2. Overall session wall time (`m_ns_flush` in the log) improves by ≥3 s vs the `cubvec/m1-f4-cp` baseline. (Sole binding wall-clock gate; if wall-time delta is <3 s, re-examine — the expected win is ~3.9 s split across start + tail.)
3. All existing `unit_tests/**` targets pass (`ninja -C build_preset_debug test` / catch2 runners with `UNIT_TESTS=ON`).
4. A new abort-mid-batch test (integration §7.2) leaves no orphan user pages (verified via `file_header.n_page_user` and `file_tracker` walk).
5. **With `skip_inner_sysop=true` inside an atomic outer sysop, a crash injected between `file_perm_expand`'s inner sysop commit (`file_manager.c:4748`) and the batch's outer sysop attach (`copy_session.cpp:392`) must leave the file header's `n_sector_total`, `n_sector_empty`, `n_sector_partial`, `n_page_free`, `n_page_total` consistent with actually-allocated user pages after recovery.** (Test: `copy_bulk_crash_after_expand.sql` via new `FI_TEST_FILE_PERM_EXPAND_AFTER_COMMIT` injection point.)
6. `skip_inner_sysop=false` path produces a bit-identical WAL stream to today (targeted unit `file_alloc_skip_inner_sysop_wal_record_spy` comparing log record type sequences).
7. No code style regressions in CI (`indent -l120`, astyle, 2-space indent, GNU brace style, `memory_wrapper.hpp` last include).
8. **WAL volume reduction on skip path (Missing Item X1)**: for a 290k-row COPY, `g_copy_probe_wal_records_skipped` reports ≈ 2 × (number of user pages allocated) records elided. Independently verify by measuring WAL bytes/records appended per flush_batch (via `log_Stat` or tdes counters): the skip path emits `(2 × n_pages_allocated)` fewer records per batch than the OFF path. Criterion: measured reduction ≥ 90% of the theoretical 2 × n_pages_allocated figure.
9. **Recovery-time gate (Missing Item X2)**: abort of a 290k-row COPY (user ROLLBACK mid-batch, or crash-recovery abort of the outer txn) completes within 2× the pre-change time. Measurement: wall-clock from start of `log_sysop_abort` (or recovery undo phase entry) to completion. Baseline captured on `cubvec/m1-f4-cp` head before applying Synthesis A; gate evaluated after Synthesis A on the same hardware. Rationale: the atomic-sysop undo walk is O(batch_size) (see §10 row 3) — for 290k pages × per-record undos this is O(millions of records); we must bound the regression.

### 8.5 Validation step (isolate where the per-page cost lives)
Before enabling the full Synthesis A change, (1) add the `fa{sysop_tail}` probe (§12 step 1a) and capture baseline numbers, (2) run the benchmark with ONLY the `log_sysop_start → log_sysop_start_atomic` change at `copy_session.cpp:357` (i.e., Option B alone; no `file_alloc` flag yet). Expected:
- `fa{sysop}` stays at ~3.9 s and `fa{sysop_tail}` stays at its baseline (the per-page inner `log_sysop_start_atomic` and `log_sysop_end_logical_undo` are still present and dominate).
- Total wall time is essentially unchanged vs baseline (Option B is a correctness fix, not a perf fix).
This confirms whether the 3.9 s is dominated by the start-atomic record (prior-LSA pressure) or the tail logical-undo record — data that feeds the criterion-1 floor and any follow-up (§9 Follow-ups). Only then apply the full Synthesis A and observe the drop. This staged rollout also isolates any regression in either change for bisection.

---

## 9. ADR

- **Decision**: **Synthesis A** — (1) upgrade `copy_session::flush_batch`'s outer sysop from `log_sysop_start` to `log_sysop_start_atomic` at `copy_session.cpp:357`, AND (2) add `skip_inner_sysop` opt-in to `file_alloc` (+ `heap_alloc_new_page` passthrough), with an always-on runtime guard refusing the skip if the outer sysop is not atomic. Enable the skip only in `locator_multi_insert_force` when `has_BU_lock == true`.
- **Drivers**:
  1. Crash-recovery correctness — `file_perm_expand` opens a COMMITTED nested sysop emitting `RVFL_EXPAND` (`file_manager.c:4690/4728/4748`); only an *atomic* parent sysop covers it via `atomic_sysop_start_lsa` on recovery (`log_recovery.c:4267-4376`).
  2. Perf win — 3.9 s / 24.4 s flush time from `fa{sysop}`/`fa{sysop_tail}` combined is recoverable only once the outer atomic sysop compensates for the inner one.
  3. Blast radius — 2 isolated edits; reversible one line at a time.
- **Alternatives considered**:
  - Option B (outer sysop upgrade only): correctness fix, no perf win. Adopted as a §8 validation milestone.
  - Option C (batch sysop API): larger API surface + new recovery handler; defer as a follow-up.
  - Option D (skip inner sysop without outer upgrade): **rejected — unsafe**. `file_perm_expand`'s committed nested sysop is orphaned on crash, leaving file header inconsistent with user-page state.
  - Option E (optimize `perm_alloc` / `init_page` directly): deeper refactor; follow-up.
- **Why chosen (Synthesis A)**: the outer-sysop upgrade closes the `file_perm_expand` crash-recovery gap that Option D would leave open. Once the parent is atomic, the inner per-page atomic sysop becomes redundant for both rollback (per-record undos suffice) and crash recovery (parent `atomic_sysop_start_lsa` suffices). The two edits together are the minimal change that is both correct AND reclaims the 3.9 s. The always-on runtime guard (not just a DEBUG assert) ensures any future mis-use is refused in release builds too, converting a potentially silent corruption into a noisy fatal-severity error.
- **Consequences**:
  - User-visible rollback of a COPY walks more per-record undos (bounded by the work done); recovery-time gate §8 criterion 9 guards regression.
  - `file_perm_expand`'s durability is unchanged — file expansion remains durable across user ROLLBACK by design.
  - Any future caller that sets `skip_inner_sysop=true` MUST hold an *atomic* outer sysop — enforced by both the always-on runtime guard AND the `log_check_atomic_sysop_is_started` debug assert.
  - HA path is unaffected (fast path gated by `HA_DISABLED()`).
  - Opens the door to Option C / E follow-ups once the sysop noise is out of the probe.
- **Follow-ups**:
  - If benchmarking shows remaining `fa{perm_alloc}=8 s` dominated by sector-bitmap walks, consider sector-reservation batching (separate plan).
  - If `fa{init_page}=3.9 s` is dominated by WAL records from `heap_vpid_init_new`, evaluate `pgbuf_log_redo_new_page`-style page-image-only logging for freshly allocated pages (separate plan).
  - Add `FI_TEST_FILE_PERM_EXPAND_AFTER_COMMIT` fault-injection point and include the Scenario-1 recovery test in the standard test matrix.
  - Audit other top-level flush paths (non-BU-lock) that might benefit from the same outer-sysop atomic upgrade.

---

## 10. Risk Callouts & Mitigations (revised)

| Risk | Without Synthesis A | With Synthesis A | Mitigation |
|---|---|---|---|
| Outer sysop undo chain fails to cover `file_perm_expand`'s committed nested sysop on crash recovery | **Medium/High** — `RVFL_EXPAND` orphaned; file header inconsistent with user-page state | **Low** — `atomic_sysop_start_lsa` anchors `log_recovery_abort_atomic_sysop` | Scenario-1 test via new `FI_TEST_FILE_PERM_EXPAND_AFTER_COMMIT` injection point |
| Non-BU caller gets `skip=true` via later refactor without atomic outer sysop | — | **High mitigated by hard refusal in release builds** | **Always-on runtime guard** in `file_alloc` skip branch (§5.3): calls `er_set (ER_FATAL_ERROR_SEVERITY, ...)` and returns `ER_FAILED` when `atomic_sysop_start_lsa == NULL_LSA` or `topops.last < 0`. Complemented by `assert (log_check_atomic_sysop_is_started)` in DEBUG. Narrow use to one call site. Doc comment on the flag explicitly requiring *atomic* outer sysop |
| `log_sysop_abort` / crash-recovery atomic-sysop undo walk performance degrades on O(batch_size) per-record undos vs one logical undo | — | Low/Medium | Abort path is rare. **Note**: `log_recovery_abort_atomic_sysop` walks O(batch_size) records; for 290k pages × per-record undos this is O(millions of records). §8 criterion 9 measures recovery time on the annb scale and gates ≤2× pre-change baseline |
| Hidden callers of `heap_alloc_new_page` not covered | Verified none | — | Grep confirmed only `locator_multi_insert_force` + `heap_vpid_alloc` (latter keeps default) |
| Build-mode guards (`SERVER_MODE`/`SA_MODE`/`CS_MODE`) behave differently | — | Low/Medium | `file_alloc` is server-side only. §7.2 `copy_bulk_sa_mode.sql` exercises `SA_MODE` build + run |
| `log_check_atomic_sysop_is_started` helper missing from log_manager.h | — | Low/Low | Confirmed missing. Introduce it (§5.6) at `log_manager.c:4187`; trivial 4-line function |

---

## 11. File list (all absolute paths)

- `/home/hgryoo/dev/cubrid_dev/src/loaddb/copy_session.cpp` — **§5.1 CORE FIX**: line 357, `log_sysop_start` → `log_sysop_start_atomic`.
- `/home/hgryoo/dev/cubrid_dev/src/storage/file_manager.h` — `file_alloc` signature.
- `/home/hgryoo/dev/cubrid_dev/src/storage/file_manager.c` — `file_alloc` body change (~5466-5487), always-on runtime guard in skip branch, preserved exit path at ~5587-5590, new `fa{sysop_tail}` probe at ~5570, new `FI_TEST_FILE_PERM_EXPAND_AFTER_COMMIT` injection point at line 4748.
- `/home/hgryoo/dev/cubrid_dev/src/transaction/log_manager.h` — export `log_check_atomic_sysop_is_started`.
- `/home/hgryoo/dev/cubrid_dev/src/transaction/log_manager.c` — add `log_check_atomic_sysop_is_started` alongside `log_check_system_op_is_started` at line 4187.
- `/home/hgryoo/dev/cubrid_dev/src/storage/heap_file.h` — `heap_alloc_new_page` signature.
- `/home/hgryoo/dev/cubrid_dev/src/storage/heap_file.c` — `heap_alloc_new_page` body.
- `/home/hgryoo/dev/cubrid_dev/src/transaction/locator_sr.c` — pass `has_BU_lock` into `heap_alloc_new_page` (near line 13835).
- `/home/hgryoo/dev/cubrid_dev/src/base/fault_injection.h` — add `FI_TEST_FILE_PERM_EXPAND_AFTER_COMMIT` to the FILE MANAGER group (200000-range). FILE MANAGER group currently contains only `FI_TEST_FILE_MANAGER_UNDO_TRACKER_REGISTER = 200000`; next free value is `200001`. Assign `FI_TEST_FILE_PERM_EXPAND_AFTER_COMMIT = 200001`.
- `/home/hgryoo/dev/cubrid_dev/src/base/fault_injection.c` — register the new injection point.
- `/home/hgryoo/dev/cubrid_dev/unit_tests/storage/` — new test cases for skip on/off + atomic-outer-required runtime-guard test + WAL-record-spy test.

---

## 12. Steps (actionable)

1. **Capture baseline + add `fa{sysop_tail}` probe (required for validation)**:
   - **Step 1a (REQUIRED)**: add `fa{sysop_tail}` ns probe around `log_sysop_end_logical_undo` in `file_alloc` at `file_manager.c:~5570`, exposed via `/tmp/copy_counter.log`. **Verify**: probe emits a non-zero value on a baseline 290k annb run.
   - **Step 1b**: capture baseline numbers for `fa{sysop}`, `fa{sysop_tail}`, `m_ns_flush`, abort-wall-time on `cubvec/m1-f4-cp` HEAD.
2. **Introduce `log_check_atomic_sysop_is_started` helper** (§5.6): add to `log_manager.c:4187` + export in `log_manager.h`. **Verify**: `./build.sh -m debug` compiles; helper returns true under `log_sysop_start_atomic`, false under `log_sysop_start` and when no sysop active (small unit test).
3. **Add `FI_TEST_FILE_PERM_EXPAND_AFTER_COMMIT`** (§5.3 / Scenario 1 / §11): assign `= 200001` in `fault_injection.h` FILE MANAGER group; register handler in `fault_injection.c`; inject call at `file_manager.c:4748` after `log_sysop_commit` returns. **Verify**: `FI_TEST` compiles in DEBUG + release; injection fires when enabled.
4. **Upgrade outer sysop to atomic (correctness-first)**: `copy_session.cpp:357`, `log_sysop_start` → `log_sysop_start_atomic`. **Verify**: run §8.5 validation — `fa{sysop}` and `fa{sysop_tail}` stay at baseline (perf unchanged; correctness improved). Existing CI + annb happy path pass. `copy_bulk_crash_after_expand.sql` (Scenario 1) passes under the upgrade alone.
5. **Add flag to `file_alloc`**: `file_manager.h` + `file_manager.c`. Default `false`. Gate the two `log_sysop_*` calls. **Always-on runtime guard** in the `true` branch with `er_set (ER_FATAL_ERROR_SEVERITY, ...)` and `ER_FAILED` return — plus DEBUG `assert`. Preserve the existing page-unfix exit path at `file_manager.c:5587-5590`. **Verify**: `skip_inner_sysop=false` produces bit-identical WAL to today (unit `file_alloc_skip_inner_sysop_wal_record_spy`); `skip_inner_sysop=true` under plain outer sysop returns `ER_FAILED` and sets er_errid (unit `file_alloc_skip_inner_sysop_on_without_atomic_outer`).
6. **Thread flag through `heap_alloc_new_page`**: `heap_file.h` + `heap_file.c`. Other callers keep default. **Verify**: `./build.sh -m debug` succeeds; all 13 `file_alloc` callers compile unchanged.
7. **Enable on BU-lock fast path**: `locator_sr.c`, pass `has_BU_lock` as `skip_inner_sysop` (near line 13835). **Verify**: annb 290k run — `fa{sysop_tail}` <100 ms; row count == 290,000; total wall time improves ≥3 s; `g_copy_probe_wal_records_skipped` ≈ 2 × n_pages_allocated.
8. **Add correctness + perf tests**: unit tests for skip on/off + WAL-record-spy + runtime-guard. Integration tests per §7.2 including `copy_bulk_sa_mode.sql`. e2e per §7.3. **Verify**: all pass; §8 criteria 1, 2, 4, 5, 6, 8, 9 met.
9. **Benchmark + document**: rerun annb; record new `/tmp/copy_counter.log` numbers (including `fa{sysop_tail}`) and abort-time in the commit body. **Verify**: wall-clock improvement ≥3 s (criterion 2); recovery-time within 2× baseline (criterion 9); no regression on non-COPY paths (existing CI shell tests).

---

## Open Questions (to be appended to `.omc/plans/open-questions.md`)

- [x] **RESOLVED**: Audit of `file_perm_alloc`'s log-record stream completed. Classification:
  - `file_perm_alloc`'s own records (bitmap flip, partial-table updates on `page_fhead`) → emitted in the OUTER atomic sysop (once §5.1 is applied) → reachable by parent atomic marker on crash AND by parent per-record undo walk on rollback.
  - `file_perm_expand`'s records (`RVFL_EXPAND` at `file_manager.c:4728`, `fhead` bookkeeping at 4714-4723) → emitted inside `file_perm_expand`'s NESTED sysop that is COMMITTED at `file_manager.c:4748` → reachable ONLY by parent atomic marker on crash (not by per-record undo walk on rollback, by design).
  - **Dispositive case**: `RVFL_EXPAND` inside the committed nested sysop. This is the reason Synthesis A (outer atomic) is required; a plain outer sysop cannot cover it.
- [x] **RESOLVED (see §1.6)**: BU_LOCK lifetime and visibility. **Verified**: BU_LOCK acquired via `xlocator_find_class_oid` at COPY session setup (network_interface_sr.cpp:12250 and load_server_loader.cpp:126/137/158/276), held through outer-txn commit/abort with no mid-batch release path. `log_sysop_attach_to_outer` does not touch locks. `class_entry->granted_mode == BU_LOCK` disallows escalation (lock_manager.c:2911-2917). **No visibility hazard introduced by Synthesis A.**
- [ ] Should we also apply the skip to `btree_load.c:2033` (bulk-loaded index on a BU-locked class)? — Why it matters: btree creation during COPY could benefit similarly; out of scope for this plan.
- [x] **RESOLVED**: `log_check_atomic_sysop_is_started` does NOT exist in the codebase. Introduce it at `log_manager.c:4187` (§5.6).
- [x] **RESOLVED**: `FI_TEST_*` naming convention. Verified: `fault_injection.h:55-82` shows codes grouped by subsystem. The IO & DISK MANAGER group occupies 100000-100004. The FILE MANAGER group starts at 200000 with only `FI_TEST_FILE_MANAGER_UNDO_TRACKER_REGISTER = 200000` present. The new injection point is semantically a FILE MANAGER event (fires inside `file_perm_expand`), so assign `FI_TEST_FILE_PERM_EXPAND_AFTER_COMMIT = 200001` (FILE MANAGER group).
