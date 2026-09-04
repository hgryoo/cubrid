#!/usr/bin/env python3
"""sanitizer_triage.py - turn a sanitizer log into a table, and into a baseline.

A Tier 1 run reports things every time it runs: TSan a few hundred warnings,
UBSan ten.  Raw, that is not an oracle -- nobody reads it per run, so nobody
notices the one report that is new.  This groups reports by kind and site, and
emits the group as a suppression file so the next run's output is the *new*
findings and nothing else.

  ./sanitizer_triage.py run.log                     # the table
  ./sanitizer_triage.py run.log --show page_buffer  # full text of matching rows
  ./sanitizer_triage.py run.log --suppress > x.supp

A baseline is a baseline, not a verdict: nothing in it has been adjudicated.
Its only job is to make silence mean something.

Three producers, three shapes:

  TSan   fenced by ================== lines, stacks with #N frames
  UBSan  a 'runtime error:' line, then optional notes, then SUMMARY
  ASan   ==pid==ERROR: AddressSanitizer: <kind>

ASan findings are listed but never baselined.  An ASan report is a memory error
that stops the run; suppressing one hides a defect rather than a known-noisy
site.
"""
import argparse
import collections
import re
import sys

# ---------------------------------------------------------------- TSan

# Frames that are never the answer: the sanitizer itself, and the std::thread
# and std::invoke scaffolding every daemon stack goes through.
TSAN_NOISE = re.compile (
  r"(include/c\+\+/|libstdc\+\+\.so|__tsan|__sanitizer|<null> <null>|"
  r"\bstd::|/usr/lib/llvm)")
FRAME = re.compile (r"^\s+#(\d+) (.+?) (\S+?):(\d+)(?::\d+)? \(")
TSAN_WARNING = re.compile (r"^WARNING: ThreadSanitizer: (.+?) \(pid=")
TSAN_SUMMARY = re.compile (r"^SUMMARY: ThreadSanitizer: (.+?) (?:\(|/)")

# For a lock-order-inversion the top engine frame is always the locking
# primitive itself, which identifies nothing -- every rwlock in the engine goes
# through it, so a rule keyed on it would silence every future deadlock report.
# Skip the primitives and take the caller.
TSAN_LOCK_PRIMITIVE = re.compile (
  r"\b(rwlock_(read|write)_(lock|unlock)|rmutex_(lock|unlock)|csect_[a-z_]*(lock|enter|exit)"
  r"|pthread_(mutex|rwlock)_[a-z]+)\b")


def tsan_reports (lines):
  """Reports are fenced by ================== lines; anything outside is the
  program's own stdout, which interleaves freely and is dropped here."""
  cur, out = None, []
  for ln in lines:
    if ln.startswith ("=================="):
      if cur is not None:
        if any (TSAN_WARNING.match (x) or TSAN_SUMMARY.match (x) for x in cur):
          out.append (cur)
        cur = None
      else:
        cur = []
    elif cur is not None:
      cur.append (ln)
  return out


def tsan_classify (report):
  kind = "unknown"
  for ln in report:
    m = TSAN_WARNING.match (ln) or TSAN_SUMMARY.match (ln)
    if m:
      kind = m.group (1)
      break

  # The top frame that is engine code.  Everything above it is scaffolding, and
  # everything below it is context.
  skip_locks = "lock-order-inversion" in kind
  for ln in report:
    m = FRAME.match (ln)
    if not m or TSAN_NOISE.search (ln):
      continue
    if skip_locks and TSAN_LOCK_PRIMITIVE.search (ln):
      continue
    func, path, line = m.group (2), m.group (3), m.group (4)
    func = func.split ("(")[0].strip ()
    return "tsan", kind, f"{path.rsplit('/', 1)[-1]}:{line}", func
  return "tsan", kind, "?", "?"


def tsan_rule (kind, where, func):
  """TSan matches a suppression pattern against the function name and the file
  name both, so a plain function name is the tightest rule available.  A
  template instantiation's demangled name carries argument types and does not
  match reliably; fall back to the file for those."""
  pat = where.split (":")[0] if "<" in func else func
  if "race" in kind:
    rule = "race"
  elif "lock-order-inversion" in kind:
    rule = "deadlock"
  else:
    rule = "mutex"
  return rule + ":" + pat


# --------------------------------------------------------------- UBSan

UBSAN_ERROR = re.compile (r"^(\S+?):(\d+):(\d+): runtime error: (.+)$")

# UBSan prints 'undefined-behavior' in every SUMMARY line regardless of which
# check fired, so the check name -- which is what a suppression is keyed on --
# has to come back out of the message text.
UBSAN_KINDS = [
  (re.compile (r"misaligned address|member access within misaligned"), "alignment"),
  (re.compile (r"shift exponent"), "shift-exponent"),
  (re.compile (r"left shift of|shift of negative"), "shift-base"),
  (re.compile (r"through pointer to incorrect function type"), "function"),
  (re.compile (r"signed integer overflow"), "signed-integer-overflow"),
  (re.compile (r"unsigned integer overflow"), "unsigned-integer-overflow"),
  (re.compile (r"division by zero"), "integer-divide-by-zero"),
  (re.compile (r"null pointer passed as"), "nonnull-attribute"),
  (re.compile (r"null pointer|member call on null"), "null"),
  (re.compile (r"applying .* offset|pointer index expression"), "pointer-overflow"),
  (re.compile (r"not a valid value for type 'bool'"), "bool"),
  (re.compile (r"variable length array bound"), "vla-bound"),
  (re.compile (r"__builtin_unreachable"), "unreachable"),
  (re.compile (r"downcast of|member access within address"), "vptr"),
  (re.compile (r"outside the range of representable"), "float-cast-overflow"),
  (re.compile (r"through pointer to object of type|load of value"), "enum"),
]


def ubsan_kind (message):
  for pat, name in UBSAN_KINDS:
    if pat.search (message):
      return name
  return "undefined"


def ubsan_reports (lines):
  """A report starts at a 'runtime error:' line and runs to its SUMMARY, or to
  the next report if print_stacktrace is off and no SUMMARY is printed."""
  out, cur = [], None
  for ln in lines:
    if UBSAN_ERROR.match (ln):
      if cur:
        out.append (cur)
      cur = [ln]
    elif cur is not None:
      cur.append (ln)
      if ln.startswith ("SUMMARY: UndefinedBehaviorSanitizer"):
        out.append (cur)
        cur = None
  if cur:
    out.append (cur)
  return out


def ubsan_classify (report):
  m = UBSAN_ERROR.match (report[0])
  path, line, message = m.group (1), m.group (2), m.group (4)
  base = path.rsplit ("/", 1)[-1]
  # The message carries the offending address and its bytes, which differ every
  # run; keep only the part that identifies the check.
  short = re.sub (r"0x[0-9a-f]+", "0x…", message)
  return "ubsan", ubsan_kind (message), f"{base}:{line}", short[:70]


def ubsan_rule (kind, where, _detail):
  """UBSan suppressions are keyed on the check name and matched against the
  source file, with no line granularity -- so one rule silences the check for
  the whole file.  The leading * is needed because the pattern is matched
  against the full path as printed."""
  return f"{kind}:*{where.split(':')[0]}"


# ---------------------------------------------------------------- ASan

ASAN_ERROR = re.compile (r"^==\d+==ERROR: AddressSanitizer: (\S+)")


def asan_reports (lines):
  out, cur = [], None
  for ln in lines:
    if ASAN_ERROR.match (ln):
      if cur:
        out.append (cur)
      cur = [ln]
    elif cur is not None:
      cur.append (ln)
      if ln.startswith ("SUMMARY: AddressSanitizer"):
        out.append (cur)
        cur = None
  if cur:
    out.append (cur)
  return out


def asan_classify (report):
  kind = ASAN_ERROR.match (report[0]).group (1)
  for ln in report:
    m = FRAME.match (ln)
    if not m or TSAN_NOISE.search (ln):
      continue
    func = m.group (2).split ("(")[0].strip ()
    return "asan", kind, f"{m.group (3).rsplit ('/', 1)[-1]}:{m.group (4)}", func
  return "asan", kind, "?", "?"


# ----------------------------------------------------------------- main

PARSERS = [
  ("tsan", tsan_reports, tsan_classify, tsan_rule),
  ("ubsan", ubsan_reports, ubsan_classify, ubsan_rule),
  ("asan", asan_reports, asan_classify, None),
]


def main ():
  ap = argparse.ArgumentParser (
    description = "Group sanitizer reports; emit a baseline suppression file.")
  ap.add_argument ("log")
  ap.add_argument ("--suppress", nargs = "?", const = "auto",
                   choices = ["auto", "tsan", "ubsan"],
                   help = "emit a suppression file instead of the table")
  ap.add_argument ("--show", metavar = "SUBSTR",
                   help = "print the full text of reports whose row matches")
  args = ap.parse_args ()

  with open (args.log, errors = "replace") as f:
    lines = f.read ().splitlines ()

  rows = collections.Counter ()   # (tool, kind, where) -> n
  detail = {}                     # (tool, kind, where) -> function or message
  shown = []
  total = 0
  for tool, split, classify, _ in PARSERS:
    for r in split (lines):
      total += 1
      key = classify (r)
      rows[key[:3]] += 1
      detail[key[:3]] = key[3]
      if args.show and any (args.show in str (x) for x in key):
        shown.append (r)

  if args.show:
    for r in shown:
      print ("\n".join (r))
      print ("=" * 60)
    print (f"{len (shown)} of {total} reports", file = sys.stderr)
    return

  tools = {t for (t, _, _) in rows}

  if args.suppress:
    want = args.suppress
    if want == "auto":
      suppressible = tools & {"tsan", "ubsan"}
      if len (suppressible) != 1:
        sys.exit (f"log has reports from {sorted (tools) or ['nothing']}; "
                  "pass --suppress tsan or --suppress ubsan (they go in "
                  "different files)")
      want = suppressible.pop ()
    rule_of = dict ((t, r) for t, _, _, r in PARSERS)[want]
    print (f"# generated by sanitizer_triage.py --suppress {want}")
    print ("# A baseline, not a verdict: nothing here has been adjudicated.")
    print ("# Delete a rule to make that finding visible again.")
    print ("#")
    print ("# '#' is honoured only at the start of a line, so each rule's")
    print ("# comment sits above it; a trailing one becomes part of the pattern.")
    emitted = set ()
    for (tool, kind, where), n in rows.most_common ():
      if tool != want:
        continue
      rule = rule_of (kind, where, detail[(tool, kind, where)])
      if rule in emitted:
        continue
      emitted.add (rule)
      print (f"\n# {n}x  {kind}  at {where}  ({detail[(tool, kind, where)]})")
      print (rule)
    return

  print (f"{total} reports, {len (rows)} distinct\n")
  print (f"{'tool':<6} {'n':>5}  {'kind':<38}  {'site':<28}  detail")
  print ("-" * 118)
  for (tool, kind, where), n in rows.most_common ():
    print (f"{tool:<6} {n:>5}  {kind[:38]:<38}  {where[:28]:<28}  "
           f"{detail[(tool, kind, where)]}")

  if "asan" in tools:
    print ("\nASan findings are listed but never baselined: an ASan report is a "
           "memory error\nthat stops the run, so suppressing one hides a defect "
           "rather than a noisy site.")


if __name__ == "__main__":
  main ()
