param(
    [Parameter(Mandatory=$false)]
    [string]$Command,

    [Parameter(Mandatory=$false)]
    [string]$ExampleName
)

$BUILD_DIR = "build"
$EXAMPLES_DIR = "examples"

if (-not $Command) {
    Write-Host "Usage: build.ps1 [clean|build-example example_name|build-all]"
    Write-Host ""
    Write-Host "Commands:"
    Write-Host "  clean              - Clean and rebuild CMake project"
    Write-Host "  build-example name - Build specified example and copy exe to example directory"
    Write-Host "  build-all          - Build all examples and copy exes to their directories"
    exit 1
}

if ($Command -eq "clean") {
    Write-Host "Cleaning and rebuilding CMake project..."
    if (Test-Path $BUILD_DIR) {
        Remove-Item -Recurse -Force $BUILD_DIR
    }
    New-Item -ItemType Directory -Path $BUILD_DIR
    Set-Location $BUILD_DIR
    cmake ..
    Set-Location ..
    Write-Host "CMake project cleaned and rebuilt."
    exit 0
}

if ($Command -eq "build-example") {
    if (-not $ExampleName) {
        Write-Host "Error: Example name required."
        exit 1
    }
    
    if (-not (Test-Path "$EXAMPLES_DIR\$ExampleName")) {
        Write-Host "Error: Example '$ExampleName' does not exist."
        exit 1
    }
    
    Write-Host "Building example: $ExampleName"
    
    if (-not (Test-Path $BUILD_DIR)) {
        New-Item -ItemType Directory -Path $BUILD_DIR
    }
    
    Set-Location $BUILD_DIR
    cmake ..
    cmake --build . --target $ExampleName --config Release
    Set-Location ..
    
    $exePath1 = "$BUILD_DIR\examples\$ExampleName\Release\$ExampleName.exe"
    $exePath2 = "$BUILD_DIR\examples\$ExampleName\$ExampleName.exe"
    $targetPath = "$EXAMPLES_DIR\$ExampleName"
    
    if (Test-Path $exePath1) {
        Write-Host "Copying $ExampleName.exe to $targetPath\"
        Copy-Item $exePath1 $targetPath
    } elseif (Test-Path $exePath2) {
        Write-Host "Copying $ExampleName.exe to $targetPath\"
        Copy-Item $exePath2 $targetPath
    } else {
        Write-Host "Warning: Executable for $ExampleName not found."
    }
    
    exit 0
}

if ($Command -eq "build-all") {
    Write-Host "Building all examples..."
    
    if (-not (Test-Path $BUILD_DIR)) {
        New-Item -ItemType Directory -Path $BUILD_DIR
    }
    
    Set-Location $BUILD_DIR
    cmake ..
    cmake --build . --config Release
    Set-Location ..
    
    # 查找所有示例并复制exe文件
    Get-ChildItem -Path $EXAMPLES_DIR -Directory | ForEach-Object {
        $exampleName = $_.Name
        $exePath1 = "$BUILD_DIR\examples\$exampleName\Release\$exampleName.exe"
        $exePath2 = "$BUILD_DIR\examples\$exampleName\$exampleName.exe"
        $targetPath = "$EXAMPLES_DIR\$exampleName"
        
        if (Test-Path $exePath1) {
            Write-Host "Copying $exampleName.exe to $targetPath\"
            Copy-Item $exePath1 $targetPath
        } elseif (Test-Path $exePath2) {
            Write-Host "Copying $exampleName.exe to $targetPath\"
            Copy-Item $exePath2 $targetPath
        } else {
            Write-Host "Warning: Executable for $exampleName not found."
        }
    }
    
    exit 0
}

Write-Host "Unknown command: $Command"
exit 1