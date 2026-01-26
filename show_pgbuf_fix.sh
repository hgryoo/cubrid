PID=$1
MAX=$2

gdb -p $PID -batch \
  -ex "set pagination off" \
  -ex "thread apply all bt" \
| awk -v max="$MAX" '
  /^Thread / {
    if (hit) {
      end = (start + max <= cnt) ? start + max : cnt
      print buf[1]
      for (i=pg; i<=end; i++) print buf[i]
      print ""
    }
    hit=0
    pg=0
    start=0
    cnt=0
    buf[++cnt]=$0
    next
  }

  {
    buf[++cnt]=$0

    if ($0 ~ /pgbuf_fix/) {
      hit=1; pg=cnt; start=cnt+1; reason="pgbuf_fix"
    }

    #else if ($0 ~ /std::unique_lock|std::lock_guard|std::mutex::lock/) {
    #  hit=1; trig=cnt; start=cnt+1; reason="std::mutex"
    #}

    #else if ($0 ~ /pthread_mutex_lock|__lll_lock_wait|futex|__futex_abstimed_wait_common/) {
    #  hit=1; trig=cnt; start=cnt+1; reason="pthread_mutex"
    #}

  }

  END {
    if (hit) {
      end = (start + max  <= cnt) ? start + max  : cnt
      for (i=pg; i<=end; i++) print buf[i]
    }
  }
'

