# Uso: .\tools\report.ps1 [nome-do-log]  -> compila, roda o jogo e grava build\relatorio.txt
param([string]$Log = "run")
cmake --build build --parallel 4 2>&1 | Select-Object -Last 8
if ($LASTEXITCODE -ne 0) { Write-Host "COMPILACAO FALHOU: me mande as linhas acima." -ForegroundColor Red; exit 1 }
$log = "build\$Log.log"
.\build\mm10.exe 2>&1 | Tee-Object -FilePath $log | Out-Null
$r = "build\relatorio.txt"
"=== IOS ===" | Set-Content $r
Select-String -Path $log -Pattern '^\[IOS\]' | Select-Object -Last 40 | ForEach-Object Line | Add-Content $r
"=== OSREPORT ===" | Add-Content $r
Select-String -Path $log -Pattern '^\[OSReport\]' | Select-Object -Last 10 | ForEach-Object Line | Add-Content $r
"=== PARADA ===" | Add-Content $r
Select-String -Path $log -Pattern 'GUEST STOP' -Context 0,12 | ForEach-Object { $_.Line; $_.Context.PostContext } | Add-Content $r
Get-Content $r