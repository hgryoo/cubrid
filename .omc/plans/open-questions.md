# Open Questions (cross-plan)

## copy-file-alloc-skip-inner-sysop - 2026-04-17
- [ ] Does `file_perm_alloc`'s internal log-record stream include any record type whose recovery handler *requires* the surrounding sysop to be an atomic sysop? — Why it matters: if yes, Option A (skip inner sysop) is unsafe and we must use Option C (batched sysop). Investigation: audit `file_rv_*` handlers called from log records emitted inside `file_perm_alloc`.
- [ ] Is there any scenario where `copy_session::flush_batch`'s outer sysop could be committed (via `attach_to_outer`) *before* the child txn commits, leaving newly-allocated pages visible to concurrent readers after BU_LOCK is dropped? — Why it matters: visibility/isolation correctness; need to confirm BU_LOCK lifetime spans the entire COPY session, not just a single batch.
- [ ] Should we also apply `skip_inner_sysop` to `btree_load.c:2033` (bulk-loaded index creation on a BU-locked class)? — Why it matters: btree creation during COPY could benefit similarly, but current COPY scope has no index; out of scope for this plan. Worth a follow-up measurement after Option A lands.
