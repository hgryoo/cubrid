PID=$1
PREV=./gdb_bt.prev
CUR=./gdb_bt.cur

gdb -p $PID -batch \
  -ex "set pagination off" \
  -ex "thread apply all bt" > "$PREV"

while true; do
  gdb -p $PID -batch \
	 -ex "set pagination off" \
	 -ex "thread apply all bt" > "$CUR"
  
  diff -u "$PREV" "$CUR" | sed -n '/^[+-]/p'
  mv "$CUR" "$PREV"
  sleep 5
done

