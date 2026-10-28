# run_tests.ps1 - build the tools, lint the RTL, and run every test
#
#   powershell -ExecutionPolicy Bypass -File run_tests.ps1 [-Random 50]
#
# Needs GCC (WinLibs) and Icarus Verilog. Both are looked up in their default
# install locations. Exit status is 0 only if everything passes.
param([int]$Random = 50)

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

# ---------------------------------------------------------------- toolchain
$gccDir = (Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Recurse -Filter gcc.exe -ErrorAction SilentlyContinue |
           Select-Object -First 1).DirectoryName
$ivDir  = 'C:\iverilog\bin'
if (-not $gccDir)                                   { Write-Host 'GCC not found (winget install BrechtSanders.WinLibs.POSIX.UCRT)'; exit 2 }
if (-not (Test-Path "$ivDir\iverilog.exe"))         { Write-Host 'Icarus Verilog not found (winget install Icarus.Verilog)'; exit 2 }
$env:Path = "$gccDir;$ivDir;$env:Path"

New-Item -ItemType Directory -Force build | Out-Null

# -------------------------------------------------------------------- tools
foreach ($t in 'rvasm', 'rviss', 'rvgen', 'hdl_lint') {
    $src = "tools\$t.c"
    $exe = "build\$t.exe"
    if (-not (Test-Path $exe) -or (Get-Item $src).LastWriteTime -gt (Get-Item $exe).LastWriteTime) {
        Write-Host "building $t"
        & gcc -Wall -Wextra -O2 -o $exe $src
        if ($LASTEXITCODE -ne 0) { Write-Host "BUILD FAILED: $t"; exit 1 }
    }
}

$failures = 0
function Fail($msg) { Write-Host "  FAIL  $msg" -ForegroundColor Red; $script:failures++ }
function Pass($msg) { Write-Host "  pass  $msg" -ForegroundColor Green }

# --------------------------------------------------------------------- lint
Write-Host "`n[lint]"
$null = & build\hdl_lint.exe -x 7 2 tools\lint_selftest\bad.v
if ($LASTEXITCODE -eq 0) { Pass 'lint self-test (finds all planted problems)' } else { Fail 'lint self-test' }

$hdl = @(Get-ChildItem rtl -Recurse -Filter *.v | ForEach-Object { $_.FullName }) + (Resolve-Path tb\top_tb.v).Path
$out = & build\hdl_lint.exe $hdl
if ($LASTEXITCODE -eq 0) { Pass "rtl and testbench are lint clean" } else { $out; Fail 'lint found problems in the design' }

# ------------------------------------------------------------ compile design
Write-Host "`n[compile]"
$out = & iverilog -g2012 -Wall -I rtl -o build\sim.vvp tb\top_tb.v 2>&1
if ($LASTEXITCODE -eq 0 -and -not $out) { Pass 'iverilog -Wall: no errors, no warnings' }
else { $out | ForEach-Object { "$_" }; Fail 'iverilog compile'; exit 1 }

# ---------------------------------------------------------------- run a test
function Run-Test($name, $asm, $expects) {
    $hex  = "build\$name.hex"
    $dump = "build\$name.rtl.txt"
    $o = & build\rvasm.exe $asm $hex
    if ($LASTEXITCODE -ne 0) { $o; Fail "$name (assembler)"; return }

    $o = & vvp build\sim.vvp "+PROG=$hex" "+DUMP=$dump"
    $rtlInfo = ($o | Where-Object { $_ -like 'RTL:*' }) -join ''

    $args = @($hex, '-c', $dump)
    foreach ($e in $expects) { $args += @('-e', $e) }
    $m = & build\rviss.exe @args
    if ($LASTEXITCODE -eq 0) {
        Pass ("{0,-18} {1}   [{2}]" -f $name, (($m | Where-Object { $_ -like 'model:*' }) -join ''), $rtlInfo.Trim())
    } else {
        $m | ForEach-Object { "    $_" }
        Fail $name
    }
}

# ------------------------------------------------------------ directed tests
Write-Host "`n[directed tests: RTL vs C model, plus hand-checked results]"
$directed = [ordered]@{
    'test_alu'     = @('x3=93','x4=107','x5=96','x6=0xFFFFFFFD','x7=0xFFFFFF9D','x10=800','x11=0x1FFFFFFF',
                       'x12=0xFFFFFFFF','x13=1','x14=0','x15=0','x16=1','x27=0x1064','x28=0x12345678',
                       'x29=0x1234FFFF','x31=0x80000000')
    'test_hazards' = @('x2=6','x3=11','x4=22','x5=16','x6=22','x7=23','x9=23','x11=46','x13=77','x14=5',
                       'x15=1024','x17=128','x18=22','x21=0','x23=0','x24=1')
    'test_branch'  = @('x22=55','x28=0x7f','x20=55','x21=21')
    'test_sort'    = @('x20=120','m32=0xFFFFFFCE','m39=100','m48=0xFFFFFFCE')
    'test_subword' = @('x2=127','x3=0xFFFFFFFF','x4=0xFFFFFF81','x5=0xFFFFFF80','x6=255','x7=128',
                       'x8=0xFFFFFF7F','x9=0xFFFF8081','x11=0xFF7F','x12=0x8081','x15=0xBEEFCD7F',
                       'x16=0xCDEFBEEF','x17=0xFFFFFFCD','x18=0xFFFFFFCE','m64=0xCDEFBEEF','m65=0xCDCD')
    'test_muldiv'  = @('x3=0xFFFFFFEB','x4=0xFFFFFFFF','x5=6','x6=6','x7=0xFFFFFFFF','x8=0xFFFFFFFE',
                       'x9=1','x12=0xFFFFFFFE','x13=0xFFFFFFFF','x14=0','x15=7','x16=0xFFFFFFFF',
                       'x17=7','x18=0xFFFFFFFF','x19=7','x20=0x80000000','x21=0','x22=49','x23=49','x24=56')
}
foreach ($k in $directed.Keys) { Run-Test $k "tests\$k.s" $directed[$k] }

# --------------------------------------------------------------- random tests
Write-Host "`n[random programs: $Random seeds, RTL vs C model]"
$randFail = 0
for ($seed = 1; $seed -le $Random; $seed++) {
    $name = "rand_$seed"
    $null = & build\rvgen.exe $seed "build\$name.s"
    $null = & build\rvasm.exe "build\$name.s" "build\$name.hex"
    if ($LASTEXITCODE -ne 0) { Fail "$name (assembler)"; $randFail++; continue }
    $null = & vvp build\sim.vvp "+PROG=build\$name.hex" "+DUMP=build\$name.rtl.txt"
    $m = & build\rviss.exe "build\$name.hex" -c "build\$name.rtl.txt"
    if ($LASTEXITCODE -ne 0) { $m | ForEach-Object { "    $_" }; Fail "$name (seed $seed)"; $randFail++ }
}
if ($randFail -eq 0) { Pass "$Random random programs match the model" }

# ------------------------------------------------------------------- summary
Write-Host ''
if ($failures -eq 0) { Write-Host 'ALL TESTS PASSED' -ForegroundColor Green; exit 0 }
Write-Host "$failures FAILURE(S)" -ForegroundColor Red
exit 1
