#!/bin/sh
# PageTurn — листание страниц по сети для стандартной читалки PocketBook.
# Запускает сервер в фоне (под надзирателем, переживает USB) и показывает экран приложения.
# Все файлы лежат в скрытой системной папке /mnt/ext1/system/pageturn (не попадают в библиотеку).
D=/mnt/ext1/system/pageturn
T=/var/tmp
LIBS=$T/ptlibs
BIN=$T/pageturn
UI=$T/pageturn-ui
LOG=$D/pageturn.log
SUP=$T/pt_supervisor.sh

mkdir -p "$LIBS"
# системные библиотеки прошивки лежат на самой книге — копировать ничего не нужно
export LD_LIBRARY_PATH=/ebrmain/lib:/lib:/usr/lib
[ -d "$D/libs" ] && { [ -f "$LIBS/libinkview.so" ] || cp -f "$D/libs/"* "$LIBS/" 2>/dev/null; }

NEED=0
cmp -s "$D/pageturn" "$BIN" 2>/dev/null || { cp -f "$D/pageturn" "$BIN"; chmod +x "$BIN"; NEED=1; }
cp -f "$D/pageturn-ui" "$UI"
chmod +x "$UI"

SUPPID=$(cat "$T/pt_supervisor.pid" 2>/dev/null)
if [ -z "$SUPPID" ] || ! kill -0 "$SUPPID" 2>/dev/null; then NEED=1; fi

if [ "$NEED" = 1 ]; then
  [ -n "$SUPPID" ] && kill -9 "$SUPPID" 2>/dev/null
  killall -9 pageturn 2>/dev/null
  killall -9 pbnext 2>/dev/null
  cat > "$SUP" <<EOF
#!/bin/sh
echo \$\$ > $T/pt_supervisor.pid
while true; do
  $BIN "$LOG" eink-reader.app 57321
  RC=\$?
  echo "[\$(date +%T)] [supervisor] pageturn завершился (код \$RC), перезапуск через 3 с" >> "$LOG"
  sleep 3
done
EOF
  chmod 700 "$SUP"
  setsid nohup "$SUP" >/dev/null 2>&1 &
  sleep 2
fi

exec "$UI" >> "$D/ui.log" 2>&1
