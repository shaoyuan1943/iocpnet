# IOCP Network Library

Windows IOCP-based C++20 Network Library

## 项目结构

- `iocpnet/` - 核心库源代码
- `examples/` - 使用示例
  - `echo_client/` - Echo客户端示例
  - `echo_server/` - Echo服务端示例
- `build.ps1` - PowerShell构建脚本
- `build.bat` - 批处理构建脚本（备用）

## 构建说明

使用提供的PowerShell脚本构建项目：

```powershell
# 清理并重新生成CMake工程
powershell -ExecutionPolicy Bypass -File build.ps1 clean

# 构建指定示例并将exe拷贝到对应目录
powershell -ExecutionPolicy Bypass -File build.ps1 build-example example_name

# 构建所有示例并将exe分别拷贝到对应目录
powershell -ExecutionPolicy Bypass -File build.ps1 build-all
```

## 添加新文件

### 添加到核心库
1. 在 `iocpnet/` 目录下创建新的 `.cpp` 和 `.h` 文件
2. 更新 `iocpnet/CMakeLists.txt` 文件，将新文件添加到 `add_library()` 命令中

### 添加新的示例程序
1. 在 `examples/` 目录下创建新的子目录
2. 在新子目录中添加 `main.cpp` 和 `CMakeLists.txt`
3. 更新 `examples/CMakeLists.txt` 文件，添加新的子目录

### 在示例中使用核心库
在示例程序中包含核心库头文件时，使用以下方式：

```cpp
#include <iocpnet/iocpnet.h>
// 或者
#include "iocpnet/iocpnet.h"
```

核心库的包含路径已经自动设置，示例程序可以直接链接和使用核心库。

## 要求

- Windows平台
- Visual Studio 2022或更高版本
- CMake 3.20或更高版本

## 使用示例

```powershell
# 构建echo_server示例
powershell -ExecutionPolicy Bypass -File build.ps1 build-example echo_server

# 构建所有示例
powershell -ExecutionPolicy Bypass -File build.ps1 build-all
```