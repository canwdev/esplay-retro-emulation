$ErrorActionPreference = "Stop"

$root = Join-Path $PSScriptRoot "sd\\many"
New-Item -ItemType Directory -Force -Path $root | Out-Null

for ($i = 1; $i -le 512; $i++) {
  $name = "{0:D4}.txt" -f $i
  $path = Join-Path $root $name
  if (-not (Test-Path $path)) {
    "file $i" | Set-Content -NoNewline -Encoding UTF8 $path
  }
}
