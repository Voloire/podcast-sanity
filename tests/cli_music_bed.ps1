param([string]$Render)
$ErrorActionPreference = 'Stop'
$t = Join-Path $env:SANITY_TMP 'cli-music'
New-Item -ItemType Directory -Force $t | Out-Null
& $Render --gen-corpus "$t\voice.wav" --profile studio --seconds 10 --quiet
if ($LASTEXITCODE -ne 0) { throw "voice generation failed: $LASTEXITCODE" }
& $Render --gen-corpus "$t\bed.wav" --profile studio --seconds 4 --quiet   # any audio works as a bed
if ($LASTEXITCODE -ne 0) { throw "bed generation failed: $LASTEXITCODE" }
& $Render "$t\voice.wav" --out "$t\mix.wav" --music "$t\bed.wav" --music-tail 1 --report "$t\mix.json" --quiet
if ($LASTEXITCODE -ne 0) { throw "render failed: $LASTEXITCODE" }
$j = Get-Content "$t\mix.json" -Raw | ConvertFrom-Json
if (-not ($j.stages | Where-Object { $_.name -eq 'music_bed' -and $_.enabled })) { throw 'music_bed stage missing' }
if ($j.output.frames -ne $j.input.frames + 48000) { throw "tail not added: $($j.output.frames) vs $($j.input.frames)" }
'OK music bed'
