<#
  Adds the PageTurn icon to the PocketBook application menu (Windows, PowerShell 5.1+).

  Prerequisite: PageTurn-book-*.zip is already extracted into the root of the book's drive.

  What it does:
    1. backs up system\config\desktop\view.json and hash.txt (*.pageturn-backup, only once);
    2. adds a "U_pageturn" entry to view.json and lists it in the @General group;
    3. recomputes hash.txt (hash2 = MD5 of view.json, trailer = CRC32 of the hash block);
    4. validates the JSON; on any error restores the backups.

  Usage:   .\add-icon.ps1                 (auto-detects the book drive)
           .\add-icon.ps1 -Root E:\       (explicit drive)
           .\add-icon.ps1 -Root E:\ -Remove   (restore the original files)

  Tested only on a PocketBook 629 (Verse). Other firmware versions may use a different format - use at your own risk.
#>
param([string]$Root, [switch]$Remove)
$ErrorActionPreference = "Stop"

if (-not $Root) {
  $Root = (Get-PSDrive -PSProvider FileSystem | Where-Object { Test-Path (Join-Path $_.Root "system\config\desktop\view.json") } | Select-Object -First 1).Root
  if (-not $Root) { throw "PocketBook drive not found. Connect the book via USB or pass -Root E:\" }
}
$d = Join-Path $Root "system\config\desktop"
if (-not (Test-Path "$d\view.json")) { throw "$d\view.json not found - is $Root a PocketBook drive?" }

if ($Remove) {
  foreach ($n in "view.json", "hash.txt") {
    if (Test-Path "$d\$n.pageturn-backup") { Copy-Item "$d\$n.pageturn-backup" "$d\$n" -Force; "restored $n" }
    else { "no backup for $n" }
  }
  return
}

foreach ($f in "system\pageturn\icons\desktop_app_pageturn.bmp", "system\pageturn\icons\desktop_app_pageturn_f.bmp", "applications\PageTurn.app") {
  if (-not (Test-Path (Join-Path $Root $f))) { throw "$f is missing - extract PageTurn-book-*.zip into the root of the drive first" }
}

Add-Type -TypeDefinition @'
public static class PtCrc {
  static uint[] T = Make();
  static uint[] Make() { var t = new uint[256]; for (uint i = 0; i < 256; i++) { uint c = i; for (int j = 0; j < 8; j++) c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1; t[i] = c; } return t; }
  public static uint Crc32(byte[] d) { uint c = 0xFFFFFFFFu; foreach (var b in d) c = T[(c ^ b) & 0xFF] ^ (c >> 8); return c ^ 0xFFFFFFFFu; }
}
'@

$utf8 = New-Object System.Text.UTF8Encoding($false)
foreach ($n in "view.json", "hash.txt") {
  if (-not (Test-Path "$d\$n.pageturn-backup")) { Copy-Item "$d\$n" "$d\$n.pageturn-backup" }
}

try {
  $v = [System.IO.File]::ReadAllText("$d\view.json", $utf8)
  if ($v.Contains('"U_pageturn"')) { "view.json already contains U_pageturn - nothing to change"; return }

  $nl = if ($v.Contains("`r`n")) { "`r`n" } else { "`n" }
  $entry = '        "U_pageturn": {' + $nl +
    '            "focused_icon": {' + $nl + '                "path": "system/pageturn/icons/desktop_app_pageturn_f.bmp"' + $nl + '            },' + $nl +
    '            "icon": {' + $nl + '                "path": "system/pageturn/icons/desktop_app_pageturn.bmp"' + $nl + '            },' + $nl +
    '            "path": "/mnt/ext1/applications/PageTurn.app",' + $nl +
    '            "title": "PageTurn"' + $nl + '        },' + $nl
  $marker = '    "applications": {' + $nl
  $i = $v.IndexOf($marker)
  if ($i -lt 0) { throw "unexpected view.json format: no applications block" }
  $v = $v.Insert($i + $marker.Length, $entry)

  # add to the first group's app list, after the last entry of "@General"
  $g = $v.IndexOf('"@General"')
  if ($g -lt 0) { throw "unexpected view.json format: no @General group" }
  $a = $v.IndexOf('"apps": [', $g)
  $e = $v.IndexOf(']', $a)
  if ($a -lt 0 -or $e -lt 0) { throw "unexpected view.json format: no apps list" }
  $list = $v.Substring($a, $e - $a).TrimEnd()
  $tail = $v.Substring($e)
  $v = $v.Substring(0, $a) + $list + ',' + $nl + '                    "U_pageturn"' + $nl + '                ' + $tail
  [System.IO.File]::WriteAllText("$d\view.json", $v, $utf8)

  $obj = Get-Content "$d\view.json" -Raw -Encoding UTF8 | ConvertFrom-Json
  if ($obj.applications.U_pageturn.title -ne "PageTurn" -or -not ($obj.view.groups[0].apps -contains "U_pageturn")) { throw "validation failed" }

  $old = [System.IO.File]::ReadAllText("$d\hash.txt", $utf8)
  $hash1 = ([regex]::Match($old, "hash1=([0-9a-f]+)")).Groups[1].Value
  if (-not $hash1) { throw "unexpected hash.txt format" }
  $md5 = [System.BitConverter]::ToString([System.Security.Cryptography.MD5]::Create().ComputeHash([System.IO.File]::ReadAllBytes("$d\view.json"))).Replace("-", "").ToLower()
  $body = "hash1=$hash1`nhash2=$md5`n"
  $crc = [PtCrc]::Crc32($utf8.GetBytes($body))
  [System.IO.File]::WriteAllText("$d\hash.txt", $body + "`n#" + ("{0:x8}" -f $crc) + "`n", $utf8)
  "Done. Disconnect the book and reboot it; the PageTurn icon appears in the application menu."
  "To undo: .\add-icon.ps1 -Root $Root -Remove"
}
catch {
  foreach ($n in "view.json", "hash.txt") { Copy-Item "$d\$n.pageturn-backup" "$d\$n" -Force }
  Write-Error "Failed, originals restored: $_"
}
