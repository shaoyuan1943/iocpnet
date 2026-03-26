# IOCP Network Library

Windows IOCP-based C++20 Network Library

## Project Structure

- `iocpnet/` Library code.
- `examples/echo_server` Test server.
- `examples/echo_client` Test client.
- `build.ps1` Build script for PowerShell.
- `build.bat` Build script for Cmd.

## How To Build

### Require

- Only in windows.
- Visual Studio 2022 or higher.
- CMake 3.20 or higher.

### Build

- use `build.bat`
    - `build.bat clean`
    - `build.bat build-example echo_server`
    - `build.bat rebuild`
    - `build.bat help` To get more information.

## How To Use

- Include `iocpnet` source code to your project.
- Use `#include "iocpnet/iocpnet.h"` to use.

## How To Add Files

### Add Source Code File

- Create new file(.h or .cc) in `iocpnet/`.
- Update `iocpnet/CMakeLists.txt`, add new files to `add_library()`.