[CmdletBinding()]
param(
    [switch]$Stop,
    [switch]$NoBrowser,
    [int]$MySqlPort = 3307,
    [int]$FastApiPort = 8000,
    [int]$WebPort = 8080,
    [string]$DbUsername = "root",
    [string]$DbPassword = $env:DB_PASSWORD,
    [string]$PythonExecutable = $env:SMOKE_PYTHON,
    [string]$MavenExecutable = $env:MAVEN_CMD
)

$ErrorActionPreference = "Stop"

$productRoot = (Resolve-Path -LiteralPath $PSScriptRoot).Path
$workspaceRoot = (Resolve-Path -LiteralPath (Join-Path $productRoot "..")).Path
$runtimeRoot = Join-Path $workspaceRoot ".runtime"
$logRoot = Join-Path $runtimeRoot "logs"
$stateFile = Join-Path $runtimeRoot "uav-v0.5-services.json"
$springRoot = Join-Path $productRoot "web\springboot\uav-video-platform"

function Write-Step([string]$Message) {
    Write-Host "[UAV V0.5] $Message" -ForegroundColor Cyan
}

function Resolve-Executable {
    param(
        [string]$ConfiguredPath,
        [string[]]$CommandNames,
        [string[]]$CandidatePaths,
        [string]$DisplayName
    )

    if ($ConfiguredPath) {
        if (Test-Path -LiteralPath $ConfiguredPath -PathType Leaf) {
            return (Resolve-Path -LiteralPath $ConfiguredPath).Path
        }
        $configuredCommand = Get-Command $ConfiguredPath -ErrorAction SilentlyContinue
        if ($configuredCommand) {
            return $configuredCommand.Source
        }
        throw "$DisplayName path does not exist: $ConfiguredPath"
    }

    foreach ($candidatePath in $CandidatePaths) {
        if ($candidatePath -and (Test-Path -LiteralPath $candidatePath -PathType Leaf)) {
            return (Resolve-Path -LiteralPath $candidatePath).Path
        }
    }

    foreach ($commandName in $CommandNames) {
        $command = Get-Command $commandName -ErrorAction SilentlyContinue
        if ($command) {
            return $command.Source
        }
    }

    throw "$DisplayName was not found. Install it or pass its path to start.ps1."
}

function Test-TcpPort {
    param(
        [string]$HostName,
        [int]$Port,
        [int]$TimeoutMilliseconds = 500
    )

    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $connection = $client.BeginConnect($HostName, $Port, $null, $null)
        if (-not $connection.AsyncWaitHandle.WaitOne($TimeoutMilliseconds)) {
            return $false
        }
        $client.EndConnect($connection)
        return $true
    } catch {
        return $false
    } finally {
        $client.Dispose()
    }
}

function Wait-TcpPort {
    param(
        [string]$Name,
        [string]$HostName,
        [int]$Port,
        [int]$TimeoutSeconds = 45
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        if (Test-TcpPort -HostName $HostName -Port $Port) {
            Write-Step "$Name is ready at $HostName`:$Port"
            return
        }
        Start-Sleep -Milliseconds 500
    }
    throw "$Name did not start within $TimeoutSeconds seconds. Check logs: $logRoot"
}

function Get-LiveManagedServices {
    if (-not (Test-Path -LiteralPath $stateFile -PathType Leaf)) {
        return @()
    }

    try {
        $state = Get-Content -Raw -Encoding UTF8 -LiteralPath $stateFile | ConvertFrom-Json
        return @($state.services | Where-Object {
            Get-Process -Id $_.pid -ErrorAction SilentlyContinue
        })
    } catch {
        Write-Warning "The old service state file could not be read."
        return @()
    }
}

function Stop-ManagedServices {
    $services = @(Get-LiveManagedServices)
    if ($services.Count -eq 0) {
        Write-Step "No running services managed by start.ps1 were found."
        return
    }

    [array]::Reverse($services)
    foreach ($service in $services) {
        $process = Get-Process -Id $service.pid -ErrorAction SilentlyContinue
        if (-not $process) {
            continue
        }

        $recordedStart = [DateTime]::Parse($service.startedAt).ToUniversalTime()
        $actualStart = $process.StartTime.ToUniversalTime()
        if ([Math]::Abs(($actualStart - $recordedStart).TotalSeconds) -gt 2) {
            Write-Warning "Skipping $($service.name): PID $($service.pid) was reused."
            continue
        }

        Write-Step "Stopping $($service.name) (PID $($service.pid))"
        Stop-Process -Id $service.pid -Force
    }

    if (Test-Path -LiteralPath $stateFile -PathType Leaf) {
        Remove-Item -LiteralPath $stateFile -Force
    }
    Write-Step "Platform services have stopped."
}

function Start-LoggedProcess {
    param(
        [string]$Name,
        [string]$FilePath,
        [string[]]$ArgumentList,
        [string]$WorkingDirectory,
        [string]$LogPrefix
    )

    $stdoutPath = Join-Path $logRoot "$LogPrefix.out.log"
    $stderrPath = Join-Path $logRoot "$LogPrefix.err.log"
    $process = Start-Process `
        -FilePath $FilePath `
        -ArgumentList $ArgumentList `
        -WorkingDirectory $WorkingDirectory `
        -WindowStyle Hidden `
        -RedirectStandardOutput $stdoutPath `
        -RedirectStandardError $stderrPath `
        -PassThru

    Write-Step "$Name started (PID $($process.Id))"
    return [pscustomobject]@{
        name = $Name
        pid = $process.Id
        startedAt = $process.StartTime.ToUniversalTime().ToString("o")
    }
}

if ($Stop) {
    Stop-ManagedServices
    exit 0
}

New-Item -ItemType Directory -Path $logRoot -Force | Out-Null
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$managedServices = [System.Collections.Generic.List[object]]::new()
foreach ($existingService in @(Get-LiveManagedServices)) {
    $managedServices.Add($existingService)
}

try {
    if (Test-TcpPort -HostName "127.0.0.1" -Port $MySqlPort) {
        Write-Step "Reusing MySQL on port $MySqlPort."
    } else {
        $mysqlBase = Get-ChildItem `
            -LiteralPath (Join-Path $runtimeRoot "mysql-dist") `
            -Directory `
            -ErrorAction SilentlyContinue |
            Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName "bin\mysqld.exe") } |
            Select-Object -First 1
        if (-not $mysqlBase) {
            throw "Local MySQL was not found under .runtime\mysql-dist."
        }

        $mysqlData = Join-Path $runtimeRoot "mysql-data"
        if (-not (Test-Path -LiteralPath $mysqlData -PathType Container)) {
            throw "MySQL data directory was not found: $mysqlData"
        }

        $mysqlProcess = Start-LoggedProcess `
            -Name "MySQL" `
            -FilePath (Join-Path $mysqlBase.FullName "bin\mysqld.exe") `
            -ArgumentList @(
                "--no-defaults",
                "--basedir=$($mysqlBase.FullName)",
                "--datadir=$mysqlData",
                "--port=$MySqlPort",
                "--bind-address=127.0.0.1",
                "--console"
            ) `
            -WorkingDirectory $workspaceRoot `
            -LogPrefix "$timestamp-mysql"
        $managedServices.Add($mysqlProcess)
        Wait-TcpPort -Name "MySQL" -HostName "127.0.0.1" -Port $MySqlPort

        # On Windows mysqld can leave the listener in a child process. Track
        # every process from this exact distribution so -Stop and rollback
        # close both the launcher and the server process.
        $mysqlExecutable = (Resolve-Path -LiteralPath (
            Join-Path $mysqlBase.FullName "bin\mysqld.exe"
        )).Path
        foreach ($serverProcess in @(Get-Process -Name "mysqld" -ErrorAction SilentlyContinue)) {
            $alreadyManaged = @(
                $managedServices | Where-Object { $_.pid -eq $serverProcess.Id }
            ).Count -gt 0
            if ($serverProcess.Path -eq $mysqlExecutable -and -not $alreadyManaged) {
                $managedServices.Add([pscustomobject]@{
                    name = "MySQL"
                    pid = $serverProcess.Id
                    startedAt = $serverProcess.StartTime.ToUniversalTime().ToString("o")
                })
            }
        }
    }

    if (Test-TcpPort -HostName "127.0.0.1" -Port $FastApiPort) {
        Write-Step "Reusing FastAPI on port $FastApiPort."
    } else {
        $pythonCandidates = @(
            $(if ($env:CONDA_PREFIX) { Join-Path $env:CONDA_PREFIX "python.exe" }),
            $(if ($env:USERPROFILE) {
                Join-Path $env:USERPROFILE "miniconda3\envs\psdk\python.exe"
            })
        )
        $python = Resolve-Executable `
            -ConfiguredPath $PythonExecutable `
            -CommandNames @("python.exe", "python") `
            -CandidatePaths $pythonCandidates `
            -DisplayName "Python"

        $env:SMOKE_MODEL_PATH = Join-Path $productRoot "models\yolov8s.pt"
        $env:SMOKE_DEVICE = $(if ($env:SMOKE_DEVICE) { $env:SMOKE_DEVICE } else { "0" })
        $env:SMOKE_API_RESULTS_ROOT = Join-Path $productRoot "results\api_tasks"

        $fastApiProcess = Start-LoggedProcess `
            -Name "FastAPI" `
            -FilePath $python `
            -ArgumentList @(
                "-m", "uvicorn", "ai_service.api:app",
                "--app-dir", "python",
                "--host", "127.0.0.1",
                "--port", "$FastApiPort"
            ) `
            -WorkingDirectory $productRoot `
            -LogPrefix "$timestamp-fastapi"
        $managedServices.Add($fastApiProcess)
        Wait-TcpPort -Name "FastAPI" -HostName "127.0.0.1" -Port $FastApiPort
    }

    if (Test-TcpPort -HostName "127.0.0.1" -Port $WebPort) {
        Write-Step "Reusing Spring Boot on port $WebPort."
    } else {
        $mavenCandidates = @()
        if ($env:USERPROFILE) {
            $mavenCache = Join-Path $env:USERPROFILE ".m2\wrapper\dists"
            if (Test-Path -LiteralPath $mavenCache -PathType Container) {
                $mavenCandidates = @(
                    Get-ChildItem -LiteralPath $mavenCache -Recurse -Filter "mvn.cmd" `
                        -File -ErrorAction SilentlyContinue |
                        Sort-Object LastWriteTime -Descending |
                        Select-Object -ExpandProperty FullName
                )
            }
        }
        $maven = Resolve-Executable `
            -ConfiguredPath $MavenExecutable `
            -CommandNames @("mvn.cmd", "mvn") `
            -CandidatePaths $mavenCandidates `
            -DisplayName "Maven"

        Write-Step "Building the Spring Boot application."
        Push-Location $springRoot
        try {
            & $maven "-DskipTests" package
            if ($LASTEXITCODE -ne 0) {
                throw "Spring Boot build failed with Maven exit code $LASTEXITCODE."
            }
        } finally {
            Pop-Location
        }

        $jar = Get-ChildItem -LiteralPath (Join-Path $springRoot "target") `
            -Filter "uav-video-platform-*.jar" -File |
            Where-Object { $_.Name -notlike "*.original" } |
            Sort-Object LastWriteTime -Descending |
            Select-Object -First 1
        if (-not $jar) {
            throw "The Spring Boot JAR was not found after the Maven build."
        }

        $javaCandidates = @(
            $(if ($env:JAVA_HOME) { Join-Path $env:JAVA_HOME "bin\java.exe" })
        )
        $java = Resolve-Executable `
            -ConfiguredPath "" `
            -CommandNames @("java.exe", "java") `
            -CandidatePaths $javaCandidates `
            -DisplayName "Java"

        $env:DB_HOST = "127.0.0.1"
        $env:DB_PORT = "$MySqlPort"
        $env:DB_USERNAME = $DbUsername
        $env:SMOKE_ANALYSIS_URL = "http://127.0.0.1:$FastApiPort"
        if ($DbPassword) {
            $env:DB_PASSWORD = $DbPassword
        }

        $springArguments = @(
            "-jar",
            $jar.FullName,
            "--server.port=$WebPort"
        )
        if (-not $DbPassword) {
            $springArguments += "--spring.datasource.password="
        }

        $springProcess = Start-LoggedProcess `
            -Name "Spring Boot" `
            -FilePath $java `
            -ArgumentList $springArguments `
            -WorkingDirectory $springRoot `
            -LogPrefix "$timestamp-spring"
        $managedServices.Add($springProcess)
        Wait-TcpPort -Name "Spring Boot" -HostName "127.0.0.1" -Port $WebPort -TimeoutSeconds 60
    }

    [pscustomobject]@{
        createdAt = [DateTime]::UtcNow.ToString("o")
        services = @($managedServices)
    } | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 -LiteralPath $stateFile

    Write-Host ""
    Write-Host "Platform started successfully:" -ForegroundColor Green
    Write-Host "  Website:  http://127.0.0.1:$WebPort"
    Write-Host "  FastAPI:  http://127.0.0.1:$FastApiPort/docs"
    Write-Host "  Logs:     $logRoot"
    Write-Host "  Stop:     powershell -ExecutionPolicy Bypass -File `"$productRoot\start.ps1`" -Stop"

    if (-not $NoBrowser) {
        Start-Process "http://127.0.0.1:$WebPort"
    }
} catch {
    Write-Error $_
    $newServices = @($managedServices)
    [array]::Reverse($newServices)
    foreach ($service in $newServices) {
        Stop-Process -Id $service.pid -Force -ErrorAction SilentlyContinue
    }
    exit 1
}
