# Uso: .\tools\rebuild.ps1 [nome-do-log]  -> regera a traducao, compila, roda e grava build\relatorio.txt
param([string]$Log = "run")
python ..\WiiRecomp\recompile_wii.py private\main.dol --no-scaffold --config-dir games\megaman10 --out runtime\src\generated 2>&1 | Select-Object -Last 8
if ($LASTEXITCODE -ne 0) { Write-Host "TRADUCAO FALHOU" -ForegroundColor Red; exit 1 }
.\tools\report.ps1 $Log
