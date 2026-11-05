# EasySnip

Windows 11 截图、标注与贴图工具，使用 C++17、Qt Widgets 5.14.2 与 MinGW 7.3 64 位开发。

## 运行

解压 `EasySnip-0.1.0-win64.zip`，运行 `EasySnip/EasySnip.exe`。请保留同目录的 DLL、platforms、imageformats 和 printsupport 文件夹。

程序首次启动显示历史管理界面。关闭主界面后可通过托盘重新打开。托盘菜单中的“退出”会结束程序。禁用托盘图标后，关闭主窗口会退出。

默认数据目录由 Qt 的 `AppLocalDataLocation` 提供，通常为 `%LOCALAPPDATA%\EasySnip\EasySnip`。可使用 `--data-dir` 指定目录。自动保存和开机启动默认关闭。

## 常用操作

| 操作 | 默认按键或入口 |
| --- | --- |
| 开始截图 | F1 / 托盘左键 / 主窗口“开始截图” |
| 剪贴板贴图 | F3 / 托盘中键 |
| 复用上次区域 | F4 |
| 截取活动窗口 | Ctrl+Shift+F1 |
| 显示或隐藏当前组贴图 | Ctrl+Shift+H |
| 打开历史管理 | Ctrl+Shift+F3 |
| 超级截屏 | 按住 Alt+Shift，在屏幕上拖动鼠标 |
| 精确尺寸与比例 | 截图状态按 D，可输入屏幕坐标、尺寸、比例和圆角 |
| 直接输入选区宽高 | 截图状态输入 `800x600` 后按 Enter |
| 全屏 / 当前屏幕 | 截图状态 F / S |
| 切换窗口与元素检测 | Tab，滚轮向上选父级、向下选子级 |
| 逐像素调整 | 方向键移动选区，Shift+方向键调整右下边，Ctrl+方向键调整左上边 |
| 逐像素移动鼠标 | 无选区时使用方向键，或 Alt+方向键 |
| 拖动中移动整个选区 | 按住空格 |
| 复制屏幕色值 / 切换格式 | P / V |
| 鼠标指针显示开关 | C |
| 刷新 / 连续刷新与定格 | R / Shift+R |
| 进入标注 | Enter / 双击选区 |
| 取消或重新选择 | Esc 取消，右键先清除选区 |
| 对象编辑 | 选择工具后拖动对象，拖动右下角调整尺寸，侧栏调整旋转和样式 |
| 约束图形 | Shift 绘制正方形、圆，以及水平、垂直、45° 线条 |
| 快速匹配检测元素 | 截图时启用元素检测，编辑器选用矩形、椭圆或区域效果工具，再 Ctrl+单击已检测元素 |
| 折线 | 逐点单击，双击或 Enter 结束 |
| 对象复制、粘贴、删除 | 画布聚焦且选中对象时 Ctrl+C / Ctrl+V / Delete |
| 克隆对象 | 选择工具下 Alt+拖动 |
| 编辑文字 | 选择工具下双击文字 |
| 撤销 / 重做 | Ctrl+Z / Ctrl+Y，或底部按钮 |
| 画笔粗细 / 画布缩放 | 滚轮 / Ctrl+滚轮，粗细也支持 `[` 和 `]` |
| 色块透明度 | 在侧栏色块上滚动鼠标滚轮 |
| 完成与组合输出 | Ctrl+Enter / “完成”，按设置组合复制、保存、贴图 |
| 跳过自动保存 | 按住 Alt 点击输出按钮 |
| 编辑贴图 | 双击贴图，或右键“标注编辑” |
| 贴图缩放 / 透明度 | 滚轮 / Ctrl+滚轮 |
| 恢复关闭贴图 | 托盘“恢复最近关闭贴图”，或历史界面的“已关闭贴图” |

复制图像的按钮始终复制完整合成图像。默认同时写入图片文件 URL，可在图像程序或文件管理器粘贴。对应文件保存在数据目录的 `clipboard` 文件夹中。

截图历史和贴图保存源图、可编辑标注与缩略图。单张记录使用原子文件提交，图像更新成功后再替换元数据。历史数量淘汰会保留仍属于贴图的记录。分组切换显示当前组的贴图，其他组的状态继续保存在数据目录中。

## 构建与测试

在 PowerShell 中执行以下命令。

```powershell
cd E:\Code\EasySnip
.\scripts\build.ps1 -Test -Package
```

默认 SDK 路径为 `E:\Qt5.14.2`。其他位置可使用 `-QtRoot` 指定。也可以在 Qt Creator 中打开 `EasySnip.pro`，选择 Qt 5.14.2 MinGW 64-bit Kit。

构建输出位于 `build/bin/EasySnip.exe`，便携包位于 `dist/EasySnip`。自动回归测试结果和界面渲染位于 `test-results`。

该 SDK 的部署工具会将部分插件归类为 debug，构建脚本在自动部署失败后使用已核实的模块与插件清单复制依赖，并生成独立的 `qt.conf`。

Windows 原生冒烟测试需要正常的交互式桌面环境，测试窗口使用隐藏属性。它会读取桌面图像到内存，验证屏幕坐标，并临时注册后释放 Ctrl+Alt+Shift+F11。

```powershell
$env:PATH = 'E:\Qt5.14.2\5.14.2\mingw73_64\bin;' + $env:PATH
$process = Start-Process .\build\bin\EasySnip.exe `
    -ArgumentList '--self-test E:\Code\EasySnip\test-results\native --native-smoke-test' `
    -WindowStyle Hidden -Wait -PassThru
$process.ExitCode
```

## 命令行

```text
EasySnip.exe --capture
EasySnip.exe --screen
EasySnip.exe --fullscreen
EasySnip.exe --active
EasySnip.exe --last
EasySnip.exe --clipboard
EasySnip.exe --toggle-pins
EasySnip.exe --recover
EasySnip.exe --settings
EasySnip.exe --open "C:\Pictures\example.png"
EasySnip.exe --tray
EasySnip.exe --data-dir "D:\EasySnip-data"
EasySnip.exe --quit
```

相同数据目录使用单实例和本地 IPC，后续命令会发送到已运行的实例。自定义全局快捷键可绑定这些命令，或绑定设置中的其他程序命令行。外部图像输出使用独立的程序路径与参数配置，`{file}` 会替换为导出的图片路径，参数之间使用字面量 `\n` 分隔。

## 功能覆盖与边界

功能对照与验收范围见 `docs/feature-status.md`。0.1.0 已提供截图、编辑、贴图、分组、历史、识别、Windows 原生分享及组合输出，并支持配置 OpenGL 加速预览。

F3 也可将剪贴板文字或 HTML 渲染为贴图，或下载剪贴板中的 HTTP/HTTPS 图片直链。下载使用 Windows WinHTTP，默认上限 8 MiB。

常用高级配置可在退出软件后编辑数据目录中的 `settings.ini`。以下配置需要按 INI 的分节格式填写，示例中的组名与键名分别对应表中的斜杠前后部分。

| 配置键 | 默认值及用途 |
| --- | --- |
| `runtime/gpuPreview` | `false`，设为 `true` 启用 OpenGL 画布预览，初始化失败自动回退 |
| `appearance/uiScale` | 默认跟随主屏幕 DPI，允许 1.0–3.0，修改后重启。截图坐标始终使用物理像素 |
| `capture/nativeElementsFor` | 空，填写需要使用原生子窗口检测的程序名称或通配符，分号分隔 |
| `capture/animateDetection` | `true`，检测范围过渡动画 |
| `pins/htmlForceWrap` | `true`，HTML 和文字贴图强制折行 |
| `pins/includeActiveInMultiSelect` | `false`，历史多选操作额外纳入最近活动贴图 |
| `output/webImageMaxBytes` | `8388608`，网页图片下载字节数上限 |
| `output/menuVisible` | `true`，显示编辑器“更多”输出菜单 |
| `log/flushMs` | `1000`，日志刷新间隔，允许 100–60000 毫秒 |

```ini
[runtime]
gpuPreview=true

[capture]
nativeElementsFor=example.exe;legacy*.exe
animateDetection=true
```

窗口检测使用 Win32 和 DWM 边界，元素检测使用 MSAA。实际精度取决于目标程序提供的无障碍信息。多屏捕获使用 Windows 物理像素坐标和 Per-Monitor V2 清单。不同缩放比例的多屏布局、特殊 HDR/受保护内容、打印机和 Listary 联动需要在目标环境验收。

## 第三方组件

Qt 采用动态链接。ZXing-C++ v1.4.0 的源码位于 `third_party/zxing`，用于二维码和条形码识别。版本固定以匹配指定的 MinGW 工具链。原始许可文件与 NOTICE 随源码和便携包一起提供。

项目远程地址见 GitHub 仓库。项目许可由仓库所有者决定，第三方组件各自遵循其许可条款。[EasySnip 仓库](https://github.com/threo9/EasySnip/)
