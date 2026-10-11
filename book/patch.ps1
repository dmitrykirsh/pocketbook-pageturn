param([string[]]$Files)
foreach ($f in $Files) {
  $fs = [System.IO.File]::Open($f, 'Open', 'ReadWrite'); $fs.Position = 36
  $fs.Write([BitConverter]::GetBytes([uint32]0x05000200), 0, 4); $fs.Close()
  $bytes = [System.IO.File]::ReadAllBytes($f)
  $needle = [System.Text.Encoding]::ASCII.GetBytes("ld-linux-armhf.so.3`0")
  $found = -1
  for ($i = 0; $i -le $bytes.Length - $needle.Length; $i++) {
    $m = $true
    for ($j = 0; $j -lt $needle.Length; $j++) { if ($bytes[$i+$j] -ne $needle[$j]) { $m = $false; break } }
    if ($m) { $found = $i; break }
  }
  if ($found -lt 0) { throw "needle not found in $f" }
  $repl = [System.Text.Encoding]::ASCII.GetBytes("ld-linux.so.3")
  for ($k = 0; $k -lt $needle.Length; $k++) { $bytes[$found+$k] = 0 }
  for ($k = 0; $k -lt $repl.Length; $k++) { $bytes[$found+$k] = $repl[$k] }
  [System.IO.File]::WriteAllBytes($f, $bytes)
  "patched $(Split-Path $f -Leaf): needle at $found, size $($bytes.Length)"
}
