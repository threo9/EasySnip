# EasySnip

Windows 11 截图与标注工具，使用 C++17、Qt Widgets 5.14.2 与 MinGW 7.3 64 位开发。

## 运行

解压 `EasySnip-0.1.0-win64.zip`，运行 `EasySnip/EasySnip.exe`。请保留同目录的 DLL、platforms、imageformats 和 printsupport 文件夹。

程序默认静默驻留系统托盘。需要历史管理界面时，可点击托盘菜单中的“截图历史”，或按历史快捷键（默认 Ctrl+Shift+F3）。普通重复启动同样按托盘方式处理，`--tray` 参数继续可用。托盘菜单中的“退出”会结束程序。禁用托盘图标或系统托盘不可用时，程序显示历史管理界面以提供操作入口。禁用托盘图标后，关闭主窗口会退出。

默认数据目录由 Qt 的 `AppLocalDataLocation` 提供，通常为 `%LOCALAPPDATA%\EasySnip\EasySnip`。可使用 `--data-dir` 指定目录。自动保存和开机启动默认关闭。

## 常用操作

| 操作 | 默认按键或入口 |
| --- | --- |
| 开始截图 | F1 / 托盘左键 / 主窗口“开始截图” |
| 复用上次区域 | F4 |
| 截取活动窗口 | Ctrl+Shift+F1 |
| 打开历史管理 | Ctrl+Shift+F3 |
| 超级截屏 | 按住 Alt+Shift 拖动鼠标，设置中可改为 Win 或 Ctrl+Alt |
| 精确尺寸与比例 | 截图状态按 Ctrl+D 或点击“尺寸 / 比例”，输入坐标、尺寸、比例和圆角 |
| 直接输入选区宽高 | 截图状态输入 `800x600` 后按 Enter |
| 全屏 / 当前屏幕 | 截图状态 F / Shift+S |
| 切换窗口与元素检测 | Tab，滚轮向上选父级、向下选子级 |
| 逐像素调整 | 方向键移动选区，Ctrl+方向键将对应边扩大 1 px，Shift+方向键将对应边缩小 1 px |
| 逐像素移动鼠标 | WASD，按住左键时也可微调拖拽；无选区时也支持方向键 |
| 拖动中移动整个选区 | 按住空格 |
| 放大镜与取色 | Alt 开关放大镜，显示时 C 复制颜色、Shift 切换 RGB / HEX；兼容 P / V |
| 鼠标指针显示开关 | I / “指针”按钮 |
| 刷新 / 连续刷新与定格 | R / Shift+R |
| 选区内标注 | 选定区域后自动显示浮动工具栏，点击工具即可原位编辑 |
| 调整截图区域 | 工具栏虚线框按钮 |
| 取消或重新选择 | Esc 取消，调整选区时右键清除选区 |
| 对象编辑 | 用选择工具或同类工具选中对象，拖动四角缩放，上方圆点旋转；右键结束编辑 |
| 约束图形 | Shift 绘制正方形、圆，以及水平、垂直、45° 线条 |
| 快速匹配检测元素 | 开启元素检测，在矩形、椭圆或矩形区域效果工具下右键匹配；已选中对象时先右键结束编辑 |
| 折线 | 拖动画单线，点按画折线，右键、双击或 Enter 结束；Tab 切换直线与箭头 |
| 对象复制、粘贴、删除 | Ctrl+Shift+C / Ctrl+Shift+V / Delete |
| 克隆对象 | Ctrl+拖动选中标注，也兼容 Alt+拖动 |
| 编辑文字 | 双击文字修改内容，四角改变字号，上方圆点旋转，Shift+拖动四角恢复水平 |
| 撤销 / 重做 | Ctrl+Z / Ctrl+Y，或底部按钮 |
| 画笔粗细 / 画布缩放 | 滚轮或 1/2 调整粗细，也支持 `[` 和 `]`；独立编辑器中 Ctrl+滚轮缩放 |
| 笔尖与调色板 | 1–5 个笔尖可分别用滚轮调节，拖动色块排序，“色板…”可定制与导入导出 |
| 色块透明度 | 在较大的颜色按钮上滚动鼠标滚轮，或在颜色对话框输入 Alpha 0–255 |
| 复制截图 | Ctrl+C、Enter、双击选区或复制按钮；正在绘制折线时 Enter 先结束折线 |
| 快速保存 | Ctrl+Shift+S，或按住 Shift 点击保存按钮 |
| 完成与组合输出 | Ctrl+Enter / “完成”，按设置组合复制与保存 |
| 截图内历史回放 | `,` 查看更早记录，`.` 向后回放直至返回当前截图，可继续编辑和输出 |
| 跳过自动保存 | 按住 Alt 点击输出按钮 |

默认截图键为 F1。已保存的自定义快捷键优先使用设置值。WASD 默认用于移动鼠标，因此尺寸对话框采用 Ctrl+D，当前屏幕采用 Shift+S。

复制图像的按钮与 Ctrl+C 均复制完整合成图像。默认同时写入图片文件 URL，可在图像程序或文件管理器粘贴。对应文件保存在数据目录的 `clipboard` 文件夹中。

区域截图、预设区域和超级截屏均在选区旁显示工具栏。图形、线条、区域效果和文字按钮的下拉菜单提供同组工具，选择标注工具后可直接调整颜色、粗细和字号。“样式…”提供填充、描边、旋转等设置。截图工具栏中的复制、保存和完成操作成功后结束截图，取消保存对话框或输出失败时可以继续编辑。历史记录和本地图像通过独立编辑器打开。

截图历史保存源图、可编辑标注与缩略图。截图内回放按需读取历史，在本次会话中保留已访问记录的编辑和当前截图草稿。长图预览按屏幕缩放，输出使用原始分辨率。开启“保存取消或未输出的选区到历史”后，取消时会保留有效选区，回放并输出旧截图时也会收录未输出的当前草稿。单张记录使用原子文件提交，图像更新成功后再替换元数据。历史数量按设置的上限淘汰。旧版保存的图像统一作为截图历史读取，可继续编辑、复制、保存和备份。

## 构建与测试

在 PowerShell 中执行以下命令。

```powershell
cd E:\Code\EasySnip
.\scripts\build.ps1 -Test -Package
```

默认 SDK 路径为 `E:\Qt5.14.2`。其他位置可使用 `-QtRoot` 指定。也可以在 Qt Creator 中打开 `EasySnip.pro`，选择 Qt 5.14.2 MinGW 64-bit Kit。

构建输出位于 `build/bin/EasySnip.exe`，便携包位于 `dist/EasySnip`。自动回归测试结果和界面渲染位于 `test-results`。

需要独立的便携目录时可加 `-PackageName EasySnip-features-new`，生成 `dist/EasySnip-features-new` 和同名 ZIP 包。运行新版前请从旧版托盘菜单退出，单实例按数据目录识别运行中的程序。

该 SDK 的部署工具会将部分插件归类为 debug，构建脚本在自动部署失败后使用已核实的模块与插件清单复制依赖，并生成独立的 `qt.conf`。

Windows 原生冒烟测试需要正常的交互式桌面环境。它会读取桌面图像到内存，验证屏幕坐标，短暂显示用于控件识别的测试窗口与选区，并临时注册后释放 F1、Ctrl+Alt+Shift+F10/F11。测试完成后关闭测试窗口并恢复鼠标位置和剪贴板。运行测试前应退出正在使用这些快捷键的 EasySnip 实例。

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
EasySnip.exe --settings
EasySnip.exe --open "C:\Pictures\example.png"
EasySnip.exe --tray
EasySnip.exe --admin
EasySnip.exe --data-dir "D:\EasySnip-data"
EasySnip.exe --quit
```

相同数据目录使用单实例和本地 IPC，后续命令会发送到已运行的实例。自定义全局快捷键可绑定这些命令，或绑定设置中的其他程序命令行。每个快捷键可分别设置忽略程序，支持程序名、完整路径、通配符和分号分隔。外部图像输出使用独立的程序路径与参数配置，`{file}` 会替换为导出的图片路径，参数之间使用字面量 `\n` 分隔。

## 功能覆盖与边界

当前版本已按用户要求移除贴图窗口、启动恢复、贴图分组、贴图输出及相关快捷键和命令行入口。

功能对照与验收范围见 `docs/feature-status.md`。0.1.0 已提供截图、编辑、历史、识别、Windows 原生分享及组合输出，并支持配置 OpenGL 加速预览。


常用高级配置可在退出软件后编辑数据目录中的 `settings.ini`。以下配置需要按 INI 的分节格式填写，示例中的组名与键名分别对应表中的斜杠前后部分。

| 配置键 | 默认值及用途 |
| --- | --- |
| `runtime/gpuPreview` | `false`，设为 `true` 启用 OpenGL 画布预览，初始化失败自动回退 |
| `appearance/uiScale` | 默认跟随主屏幕 DPI，允许 1.0–3.0，修改后重启。截图坐标始终使用物理像素 |
| `capture/nativeElementsFor` | 空，填写需要使用原生子窗口检测的程序名称或通配符，分号分隔 |
| `capture/animateDetection` | `true`，检测范围过渡动画 |
| `output/menuVisible` | `true`，显示编辑器“更多”输出菜单 |
| `log/flushMs` | `1000`，日志刷新间隔，允许 100–60000 毫秒 |

```ini
[runtime]
gpuPreview=true

[capture]
nativeElementsFor=example.exe;legacy*.exe
animateDetection=true
```

窗口检测使用 Win32 和 DWM 边界，元素检测结合 UI Automation、MSAA 与原生子窗口。Windows 11 托盘和展开面板通过 XAML 子窗口读取图标边界，并在截图前缓存。截图中按 Tab 切换元素检测，滚轮向上选择父级、向下返回子级。检测在后台执行，截图界面出现后继续使用对应的窗口与图标快照。实际精度取决于目标程序提供的无障碍信息。多屏捕获使用 Windows 物理像素坐标和 Per-Monitor V2 清单。不同缩放比例的多屏布局、特殊 HDR/受保护内容、打印机和 Listary 联动需要在目标环境验收。

## 第三方组件

Qt 采用动态链接。ZXing-C++ v1.4.0 的源码位于 `third_party/zxing`，用于二维码和条形码识别。版本固定以匹配指定的 MinGW 工具链。原始许可文件与 NOTICE 随源码和便携包一起提供。

项目远程地址见 GitHub 仓库。项目许可由仓库所有者决定，第三方组件各自遵循其许可条款。[EasySnip 仓库](https://github.com/threo9/EasySnip/)

## 管理员启动与快捷键诊断

缺省截图快捷键为 F1。设置中的“以管理员权限启动”在下次启动时通过 Windows UAC 请求提升，确认后静默驻留托盘，也可使用 `--admin` 启动。已运行的实例优先接收命令，切换权限时先从托盘退出当前实例。

全局快捷键使用 Windows RegisterHotKey 注册。切换前台程序时保持注册，通过按键分发阶段处理忽略程序规则。占用冲突会记录 Windows 错误码并每 2 秒重试，成功注册及触发事件写入数据目录中的 `EasySnip.log`。托盘状态下按 F1 立即进入捕获流程，界面隐藏时留出 80 毫秒等待画面更新。
