# 世界时钟（C++ / Win32）

这是原世界时钟小组件的原生 C++ 重写版，不依赖 Python、Tkinter、Pillow 或 tzdata。

功能包括 Windows 时区与夏令时、最多 10 个时钟、透明圆角窗口、右键设置、拖动、锁定、置顶、开机启动、大小/宽度/间距和轮廓颜色设置。配置保存在 `%APPDATA%\TimezoneWidget\config.ini`。

当前项目也支持 Miniconda 中的 MinGW-w64。工具链安装命令：

```powershell
conda install -n base -c conda-forge gxx=15.2 cmake ninja
```

之后双击 `build.bat` 即可。脚本会优先使用 Conda 工具链；也兼容安装了“使用 C++ 的桌面开发”工作负载的 Visual Studio 2022。

Visual Studio 手动构建命令：

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

Conda 输出：`build-mingw\世界时钟.exe`；Visual Studio 输出：`build\Release\世界时钟.exe`。要求 Windows 10 1607 或更高版本。
