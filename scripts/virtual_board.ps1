# 本地虚拟板运行器，不访问串口或真实蓝牙。
[CmdletBinding()]
param(
    [ValidateSet('setup','doctor','build','test','debug')][string]$Action='doctor',
    [string]$BuildDir='build/virtual-board-build',
    [string]$IdfPath='',
    [string]$IdfPython='',
    [string]$Distribution='Ubuntu-20.04',
    [string]$Gdb='gdb-multiarch',
    [string]$SdkconfigDefaults='',
    [switch]$Security,
    [switch]$Running
)
$ErrorActionPreference='Stop'
$Root=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$Tools=Join-Path $Root 'build/virtual-board-tools'
function ResolveIdfPython {
    if ($IdfPython) {
        if (-not (Test-Path -LiteralPath $IdfPython -PathType Leaf)) { throw '指定IDF Python不存在' }
        return (Resolve-Path -LiteralPath $IdfPython).Path
    }
    if ($env:IDF_PYTHON_ENV_PATH) {
        $candidate=Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts/python.exe'
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    }
    $base=Join-Path $(if($env:IDF_TOOLS_PATH){$env:IDF_TOOLS_PATH}else{Join-Path $env:USERPROFILE '.espressif'}) 'python_env'
    $candidates=@(Get-ChildItem -LiteralPath $base -Directory | Where-Object Name -Like 'idf5.5_*' | ForEach-Object { Join-Path $_.FullName 'Scripts/python.exe' } | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf })
    if ($candidates.Count -ne 1) { throw '无法唯一确定IDF5.5 Python，请用-IdfPython指定' }
    return $candidates[0]
}
function WslPath([string]$Path){
    $result=& wsl -d $Distribution --exec wslpath -a $Path
    if($LASTEXITCODE -ne 0){throw 'WSL路径转换失败'}
    return ($result | Select-Object -Last 1).Trim()
}
function RunWsl([string[]]$Arguments){
    & wsl -d $Distribution --exec @Arguments
    if($LASTEXITCODE -ne 0){throw "WSL执行失败：$LASTEXITCODE"}
}
$RootLinux=WslPath $Root
$Venv="$RootLinux/build/virtual-board-tools/venv/bin/python"
$Emulator="$RootLinux/build/virtual-board-tools/esp-emu"
if($Action -eq 'setup'){
    RunWsl @('python3','-m','venv',"$RootLinux/build/virtual-board-tools/venv")
    RunWsl @($Venv,'-m','pip','install','-r',"$RootLinux/requirements-virtual-board.txt")
    RunWsl @($Venv,"$RootLinux/scripts/tools/virtual_board.py",'install','--destination',"$RootLinux/build/virtual-board-tools")
    exit 0
}
if($Action -eq 'doctor'){
    RunWsl @($Venv,"$RootLinux/scripts/tools/virtual_board.py",'doctor','--emulator',$Emulator)
    exit 0
}
$Build=if([IO.Path]::IsPathRooted($BuildDir)){$BuildDir}else{Join-Path $Root $BuildDir}
if($Action -eq 'build'){
    $Python=ResolveIdfPython
    if(-not $IdfPath){$IdfPath=if($env:IDF_PATH -and (Test-Path -LiteralPath (Join-Path $env:IDF_PATH 'tools/idf.py'))){$env:IDF_PATH}else{Join-Path $Root 'build/toolchain/esp-idf-v5.5.4'}}
    if(-not(Test-Path -LiteralPath (Join-Path $IdfPath 'tools/idf.py'))){throw '请指定ESP-IDF5.5.4路径'}
    $env:IDF_PATH=$IdfPath
    $env:IDF_PYTHON_ENV_PATH=Split-Path -Parent (Split-Path -Parent $Python)
    $env:IDF_CCACHE_ENABLE='1'
    . (Join-Path $IdfPath 'export.ps1')
    $Version=& $Python (Join-Path $IdfPath 'tools/idf.py') --version
    if($LASTEXITCODE -ne 0 -or $Version -notmatch 'v5\.5\.4'){throw '需要ESP-IDF5.5.4'}
    if(-not(Test-Path -LiteralPath $Build)){New-Item -ItemType Directory -Path $Build | Out-Null}
    $Defaults=Join-Path $Build 'sdkconfig.defaults'
    $BaseDefaults=Join-Path $Root 'sdkconfig.defaults'
    if($SdkconfigDefaults){
        if(-not (Test-Path -LiteralPath $SdkconfigDefaults -PathType Leaf)){throw '指定sdkconfig defaults不存在'}
        $Content=[IO.File]::ReadAllText($BaseDefaults,[Text.Encoding]::UTF8)+"`n"+[IO.File]::ReadAllText($SdkconfigDefaults,[Text.Encoding]::UTF8)+"`nCONFIG_BOARD_GENERIC_C3=y`n"
    } else {
        $Content=[IO.File]::ReadAllText($BaseDefaults,[Text.Encoding]::UTF8)+"`nCONFIG_BOARD_GENERIC_C3=y`n"
    }
    [IO.File]::WriteAllText($Defaults,$Content,[Text.UTF8Encoding]::new($false))
    Push-Location $Root
    try{
        & $Python (Join-Path $IdfPath 'tools/idf.py') --ccache -B $Build -D "SDKCONFIG=$(Join-Path $Build 'sdkconfig')" -D "SDKCONFIG_DEFAULTS=$Defaults" -D 'PORTAL_TEST_AUTO_CONFIG=OFF' reconfigure build
        if($LASTEXITCODE -ne 0){throw '本地虚拟板固件构建失败'}
    }finally{Pop-Location}
    exit 0
}
$SessionRoot=Join-Path $Root 'build/virtual-board-validation'
if(-not(Test-Path -LiteralPath $SessionRoot)){New-Item -ItemType Directory -Path $SessionRoot | Out-Null}
$Image=Join-Path $SessionRoot ("firmware-"+[Guid]::NewGuid().ToString('N')+'.bin')
$ImageLinux=WslPath $Image
$BuildLinux=WslPath $Build
RunWsl @($Venv,"$RootLinux/scripts/tools/virtual_board.py",'merge','--build-dir',$BuildLinux,'--output',$ImageLinux)
if($Action -eq 'debug'){
    RunWsl @($Venv,"$RootLinux/tests/virtual_board_gdb.py",'--emulator',$Emulator,'--firmware',$ImageLinux,'--elf',"$BuildLinux/bikepower-c.elf",'--gdb',$Gdb)
    exit 0
}
$Git=& git -C $Root rev-parse --short HEAD
if($LASTEXITCODE -ne 0){throw '读取Git身份失败'}
$ArgsList=@($Venv,"$RootLinux/tests/virtual_board_e2e.py",'--emulator',$Emulator,'--firmware',$ImageLinux,'--elf',"$BuildLinux/bikepower-c.elf",'--expected-git',$Git.Trim())
if($Security){$ArgsList+='--security'}
if($Running){$ArgsList+='--running'}
RunWsl $ArgsList
