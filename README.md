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

构建成功后会自动启动刚生成的程序。若不需要自动启动，可在配置时关闭：

```powershell
cmake -S . -B build/Release -DCOMMUNICATION_ANALYZER_RUN_AFTER_BUILD=OFF
```

安装后的程序：

```text
Install/CommunicationAnalyzer/CommunicationAnalyzer.exe
```

项目不使用 Qt Designer `.ui` 文件，也不对 Ela 控件应用 QSS。

## 串口 MCP（Codex）

在工具的“设置 -> AI 接入”中开启“串口 MCP 服务”，然后注册本地 Streamable HTTP 服务：

```powershell
codex mcp add communication-analyzer --url http://127.0.0.1:8765/mcp
```

重新启动 Codex 后，可使用以下工具：

- `serial_list_ports`：读取可用串口。
- `serial_get_status`：读取当前连接状态和串口参数。
- `serial_read`：按游标读取 MCP 开启期间的 RX/TX 记录。
- `serial_open`：打开指定串口。
- `serial_close`：关闭当前串口。
- `serial_send`：按 HEX 或 UTF-8 发送数据。

MCP 服务仅监听 `127.0.0.1:8765`，关闭设置开关后立即停止监听并清除 MCP 读取队列。
