# CommunicationAnalyzer

基于 Qt 6.6.3、Qt Serial Port 和 ElaWidgetTools 的通讯分析工具。

## 环境

- Qt 6.6.3 MinGW 64-bit
- MinGW 11.2.0
- CMake 3.21+
- Ninja
- VS Code Profile：`ElaWidgetTools Qt 6.6.3`

## 获取源码

```powershell
git clone --recursive <repository-url>
```

已有仓库补全子模块：

```powershell
git submodule update --init --recursive
```

## 编译

在 VS Code 中选择 `Release`，然后依次执行：

1. `CMake: Configure`
2. `CMake: Build`
3. `CMake: Build Target` -> `install`

命令行方式：

```powershell
cmake -S . -B build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/Release --parallel
cmake --install build/Release
```

安装后的程序：

```text
Install/CommunicationAnalyzer/CommunicationAnalyzer.exe
```

项目不使用 Qt Designer `.ui` 文件，也不对 Ela 控件应用 QSS。
