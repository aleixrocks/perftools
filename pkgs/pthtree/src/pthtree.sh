#!/usr/bin/env bash
# pthtree — wrapper for libpthtree.so that prints a pthread hierarchy tree
#
# -----------------------------------------------------------------------------
# SYNOPSIS
#   pthtree [OPTIONS] -- <command> [args ...]
#   pthtree [OPTIONS] <command> [args ...]      # "--" optional if <command>
#
# OPTIONS
#   -v, --verbose       Show *full* shared‑library paths instead of basenames.
#                       (exports PTHTREE_VERBOSE=1)
#
#   -d, --delay-dump    Delay the final tree‑dump until every thread has
#                       finished. *Risky*: if the main thread terminates before
#                       the last thread, **no dump will be produced**. Use this
#                       only when you see "?" CPU‑time fields and want to give
#                       threads more time to finish.
#                       (exports PTHTREE_DELAY=1)
#
#   -h, --help          Print this help and exit.
#
# ENVIRONMENT
#   PTHTREE_LIB         Path to the interposer (default: libpthtree.so that sits
#                       next to this script).
# -----------------------------------------------------------------------------
# The script prepends the interposer to LD_PRELOAD, sets the requested PTHTREE_*
# flags, then execs the target command.

set -euo pipefail

print_help() {
  sed -n '/^# pthtree/,/^set -euo/p' "$0" | sed 's/^# //; /^set -euo/d'
}

# ---------------- locate lib ---------------------------------------------------
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)"
LIB="${PTHTREE_LIB:-$script_dir/../lib/libpthtree.so}"
if [[ ! -f $LIB ]]; then
  echo "pthtree: cannot find libpthtree.so (looked at $LIB)" >&2
  exit 1
fi

# ---------------- parse options -----------------------------------------------
verbose=0
delay=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    -v|--verbose)
      verbose=1; shift ;;
    -d|--delay-dump)
      delay=1; shift ;;
    -h|--help)
      print_help; exit 0 ;;
    --)
      shift; break ;;
    -*?)
      echo "pthtree: unknown option: $1" >&2; echo "Try 'pthtree --help'" >&2; exit 1 ;;
    *)
      break ;;
  esac
done

if [[ $# -eq 0 ]]; then
  echo "pthtree: missing command to run" >&2
  echo "Try 'pthtree --help'" >&2
  exit 1
fi

# ---------------- export environment ------------------------------------------
export LD_PRELOAD="$LIB${LD_PRELOAD:+:$LD_PRELOAD}"
[[ $verbose -eq 1 ]] && export PTHTREE_VERBOSE=1
[[ $delay   -eq 1 ]] && export PTHTREE_DELAY=1

# ---------------- hand off -----------------------------------------------------
exec "$@"

