# 时间管理器 · Time-Manager

<p align="center">
  <img src="docs/icon.png" width="128" alt="Time-Manager 图标">
</p>

一个轻量的 Windows 桌面日程提醒工具，使用纯 Win32 API + C++ 编写，无第三方运行时依赖。支持日历视图、分钟级日程、自定义提醒、仿 Windows 11 的 Toast 通知、托盘常驻与开机自启，并提供中文 / 日本語 / English 三种界面语言。

![主界面](docs/screenshot-main.png)

## 功能

- **日历视图**：今天的日期用橙色虚线框标出，有日程的日期使用醒目的蓝色底色；点击日期即可筛选当天日程，支持箭头、滚轮和键盘切换月份
- **日程管理**：任意日期，开始/结束时间精确到分钟，可填写标题与备注
- **自定义提醒**：每条日程最多 12 条提醒规则，可提前 N 分钟 / 小时 / 天
- **Toast 通知**：到点从屏幕右下角滑入，支持多条堆叠、悬停暂停、点击打开主窗口
- **提示音**：系统提示音、简单蜂鸣，或自定义 `.wav` 文件
- **托盘常驻**：关闭窗口自动最小化到托盘，后台继续提醒；托盘菜单可恢复或退出
- **开机自启**：写入当前用户注册表 `HKCU\...\Run`，可选择启动时最小化
- **多语言**：设置中可切换中文 / 日本語 / English，点“确定”后立即生效
- **本地数据**：`tasks.dat` / `settings.dat` 保存在 exe 同目录，UTF-8 编码，不联网、无遥测

![设置界面](docs/screenshot-settings.png)

## 环境要求

- Windows 10 / 11 x64
- Visual Studio 2026，安装“使用 C++ 的桌面开发”工作负载
- Windows 10/11 SDK
- 项目当前使用 **v145** 平台工具集（VS 2026）

## 构建

1. 打开 `Time-Maneger.slnx`
2. 选择 `x64` 和 `Release`
3. 生成解决方案

输出文件：

```
x64\Release\Time-Maneger.exe
```

也可以在 Visual Studio 的 Developer PowerShell 中执行：

```powershell
msbuild Time-Maneger.vcxproj /p:Configuration=Release /p:Platform=x64
```

## 使用

1. 把 `Time-Maneger.exe` 放到一个有写入权限的文件夹（**不要放在 `C:\Program Files`**，程序需要在该目录创建数据文件）
2. 首次运行会自动生成 `tasks.dat` 和 `settings.dat`
3. 点“添加日程”填写日期、时间、标题、备注和提醒规则
4. 点“设置”可以修改语言、通知停留时间、提示音、开机自启等
5. 点窗口关闭按钮会最小化到托盘；双击托盘图标恢复，右键可退出

## 数据与隐私

- 所有日程和设置只保存在本机 exe 同目录，不会上传到任何服务器
- 备份：复制 `tasks.dat` 和 `settings.dat` 即可
- 清空日程：关闭程序后删除 `tasks.dat`
- 恢复默认设置：关闭程序后删除 `settings.dat`
- 程序只会在你开启“开机自启”时写入注册表，不会修改系统其他位置

## 项目结构

```
main.cpp                  主程序（Win32、GDI、多语言、日历、通知）
app.rc                    资源脚本
app.ico                   应用图标（16–256 多尺寸）
resource.h                资源 ID
Time-Maneger.vcxproj      Visual Studio 工程
Time-Maneger.slnx         解决方案
docs/                     README 使用的图标与截图
```

## 制作人员

- 作者：枫殇林叶
- 邮箱：3507609128@qq.com
- 应用图标由作者为该项目绘制

## English

A lightweight Windows schedule reminder written in C++ with the Win32 API. It features a calendar with today/task highlighting, minute-precise events, customizable reminder rules, Windows 11-style toast notifications, tray support, autostart, and a Chinese / Japanese / English UI.

Requires Windows 10/11 x64 and Visual Studio 2026 (v145 toolset). Open `Time-Maneger.slnx`, build `x64/Release`, and run `Time-Maneger.exe`. All data is stored locally next to the executable.

## License

[MIT](LICENSE)
