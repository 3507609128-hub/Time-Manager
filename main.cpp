/*
============================================================================
  时间管理器（Time-Manager）v2
----------------------------------------------------------------------------
  功能：
    1. 图形界面添加任意日期的日程（开始/结束时间精确到分钟）；
    2. 每条日程可自由添加、删除提醒规则（不限制次数，如提前 N 分钟/小时/天）；
    3. 到点在屏幕右下角弹出仿 Windows 11 风格的 Toast 通知（滑入、停留、
       淡出、多条堆叠、点击打开主窗口），提示音可自由选择；
    4. 左侧自绘日历：今天的日期用虚线框标出，有日程的日期用醒目底色标出，
       点击日期筛选当天日程；
    5. 开机自启（写入注册表，后台最小化）、系统托盘常驻，关闭按钮最小化
       到托盘；
    6. 界面支持中文 / 日本語 / English 三种语言，可在设置中切换；
    7. 内置经典白 / 清新薄荷 / 奶油晨光 / 樱粉初雪四套界面风格，
       默认清新薄荷，可在设置中随时替换；
    8. 日程与设置自动保存在程序目录（tasks.dat / settings.dat，UTF-8）。

  编码约定：不使用 class / do-while / range-based for；
            任务、规则、通知槽位等数组下标一律从 1 开始。
  编译：Visual Studio 2026，x64 Debug/Release，Unicode，纯 Win32 API。
============================================================================
*/

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>
#include <strsafe.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>

#include "resource.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "advapi32.lib")

/* 启用 comctl32 6.0 现代控件外观 */
#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

/*============================= 常量 =============================*/

#define MAX_TASKS       200
#define MAX_REMIND      12              /* 每条日程最多 12 条提醒规则 */
#define REPEAT_MAX_WEEKS 20             /* 连续日程最多 20 周 */
#define MAX_TOASTS      4               /* 屏幕上最多同时显示 4 条通知 */
#define MAX_TITLE       64
#define MAX_NOTE        256
#define MAX_LINE        2000
#define TIMER_CHECK     1

/* 主窗口控件 ID */
#define IDC_CAL          1001
#define IDC_BTN_ADD      1002
#define IDC_BTN_DELETE   1003
#define IDC_BTN_TODAY    1004
#define IDC_BTN_ALL      1005
#define IDC_BTN_SET      1006
#define IDC_CHK_AUTO     1007
#define IDC_TIP          1008
#define IDC_LIST         1009
#define IDC_STATUS       1010
#define IDC_STAT_TODAY   1011
#define IDC_SEPARATOR    1012
#define IDC_VIEW_MONTH   1013
#define IDC_VIEW_WEEK    1014
#define IDC_WEEK_PREV    1015
#define IDC_WEEK_NEXT    1016
#define IDC_WEEK_TODAY   1017
#define IDC_WEEK_TITLE   1018
#define IDC_WEEK         1019

/* 添加日程窗口控件 ID */
#define IDC_SDATE        2001
#define IDC_STIME        2002
#define IDC_EDATE        2003
#define IDC_ETIME        2004
#define IDC_TITLE        2005
#define IDC_NOTE         2006
#define IDC_OK           2007
#define IDC_CANCEL       2008
#define IDC_RLIST        2009
#define IDC_RVALUE       2010
#define IDC_RUNIT        2011
#define IDC_RADD         2012
#define IDC_RDEL         2013
#define IDC_ADD_HINT     2014
#define IDC_CAT          2015
#define IDC_REPEAT       2016

/* 设置窗口控件 ID */
#define IDC_SET_AUTO     3001
#define IDC_SET_MIN      3002
#define IDC_SET_DUR      3003
#define IDC_SET_SOUND    3004
#define IDC_SET_TEST     3005
#define IDC_SET_WAV      3006
#define IDC_SET_BROWSE   3007
#define IDC_SET_OK       3008
#define IDC_SET_CANCEL   3009
#define IDC_SET_LANG     3010
#define IDC_SET_HINT     3011
#define IDC_SET_CREDITS  3012
#define IDC_SET_EMAIL    3013
#define IDC_SET_THEME    3014

/* 托盘菜单 */
#define ID_TRAY_SHOW     3101
#define ID_TRAY_EXIT     3102
#define WM_APP_TRAY      (WM_APP + 1)
#define WM_APP_CAL_SELECT (WM_APP + 2)

/* 提示音选项编号 */
#define SOUND_MUTE       0
#define SOUND_DEFAULT    1
#define SOUND_ASTERISK   2
#define SOUND_EXCLAM     3
#define SOUND_HAND       4
#define SOUND_BEEP       5
#define SOUND_WAV        6

/* 活动分类编号（下标从 1 开始） */
#define CAT_COURSE       1
#define CAT_CHORES       2
#define CAT_WORK         3
#define CAT_SPORT        4
#define CAT_FUN          5
#define CAT_OTHER        6
#define CAT_COUNT        6
#define CAT_DEFAULT      CAT_COURSE

/* 视图模式：0=月视图（日历+列表），1=周视图（时间网格） */
#define VIEW_MONTH       0
#define VIEW_WEEK        1

/* 周视图参数：全天 0:00–24:00 */
#define WEEK_START_HOUR  0
#define WEEK_END_HOUR    24
#define WEEK_HOURS       (WEEK_END_HOUR - WEEK_START_HOUR)
#define WEEK_HEADER_H    S(44)
#define WEEK_AXIS_W      S(54)
#define WEEK_ROW_H       S(52)
#define WEEK_MAX_LANES   4

/*============================= 数据结构 =============================*/

struct Reminder
{
    int minutes;        /* 提前多少分钟 */
    int fired;          /* 是否已弹过 */
};

struct Task
{
    int id;
    int category;       /* 活动分类：1..CAT_COUNT */
    SYSTEMTIME stStart;
    SYSTEMTIME stEnd;
    wchar_t title[MAX_TITLE];
    wchar_t note[MAX_NOTE];
    int rcount;
    struct Reminder r[MAX_REMIND + 1];   /* 下标 1..rcount */
};

struct Settings
{
    int autostart;      /* 开机自启 */
    int minStart;       /* 启动时最小化到托盘 */
    int sound;          /* 提示音选项 */
    int toastSec;       /* 通知停留秒数 */
    wchar_t wavPath[MAX_PATH];
    int lang;           /* 界面语言：LANG_ZH / LANG_JA / LANG_EN */
    int theme;          /* 界面风格：THEME_* */
};

struct Toast
{
    HWND hwnd;
    int state;          /* 0 滑入 1 停留 2 淡出 */
    int tick;
    int showTick;
    int hover;
    int tracking;
    int closeHover;
    int alpha;
    int x, y;
    int startX, targetX;
    wchar_t head[200];
    wchar_t body1[200];
    wchar_t body2[200];
};

/*============================= 界面风格 =============================*/

enum { THEME_CLASSIC = 0, THEME_MINT = 1, THEME_CREAM = 2, THEME_SAKURA = 3,
       THEME_COUNT = 4, THEME_DEFAULT = THEME_MINT };

struct Theme
{
    COLORREF page;          /* 页面底色 */
    COLORREF card;          /* 卡片 / 列表 / 日历底色 */
    COLORREF border;        /* 卡片边框 / 网格线 */
    COLORREF text;          /* 主文字 */
    COLORREF muted;         /* 次要文字 */
    COLORREF accent;        /* 主强调色 */
    COLORREF accentDark;    /* 强调色深色 */
    COLORREF accentSoft;    /* 选中浅底 */
    COLORREF hover;         /* 悬停 */
    COLORREF today;         /* 今天虚线框 */
    COLORREF sun;           /* 周日 */
    COLORREF sat;           /* 周六 */
    COLORREF weekendBg;     /* 周末列淡底 */
    COLORREF nightBg;       /* 周视图夜间时段 */
    COLORREF axisBg;        /* 周视图时间轴 / 表头 */
    COLORREF altRow;        /* 列表交替行 */
    COLORREF todayBg;       /* 周视图今天列 */
    COLORREF line;          /* 周视图网格线 */
    COLORREF now;           /* 当前时间线 */
    COLORREF empty;         /* 空状态文字 */
    COLORREF toastBg;       /* Toast 外框底 */
    COLORREF toastBorder;   /* Toast 边框 */
    COLORREF toastCard;     /* Toast 卡片 */
    COLORREF toastTitle;    /* Toast 应用名 */
    COLORREF toastHead;     /* Toast 标题 */
    COLORREF toastBody;     /* Toast 正文 */
    COLORREF tipBg;         /* 悬停气泡底 */
    COLORREF tipText;       /* 悬停气泡主文字 */
    COLORREF tipMuted;      /* 悬停气泡次要文字 */
    COLORREF dialogBg;      /* 对话框底色 */
};

static const struct Theme g_themes[THEME_COUNT] =
{
    /* 0 经典白：与旧版配色保持一致 */
    {
        RGB(238, 242, 247), RGB(255, 255, 255), RGB(222, 228, 238),
        RGB(38, 38, 38),    RGB(128, 128, 128), RGB(0, 120, 212),
        RGB(0, 76, 140),    RGB(224, 238, 253), RGB(242, 246, 251),
        RGB(224, 78, 26),   RGB(196, 64, 64),   RGB(64, 104, 176),
        RGB(245, 246, 250), RGB(243, 246, 250), RGB(247, 249, 252),
        RGB(247, 249, 252), RGB(238, 246, 255), RGB(228, 232, 238),
        RGB(224, 78, 26),   RGB(160, 164, 170),
        RGB(248, 249, 251), RGB(225, 226, 230), RGB(255, 255, 255),
        RGB(140, 140, 140), RGB(28, 30, 34),    RGB(96, 100, 108),
        RGB(255, 255, 255), RGB(30, 30, 30),    RGB(86, 86, 86),
        RGB(240, 240, 240)
    },
    /* 1 清新薄荷：柔和薄荷页 + 青绿强调 + 珊瑚今日 */
    {
        RGB(241, 250, 246), RGB(255, 255, 255), RGB(216, 233, 225),
        RGB(31, 56, 49),    RGB(110, 141, 131), RGB(42, 157, 130),
        RGB(27, 116, 97),   RGB(221, 243, 235), RGB(233, 248, 242),
        RGB(240, 138, 93),  RGB(217, 120, 104), RGB(79, 143, 166),
        RGB(239, 249, 245), RGB(242, 250, 247), RGB(247, 252, 250),
        RGB(247, 252, 250), RGB(230, 247, 241), RGB(220, 235, 228),
        RGB(240, 138, 93),  RGB(154, 176, 168),
        RGB(245, 252, 249), RGB(217, 235, 227), RGB(255, 255, 255),
        RGB(126, 156, 146), RGB(22, 53, 44),    RGB(94, 126, 115),
        RGB(255, 255, 255), RGB(30, 58, 50),    RGB(95, 127, 116),
        RGB(242, 251, 247)
    },
    /* 2 奶油晨光：暖奶油页 + 琥珀强调 */
    {
        RGB(253, 248, 239), RGB(255, 255, 255), RGB(238, 223, 200),
        RGB(63, 51, 38),    RGB(141, 123, 100), RGB(217, 144, 63),
        RGB(163, 97, 30),   RGB(251, 235, 215), RGB(253, 243, 229),
        RGB(217, 103, 74),  RGB(200, 107, 82),  RGB(111, 142, 110),
        RGB(251, 244, 233), RGB(253, 248, 240), RGB(254, 250, 243),
        RGB(254, 250, 243), RGB(252, 239, 224), RGB(240, 228, 210),
        RGB(217, 103, 74),  RGB(179, 161, 139),
        RGB(255, 251, 244), RGB(239, 225, 204), RGB(255, 255, 255),
        RGB(160, 138, 112), RGB(59, 47, 35),    RGB(123, 105, 85),
        RGB(255, 255, 255), RGB(62, 50, 37),    RGB(130, 111, 89),
        RGB(254, 249, 241)
    },
    /* 3 樱粉初雪：淡粉页 + 玫红强调 */
    {
        RGB(255, 246, 249), RGB(255, 255, 255), RGB(242, 220, 229),
        RGB(67, 49, 58),    RGB(148, 122, 134), RGB(217, 108, 146),
        RGB(168, 70, 107),  RGB(251, 226, 236), RGB(255, 240, 245),
        RGB(229, 138, 78),  RGB(217, 108, 122), RGB(138, 123, 184),
        RGB(255, 243, 247), RGB(255, 247, 250), RGB(255, 250, 252),
        RGB(255, 250, 252), RGB(252, 235, 242), RGB(244, 225, 232),
        RGB(217, 108, 146), RGB(180, 154, 165),
        RGB(255, 248, 251), RGB(244, 223, 231), RGB(255, 255, 255),
        RGB(165, 139, 150), RGB(63, 46, 54),    RGB(133, 109, 120),
        RGB(255, 255, 255), RGB(67, 49, 58),    RGB(140, 115, 126),
        RGB(255, 248, 251)
    }
};

/*============================= 全局变量 =============================*/

static HINSTANCE g_hInst = NULL;
static HICON  g_hIconLarge = NULL;
static HICON  g_hIconSmall = NULL;
static HFONT  g_fontUi = NULL;
static HFONT  g_fontBold = NULL;
static HFONT  g_fontStatus = NULL;
static HFONT  g_fontTTitle = NULL;
static HFONT  g_fontTHead = NULL;
static HFONT  g_fontTBody = NULL;
static HFONT  g_fontTI = NULL;
static HFONT  g_fontCal = NULL;
static HFONT  g_fontCalBold = NULL;
static HFONT  g_fontCalWeek = NULL;
static HFONT  g_fontCalTitle = NULL;
static HFONT  g_fontWeekHead = NULL;
static HFONT  g_fontWeekAxis = NULL;
static HFONT  g_fontWeekBlock = NULL;
static HFONT  g_fontTipTitle = NULL;
static HFONT  g_fontTipBody = NULL;
static const wchar_t* g_fontFamily = L"Microsoft YaHei UI";
static int    g_dpi = 96;
static HBRUSH g_hBgBrush = NULL;    /* 主窗口页面底色 */
static HBRUSH g_hDlgBrush = NULL;   /* 对话框底色 */
static HBRUSH g_hCardBrush = NULL;  /* 对话框卡片底色 */
static const struct Theme* g_theme = NULL;

static HWND   g_hMain = NULL;
static HWND   g_hList = NULL;
static HWND   g_hCal = NULL;
static HWND   g_hWeek = NULL;
static HWND   g_hTip = NULL;
static HWND   g_hTip2 = NULL;
static HWND   g_hChkAuto = NULL;
static HWND   g_hStatus = NULL;
static HWND   g_hStatToday = NULL;
static HWND   g_hBtnViewMonth = NULL;
static HWND   g_hBtnViewWeek = NULL;
static HWND   g_hBtnWeekPrev = NULL;
static HWND   g_hBtnWeekNext = NULL;
static HWND   g_hBtnWeekToday = NULL;
static HWND   g_hWeekTitle = NULL;

static struct Task g_tasks[MAX_TASKS + 1];
static int  g_count = 0;
static int  g_nextId = 1;
static wchar_t g_dataPath[MAX_PATH];
static wchar_t g_setPath[MAX_PATH];

static struct Settings g_set;

/* 列表筛选：0=全部，1=只看 g_filterDate 当天 */
static int g_filterMode = 0;
static SYSTEMTIME g_filterDate;

/* 托盘 */
static NOTIFYICONDATAW g_nid;
static int g_realQuit = 0;
static int g_trayHinted = 0;

/* 添加日程窗口 */
static HWND g_hAdd = NULL;
static int  g_addResult = 0;
static HWND g_dlgSDate = NULL, g_dlgSTime = NULL;
static HWND g_dlgEDate = NULL, g_dlgETime = NULL;
static HWND g_dlgTitle = NULL, g_dlgNote = NULL;
static HWND g_dlgCat = NULL;
static HWND g_dlgRList = NULL, g_dlgRValue = NULL, g_dlgRUnit = NULL;
static int  g_dlgRMinutes[MAX_REMIND + 1];
static int  g_dlgRCount = 0;
static int  g_dlgCategory = CAT_DEFAULT;

/* 设置窗口 */
static HWND g_hSet = NULL;
static int  g_setResult = 0;
static struct Settings g_dlgSet;

/* 通知窗口（下标 1..MAX_TOASTS） */
static struct Toast g_toasts[MAX_TOASTS + 1];

/* 自绘日历状态 */
static int g_calYear = 0;
static int g_calMonth = 0;
static SYSTEMTIME g_calSel;         /* 日历中选中的日期，同时作为筛选日期 */
static int g_calHoverDay = 0;       /* 鼠标悬停的日（0 表示无） */
static int g_calHoverNav = 0;       /* -1 上一月，1 下一月，0 无 */
static int g_calTracking = 0;
static SYSTEMTIME g_lastToday;      /* 用于检测跨天刷新 */
static int g_lang = 0;              /* 当前界面语言，在语言区块中初始化 */

/* 视图模式与周视图状态 */
static int g_viewMode = VIEW_MONTH;
static SYSTEMTIME g_weekAnchor;     /* 周视图锚定日期（所在周） */
static int g_weekScroll = 0;        /* 周视图垂直滚动偏移（像素） */
static int g_weekHoverTask = 0;     /* 周视图悬停的任务 id（0 表示无） */
static int g_weekTracking = 0;
static int g_weekSelId = 0;         /* 周视图单击选中的任务 id（用于删除） */

/* 分类配色（下标 1..CAT_COUNT）：
   g_catFill 为方块饱和色（配白字），g_catSoft 为月历浅色底（配深字） */
static const COLORREF g_catFill[CAT_COUNT + 1] = {
    0,
    RGB(0x5B, 0x9B, 0xD5),  /* 1 课程 蓝 */
    RGB(0x70, 0xAD, 0x47),  /* 2 家务 绿 */
    RGB(0x9B, 0x7F, 0xD4),  /* 3 工作 紫 */
    RGB(0xE8, 0x71, 0x8A),  /* 4 运动 红 */
    RGB(0xF5, 0xA6, 0x23),  /* 5 娱乐 橙 */
    RGB(0x9A, 0xA0, 0xA6)   /* 6 其他 灰 */
};
static const COLORREF g_catSoft[CAT_COUNT + 1] = {
    0,
    RGB(0xDA, 0xE9, 0xF8),  /* 1 课程 浅蓝 */
    RGB(0xE2, 0xF0, 0xD9),  /* 2 家务 浅绿 */
    RGB(0xEC, 0xE6, 0xF7),  /* 3 工作 浅紫 */
    RGB(0xFB, 0xE3, 0xE8),  /* 4 运动 浅红 */
    RGB(0xFD, 0xEF, 0xD3),  /* 5 娱乐 浅橙 */
    RGB(0xEC, 0xEE, 0xEF)   /* 6 其他 浅灰 */
};

/* 默认提醒规则（分钟）：1 天、1 小时、30 分、5 分 */
static const int DEFAULT_RULES[4] = { 1440, 60, 30, 5 };

/*============================= 多语言 =============================*/

enum { LANG_ZH = 0, LANG_JA = 1, LANG_EN = 2, LANG_COUNT = 3 };

/*
  所有界面文本集中在此表：X(枚举名, 中文, 日本語, English)。
  使用宏保证三种语言的条目一一对应，不会错位。
*/
#define STR_TABLE(X) \
    X(APP_TITLE,       L"时间管理器 Time-Manager", L"タイムマネージャー Time-Manager", L"Time-Manager") \
    X(BTN_ADD,         L"添加日程", L"予定を追加", L"Add") \
    X(BTN_DELETE,      L"删除选中", L"選択を削除", L"Delete") \
    X(BTN_DELETE_N,    L"删除选中 (%d)", L"選択を削除 (%d)", L"Delete (%d)") \
    X(BTN_TODAY,       L"今日", L"今日", L"Today") \
    X(BTN_ALL,         L"全部", L"すべて", L"All") \
    X(BTN_SET,         L"设置", L"設定", L"Settings") \
    X(CHK_AUTOSTART,   L"开机自动启动", L"Windows 起動時に自動起動", L"Start with Windows") \
    X(TIP,             L"关闭窗口后将在托盘后台运行", L"閉じるとトレイで継続します", L"Close to tray, keeps running") \
    X(STAT_TODAY,      L"今日（%02d/%02d）有 %d 个日程", L"今日（%02d/%02d）の予定：%d 件", L"Today (%02d/%02d): %d events") \
    X(LIST_DATE,       L"日期", L"日付", L"Date") \
    X(LIST_DAY,        L"星期", L"曜日", L"Day") \
    X(LIST_START,      L"开始", L"開始", L"Start") \
    X(LIST_END,        L"结束", L"終了", L"End") \
    X(LIST_TIME,       L"时间", L"時間", L"Time") \
    X(LIST_TITLE,      L"标题", L"タイトル", L"Title") \
    X(LIST_NOTE,       L"备注", L"メモ", L"Note") \
    X(LIST_REMIND,     L"提醒", L"通知", L"Remind") \
    X(REMIND_COUNT,    L"%d 次", L"%d 回", L"%d times") \
    X(STATUS_ALL,      L"共 %d 个日程    数据文件：%s", L"%d 件の予定    データ：%s", L"%d events    Data: %s") \
    X(STATUS_FILTER,   L"%s（%s）共 %d 个日程    点击「全部」显示所有日程", L"%s（%s）%d 件    「すべて」で全件表示", L"%s (%s): %d events    Click \"All\" to show everything") \
    X(TRAY_TIP,        L"时间管理器（后台运行中）", L"タイムマネージャー（バックグラウンド実行中）", L"Time-Manager (running in background)") \
    X(TRAY_OPEN,       L"打开时间管理器", L"タイムマネージャーを開く", L"Open Time-Manager") \
    X(TRAY_EXIT,       L"退出", L"終了", L"Exit") \
    X(TRAY_BALLOON,    L"程序将在后台继续运行，双击托盘图标可恢复窗口。", L"バックグラウンドで実行を続けます。トレイアイコンをダブルクリックすると復元します。", L"Still running in the background. Double-click the tray icon to restore.") \
    X(ALREADY_RUNNING, L"时间管理器已经在运行中，请查看任务栏右下角托盘。", L"タイムマネージャーは既に実行中です。タスクトレイを確認してください。", L"Time-Manager is already running. Check the notification area.") \
    X(OK,              L"确定", L"OK", L"OK") \
    X(CANCEL,          L"取消", L"キャンセル", L"Cancel") \
    X(NOTICE,          L"提示", L"ヒント", L"Notice") \
    X(ERR_SAVE,        L"数据文件保存失败，请检查程序目录的写入权限。", L"データファイルを保存できません。プログラムフォルダーの書き込み権限を確認してください。", L"Could not save the data file. Check write permission for the program folder.") \
    X(NO_SELECTION,    L"请先在列表中选中要删除的日程（可按住 Ctrl 多选）。", L"削除する予定をリストから選択してください（Ctrl で複数選択可）。", L"Select one or more events in the list first (Ctrl-click for multiple).") \
    X(CONFIRM_DELETE,  L"确定删除日程「%s」吗？此操作不可恢复。", L"予定「%s」を削除しますか？この操作は元に戻せません。", L"Delete \"%s\"? This cannot be undone.") \
    X(CONFIRM_DELETE_MULTI, L"确定删除选中的 %d 个日程吗？此操作不可恢复。", L"選択した %d 件の予定を削除しますか？この操作は元に戻せません。", L"Delete the %d selected events? This cannot be undone.") \
    X(CONFIRM_DEL_TITLE, L"确认删除", L"削除の確認", L"Confirm deletion") \
    X(ADD_TITLE,       L"添加日程", L"予定を追加", L"Add event") \
    X(LBL_START,       L"开始：", L"開始：", L"Start:") \
    X(LBL_END,         L"结束：", L"終了：", L"End:") \
    X(LBL_TITLE,       L"标题：", L"タイトル：", L"Title:") \
    X(LBL_NOTE,        L"备注：", L"メモ：", L"Note:") \
    X(LBL_RULES,       L"提醒规则：", L"通知ルール：", L"Reminders:") \
    X(LBL_REPEAT,      L"重复：", L"繰り返し：", L"Repeat:") \
    X(REPEAT_ONCE,     L"单次日程", L"単発の予定", L"Single event") \
    X(REPEAT_WEEKS,    L"每周重复，连续 %d 周", L"毎週繰り返し（%d 週間）", L"Weekly for %d weeks") \
    X(ERR_TASK_REPEAT, L"连续日程将超出数量上限，请减少周数。", L"繰り返し予定が上限を超えます。週数を減らしてください。", L"The weekly events would exceed the event limit. Reduce the number of weeks.") \
    X(RULE_ADD,        L"添加规则", L"ルールを追加", L"Add rule") \
    X(RULE_DEL,        L"删除选中", L"選択を削除", L"Remove") \
    X(ADD_HINT,        L"提醒规则可自由添加或删除，最多 12 条；到点会在右下角弹出通知。", L"通知ルールは自由に追加・削除できます（最大 12 件）。時刻になると右下に通知が表示されます。", L"Add or remove reminder rules (up to 12). Notifications appear at the bottom right.") \
    X(UNIT_MIN,        L"分钟", L"分", L"min") \
    X(UNIT_HOUR,       L"小时", L"時間", L"hour") \
    X(UNIT_DAY,        L"天", L"日", L"day") \
    X(RULE_ITEM,       L"提前 %s", L"%s 前", L"%s before") \
    X(ERR_RULE_MAX,    L"每条日程最多 12 条提醒规则。", L"通知ルールは 1 件の予定につき最大 12 件です。", L"Up to 12 reminder rules per event.") \
    X(ERR_RULE_POS,    L"请输入大于 0 的数字。", L"0 より大きい数値を入力してください。", L"Please enter a number greater than 0.") \
    X(ERR_RULE_YEAR,   L"提前时间不能超过 1 年。", L"通知の指定は 1 年以内にしてください。", L"The lead time cannot exceed one year.") \
    X(ERR_RULE_DUP,    L"该提醒规则已存在。", L"同じ通知ルールが既にあります。", L"This reminder rule already exists.") \
    X(ERR_RULE_SEL,    L"请先选中要删除的提醒规则。", L"削除する通知ルールを選択してください。", L"Select a reminder rule to remove first.") \
    X(ERR_TASK_MAX,    L"日程数量已达上限。", L"予定の上限に達しました。", L"The event limit has been reached.") \
    X(ERR_TIME_INVALID,L"请选择有效的日期和时间。", L"有効な日時を選択してください。", L"Please choose a valid date and time.") \
    X(ERR_TIME_ORDER,  L"结束时间不能早于开始时间。", L"終了時刻は開始時刻より後にしてください。", L"The end time cannot be earlier than the start time.") \
    X(ERR_TITLE_EMPTY, L"请填写任务标题。", L"タイトルを入力してください。", L"Please enter a title.") \
    X(SET_TITLE,       L"设置", L"設定", L"Settings") \
    X(SET_SEC_APPEAR,  L"外观与启动", L"外観と起動", L"Appearance & Startup") \
    X(SET_SEC_ALERT,   L"提醒方式", L"通知", L"Notifications") \
    X(SET_LANG,        L"语言：", L"言語：", L"Language:") \
    X(SET_THEME,       L"界面风格：", L"テーマ：", L"Theme:") \
    X(THEME_CLASSIC,   L"经典白", L"クラシック", L"Classic") \
    X(THEME_MINT,      L"清新薄荷", L"フレッシュミント", L"Fresh Mint") \
    X(THEME_CREAM,     L"奶油晨光", L"クリームモーニング", L"Cream Morning") \
    X(THEME_SAKURA,    L"樱粉初雪", L"サクラ", L"Sakura") \
    X(LANG_ZH,         L"中文", L"中文", L"中文") \
    X(LANG_JA,         L"日本語", L"日本語", L"日本語") \
    X(LANG_EN,         L"English", L"English", L"English") \
    X(SET_AUTOSTART,   L"开机时自动启动（最小化到托盘）", L"ログイン時に自動起動（トレイに最小化）", L"Start automatically at login (minimized to tray)") \
    X(SET_MINSTART,    L"手动启动时也最小化到托盘", L"手動起動時もトレイに最小化", L"Also minimize to tray on manual start") \
    X(SET_DURATION,    L"通知停留：", L"通知の表示：", L"Show for:") \
    X(SET_SECONDS,     L"%d 秒", L"%d 秒", L"%d sec") \
    X(SET_SOUND,       L"提示音：", L"通知音：", L"Sound:") \
    X(SND_MUTE,        L"静音（不播放）", L"消音（再生しない）", L"Mute") \
    X(SND_DEFAULT,     L"系统默认", L"システム既定", L"System default") \
    X(SND_ASTERISK,    L"信息提示（叮咚）", L"情報（ピン）", L"Information (ding)") \
    X(SND_EXCLAM,      L"警告提示", L"警告", L"Warning") \
    X(SND_HAND,        L"关键提示", L"重大", L"Critical") \
    X(SND_BEEP,        L"简单蜂鸣", L"ビープ", L"Simple beep") \
    X(SND_WAV,         L"自定义 WAV 文件", L"カスタム WAV ファイル", L"Custom WAV file") \
    X(SET_TEST,        L"试听", L"試聴", L"Test") \
    X(SET_WAV,         L"自定义 WAV：", L"カスタム WAV：", L"Custom WAV:") \
    X(SET_BROWSE,      L"浏览…", L"参照…", L"Browse…") \
    X(SET_HINT,        L"选择“自定义 WAV 文件”时，请指定 .wav 音频文件作为提醒声音。", L"「カスタム WAV ファイル」を選んだ場合は、通知音として .wav ファイルを指定してください。", L"When \"Custom WAV file\" is selected, choose a .wav audio file as the reminder sound.") \
    X(SET_TEST_MUTE,   L"已选择静音，不会播放声音。", L"消音が選択されているため、音は再生されません。", L"Mute is selected, so no sound will play.") \
    X(SET_TEST_NO_WAV, L"请先选择一个 WAV 文件。", L"先に WAV ファイルを選択してください。", L"Please choose a WAV file first.") \
    X(CREDITS_AUTHOR,  L"制作人员：枫殇林叶", L"制作：枫殇林叶", L"Created by: 枫殇林叶") \
    X(CREDITS_EMAIL,   L"联系邮箱：<a href=\"mailto:%s\">%s</a>", L"連絡先：<a href=\"mailto:%s\">%s</a>", L"Contact: <a href=\"mailto:%s\">%s</a>") \
    X(FILTER_WAV,      L"WAV 音频文件 (*.wav)", L"WAV オーディオ (*.wav)", L"WAV audio (*.wav)") \
    X(FILTER_ALL,      L"所有文件 (*.*)", L"すべてのファイル (*.*)", L"All files (*.*)") \
    X(TOAST_APP,       L"时间管理器", L"タイムマネージャー", L"Time-Manager") \
    X(TOAST_HEAD,      L"任务「%s」将在 %s后开始", L"「%s」は %s後に開始します", L"\"%s\" starts in %s") \
    X(TOAST_TIME,      L"%s（%s）%s 开始，%s 结束", L"%s（%s）%s 開始、%s 終了", L"%s (%s)  %s – %s") \
    X(TOAST_NOTE,      L"备注：%s", L"メモ：%s", L"Note: %s") \
    X(NOW,             L"现在", L"今", L"now") \
    X(LEAD_DAY,        L"%d 天", L"%d日", L"%dd") \
    X(LEAD_HOUR,       L"%d 小时", L"%d時間", L"%dh") \
    X(LEAD_MIN,        L"%d 分钟", L"%d分", L"%dm") \
    X(LBL_CAT,         L"分类：", L"カテゴリー：", L"Category:") \
    X(CAT_COURSE,      L"课程", L"授業", L"Course") \
    X(CAT_CHORES,      L"家务", L"家事", L"Chores") \
    X(CAT_WORK,        L"工作", L"仕事", L"Work") \
    X(CAT_SPORT,       L"运动", L"運動", L"Sport") \
    X(CAT_FUN,         L"娱乐", L"趣味", L"Leisure") \
    X(CAT_OTHER,       L"其他", L"その他", L"Other") \
    X(LIST_CAT,        L"分类", L"カテゴリー", L"Category") \
    X(VIEW_MONTH,      L"月视图", L"月表示", L"Month") \
    X(VIEW_WEEK,       L"周视图", L"週表示", L"Week") \
    X(WEEK_PREV,       L"◀", L"◀", L"◀") \
    X(WEEK_NEXT,       L"▶", L"▶", L"▶") \
    X(WEEK_TODAY,      L"本周", L"今週", L"This week") \
    X(WEEK_RANGE,      L"%d/%d – %d/%d", L"%d/%d – %d/%d", L"%d/%d – %d/%d") \
    X(WEEK_NO_EVENT,   L"本周暂无日程", L"今週の予定はありません", L"No events this week") \
    X(DAY_EVENT_COUNT, L"%d 个日程", L"%d 件の予定", L"%d events") \
    X(AGENDA_EMPTY,    L"还没有日程，点右上角「添加日程」新增一条", L"予定はまだありません。右上の「予定を追加」から作成できます", L"No events yet — use \"Add\" at the top right") \
    X(AGENDA_EMPTY_DAY, L"这一天还没有安排，再点一次日期可取消筛选", L"この日の予定はありません。日付をもう一度押すと絞り込みを解除します", L"Nothing planned for this day. Click the date again to clear the filter") \
    X(AGENDA_TODAY,    L"今天", L"今日", L"Today") \
    X(TIP_CAT,         L"分类：%s", L"カテゴリー：%s", L"Category: %s") \
    X(TIP_TIME,        L"%s %s – %s", L"%s %s – %s", L"%s %s – %s") \
    X(TIP_REMIND,      L"提醒：%d 次", L"通知：%d 回", L"Reminders: %d") \
    X(TIP_NOTE,        L"备注：%s", L"メモ：%s", L"Note: %s") \
    X(TIP_MORE,        L"… 另外还有 %d 个日程", L"… 他 %d 件", L"… +%d more") \
    X(TIP_EMPTY,       L"暂无日程", L"予定なし", L"No events")

enum StrId
{
#define X(id, zh, ja, en) STR_##id,
    STR_TABLE(X)
#undef X
    STR_COUNT
};

static const wchar_t* const g_str[LANG_COUNT][STR_COUNT] =
{
    {
#define X(id, zh, ja, en) zh,
        STR_TABLE(X)
#undef X
    },
    {
#define X(id, zh, ja, en) ja,
        STR_TABLE(X)
#undef X
    },
    {
#define X(id, zh, ja, en) en,
        STR_TABLE(X)
#undef X
    }
};

static const wchar_t* Str(int id)
{
    if (id < 0 || id >= STR_COUNT)
    {
        return L"";
    }
    return g_str[g_lang][id];
}

/* 带格式参数的本地化文本 */
static void FormatStr(wchar_t* buf, size_t cch, int id, ...)
{
    va_list ap;
    va_start(ap, id);
    StringCchVPrintfW(buf, cch, Str(id), ap);
    va_end(ap);
}

static const wchar_t* const g_weekFull[LANG_COUNT][7] =
{
    { L"周日", L"周一", L"周二", L"周三", L"周四", L"周五", L"周六" },
    { L"日曜日", L"月曜日", L"火曜日", L"水曜日", L"木曜日", L"金曜日", L"土曜日" },
    { L"Sunday", L"Monday", L"Tuesday", L"Wednesday", L"Thursday", L"Friday", L"Saturday" }
};

static const wchar_t* const g_weekShort[LANG_COUNT][7] =
{
    { L"日", L"一", L"二", L"三", L"四", L"五", L"六" },
    { L"日", L"月", L"火", L"水", L"木", L"金", L"土" },
    { L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat" }
};

static const wchar_t* const g_monthName[LANG_COUNT][12] =
{
    { L"1月", L"2月", L"3月", L"4月", L"5月", L"6月",
      L"7月", L"8月", L"9月", L"10月", L"11月", L"12月" },
    { L"1月", L"2月", L"3月", L"4月", L"5月", L"6月",
      L"7月", L"8月", L"9月", L"10月", L"11月", L"12月" },
    { L"January", L"February", L"March", L"April", L"May", L"June",
      L"July", L"August", L"September", L"October", L"November", L"December" }
};

static void FmtMonthTitle(int year, int month, wchar_t* buf, int cch)
{
    if (g_lang == LANG_EN)
    {
        StringCchPrintfW(buf, cch, L"%s %d", g_monthName[LANG_EN][month - 1], year);
    }
    else
    {
        StringCchPrintfW(buf, cch, L"%d年%d月", year, month);
    }
}

/* 未保存过语言设置时，跟随系统界面语言 */
static int DetectSystemLang(void)
{
    switch (PRIMARYLANGID(GetUserDefaultUILanguage()))
    {
    case LANG_JAPANESE:
        return LANG_JA;
    case LANG_ENGLISH:
        return LANG_EN;
    default:
        return LANG_ZH;
    }
}

static const wchar_t* SoundStr(int sound)
{
    switch (sound)
    {
    case SOUND_MUTE:     return Str(STR_SND_MUTE);
    case SOUND_DEFAULT:  return Str(STR_SND_DEFAULT);
    case SOUND_ASTERISK: return Str(STR_SND_ASTERISK);
    case SOUND_EXCLAM:   return Str(STR_SND_EXCLAM);
    case SOUND_HAND:     return Str(STR_SND_HAND);
    case SOUND_BEEP:     return Str(STR_SND_BEEP);
    case SOUND_WAV:      return Str(STR_SND_WAV);
    }
    return L"";
}

/* 分类名称（分类编号 1..CAT_COUNT） */
static const wchar_t* CatStr(int cat)
{
    if (cat < 1 || cat > CAT_COUNT)
    {
        return Str(STR_CAT_OTHER);
    }
    return Str(STR_CAT_COURSE + cat - 1);
}

/* 界面风格名称 */
static const wchar_t* ThemeStr(int theme)
{
    switch (theme)
    {
    case THEME_CLASSIC: return Str(STR_THEME_CLASSIC);
    case THEME_MINT:    return Str(STR_THEME_MINT);
    case THEME_CREAM:   return Str(STR_THEME_CREAM);
    case THEME_SAKURA:  return Str(STR_THEME_SAKURA);
    }
    return Str(STR_THEME_MINT);
}

static ULONGLONG SysTimeToU64(const SYSTEMTIME* st);

/* 判断两个日期是否相同 */
static int SameDate(const SYSTEMTIME* a, const SYSTEMTIME* b)
{
    return a->wYear == b->wYear && a->wMonth == b->wMonth && a->wDay == b->wDay;
}

/* 只填了年月日的 SYSTEMTIME，把星期字段补正确（wDayOfWeek 常被漏填，
   会让“所在周的周一”算错，周视图列头因此随选中日期漂移） */
static void NormalizeWeekday(SYSTEMTIME* st)
{
    FILETIME ft;
    SYSTEMTIME out;
    st->wDayOfWeek = 0;
    if (SystemTimeToFileTime(st, &ft) && FileTimeToSystemTime(&ft, &out))
    {
        *st = out;
    }
}

/* 日期加减整数天，结果写回 st */
static void AddDays(SYSTEMTIME* st, int delta)
{
    FILETIME ft;
    ULARGE_INTEGER u;
    if (!SystemTimeToFileTime(st, &ft))
    {
        return;
    }
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    u.QuadPart += (LONGLONG)delta * 864000000000ULL;
    ft.dwLowDateTime = u.LowPart;
    ft.dwHighDateTime = u.HighPart;
    FileTimeToSystemTime(&ft, st);
}

/* 取给定日期所在周的周一（中国习惯以周一为一周开始） */
static void MondayOfDate(const SYSTEMTIME* date, SYSTEMTIME* monday)
{
    *monday = *date;
    NormalizeWeekday(monday);
    int dow = monday->wDayOfWeek;     /* 0=周日 ... 6=周六 */
    int delta = (dow == 0) ? -6 : (1 - dow);
    AddDays(monday, delta);
}

/* 判断任务是否发生在给定日期（支持跨天任务） */
static int TaskOnDate(const struct Task* t, int year, int month, int day)
{
    SYSTEMTIME d;
    ZeroMemory(&d, sizeof(d));
    d.wYear = (WORD)year;
    d.wMonth = (WORD)month;
    d.wDay = (WORD)day;
    ULONGLONG s = SysTimeToU64(&t->stStart);
    ULONGLONG e = SysTimeToU64(&t->stEnd);
    ULONGLONG d0 = SysTimeToU64(&d);
    SYSTEMTIME d2 = d;
    AddDays(&d2, 1);
    ULONGLONG d1 = SysTimeToU64(&d2);
    return s < d1 && e > d0;
}

/*============================= 前向声明 =============================*/

LRESULT CALLBACK MainWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK AddDlgProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK SetDlgProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK ToastProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK CalWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK WeekWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK AgendaWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK TipWndProc(HWND, UINT, WPARAM, LPARAM);

static ULONGLONG SysTimeToU64(const SYSTEMTIME* st);
void RefreshList(void);
void RefreshWeek(void);
void RefreshCalendar(void);
static void Main_OnSize(HWND hWnd, int W, int H);
void ShowHoverTip(const wchar_t* text, const RECT* avoid, int firstBold, COLORREF accent);
void HideHoverTip(void);
void SetViewMode(int mode);
void SaveTasks(void);
void LoadTasks(void);
void SaveSettings(void);
void LoadSettings(void);
void CheckReminders(void);
void DeleteSelected(void);
static void UpdateDeleteButton(void);
void QueueToast(const struct Task* t, int leadMinutes);
void ShowAddDialog(void);
void ShowSettingsDialog(void);
void ApplyLanguage(void);
void ApplyTheme(void);
void CalSetDate(const SYSTEMTIME* st);
static void CreateAppFonts(void);

/*============================= 通用工具 =============================*/

static int S(int v)
{
    return MulDiv(v, g_dpi, 96);
}

static ULONGLONG SysTimeToU64(const SYSTEMTIME* st)
{
    FILETIME ft;
    ULARGE_INTEGER u;
    u.QuadPart = 0;
    if (SystemTimeToFileTime(st, &ft))
    {
        u.LowPart = ft.dwLowDateTime;
        u.HighPart = ft.dwHighDateTime;
    }
    return u.QuadPart;
}

/* 返回 target 相对 now 的剩余秒数（未来为正、过去为负），秒级精度避免提前一分钟取整误差。
   注意：必须先转成有符号整数再相减，否则过去时间会因无符号下溢变成巨大正数。 */
static LONGLONG DiffSeconds(const SYSTEMTIME* now, const SYSTEMTIME* target)
{
    LONGLONG a = (LONGLONG)SysTimeToU64(now);
    LONGLONG b = (LONGLONG)SysTimeToU64(target);
    return (b - a) / 10000000LL;
}

static void FmtDate(const SYSTEMTIME* st, wchar_t* buf, int cch)
{
    StringCchPrintfW(buf, cch, L"%04d/%02d/%02d", st->wYear, st->wMonth, st->wDay);
}

static void FmtTime(const SYSTEMTIME* st, wchar_t* buf, int cch)
{
    StringCchPrintfW(buf, cch, L"%02d:%02d", st->wHour, st->wMinute);
}

/* 提前量按当前语言格式化：5 分钟 / 1 小时 / 1 天 2 小时 ... */
static void FmtLead(int minutes, wchar_t* buf, int cch)
{
    if (minutes <= 0)
    {
        StringCchCopyW(buf, cch, Str(STR_NOW));
        return;
    }
    int d = minutes / 1440;
    int h = (minutes % 1440) / 60;
    int m = minutes % 60;
    wchar_t tmp[96];
    tmp[0] = 0;
    if (d > 0)
    {
        StringCchPrintfW(tmp + lstrlenW(tmp), 96 - lstrlenW(tmp), Str(STR_LEAD_DAY), d);
    }
    if (h > 0)
    {
        if (tmp[0] != 0)
        {
            StringCchCatW(tmp, 96, L" ");
        }
        StringCchPrintfW(tmp + lstrlenW(tmp), 96 - lstrlenW(tmp), Str(STR_LEAD_HOUR), h);
    }
    if (m > 0)
    {
        if (tmp[0] != 0)
        {
            StringCchCatW(tmp, 96, L" ");
        }
        StringCchPrintfW(tmp + lstrlenW(tmp), 96 - lstrlenW(tmp), Str(STR_LEAD_MIN), m);
    }
    StringCchCopyW(buf, cch, tmp);
}

static void CleanText(wchar_t* s)
{
    for (int i = 0; s[i] != 0; i++)
    {
        if (s[i] == L'|' || s[i] == L'\r' || s[i] == L'\n')
        {
            s[i] = L' ';
        }
    }
}

/* 按竖线拆分，空字段保留，parts 下标从 1 开始 */
static int SplitLine(wchar_t* line, wchar_t* parts[], int maxParts)
{
    int n = 1;
    parts[1] = line;
    wchar_t* p = line;
    while (*p != 0)
    {
        if (*p == L'|')
        {
            *p = 0;
            n++;
            if (n > maxParts)
            {
                return maxParts;
            }
            parts[n] = p + 1;
        }
        p++;
    }
    return n;
}

static void InitPaths(void)
{
    DWORD n = GetModuleFileNameW(NULL, g_dataPath, MAX_PATH);
    if (n == 0)
    {
        StringCchCopyW(g_dataPath, MAX_PATH, L"tasks.dat");
        StringCchCopyW(g_setPath, MAX_PATH, L"settings.dat");
        return;
    }
    int pos = -1;
    for (int i = 0; g_dataPath[i] != 0; i++)
    {
        if (g_dataPath[i] == L'\\' || g_dataPath[i] == L'/')
        {
            pos = i;
        }
    }
    if (pos >= 0)
    {
        g_dataPath[pos + 1] = 0;
    }
    StringCchCopyW(g_setPath, MAX_PATH, g_dataPath);
    StringCchCatW(g_dataPath, MAX_PATH, L"tasks.dat");
    StringCchCatW(g_setPath, MAX_PATH, L"settings.dat");
}

static HFONT MakeFont(int size, int bold)
{
    return CreateFontW(-S(size), 0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, g_fontFamily);
}

static void DestroyAppFonts(void)
{
    HFONT* fonts[16] = {
        &g_fontUi, &g_fontBold, &g_fontStatus, &g_fontTTitle, &g_fontTHead,
        &g_fontTBody, &g_fontTI, &g_fontCal, &g_fontCalBold, &g_fontCalTitle,
        &g_fontWeekHead, &g_fontWeekAxis, &g_fontWeekBlock,
        &g_fontTipTitle, &g_fontTipBody
    };
    for (int i = 0; i < 15; i++)
    {
        if (*fonts[i] != NULL)
        {
            DeleteObject(*fonts[i]);
            *fonts[i] = NULL;
        }
    }
    if (g_fontCalWeek != NULL)
    {
        DeleteObject(g_fontCalWeek);
        g_fontCalWeek = NULL;
    }
}

/* 字形回退：中日英分别使用系统里最合适的界面字体 */
static void CreateAppFonts(void)
{
    if (g_lang == LANG_JA)
    {
        g_fontFamily = L"Yu Gothic UI";
    }
    else if (g_lang == LANG_EN)
    {
        g_fontFamily = L"Segoe UI";
    }
    else
    {
        g_fontFamily = L"Microsoft YaHei UI";
    }
    DestroyAppFonts();

    /* 字体整体调大：正文 12pt，日历数字 13pt，周视图方块 11pt */
    g_fontUi = MakeFont(12, FALSE);
    g_fontBold = MakeFont(12, TRUE);
    g_fontStatus = MakeFont(11, FALSE);
    g_fontTTitle = MakeFont(10, FALSE);
    g_fontTHead = MakeFont(13, TRUE);
    g_fontTBody = MakeFont(11, FALSE);
    g_fontTI = MakeFont(16, TRUE);
    g_fontCal = MakeFont(13, FALSE);
    g_fontCalBold = MakeFont(13, TRUE);
    g_fontCalWeek = MakeFont(10, TRUE);
    g_fontCalTitle = MakeFont(15, TRUE);
    g_fontWeekHead = MakeFont(12, TRUE);
    g_fontWeekAxis = MakeFont(10, FALSE);
    g_fontWeekBlock = MakeFont(11, TRUE);
    g_fontTipTitle = MakeFont(12, TRUE);
    g_fontTipBody = MakeFont(11, FALSE);
}

/* 从 exe 资源加载应用图标，失败时退回系统图标 */
static HICON LoadAppIcon(int cx, int cy)
{
    HICON icon = (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(IDI_APPICON),
        IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
    if (icon == NULL)
    {
        icon = LoadIconW(NULL, IDI_APPLICATION);
    }
    return icon;
}

/*============================= 设置 =============================*/

void LoadSettings(void)
{
    g_set.autostart = 0;
    g_set.minStart = 0;
    g_set.sound = SOUND_ASTERISK;
    g_set.toastSec = 12;
    g_set.wavPath[0] = 0;
    g_set.lang = DetectSystemLang();
    g_set.theme = THEME_DEFAULT;

    /* 注册表中的实际自启状态优先 */
    g_set.autostart = 0;

    FILE* f = NULL;
    if (_wfopen_s(&f, g_setPath, L"r, ccs=UTF-8") != 0 || f == NULL)
    {
        return;
    }
    wchar_t line[MAX_PATH + 100];
    if (fgetws(line, MAX_PATH + 100, f) != NULL)
    {
        wchar_t* parts[9];
        int n = SplitLine(line, parts, 7);
        if (n >= 4)
        {
            g_set.autostart = _wtoi(parts[1]);
            g_set.minStart = _wtoi(parts[2]);
            g_set.sound = _wtoi(parts[3]);
            g_set.toastSec = _wtoi(parts[4]);
            if (g_set.sound < 0 || g_set.sound > SOUND_WAV) { g_set.sound = SOUND_ASTERISK; }
            if (g_set.toastSec < 5 || g_set.toastSec > 60) { g_set.toastSec = 12; }
            if (n >= 5 && parts[5][0] != 0)
            {
                StringCchCopyW(g_set.wavPath, MAX_PATH, parts[5]);
            }
            if (n >= 6)
            {
                int lang = _wtoi(parts[6]);
                if (lang >= 0 && lang < LANG_COUNT)
                {
                    g_set.lang = lang;
                }
            }
            if (n >= 7)
            {
                int theme = _wtoi(parts[7]);
                if (theme >= 0 && theme < THEME_COUNT)
                {
                    g_set.theme = theme;
                }
            }
        }
    }
    fclose(f);
}

void SaveSettings(void)
{
    FILE* f = NULL;
    if (_wfopen_s(&f, g_setPath, L"w, ccs=UTF-8") != 0 || f == NULL)
    {
        return;
    }
    wchar_t wav[MAX_PATH];
    StringCchCopyW(wav, MAX_PATH, g_set.wavPath);
    CleanText(wav);
    fwprintf(f, L"%d|%d|%d|%d|%s|%d|%d\n",
        g_set.autostart, g_set.minStart, g_set.sound, g_set.toastSec,
        wav[0] != 0 ? wav : L"", g_set.lang, g_set.theme);
    fclose(f);
}

/*============================= 开机自启 =============================*/

#define AUTORUN_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"

static int IsAutoRun(void)
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, AUTORUN_KEY, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
    {
        return 0;
    }
    int ok = (RegQueryValueExW(hKey, L"Time-Maneger", NULL, NULL, NULL, NULL) == ERROR_SUCCESS);
    RegCloseKey(hKey);
    return ok;
}

static void SetAutoRun(int enable)
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, AUTORUN_KEY, 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS)
    {
        return;
    }
    if (enable)
    {
        wchar_t exe[MAX_PATH];
        if (GetModuleFileNameW(NULL, exe, MAX_PATH) == 0)
        {
            RegCloseKey(hKey);
            return;
        }
        wchar_t cmd[MAX_PATH + 20];
        StringCchPrintfW(cmd, MAX_PATH + 20, L"\"%s\" --min", exe);
        RegSetValueExW(hKey, L"Time-Maneger", 0, REG_SZ,
            (const BYTE*)cmd, (DWORD)(lstrlenW(cmd) + 1) * sizeof(wchar_t));
    }
    else
    {
        RegDeleteValueW(hKey, L"Time-Maneger");
    }
    RegCloseKey(hKey);
}

/*============================= 提示音 =============================*/

static void PlayReminderSound(void)
{
    switch (g_set.sound)
    {
    case SOUND_MUTE:
        break;
    case SOUND_DEFAULT:
        PlaySoundW(L"SystemDefault", NULL, SND_ALIAS | SND_ASYNC);
        break;
    case SOUND_ASTERISK:
        PlaySoundW(L"SystemAsterisk", NULL, SND_ALIAS | SND_ASYNC);
        break;
    case SOUND_EXCLAM:
        PlaySoundW(L"SystemExclamation", NULL, SND_ALIAS | SND_ASYNC);
        break;
    case SOUND_HAND:
        PlaySoundW(L"SystemHand", NULL, SND_ALIAS | SND_ASYNC);
        break;
    case SOUND_BEEP:
        MessageBeep(0xFFFFFFFF);
        break;
    case SOUND_WAV:
        if (g_set.wavPath[0] != 0)
        {
            PlaySoundW(g_set.wavPath, NULL, SND_FILENAME | SND_ASYNC);
        }
        break;
    }
}

/*============================= 任务存取 =============================*/

static void SortTasks(void)
{
    for (int i = 2; i <= g_count; i++)
    {
        struct Task key = g_tasks[i];
        int j = i - 1;
        while (j >= 1 && SysTimeToU64(&g_tasks[j].stStart) > SysTimeToU64(&key.stStart))
        {
            g_tasks[j + 1] = g_tasks[j];
            j--;
        }
        g_tasks[j + 1] = key;
    }
}

/* 规则按提前分钟数降序（大的先触发） */
static void SortRules(struct Task* t)
{
    for (int i = 2; i <= t->rcount; i++)
    {
        struct Reminder key = t->r[i];
        int j = i - 1;
        while (j >= 1 && t->r[j].minutes < key.minutes)
        {
            t->r[j + 1] = t->r[j];
            j--;
        }
        t->r[j + 1] = key;
    }
}

/*
  收敛已错过的提醒规则。
  includeNearest=0：程序启动加载时，保留当前所处级别补响一次；
  includeNearest=1：新建任务时，已跨过的级别不再补响；
                    但提前 5 分钟以内的规则仍然保留（马上开始，最有价值）。
  diffSec 为任务开始相对当前的剩余秒数。
  返回值：是否有规则的 fired 状态被修改（供调用方决定是否落盘）。
*/
static int NormalizeRules(struct Task* t, LONGLONG diffSec, int includeNearest)
{
    int changed = 0;
    if (diffSec < 0)
    {
        for (int i = 1; i <= t->rcount; i++)
        {
            if (t->r[i].fired == 0)
            {
                t->r[i].fired = 1;
                changed = 1;
            }
        }
        return changed;
    }
    int nearest = 0;
    for (int i = 1; i <= t->rcount; i++)
    {
        if (diffSec <= (LONGLONG)t->r[i].minutes * 60)
        {
            nearest = i;   /* 规则已降序，最后一个满足的是最小分钟数 */
        }
    }
    if (nearest == 0)
    {
        return changed;
    }
    for (int i = 1; i < nearest; i++)
    {
        if (t->r[i].fired == 0)
        {
            t->r[i].fired = 1;
            changed = 1;
        }
    }
    if (includeNearest && t->r[nearest].minutes > 5 && t->r[nearest].fired == 0)
    {
        t->r[nearest].fired = 1;
        changed = 1;
    }
    return changed;
}

void SaveTasks(void)
{
    FILE* f = NULL;
    if (_wfopen_s(&f, g_dataPath, L"w, ccs=UTF-8") != 0 || f == NULL)
    {
        MessageBoxW(g_hMain, Str(STR_ERR_SAVE),
            Str(STR_APP_TITLE), MB_OK | MB_ICONWARNING | MB_TOPMOST);
        return;
    }

    fwprintf(f, L"%d %d\n", g_count, g_nextId);
    for (int i = 1; i <= g_count; i++)
    {
        struct Task* t = &g_tasks[i];
        wchar_t title[MAX_TITLE];
        wchar_t note[MAX_NOTE];
        StringCchCopyW(title, MAX_TITLE, t->title);
        StringCchCopyW(note, MAX_NOTE, t->note);
        CleanText(title);
        CleanText(note);

        fwprintf(f, L"T|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%s|%s\n",
            t->id, t->category,
            t->stStart.wYear, t->stStart.wMonth, t->stStart.wDay,
            t->stStart.wHour, t->stStart.wMinute,
            t->stEnd.wYear, t->stEnd.wMonth, t->stEnd.wDay,
            t->stEnd.wHour, t->stEnd.wMinute,
            title, note);

        fwprintf(f, L"R|%d", t->rcount);
        for (int k = 1; k <= t->rcount; k++)
        {
            fwprintf(f, L"|%d|%d", t->r[k].minutes, t->r[k].fired);
        }
        fwprintf(f, L"\n");
    }
    fclose(f);
}

/* 读取旧格式一行（固定 17 字段、4 条固定提醒），成功返回 1 */
static int LoadLegacyLine(wchar_t* line, struct Task* t)
{
    wchar_t* parts[20];
    int n = SplitLine(line, parts, 17);
    if (n < 17)
    {
        return 0;
    }
    ZeroMemory(t, sizeof(struct Task));
    t->id = _wtoi(parts[1]);
    t->category = CAT_DEFAULT;
    t->stStart.wYear = (WORD)_wtoi(parts[2]);
    t->stStart.wMonth = (WORD)_wtoi(parts[3]);
    t->stStart.wDay = (WORD)_wtoi(parts[4]);
    t->stStart.wHour = (WORD)_wtoi(parts[5]);
    t->stStart.wMinute = (WORD)_wtoi(parts[6]);
    t->stEnd.wYear = (WORD)_wtoi(parts[7]);
    t->stEnd.wMonth = (WORD)_wtoi(parts[8]);
    t->stEnd.wDay = (WORD)_wtoi(parts[9]);
    t->stEnd.wHour = (WORD)_wtoi(parts[10]);
    t->stEnd.wMinute = (WORD)_wtoi(parts[11]);
    StringCchCopyW(t->title, MAX_TITLE, parts[12]);
    StringCchCopyW(t->note, MAX_NOTE, parts[13]);
    t->rcount = 4;
    for (int k = 1; k <= 4; k++)
    {
        t->r[k].minutes = DEFAULT_RULES[k - 1];
        t->r[k].fired = (_wtoi(parts[13 + k]) != 0) ? 1 : 0;
    }
    return 1;
}

void LoadTasks(void)
{
    g_count = 0;
    g_nextId = 1;

    FILE* f = NULL;
    if (_wfopen_s(&f, g_dataPath, L"r, ccs=UTF-8") != 0 || f == NULL)
    {
        return;
    }

    wchar_t header[64];
    if (fgetws(header, 64, f) != NULL)
    {
        int cnt = 0;
        int nid = 1;
        swscanf_s(header, L"%d %d", &cnt, &nid);
        if (cnt < 0) { cnt = 0; }
        if (cnt > MAX_TASKS) { cnt = MAX_TASKS; }
        g_count = cnt;
        if (nid >= 1) { g_nextId = nid; }
    }

    wchar_t line[MAX_LINE];
    int index = 0;
    int havePending = 0;
    while (fgetws(line, MAX_LINE, f) != NULL && index < MAX_TASKS)
    {
        size_t ll = wcslen(line);
        while (ll > 0 && (line[ll - 1] == L'\n' || line[ll - 1] == L'\r'))
        {
            line[ll - 1] = 0;
            ll--;
        }
        if (line[0] == 0)
        {
            continue;
        }

        if (line[0] == L'T' && line[1] == L'|')
        {
            /* 新格式任务行含 14 个字段（id、分类、11 个时间字段、标题、备注）；
               早期版本为 13 个字段（无分类），读取时按字段数自适应。 */
            wchar_t* body = line + 2;
            wchar_t* parts[18];
            int n = SplitLine(body, parts, 14);
            int hasCat = (n >= 14);
            if (n < 13)
            {
                havePending = 0;
                continue;
            }
            int off = hasCat ? 1 : 0;     /* 新格式时间字段整体后移一位 */
            struct Task t;
            ZeroMemory(&t, sizeof(t));
            t.category = hasCat ? _wtoi(parts[2]) : CAT_DEFAULT;
            if (t.category < 1 || t.category > CAT_COUNT) { t.category = CAT_DEFAULT; }
            t.id = _wtoi(parts[1]);
            t.stStart.wYear = (WORD)_wtoi(parts[2 + off]);
            t.stStart.wMonth = (WORD)_wtoi(parts[3 + off]);
            t.stStart.wDay = (WORD)_wtoi(parts[4 + off]);
            t.stStart.wHour = (WORD)_wtoi(parts[5 + off]);
            t.stStart.wMinute = (WORD)_wtoi(parts[6 + off]);
            t.stEnd.wYear = (WORD)_wtoi(parts[7 + off]);
            t.stEnd.wMonth = (WORD)_wtoi(parts[8 + off]);
            t.stEnd.wDay = (WORD)_wtoi(parts[9 + off]);
            t.stEnd.wHour = (WORD)_wtoi(parts[10 + off]);
            t.stEnd.wMinute = (WORD)_wtoi(parts[11 + off]);
            StringCchCopyW(t.title, MAX_TITLE, parts[12 + off]);
            StringCchCopyW(t.note, MAX_NOTE, parts[13 + off]);
            t.rcount = 0;
            index++;
            g_tasks[index] = t;
            havePending = 1;
        }
        else if (line[0] == L'R' && line[1] == L'|' && havePending)
        {
            wchar_t* body = line + 2;
            wchar_t* parts[2 * MAX_REMIND + 4];
            int n = SplitLine(body, parts, 2 * MAX_REMIND + 2);
            int rc = _wtoi(parts[1]);
            if (rc < 0) { rc = 0; }
            if (rc > MAX_REMIND) { rc = MAX_REMIND; }
            struct Task* t = &g_tasks[index];
            t->rcount = rc;
            for (int k = 1; k <= rc; k++)
            {
                t->r[k].minutes = _wtoi(parts[2 * k]);
                t->r[k].fired = (_wtoi(parts[2 * k + 1]) != 0) ? 1 : 0;
            }
            havePending = 0;
        }
        else
        {
            /* 旧格式兼容 */
            struct Task t;
            if (LoadLegacyLine(line, &t))
            {
                index++;
                g_tasks[index] = t;
            }
            havePending = 0;
        }
    }
    fclose(f);
    g_count = index;

    for (int i = 1; i <= g_count; i++)
    {
        if (g_tasks[i].id >= g_nextId)
        {
            g_nextId = g_tasks[i].id + 1;
        }
        FILETIME ft;
        if (SystemTimeToFileTime(&g_tasks[i].stStart, &ft))
        {
            FileTimeToSystemTime(&ft, &g_tasks[i].stStart);
        }
        SortRules(&g_tasks[i]);
    }

    SYSTEMTIME now;
    GetLocalTime(&now);
    int normChanged = 0;
    for (int i = 1; i <= g_count; i++)
    {
        if (NormalizeRules(&g_tasks[i], DiffSeconds(&now, &g_tasks[i].stStart), 0))
        {
            normChanged = 1;
        }
    }
    SortTasks();
    if (normChanged)
    {
        /* 启动时收敛掉的过期/已错过提醒立即落盘，保证文件状态一致 */
        SaveTasks();
    }
}

/*============================= 提醒检查 =============================*/

void CheckReminders(void)
{
    SYSTEMTIME now;
    GetLocalTime(&now);
    int changed = 0;

    for (int i = 1; i <= g_count; i++)
    {
        struct Task* t = &g_tasks[i];
        LONGLONG diffSec = DiffSeconds(&now, &t->stStart);

        if (diffSec < 0)
        {
            /* 任务开始时间已过：所有未触发的提醒不再弹，标记已触发 */
            int need = 0;
            for (int k = 1; k <= t->rcount; k++)
            {
                if (t->r[k].fired == 0)
                {
                    t->r[k].fired = 1;
                    need = 1;
                }
            }
            if (need)
            {
                changed = 1;
            }
            continue;
        }

        for (int k = 1; k <= t->rcount; k++)
        {
            /* 精确到秒：进入“提前 N 分钟”窗口（剩余 <= N*60 秒）才触发 */
            if (t->r[k].fired == 0 && diffSec <= (LONGLONG)t->r[k].minutes * 60)
            {
                t->r[k].fired = 1;
                changed = 1;
                QueueToast(t, t->r[k].minutes);
            }
        }
    }

    if (changed)
    {
        SaveTasks();
    }
}

/*============================= Toast 通知 =============================*/

static int FindToastSlot(HWND h)
{
    for (int i = 1; i <= MAX_TOASTS; i++)
    {
        if (g_toasts[i].hwnd == h)
        {
            return i;
        }
    }
    return 0;
}

/* 重新排列所有可见通知（从右下角向上堆叠） */
static void LayoutToasts(void)
{
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int W = S(380);
    int H = S(110);
    int slot = 0;

    for (int i = 1; i <= MAX_TOASTS; i++)
    {
        struct Toast* t = &g_toasts[i];
        if (t->hwnd == NULL)
        {
            continue;
        }
        int x = wa.right - S(12) - W;
        int y = wa.bottom - S(10) - (slot + 1) * H - slot * S(8);
        t->targetX = x;
        if (t->state == 1 || t->state == 2)
        {
            t->y = y;
        }
        if (t->state == 0)
        {
            /* 滑入阶段只更新 x，y 提前固定 */
            t->y = y;
        }
        SetWindowPos(t->hwnd, HWND_TOPMOST, t->x, y, 0, 0,
            SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOZORDER);
        slot++;
    }
}

static void PaintToast(HWND h, struct Toast* t)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(h, &ps);
    int W = S(380);
    int H = S(110);

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, W, H);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);

    RECT all;
    SetRect(&all, 0, 0, W, H);
    HBRUSH white = CreateSolidBrush(g_theme->toastBg);
    FillRect(mem, &all, white);
    DeleteObject(white);

    /* 细边框 */
    HPEN border = CreatePen(PS_SOLID, 1, g_theme->toastBorder);
    HGDIOBJ oldPen = SelectObject(mem, border);
    HBRUSH cardBrush = CreateSolidBrush(g_theme->toastCard);
    HGDIOBJ oldBrush = SelectObject(mem, cardBrush);
    RoundRect(mem, 0, 0, W - 1, H - 1, S(12), S(12));
    SelectObject(mem, oldPen);
    SelectObject(mem, oldBrush);
    DeleteObject(border);
    DeleteObject(cardBrush);

    /* 左侧主色竖条 */
    HBRUSH accent = CreateSolidBrush(g_theme->accent);
    RECT bar;
    SetRect(&bar, 0, S(10), S(4), H - S(10));
    FillRect(mem, &bar, accent);
    DeleteObject(accent);

    /* 主色信息圆 + 白色 i */
    HBRUSH blueBrush = CreateSolidBrush(g_theme->accent);
    HGDIOBJ ob = SelectObject(mem, blueBrush);
    SelectObject(mem, GetStockObject(NULL_PEN));
    Ellipse(mem, S(20), S(30), S(48), S(58));
    SelectObject(mem, ob);
    DeleteObject(blueBrush);

    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, RGB(255, 255, 255));
    HGDIOBJ oldFont = SelectObject(mem, g_fontTI);
    RECT ri;
    SetRect(&ri, S(20), S(29), S(48), S(59));
    DrawTextW(mem, L"i", 1, &ri, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    /* 应用标题（小灰字） */
    SelectObject(mem, g_fontTTitle);
    SetTextColor(mem, g_theme->toastTitle);
    RECT rt;
    SetRect(&rt, S(60), S(8), W - S(34), S(18));
    DrawTextW(mem, Str(STR_TOAST_APP), -1, &rt, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

    /* 粗体首行 */
    SelectObject(mem, g_fontTHead);
    SetTextColor(mem, g_theme->toastHead);
    RECT rh;
    SetRect(&rh, S(60), S(24), W - S(34), S(44));
    DrawTextW(mem, t->head, -1, &rh, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

    /* 正文两行 */
    SelectObject(mem, g_fontTBody);
    SetTextColor(mem, g_theme->toastBody);
    RECT rb1;
    SetRect(&rb1, S(60), S(50), W - S(18), S(70));
    DrawTextW(mem, t->body1, -1, &rb1, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    RECT rb2;
    SetRect(&rb2, S(60), S(72), W - S(18), S(92));
    DrawTextW(mem, t->body2, -1, &rb2, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

    /* 关闭按钮（hover 时浅灰底） */
    int closeX = W - S(28);
    int closeY = S(4);
    if (t->closeHover)
    {
        HBRUSH hv = CreateSolidBrush(g_theme->hover);
        HGDIOBJ o = SelectObject(mem, hv);
        SelectObject(mem, GetStockObject(NULL_PEN));
        Ellipse(mem, closeX, closeY, closeX + S(22), closeY + S(22));
        SelectObject(mem, o);
        DeleteObject(hv);
    }
    HPEN xPen = CreatePen(PS_SOLID, 1, t->closeHover ? g_theme->text : g_theme->muted);
    SelectObject(mem, xPen);
    int cx = closeX + S(7);
    int cy = closeY + S(7);
    int d = S(8);
    MoveToEx(mem, cx, cy, NULL);
    LineTo(mem, cx + d, cy + d);
    MoveToEx(mem, cx + d, cy, NULL);
    LineTo(mem, cx, cy + d);
    DeleteObject(xPen);

    SelectObject(mem, oldFont);
    BitBlt(hdc, 0, 0, W, H, mem, 0, 0, SRCCOPY);

    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(h, &ps);
}

static void ToastTick(HWND h, struct Toast* t)
{
    int W = S(380);
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);

    if (t->state == 0)
    {
        t->tick++;
        int total = 16;
        int nowTick = t->tick;
        if (nowTick >= total)
        {
            t->state = 1;
            t->tick = 0;
            t->x = t->targetX;
        }
        else
        {
            float p = (float)nowTick / (float)total;
            float ease = 1.0f - (1.0f - p) * (1.0f - p);
            t->x = (int)(t->startX + (t->targetX - t->startX) * ease);
        }
    }
    else if (t->state == 1)
    {
        if (!t->hover)
        {
            t->showTick += 15;
            if (t->showTick >= g_set.toastSec * 1000)
            {
                t->state = 2;
                t->tick = 0;
            }
        }
    }
    else if (t->state == 2)
    {
        t->tick++;
        int total = 12;
        if (t->tick >= total)
        {
            t->hwnd = NULL;
            KillTimer(h, 1);
            DestroyWindow(h);
            LayoutToasts();
            return;
        }
        t->alpha = 255 - (255 * t->tick / total);
    }

    SetWindowPos(h, HWND_TOPMOST, t->x, t->y, 0, 0,
        SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOZORDER);
    SetLayeredWindowAttributes(h, 0, (BYTE)t->alpha, LWA_ALPHA);
}

LRESULT CALLBACK ToastProc(HWND h, UINT message, WPARAM wParam, LPARAM lParam)
{
    int slot = FindToastSlot(h);
    struct Toast* t = slot > 0 ? &g_toasts[slot] : NULL;

    switch (message)
    {
    case WM_TIMER:
        if (wParam == 1 && t != NULL)
        {
            ToastTick(h, t);
        }
        return 0;

    case WM_PAINT:
        if (t != NULL)
        {
            PaintToast(h, t);
        }
        return 0;

    case WM_MOUSEMOVE:
        if (t != NULL)
        {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            if (!t->tracking)
            {
                TRACKMOUSEEVENT tme;
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = h;
                TrackMouseEvent(&tme);
                t->tracking = 1;
            }
            t->hover = 1;
            int closeHover = (x >= S(352) - S(4) && x <= S(352) + S(22) &&
                              y >= S(4) && y <= S(26));
            if (closeHover != t->closeHover)
            {
                t->closeHover = closeHover;
                InvalidateRect(h, NULL, FALSE);
            }
        }
        return 0;

    case WM_MOUSELEAVE:
        if (t != NULL)
        {
            t->hover = 0;
            t->tracking = 0;
            t->closeHover = 0;
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;

    case WM_LBUTTONDOWN:
        if (t != NULL)
        {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            int isClose = (x >= S(352) - S(4) && x <= S(352) + S(22) &&
                           y >= S(4) && y <= S(26));
            if (!isClose)
            {
                ShowWindow(g_hMain, SW_SHOW);
                ShowWindow(g_hMain, SW_RESTORE);
                SetForegroundWindow(g_hMain);
            }
            t->state = 2;
            t->tick = 0;
        }
        return 0;
    }
    return DefWindowProcW(h, message, wParam, lParam);
}

void QueueToast(const struct Task* task, int leadMinutes)
{
    int slot = 0;
    for (int i = 1; i <= MAX_TOASTS; i++)
    {
        if (g_toasts[i].hwnd == NULL)
        {
            slot = i;
            break;
        }
    }
    if (slot == 0)
    {
        return;
    }

    struct Toast* t = &g_toasts[slot];
    ZeroMemory(t, sizeof(struct Toast));

    wchar_t lead[64];
    FmtLead(leadMinutes, lead, 64);

    /* 标题过长时截断，避免通知溢出 */
    wchar_t shortTitle[MAX_TITLE];
    StringCchCopyW(shortTitle, MAX_TITLE, task->title);
    if (lstrlenW(shortTitle) > 24)
    {
        shortTitle[24] = 0;
        StringCchCatW(shortTitle, MAX_TITLE, L"…");
    }
    StringCchPrintfW(t->head, 200, Str(STR_TOAST_HEAD), shortTitle, lead);

    wchar_t date[32], tm1[16], tm2[16];
    FmtDate(&task->stStart, date, 32);
    FmtTime(&task->stStart, tm1, 16);
    FmtTime(&task->stEnd, tm2, 16);
    StringCchPrintfW(t->body1, 200, Str(STR_TOAST_TIME),
        date, g_weekFull[g_lang][task->stStart.wDayOfWeek], tm1, tm2);

    if (task->note[0] != 0)
    {
        wchar_t shortNote[120];
        StringCchCopyW(shortNote, 120, task->note);
        if (lstrlenW(shortNote) > 40)
        {
            shortNote[40] = 0;
            StringCchCatW(shortNote, 120, L"…");
        }
        StringCchPrintfW(t->body2, 200, Str(STR_TOAST_NOTE), shortNote);
    }
    else
    {
        StringCchCopyW(t->body2, 200, L"");
    }

    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int W = S(380);
    int H = S(110);
    t->targetX = wa.right - S(12) - W;
    t->startX = wa.right + S(12);
    t->x = t->startX;
    t->state = 0;
    t->tick = 0;
    t->showTick = 0;
    t->alpha = 255;

    HWND h = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"TimeManagerToast", L"",
        WS_POPUP,
        t->x, wa.bottom - S(10) - H, W, H,
        NULL, NULL, g_hInst, NULL);

    t->hwnd = h;
    if (h != NULL)
    {
        HRGN rgn = CreateRoundRectRgn(0, 0, W + 1, H + 1, S(14), S(14));
        SetWindowRgn(h, rgn, TRUE);
        SetLayeredWindowAttributes(h, 0, 255, LWA_ALPHA);
        ShowWindow(h, SW_SHOWNOACTIVATE);
        SetTimer(h, 1, 15, NULL);
        LayoutToasts();
    }

    PlayReminderSound();
}

/*============================= 托盘 =============================*/

static void TrayAdd(void)
{
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hMain;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_APP_TRAY;
    g_nid.hIcon = g_hIconSmall != NULL ? g_hIconSmall : LoadIconW(NULL, IDI_APPLICATION);
    StringCchCopyW(g_nid.szTip, ARRAYSIZE(g_nid.szTip), Str(STR_TRAY_TIP));
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

static void TrayUpdateTip(void)
{
    NOTIFYICONDATAW n = g_nid;
    n.uFlags = NIF_TIP;
    StringCchCopyW(n.szTip, ARRAYSIZE(n.szTip), Str(STR_TRAY_TIP));
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

static void TrayBalloon(const wchar_t* msg)
{
    NOTIFYICONDATAW n = g_nid;
    n.uFlags = NIF_INFO;
    n.dwInfoFlags = NIIF_INFO;
    StringCchCopyW(n.szInfoTitle, ARRAYSIZE(n.szInfoTitle), Str(STR_TOAST_APP));
    StringCchCopyW(n.szInfo, ARRAYSIZE(n.szInfo), msg);
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

static void TrayMenu(POINT pt)
{
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_TRAY_SHOW, Str(STR_TRAY_OPEN));
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, ID_TRAY_EXIT, Str(STR_TRAY_EXIT));
    SetForegroundWindow(g_hMain);
    TrackPopupMenu(m, TPM_BOTTOMALIGN | TPM_RIGHTALIGN,
        pt.x, pt.y, 0, g_hMain, NULL);
    DestroyMenu(m);
}

static void ShowMainWindow(void)
{
    ShowWindow(g_hMain, SW_SHOW);
    ShowWindow(g_hMain, SW_RESTORE);
    SetForegroundWindow(g_hMain);
}

/*============================= 主列表（月视图活动流） =============================*/

/* 自绘活动流：按日期分组，每个日程只占一行。
   行内顺序：分类色点 · 时间段 · 标题 · 备注；分组标题右侧显示当天日程数。
   去掉了表头、网格线和“提醒次数”列，完整信息通过悬停气泡查看；
   点击选中，Ctrl 加减选，Shift 连选，“删除选中”按钮和 Delete 键共用一套选择集。 */

#define AG_ROW_H      S(40)
#define AG_HEAD_H     S(28)
#define AG_GROUP_GAP  S(16)
#define AG_PAD_X      S(18)
#define AG_MAX_ITEMS  (MAX_TASKS * 2 + 8)

struct AgItem
{
    int kind;           /* 0=日期标题，1=日程行 */
    int taskIndex;      /* kind==1 时对应 g_tasks 下标 */
    int group;          /* 所属分组（1..g_agGroupCount） */
    RECT rc;
};

struct AgGroup
{
    SYSTEMTIME date;
    int count;
};

static struct AgItem g_ag[AG_MAX_ITEMS];
static int g_agCount = 0;
static int g_agRows = 0;                 /* 可见日程行数（状态栏用） */
static struct AgGroup g_agGroups[MAX_TASKS + 2];
static int g_agGroupCount = 0;
static int g_agContentH = 0;
static int g_agScroll = 0;
static int g_agHover = -1;               /* 悬停条目下标（-1 表示无） */
static int g_agTracking = 0;
static int g_agAnchor = -1;              /* Shift 连选锚点（条目下标） */
static int g_agSelIds[MAX_TASKS + 1];
static int g_agSelCount = 0;

static int AgIsSelected(int taskId)
{
    for (int i = 0; i < g_agSelCount; i++)
    {
        if (g_agSelIds[i] == taskId)
        {
            return 1;
        }
    }
    return 0;
}

static void AgAddSelected(int taskId)
{
    if (taskId <= 0 || g_agSelCount >= MAX_TASKS || AgIsSelected(taskId))
    {
        return;
    }
    g_agSelIds[g_agSelCount++] = taskId;
}

static void AgRemoveSelected(int taskId)
{
    for (int i = 0; i < g_agSelCount; i++)
    {
        if (g_agSelIds[i] == taskId)
        {
            for (int k = i; k + 1 < g_agSelCount; k++)
            {
                g_agSelIds[k] = g_agSelIds[k + 1];
            }
            g_agSelCount--;
            return;
        }
    }
}

static void AgClearSelection(void)
{
    g_agSelCount = 0;
    g_agAnchor = -1;
}

static int AgSelectionCount(void)
{
    return g_agSelCount;
}

static void AgSelectAll(void)
{
    AgClearSelection();
    for (int i = 0; i < g_agCount; i++)
    {
        if (g_ag[i].kind == 1)
        {
            AgAddSelected(g_tasks[g_ag[i].taskIndex].id);
        }
    }
}

static int TaskOnFilter(const struct Task* t)
{
    if (g_filterMode == 0)
    {
        return 1;
    }
    return t->stStart.wYear == g_filterDate.wYear &&
           t->stStart.wMonth == g_filterDate.wMonth &&
           t->stStart.wDay == g_filterDate.wDay;
}

/* 重建条目：任务已按开始时间排序，同一日期自然聚成一组 */
static void AgendaRebuild(void)
{
    g_agCount = 0;
    g_agRows = 0;
    g_agGroupCount = 0;
    g_agHover = -1;
    AgClearSelection();

    int group = 0;
    for (int i = 1; i <= g_count; i++)
    {
        struct Task* t = &g_tasks[i];
        if (!TaskOnFilter(t))
        {
            continue;
        }
        if (group == 0 || !SameDate(&g_agGroups[group].date, &t->stStart))
        {
            if (g_agGroupCount >= MAX_TASKS + 1 || g_agCount + 2 > AG_MAX_ITEMS)
            {
                break;
            }
            group = ++g_agGroupCount;
            g_agGroups[group].date = t->stStart;
            g_agGroups[group].count = 0;

            struct AgItem* head = &g_ag[g_agCount++];
            head->kind = 0;
            head->taskIndex = 0;
            head->group = group;
            SetRect(&head->rc, 0, 0, 0, 0);
        }
        if (g_agCount >= AG_MAX_ITEMS)
        {
            break;
        }
        struct AgItem* it = &g_ag[g_agCount++];
        it->kind = 1;
        it->taskIndex = i;
        it->group = group;
        SetRect(&it->rc, 0, 0, 0, 0);
        g_agGroups[group].count++;
        g_agRows++;
    }
}

static void AgendaLayout(HWND h)
{
    RECT rc;
    GetClientRect(h, &rc);
    int W = rc.right;
    if (W < S(120))
    {
        W = S(120);
    }

    int y = S(12);
    for (int i = 0; i < g_agCount; i++)
    {
        struct AgItem* it = &g_ag[i];
        if (it->kind == 0)
        {
            if (it->group > 1)
            {
                y += AG_GROUP_GAP;
            }
            it->rc.left = AG_PAD_X;
            it->rc.right = W - AG_PAD_X;
            it->rc.top = y;
            it->rc.bottom = y + AG_HEAD_H;
            y += AG_HEAD_H;
        }
        else
        {
            it->rc.left = AG_PAD_X;
            it->rc.right = W - AG_PAD_X;
            it->rc.top = y;
            it->rc.bottom = y + AG_ROW_H;
            y += AG_ROW_H;
        }
    }
    g_agContentH = y + S(14);
}

static void AgClampScroll(HWND h)
{
    RECT rc;
    GetClientRect(h, &rc);
    int maxS = g_agContentH - rc.bottom;
    if (maxS < 0)
    {
        maxS = 0;
    }
    if (g_agScroll > maxS)
    {
        g_agScroll = maxS;
    }
    if (g_agScroll < 0)
    {
        g_agScroll = 0;
    }
}

static void AgUpdateScrollBar(HWND h)
{
    RECT rc;
    GetClientRect(h, &rc);
    if (rc.bottom <= 0)
    {
        return;
    }
    int need = (g_agContentH > rc.bottom) ? 1 : 0;
    ShowScrollBar(h, SB_VERT, need);       /* 内容不足一屏时隐藏滚动条，画面更干净 */
    if (!need)
    {
        g_agScroll = 0;
    }

    SCROLLINFO si;
    ZeroMemory(&si, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = (g_agContentH > 0) ? (g_agContentH - 1) : 0;
    si.nPage = rc.bottom;
    si.nPos = g_agScroll;
    SetScrollInfo(h, SB_VERT, &si, TRUE);
}

static void AgendaRelayout(HWND h)
{
    if (h == NULL)
    {
        return;
    }
    AgendaLayout(h);
    AgClampScroll(h);
    AgUpdateScrollBar(h);      /* 可能连带改变客户区宽度，Layout 会再次触发 */
    InvalidateRect(h, NULL, FALSE);
}

/* 命中测试：返回条目下标（含日期标题），无命中返回 -1 */
static int AgendaHitTest(int x, int y, RECT* outRc)
{
    POINT pt;
    pt.x = x;
    pt.y = y;
    for (int i = 0; i < g_agCount; i++)
    {
        RECT r = g_ag[i].rc;
        OffsetRect(&r, 0, -g_agScroll);
        if (PtInRect(&r, pt))
        {
            if (outRc != NULL)
            {
                *outRc = r;
            }
            return i;
        }
    }
    return -1;
}

static void RefreshCalendar(void)
{
    if (g_hCal != NULL)
    {
        InvalidateRect(g_hCal, NULL, FALSE);
    }
}

void RefreshList(void)
{
    int todayCount = 0;
    SYSTEMTIME now;
    GetLocalTime(&now);

    for (int i = 1; i <= g_count; i++)
    {
        struct Task* t = &g_tasks[i];
        if (t->stStart.wYear == now.wYear && t->stStart.wMonth == now.wMonth &&
            t->stStart.wDay == now.wDay)
        {
            todayCount++;
        }
    }

    AgendaRebuild();
    if (g_hList != NULL)
    {
        AgendaLayout(g_hList);
        g_agScroll = 0;
        AgUpdateScrollBar(g_hList);
        InvalidateRect(g_hList, NULL, FALSE);
    }

    if (g_hStatus != NULL)
    {
        wchar_t status[MAX_PATH + 200];
        if (g_filterMode == 0)
        {
            FormatStr(status, MAX_PATH + 200, STR_STATUS_ALL, g_count, g_dataPath);
        }
        else
        {
            wchar_t date[32];
            FmtDate(&g_filterDate, date, 32);
            FormatStr(status, MAX_PATH + 200, STR_STATUS_FILTER,
                date, g_weekFull[g_lang][g_filterDate.wDayOfWeek], g_agRows);
        }
        SetWindowTextW(g_hStatus, status);
    }

    if (g_hStatToday != NULL)
    {
        wchar_t buf[128];
        FormatStr(buf, 128, STR_STAT_TODAY, now.wMonth, now.wDay, todayCount);
        SetWindowTextW(g_hStatToday, buf);
    }
    UpdateDeleteButton();
}

/* 根据列表当前选中数量更新“删除选中”按钮文案（多选时显示数量） */
static void UpdateDeleteButton(void)
{
    if (g_hMain == NULL)
    {
        return;
    }
    HWND btn = GetDlgItem(g_hMain, IDC_BTN_DELETE);
    if (btn == NULL)
    {
        return;
    }

    int n = 0;
    if (g_viewMode == VIEW_MONTH && g_hList != NULL)
    {
        n = AgSelectionCount();
    }

    wchar_t buf[64];
    if (n > 1)
    {
        FormatStr(buf, 64, STR_BTN_DELETE_N, n);
    }
    else
    {
        StringCchCopyW(buf, 64, Str(STR_BTN_DELETE));
    }
    SetWindowTextW(btn, buf);
    InvalidateRect(btn, NULL, TRUE);
}

void DeleteSelected(void)
{
    int ids[MAX_TASKS];
    int idCount = 0;

    if (g_viewMode == VIEW_WEEK)
    {
        /* 周视图：删除单击选中的方块 */
        if (g_weekSelId == 0)
        {
            MessageBoxW(g_hMain, Str(STR_NO_SELECTION),
                Str(STR_NOTICE), MB_OK | MB_ICONINFORMATION);
            return;
        }
        ids[0] = g_weekSelId;
        idCount = 1;
    }
    else
    {
        /* 月视图：取活动流的选择集（支持 Ctrl / Shift 多选） */
        if (g_agSelCount == 0)
        {
            MessageBoxW(g_hMain, Str(STR_NO_SELECTION),
                Str(STR_NOTICE), MB_OK | MB_ICONINFORMATION);
            return;
        }
        for (int k = 0; k < g_agSelCount && idCount < MAX_TASKS; k++)
        {
            ids[idCount++] = g_agSelIds[k];
        }
    }

    wchar_t msg[300];
    if (idCount == 1)
    {
        const wchar_t* title = L"";
        for (int i = 1; i <= g_count; i++)
        {
            if (g_tasks[i].id == ids[0])
            {
                title = g_tasks[i].title;
                break;
            }
        }
        FormatStr(msg, 300, STR_CONFIRM_DELETE, title);
    }
    else
    {
        FormatStr(msg, 300, STR_CONFIRM_DELETE_MULTI, idCount);
    }

    if (MessageBoxW(g_hMain, msg, Str(STR_CONFIRM_DEL_TITLE),
        MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
    {
        return;
    }

    int w = 1;
    for (int i = 1; i <= g_count; i++)
    {
        int removed = 0;
        for (int k = 0; k < idCount; k++)
        {
            if (g_tasks[i].id == ids[k])
            {
                removed = 1;
                break;
            }
        }
        if (!removed)
        {
            g_tasks[w++] = g_tasks[i];
        }
    }
    g_count = w - 1;

    for (int k = 0; k < idCount; k++)
    {
        if (g_weekSelId == ids[k])
        {
            g_weekSelId = 0;
            break;
        }
    }

    SaveTasks();
    RefreshList();
    RefreshCalendar();
    RefreshWeek();
    UpdateDeleteButton();
}

/*============================= “添加日程”窗口 =============================*/

static BOOL CALLBACK SetFontToChild(HWND hChild, LPARAM lParam);

static HWND CreateLabel(HWND parent, const wchar_t* text, int x, int y, int w, int h)
{
    return CreateWindowExW(0, L"STATIC", text,
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
        x, y, w, h, parent, NULL, g_hInst, NULL);
}

static void RefreshRuleList(HWND hList)
{
    SendMessageW(hList, LB_RESETCONTENT, 0, 0);
    for (int i = 1; i <= g_dlgRCount; i++)
    {
        wchar_t text[96];
        wchar_t lead[64];
        FmtLead(g_dlgRMinutes[i], lead, 64);
        FormatStr(text, 96, STR_RULE_ITEM, lead);
        int idx = (int)SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)text);
        SendMessageW(hList, LB_SETITEMDATA, (WPARAM)idx, (LPARAM)g_dlgRMinutes[i]);
    }
}

static HWND CreateHint(HWND parent, const wchar_t* text, int x, int y, int w, int h, int id)
{
    return CreateWindowExW(0, L"STATIC", text,
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        x, y, w, h, parent, (HMENU)(LONG_PTR)id, g_hInst, NULL);
}

static void ShowNotice(HWND hWnd, int strId)
{
    MessageBoxW(hWnd, Str(strId), Str(STR_NOTICE),
        MB_OK | MB_ICONWARNING | MB_TOPMOST);
}

static BOOL AddDlg_OnCreate(HWND hWnd)
{
    CreateLabel(hWnd, Str(STR_LBL_START), S(16), S(20), S(80), S(28));
    g_dlgSDate = CreateWindowExW(0, DATETIMEPICK_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | DTS_SHORTDATEFORMAT,
        S(104), S(16), S(170), S(30), hWnd, (HMENU)(LONG_PTR)IDC_SDATE, g_hInst, NULL);
    DateTime_SetFormat(g_dlgSDate, L"yyyy/MM/dd");

    g_dlgSTime = CreateWindowExW(0, DATETIMEPICK_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | DTS_TIMEFORMAT,
        S(286), S(16), S(110), S(30), hWnd, (HMENU)(LONG_PTR)IDC_STIME, g_hInst, NULL);
    DateTime_SetFormat(g_dlgSTime, L"HH:mm");

    CreateLabel(hWnd, Str(STR_LBL_END), S(16), S(60), S(80), S(28));
    g_dlgEDate = CreateWindowExW(0, DATETIMEPICK_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | DTS_SHORTDATEFORMAT,
        S(104), S(56), S(170), S(30), hWnd, (HMENU)(LONG_PTR)IDC_EDATE, g_hInst, NULL);
    DateTime_SetFormat(g_dlgEDate, L"yyyy/MM/dd");

    g_dlgETime = CreateWindowExW(0, DATETIMEPICK_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | DTS_TIMEFORMAT,
        S(286), S(56), S(110), S(30), hWnd, (HMENU)(LONG_PTR)IDC_ETIME, g_hInst, NULL);
    DateTime_SetFormat(g_dlgETime, L"HH:mm");

    CreateLabel(hWnd, Str(STR_LBL_TITLE), S(16), S(100), S(80), S(28));
    g_dlgTitle = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        S(104), S(96), S(184), S(30), hWnd, (HMENU)(LONG_PTR)IDC_TITLE, g_hInst, NULL);
    SendMessageW(g_dlgTitle, EM_SETLIMITTEXT, (WPARAM)(MAX_TITLE - 1), 0);

    CreateLabel(hWnd, Str(STR_LBL_CAT), S(298), S(100), S(56), S(28));
    g_dlgCat = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        S(356), S(96), S(148), S(260),
        hWnd, (HMENU)(LONG_PTR)IDC_CAT, g_hInst, NULL);
    for (int c = 1; c <= CAT_COUNT; c++)
    {
        SendMessageW(g_dlgCat, CB_ADDSTRING, 0, (LPARAM)CatStr(c));
        SendMessageW(g_dlgCat, CB_SETITEMDATA, (WPARAM)(c - 1), (LPARAM)c);
    }
    SendMessageW(g_dlgCat, CB_SETCURSEL, g_dlgCategory - 1, 0);

    CreateLabel(hWnd, Str(STR_LBL_REPEAT), S(16), S(140), S(80), S(28));
    HWND hRepeat = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        S(104), S(136), S(260), S(260),
        hWnd, (HMENU)(LONG_PTR)IDC_REPEAT, g_hInst, NULL);
    SendMessageW(hRepeat, CB_ADDSTRING, 0, (LPARAM)Str(STR_REPEAT_ONCE));
    for (int w = 2; w <= REPEAT_MAX_WEEKS; w++)
    {
        wchar_t buf[96];
        FormatStr(buf, 96, STR_REPEAT_WEEKS, w);
        int idx = (int)SendMessageW(hRepeat, CB_ADDSTRING, 0, (LPARAM)buf);
        SendMessageW(hRepeat, CB_SETITEMDATA, (WPARAM)idx, (LPARAM)w);
    }
    SendMessageW(hRepeat, CB_SETCURSEL, 0, 0);

    CreateLabel(hWnd, Str(STR_LBL_NOTE), S(16), S(186), S(80), S(24));
    g_dlgNote = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE |
        ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL,
        S(104), S(180), S(400), S(72), hWnd, (HMENU)(LONG_PTR)IDC_NOTE, g_hInst, NULL);
    SendMessageW(g_dlgNote, EM_SETLIMITTEXT, (WPARAM)(MAX_NOTE - 1), 0);

    CreateLabel(hWnd, Str(STR_LBL_RULES), S(16), S(266), S(400), S(24));
    g_dlgRList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LBS_NOTIFY | WS_VSCROLL |
        LBS_HASSTRINGS,
        S(16), S(290), S(488), S(100),
        hWnd, (HMENU)(LONG_PTR)IDC_RLIST, g_hInst, NULL);

    g_dlgRValue = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
        S(104), S(400), S(64), S(30), hWnd, (HMENU)(LONG_PTR)IDC_RVALUE, g_hInst, NULL);
    SetWindowTextW(g_dlgRValue, L"15");

    g_dlgRUnit = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        S(176), S(400), S(110), S(240),
        hWnd, (HMENU)(LONG_PTR)IDC_RUNIT, g_hInst, NULL);
    SendMessageW(g_dlgRUnit, CB_ADDSTRING, 0, (LPARAM)Str(STR_UNIT_MIN));
    SendMessageW(g_dlgRUnit, CB_ADDSTRING, 0, (LPARAM)Str(STR_UNIT_HOUR));
    SendMessageW(g_dlgRUnit, CB_ADDSTRING, 0, (LPARAM)Str(STR_UNIT_DAY));
    SendMessageW(g_dlgRUnit, CB_SETCURSEL, 0, 0);

    CreateWindowExW(0, L"BUTTON", Str(STR_RULE_ADD),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        S(296), S(398), S(100), S(32), hWnd, (HMENU)(LONG_PTR)IDC_RADD, g_hInst, NULL);
    CreateWindowExW(0, L"BUTTON", Str(STR_RULE_DEL),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        S(404), S(398), S(100), S(32), hWnd, (HMENU)(LONG_PTR)IDC_RDEL, g_hInst, NULL);

    CreateHint(hWnd, Str(STR_ADD_HINT), S(104), S(442), S(400), S(42), IDC_ADD_HINT);

    CreateWindowExW(0, L"BUTTON", Str(STR_OK),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        S(300), S(498), S(100), S(34), hWnd, (HMENU)(LONG_PTR)IDC_OK, g_hInst, NULL);
    CreateWindowExW(0, L"BUTTON", Str(STR_CANCEL),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        S(408), S(498), S(100), S(34), hWnd, (HMENU)(LONG_PTR)IDC_CANCEL, g_hInst, NULL);

    /* 默认开始 = 当前时间，结束 = 1 小时后 */
    SYSTEMTIME now, endTime;
    GetLocalTime(&now);
    DateTime_SetSystemtime(g_dlgSDate, GDT_VALID, &now);
    DateTime_SetSystemtime(g_dlgSTime, GDT_VALID, &now);
    endTime = now;
    FILETIME ft1, ft2;
    ULARGE_INTEGER u;
    if (SystemTimeToFileTime(&endTime, &ft1))
    {
        u.LowPart = ft1.dwLowDateTime;
        u.HighPart = ft1.dwHighDateTime;
        u.QuadPart += 36000000000ULL;
        ft2.dwLowDateTime = u.LowPart;
        ft2.dwHighDateTime = u.HighPart;
        FileTimeToSystemTime(&ft2, &endTime);
    }
    DateTime_SetSystemtime(g_dlgEDate, GDT_VALID, &endTime);
    DateTime_SetSystemtime(g_dlgETime, GDT_VALID, &endTime);

    /* 默认四条提醒规则 */
    g_dlgRCount = 4;
    for (int i = 1; i <= 4; i++)
    {
        g_dlgRMinutes[i] = DEFAULT_RULES[i - 1];
    }
    RefreshRuleList(g_dlgRList);

    EnumChildWindows(hWnd, SetFontToChild, (LPARAM)g_fontUi);
    SetFocus(g_dlgTitle);
    return FALSE;
}

static void OnRuleAdd(HWND hWnd)
{
    if (g_dlgRCount >= MAX_REMIND)
    {
        ShowNotice(hWnd, STR_ERR_RULE_MAX);
        return;
    }
    wchar_t valText[16];
    GetWindowTextW(g_dlgRValue, valText, 16);
    int val = _wtoi(valText);
    if (val <= 0)
    {
        ShowNotice(hWnd, STR_ERR_RULE_POS);
        SetFocus(g_dlgRValue);
        return;
    }
    int unit = (int)SendMessageW(g_dlgRUnit, CB_GETCURSEL, 0, 0);
    int factor = 1;
    if (unit == 1) { factor = 60; }
    if (unit == 2) { factor = 1440; }
    int minutes = val * factor;
    if (minutes > 366 * 1440)
    {
        ShowNotice(hWnd, STR_ERR_RULE_YEAR);
        return;
    }
    for (int i = 1; i <= g_dlgRCount; i++)
    {
        if (g_dlgRMinutes[i] == minutes)
        {
            ShowNotice(hWnd, STR_ERR_RULE_DUP);
            return;
        }
    }
    g_dlgRCount++;
    g_dlgRMinutes[g_dlgRCount] = minutes;
    /* 降序排序 */
    for (int i = g_dlgRCount; i >= 2; i--)
    {
        if (g_dlgRMinutes[i] > g_dlgRMinutes[i - 1])
        {
            int tmp = g_dlgRMinutes[i];
            g_dlgRMinutes[i] = g_dlgRMinutes[i - 1];
            g_dlgRMinutes[i - 1] = tmp;
        }
    }
    RefreshRuleList(g_dlgRList);
}

static void OnRuleDelete(HWND hWnd)
{
    int sel = (int)SendMessageW(g_dlgRList, LB_GETCURSEL, 0, 0);
    if (sel < 0)
    {
        ShowNotice(hWnd, STR_ERR_RULE_SEL);
        return;
    }
    /* ListBox 行从 0 开始，对应数组下标 行号+1 */
    for (int i = sel + 1; i < g_dlgRCount; i++)
    {
        g_dlgRMinutes[i] = g_dlgRMinutes[i + 1];
    }
    g_dlgRCount--;
    RefreshRuleList(g_dlgRList);
}

static void OnAddOk(HWND hWnd)
{
    int repeatSel = (int)SendMessageW(GetDlgItem(hWnd, IDC_REPEAT), CB_GETCURSEL, 0, 0);
    int weeks = 1;
    if (repeatSel > 0)
    {
        weeks = (int)SendMessageW(GetDlgItem(hWnd, IDC_REPEAT),
            CB_GETITEMDATA, (WPARAM)repeatSel, 0);
        if (weeks < 2) { weeks = 2; }
        if (weeks > REPEAT_MAX_WEEKS) { weeks = REPEAT_MAX_WEEKS; }
    }

    if (g_count + weeks > MAX_TASKS)
    {
        ShowNotice(hWnd, weeks == 1 ? STR_ERR_TASK_MAX : STR_ERR_TASK_REPEAT);
        return;
    }

    SYSTEMTIME sd, stv, ed, etv;
    if (DateTime_GetSystemtime(g_dlgSDate, &sd) != GDT_VALID ||
        DateTime_GetSystemtime(g_dlgSTime, &stv) != GDT_VALID ||
        DateTime_GetSystemtime(g_dlgEDate, &ed) != GDT_VALID ||
        DateTime_GetSystemtime(g_dlgETime, &etv) != GDT_VALID)
    {
        ShowNotice(hWnd, STR_ERR_TIME_INVALID);
        return;
    }

    struct Task t;
    ZeroMemory(&t, sizeof(t));
    t.stStart = sd;
    t.stStart.wHour = stv.wHour;
    t.stStart.wMinute = stv.wMinute;
    t.stStart.wSecond = 0;
    t.stStart.wMilliseconds = 0;
    t.stEnd = ed;
    t.stEnd.wHour = etv.wHour;
    t.stEnd.wMinute = etv.wMinute;
    t.stEnd.wSecond = 0;
    t.stEnd.wMilliseconds = 0;

    if (SysTimeToU64(&t.stEnd) < SysTimeToU64(&t.stStart))
    {
        ShowNotice(hWnd, STR_ERR_TIME_ORDER);
        return;
    }

    wchar_t title[MAX_TITLE];
    wchar_t note[MAX_NOTE];
    ZeroMemory(title, sizeof(title));
    ZeroMemory(note, sizeof(note));
    HWND hTitle = GetDlgItem(hWnd, IDC_TITLE);
    HWND hNote = GetDlgItem(hWnd, IDC_NOTE);
    GetWindowTextW(hTitle, title, MAX_TITLE);
    GetWindowTextW(hNote, note, MAX_NOTE);
    if (title[0] == 0)
    {
        ShowNotice(hWnd, STR_ERR_TITLE_EMPTY);
        SetFocus(hTitle);
        return;
    }
    StringCchCopyW(t.title, MAX_TITLE, title);
    StringCchCopyW(t.note, MAX_NOTE, note);
    CleanText(t.title);
    CleanText(t.note);

    int catSel = (int)SendMessageW(g_dlgCat, CB_GETCURSEL, 0, 0);
    t.category = CAT_DEFAULT;
    if (catSel >= 0 && catSel < CAT_COUNT)
    {
        t.category = (int)SendMessageW(g_dlgCat, CB_GETITEMDATA, (WPARAM)catSel, 0);
        if (t.category < 1 || t.category > CAT_COUNT) { t.category = CAT_DEFAULT; }
    }

    t.rcount = g_dlgRCount;
    for (int i = 1; i <= g_dlgRCount; i++)
    {
        t.r[i].minutes = g_dlgRMinutes[i];
        t.r[i].fired = 0;
    }
    SortRules(&t);

    SYSTEMTIME now;
    GetLocalTime(&now);
    for (int k = 0; k < weeks; k++)
    {
        struct Task nt = t;
        nt.id = g_nextId++;
        if (k > 0)
        {
            AddDays(&nt.stStart, k * 7);
            AddDays(&nt.stEnd, k * 7);
        }
        for (int i = 1; i <= nt.rcount; i++)
        {
            nt.r[i].fired = 0;
        }
        NormalizeRules(&nt, DiffSeconds(&now, &nt.stStart), 1);
        g_count++;
        g_tasks[g_count] = nt;
    }
    SortTasks();
    SaveTasks();
    RefreshList();
    RefreshCalendar();
    RefreshWeek();

    g_addResult = IDOK;
}

LRESULT CALLBACK AddDlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        AddDlg_OnCreate(hWnd);
        return 0;

    case WM_ERASEBKGND:
    {
        RECT rc;
        GetClientRect(hWnd, &rc);
        FillRect((HDC)wParam, &rc, g_hDlgBrush);
        return 1;
    }

    case WM_CTLCOLORSTATIC:
    {
        int id = GetDlgCtrlID((HWND)lParam);
        SetBkMode((HDC)wParam, TRANSPARENT);
        if (id == IDC_ADD_HINT)
        {
            SetTextColor((HDC)wParam, g_theme->muted);
        }
        else
        {
            SetTextColor((HDC)wParam, g_theme->text);
        }
        return (LRESULT)g_hDlgBrush;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_RADD:
            OnRuleAdd(hWnd);
            return 0;
        case IDC_RDEL:
            OnRuleDelete(hWnd);
            return 0;
        case IDC_OK:
            OnAddOk(hWnd);
            return 0;
        case IDC_CANCEL:
        case IDCANCEL:
            g_addResult = IDCANCEL;
            return 0;
        }
        return 0;

    case WM_CLOSE:
        g_addResult = IDCANCEL;
        return 0;
    }
    return DefWindowProcW(hWnd, message, wParam, lParam);
}

void ShowAddDialog(void)
{
    g_addResult = 0;
    g_dlgCategory = CAT_DEFAULT;

    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    DWORD exStyle = WS_EX_DLGMODALFRAME;
    RECT wr;
    SetRect(&wr, 0, 0, S(520), S(546));
    AdjustWindowRectEx(&wr, style, FALSE, exStyle);

    g_hAdd = CreateWindowExW(
        exStyle,
        L"TimeManagerAddWnd", Str(STR_ADD_TITLE),
        style,
        CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top,
        g_hMain, NULL, g_hInst, NULL);

    if (g_hAdd == NULL)
    {
        return;
    }

    RECT rm, rd;
    GetWindowRect(g_hMain, &rm);
    GetWindowRect(g_hAdd, &rd);
    int w = rd.right - rd.left;
    int h = rd.bottom - rd.top;
    int x = rm.left + ((rm.right - rm.left) - w) / 2;
    int y = rm.top + ((rm.bottom - rm.top) - h) / 2;
    if (x < 0) { x = 0; }
    if (y < 0) { y = 0; }
    SetWindowPos(g_hAdd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

    EnableWindow(g_hMain, FALSE);
    ShowWindow(g_hAdd, SW_SHOW);
    UpdateWindow(g_hAdd);

    MSG msg;
    BOOL bRet;
    while (g_addResult == 0 && (bRet = GetMessageW(&msg, NULL, 0, 0)) != 0)
    {
        if (bRet == -1)
        {
            break;
        }
        if (bRet == 0)
        {
            PostQuitMessage((int)msg.wParam);
            break;
        }
        if (!IsDialogMessageW(g_hAdd, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (g_hAdd != NULL)
    {
        DestroyWindow(g_hAdd);
        g_hAdd = NULL;
    }
    EnableWindow(g_hMain, TRUE);
    SetForegroundWindow(g_hMain);
}

/*============================= “设置”窗口 =============================*/

/* 卡片式布局参数：创建控件与自绘卡片共用同一套坐标 */
#define SETDLG_CW        S(560)
#define SETDLG_CH        S(560)
#define SETDLG_M         S(20)
#define SETDLG_CARD_W    S(520)
#define SETDLG_CARD_H    S(420)
#define SETDLG_PX        S(24)
#define SETDLG_LABEL_X   (SETDLG_M + SETDLG_PX)
#define SETDLG_CTRL_X    (SETDLG_LABEL_X + S(112))
#define SETDLG_CTRL_W    S(260)
#define SETDLG_BTN_W     S(96)
#define SETDLG_BTN_H     S(34)

/* 白色圆角卡片 + 1px 边框，与主界面日历卡片同一视觉语言 */
static void SetDlgFillCard(HDC hdc, const RECT* rc)
{
    HBRUSH br = CreateSolidBrush(g_theme->card);
    HPEN pen = CreatePen(PS_SOLID, 1, g_theme->border);
    HGDIOBJ oldBrush = SelectObject(hdc, br);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    RoundRect(hdc, rc->left, rc->top, rc->right, rc->bottom, S(14), S(14));
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(br);
    DeleteObject(pen);
}

static void SetDlgHairline(HDC hdc, int x1, int x2, int y)
{
    RECT rc;
    SetRect(&rc, x1, y, x2, y + 1);
    HBRUSH br = CreateSolidBrush(g_theme->border);
    FillRect(hdc, &rc, br);
    DeleteObject(br);
}

/* 对话框背景：卡片 + 分区标题 + 两条分隔线 */
static void SetDlgPaint(HWND hWnd)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hWnd, &ps);

    RECT rc;
    GetClientRect(hWnd, &rc);
    FillRect(hdc, &rc, g_hDlgBrush);

    RECT card;
    SetRect(&card, SETDLG_M, SETDLG_M,
        SETDLG_M + SETDLG_CARD_W, SETDLG_M + SETDLG_CARD_H);
    SetDlgFillCard(hdc, &card);

    HGDIOBJ oldFont = SelectObject(hdc, g_fontBold);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, g_theme->accent);

    RECT t;
    SetRect(&t, SETDLG_LABEL_X, SETDLG_M + S(12),
        SETDLG_M + SETDLG_CARD_W - SETDLG_PX, SETDLG_M + S(38));
    DrawTextW(hdc, Str(STR_SET_SEC_APPEAR), -1, &t,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SetRect(&t, SETDLG_LABEL_X, SETDLG_M + S(204),
        SETDLG_M + SETDLG_CARD_W - SETDLG_PX, SETDLG_M + S(230));
    DrawTextW(hdc, Str(STR_SET_SEC_ALERT), -1, &t,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SelectObject(hdc, oldFont);

    /* 分组分隔线 + 页脚分隔线 */
    SetDlgHairline(hdc, SETDLG_LABEL_X, SETDLG_M + SETDLG_CARD_W - SETDLG_PX,
        SETDLG_M + S(192));
    SetDlgHairline(hdc, SETDLG_M, SETDLG_M + SETDLG_CARD_W,
        SETDLG_M + SETDLG_CARD_H + S(52));

    EndPaint(hWnd, &ps);
}

/* 主按钮：强调色填充 + 白字；其余按钮保持系统原生风格 */
static void SetDlgDrawOk(LPDRAWITEMSTRUCT dis)
{
    COLORREF fill = (dis->itemState & ODS_SELECTED)
        ? g_theme->accentDark : g_theme->accent;
    HBRUSH br = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, fill);
    HGDIOBJ oldBrush = SelectObject(dis->hDC, br);
    HGDIOBJ oldPen = SelectObject(dis->hDC, pen);
    RECT rc = dis->rcItem;
    RoundRect(dis->hDC, rc.left, rc.top, rc.right, rc.bottom, S(10), S(10));
    SelectObject(dis->hDC, oldBrush);
    SelectObject(dis->hDC, oldPen);
    DeleteObject(br);
    DeleteObject(pen);

    wchar_t text[64];
    GetWindowTextW(dis->hwndItem, text, 64);
    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, RGB(255, 255, 255));
    HGDIOBJ oldFont = SelectObject(dis->hDC, g_fontUi);
    DrawTextW(dis->hDC, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (dis->itemState & ODS_FOCUS)
    {
        RECT f = rc;
        InflateRect(&f, -S(4), -S(4));
        DrawFocusRect(dis->hDC, &f);
    }
    SelectObject(dis->hDC, oldFont);
}

static BOOL SetDlg_OnCreate(HWND hWnd)
{
    g_dlgSet = g_set;

    /* —— 外观与启动 —— */
    CreateLabel(hWnd, Str(STR_SET_LANG), SETDLG_LABEL_X, SETDLG_M + S(52),
        S(112), S(28));
    HWND hLang = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        SETDLG_CTRL_X, SETDLG_M + S(49), SETDLG_CTRL_W, S(240),
        hWnd, (HMENU)(LONG_PTR)IDC_SET_LANG, g_hInst, NULL);
    SendMessageW(hLang, CB_ADDSTRING, 0, (LPARAM)Str(STR_LANG_ZH));
    SendMessageW(hLang, CB_ADDSTRING, 0, (LPARAM)Str(STR_LANG_JA));
    SendMessageW(hLang, CB_ADDSTRING, 0, (LPARAM)Str(STR_LANG_EN));
    if (g_dlgSet.lang < 0 || g_dlgSet.lang >= LANG_COUNT)
    {
        g_dlgSet.lang = LANG_ZH;
    }
    SendMessageW(hLang, CB_SETCURSEL, g_dlgSet.lang, 0);

    CreateLabel(hWnd, Str(STR_SET_THEME), SETDLG_LABEL_X, SETDLG_M + S(92),
        S(112), S(28));
    HWND hThemeBox = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        SETDLG_CTRL_X, SETDLG_M + S(89), SETDLG_CTRL_W, S(240),
        hWnd, (HMENU)(LONG_PTR)IDC_SET_THEME, g_hInst, NULL);
    for (int i = 0; i < THEME_COUNT; i++)
    {
        SendMessageW(hThemeBox, CB_ADDSTRING, 0, (LPARAM)ThemeStr(i));
    }
    if (g_dlgSet.theme < 0 || g_dlgSet.theme >= THEME_COUNT)
    {
        g_dlgSet.theme = THEME_DEFAULT;
    }
    SendMessageW(hThemeBox, CB_SETCURSEL, g_dlgSet.theme, 0);

    CreateWindowExW(0, L"BUTTON", Str(STR_SET_AUTOSTART),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        SETDLG_LABEL_X, SETDLG_M + S(122), S(436), S(26),
        hWnd, (HMENU)(LONG_PTR)IDC_SET_AUTO, g_hInst, NULL);
    Button_SetCheck(GetDlgItem(hWnd, IDC_SET_AUTO),
        g_dlgSet.autostart ? BST_CHECKED : BST_UNCHECKED);

    CreateWindowExW(0, L"BUTTON", Str(STR_SET_MINSTART),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        SETDLG_LABEL_X, SETDLG_M + S(150), S(436), S(26),
        hWnd, (HMENU)(LONG_PTR)IDC_SET_MIN, g_hInst, NULL);
    Button_SetCheck(GetDlgItem(hWnd, IDC_SET_MIN),
        g_dlgSet.minStart ? BST_CHECKED : BST_UNCHECKED);

    /* —— 提醒方式 —— */
    const int durList[6] = { 8, 10, 12, 15, 20, 30 };

    CreateLabel(hWnd, Str(STR_SET_DURATION), SETDLG_LABEL_X, SETDLG_M + S(256),
        S(112), S(28));
    HWND hDur = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        SETDLG_CTRL_X, SETDLG_M + S(253), SETDLG_CTRL_W, S(240),
        hWnd, (HMENU)(LONG_PTR)IDC_SET_DUR, g_hInst, NULL);
    for (int i = 0; i < 6; i++)
    {
        wchar_t t[32];
        FormatStr(t, 32, STR_SET_SECONDS, durList[i]);
        SendMessageW(hDur, CB_ADDSTRING, 0, (LPARAM)t);
        if (durList[i] == g_dlgSet.toastSec)
        {
            SendMessageW(hDur, CB_SETCURSEL, i, 0);
        }
    }
    if (SendMessageW(hDur, CB_GETCURSEL, 0, 0) == CB_ERR)
    {
        SendMessageW(hDur, CB_SETCURSEL, 2, 0);
    }

    CreateLabel(hWnd, Str(STR_SET_SOUND), SETDLG_LABEL_X, SETDLG_M + S(296),
        S(112), S(28));
    HWND hSound = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        SETDLG_CTRL_X, SETDLG_M + S(293), SETDLG_CTRL_W, S(240),
        hWnd, (HMENU)(LONG_PTR)IDC_SET_SOUND, g_hInst, NULL);
    for (int i = 0; i <= SOUND_WAV; i++)
    {
        SendMessageW(hSound, CB_ADDSTRING, 0, (LPARAM)SoundStr(i));
    }
    if (g_dlgSet.sound < 0 || g_dlgSet.sound > SOUND_WAV)
    {
        g_dlgSet.sound = SOUND_ASTERISK;
    }
    SendMessageW(hSound, CB_SETCURSEL, g_dlgSet.sound, 0);

    CreateWindowExW(0, L"BUTTON", Str(STR_SET_TEST),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        SETDLG_CTRL_X + SETDLG_CTRL_W + S(10), SETDLG_M + S(292), S(76), S(34),
        hWnd, (HMENU)(LONG_PTR)IDC_SET_TEST, g_hInst, NULL);

    CreateLabel(hWnd, Str(STR_SET_WAV), SETDLG_LABEL_X, SETDLG_M + S(336),
        S(112), S(28));
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", g_dlgSet.wavPath,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        SETDLG_CTRL_X, SETDLG_M + S(333), SETDLG_CTRL_W, S(32),
        hWnd, (HMENU)(LONG_PTR)IDC_SET_WAV, g_hInst, NULL);
    CreateWindowExW(0, L"BUTTON", Str(STR_SET_BROWSE),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        SETDLG_CTRL_X + SETDLG_CTRL_W + S(10), SETDLG_M + S(332), S(76), S(34),
        hWnd, (HMENU)(LONG_PTR)IDC_SET_BROWSE, g_hInst, NULL);

    CreateHint(hWnd, Str(STR_SET_HINT), SETDLG_LABEL_X, SETDLG_M + S(374),
        S(448), S(44), IDC_SET_HINT);

    /* —— 底部按钮：确定（强调色）/ 取消 —— */
    CreateWindowExW(0, L"BUTTON", Str(STR_OK),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        SETDLG_M + SETDLG_CARD_W - SETDLG_BTN_W,
        SETDLG_M + SETDLG_CARD_H + S(16), SETDLG_BTN_W, SETDLG_BTN_H,
        hWnd, (HMENU)(LONG_PTR)IDC_SET_OK, g_hInst, NULL);
    CreateWindowExW(0, L"BUTTON", Str(STR_CANCEL),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        SETDLG_M + SETDLG_CARD_W - SETDLG_BTN_W * 2 - S(12),
        SETDLG_M + SETDLG_CARD_H + S(16), SETDLG_BTN_W, SETDLG_BTN_H,
        hWnd, (HMENU)(LONG_PTR)IDC_SET_CANCEL, g_hInst, NULL);

    /* —— 页脚：制作人员 / 联系邮箱 —— */
    CreateWindowExW(0, L"STATIC", Str(STR_CREDITS_AUTHOR),
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
        SETDLG_M, SETDLG_M + SETDLG_CARD_H + S(60), SETDLG_CARD_W, S(22),
        hWnd, (HMENU)(LONG_PTR)IDC_SET_CREDITS, g_hInst, NULL);

    wchar_t emailText[256];
    FormatStr(emailText, 256, STR_CREDITS_EMAIL,
        L"3507609128@qq.com", L"3507609128@qq.com");
    CreateWindowExW(0, WC_LINK, emailText,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        SETDLG_M, SETDLG_M + SETDLG_CARD_H + S(82), SETDLG_CARD_W, S(22),
        hWnd, (HMENU)(LONG_PTR)IDC_SET_EMAIL, g_hInst, NULL);

    EnumChildWindows(hWnd, SetFontToChild, (LPARAM)g_fontUi);
    return FALSE;
}

static void BuildWavFilter(wchar_t* buf, size_t cch)
{
    ZeroMemory(buf, cch * sizeof(wchar_t));
    size_t pos = 0;
    const wchar_t* parts[4];
    parts[0] = Str(STR_FILTER_WAV);
    parts[1] = L"*.wav";
    parts[2] = Str(STR_FILTER_ALL);
    parts[3] = L"*.*";
    for (int i = 0; i < 4; i++)
    {
        size_t len = wcslen(parts[i]);
        if (pos + len + 1 >= cch)
        {
            break;
        }
        wmemcpy(buf + pos, parts[i], len);
        pos += len + 1;
    }
}

static void OnSetBrowse(HWND hWnd)
{
    wchar_t buf[MAX_PATH];
    GetWindowTextW(GetDlgItem(hWnd, IDC_SET_WAV), buf, MAX_PATH);
    wchar_t filter[320];
    BuildWavFilter(filter, 320);
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrDefExt = L"wav";
    if (GetOpenFileNameW(&ofn))
    {
        SetWindowTextW(GetDlgItem(hWnd, IDC_SET_WAV), buf);
        SendMessageW(GetDlgItem(hWnd, IDC_SET_SOUND), CB_SETCURSEL, SOUND_WAV, 0);
    }
}

static void OnSetTest(HWND hWnd)
{
    int sound = (int)SendMessageW(GetDlgItem(hWnd, IDC_SET_SOUND), CB_GETCURSEL, 0, 0);
    wchar_t wav[MAX_PATH];
    GetWindowTextW(GetDlgItem(hWnd, IDC_SET_WAV), wav, MAX_PATH);

    switch (sound)
    {
    case SOUND_MUTE:
        MessageBoxW(hWnd, Str(STR_SET_TEST_MUTE), Str(STR_SET_TEST),
            MB_OK | MB_ICONINFORMATION);
        break;
    case SOUND_DEFAULT:
        PlaySoundW(L"SystemDefault", NULL, SND_ALIAS | SND_ASYNC);
        break;
    case SOUND_ASTERISK:
        PlaySoundW(L"SystemAsterisk", NULL, SND_ALIAS | SND_ASYNC);
        break;
    case SOUND_EXCLAM:
        PlaySoundW(L"SystemExclamation", NULL, SND_ALIAS | SND_ASYNC);
        break;
    case SOUND_HAND:
        PlaySoundW(L"SystemHand", NULL, SND_ALIAS | SND_ASYNC);
        break;
    case SOUND_BEEP:
        MessageBeep(0xFFFFFFFF);
        break;
    case SOUND_WAV:
        if (wav[0] == 0)
        {
            MessageBoxW(hWnd, Str(STR_SET_TEST_NO_WAV), Str(STR_SET_TEST),
                MB_OK | MB_ICONWARNING);
        }
        else
        {
            PlaySoundW(wav, NULL, SND_FILENAME | SND_ASYNC);
        }
        break;
    }
}

static void OnSetOk(HWND hWnd)
{
    int sound = (int)SendMessageW(GetDlgItem(hWnd, IDC_SET_SOUND), CB_GETCURSEL, 0, 0);
    int durIdx = (int)SendMessageW(GetDlgItem(hWnd, IDC_SET_DUR), CB_GETCURSEL, 0, 0);
    int langIdx = (int)SendMessageW(GetDlgItem(hWnd, IDC_SET_LANG), CB_GETCURSEL, 0, 0);
    int themeIdx = (int)SendMessageW(GetDlgItem(hWnd, IDC_SET_THEME), CB_GETCURSEL, 0, 0);
    const int durList[6] = { 8, 10, 12, 15, 20, 30 };
    int autoRun = Button_GetCheck(GetDlgItem(hWnd, IDC_SET_AUTO)) == BST_CHECKED;
    int minStart = Button_GetCheck(GetDlgItem(hWnd, IDC_SET_MIN)) == BST_CHECKED;

    wchar_t wav[MAX_PATH];
    GetWindowTextW(GetDlgItem(hWnd, IDC_SET_WAV), wav, MAX_PATH);

    if (durIdx < 0) { durIdx = 2; }
    if (sound < 0) { sound = SOUND_ASTERISK; }
    if (langIdx < 0 || langIdx >= LANG_COUNT) { langIdx = g_dlgSet.lang; }
    if (themeIdx < 0 || themeIdx >= THEME_COUNT) { themeIdx = g_dlgSet.theme; }

    g_set.autostart = autoRun;
    g_set.minStart = minStart;
    g_set.sound = sound;
    g_set.toastSec = durList[durIdx];
    g_set.lang = langIdx;
    g_set.theme = themeIdx;
    StringCchCopyW(g_set.wavPath, MAX_PATH, wav);
    CleanText(g_set.wavPath);

    SetAutoRun(autoRun);
    SaveSettings();

    if (g_hChkAuto != NULL)
    {
        Button_SetCheck(g_hChkAuto, autoRun ? BST_CHECKED : BST_UNCHECKED);
    }
    g_setResult = IDOK;
}

LRESULT CALLBACK SetDlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        SetDlg_OnCreate(hWnd);
        return 0;

    case WM_PAINT:
        SetDlgPaint(hWnd);
        return 0;

    case WM_ERASEBKGND:
    {
        RECT rc;
        GetClientRect(hWnd, &rc);
        FillRect((HDC)wParam, &rc, g_hDlgBrush);
        return 1;
    }

    case WM_DRAWITEM:
    {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlID == IDC_SET_OK)
        {
            SetDlgDrawOk(dis);
            return TRUE;
        }
        break;
    }

    case WM_CTLCOLORBTN:
    {
        int id = GetDlgCtrlID((HWND)lParam);
        HBRUSH br = (id == IDC_SET_AUTO || id == IDC_SET_MIN)
            ? g_hCardBrush : g_hDlgBrush;
        SetBkColor((HDC)wParam, (id == IDC_SET_AUTO || id == IDC_SET_MIN)
            ? g_theme->card : g_theme->dialogBg);
        SetTextColor((HDC)wParam, g_theme->text);
        return (LRESULT)br;
    }

    case WM_CTLCOLORSTATIC:
    {
        int id = GetDlgCtrlID((HWND)lParam);
        SetBkMode((HDC)wParam, TRANSPARENT);
        if (id == IDC_SET_CREDITS || id == IDC_SET_HINT)
        {
            SetTextColor((HDC)wParam, g_theme->muted);
        }
        else
        {
            SetTextColor((HDC)wParam, g_theme->text);
        }
        if (id == IDC_SET_CREDITS || id == IDC_SET_EMAIL)
        {
            return (LRESULT)g_hDlgBrush;
        }
        return (LRESULT)g_hCardBrush;
    }

    case WM_KEYDOWN:
        if (wParam == VK_RETURN)
        {
            OnSetOk(hWnd);
            return 0;
        }
        break;

    case WM_NOTIFY:
    {
        LPNMHDR pnm = (LPNMHDR)lParam;
        if (pnm->idFrom == IDC_SET_EMAIL &&
            (pnm->code == NM_CLICK || pnm->code == NM_RETURN))
        {
            ShellExecuteW(hWnd, L"open", L"mailto:3507609128@qq.com",
                NULL, NULL, SW_SHOWNORMAL);
        }
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_SET_BROWSE:
            OnSetBrowse(hWnd);
            return 0;
        case IDC_SET_TEST:
            OnSetTest(hWnd);
            return 0;
        case IDC_SET_OK:
            OnSetOk(hWnd);
            return 0;
        case IDC_SET_CANCEL:
        case IDCANCEL:
            g_setResult = IDCANCEL;
            return 0;
        }
        return 0;

    case WM_CLOSE:
        g_setResult = IDCANCEL;
        return 0;
    }
    return DefWindowProcW(hWnd, message, wParam, lParam);
}

void ShowSettingsDialog(void)
{
    g_setResult = 0;
    int oldLang = g_set.lang;
    int oldTheme = g_set.theme;

    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    DWORD exStyle = WS_EX_DLGMODALFRAME;
    RECT wr;
    SetRect(&wr, 0, 0, SETDLG_CW, SETDLG_CH);
    AdjustWindowRectEx(&wr, style, FALSE, exStyle);

    g_hSet = CreateWindowExW(
        exStyle,
        L"TimeManagerSetWnd", Str(STR_SET_TITLE),
        style,
        CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top,
        g_hMain, NULL, g_hInst, NULL);
    if (g_hSet == NULL)
    {
        return;
    }

    RECT rm, rd;
    GetWindowRect(g_hMain, &rm);
    GetWindowRect(g_hSet, &rd);
    int w = rd.right - rd.left;
    int h = rd.bottom - rd.top;
    int x = rm.left + ((rm.right - rm.left) - w) / 2;
    int y = rm.top + ((rm.bottom - rm.top) - h) / 2;
    if (x < 0) { x = 0; }
    if (y < 0) { y = 0; }
    SetWindowPos(g_hSet, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

    EnableWindow(g_hMain, FALSE);
    ShowWindow(g_hSet, SW_SHOW);
    UpdateWindow(g_hSet);

    MSG msg;
    BOOL bRet;
    while (g_setResult == 0 && (bRet = GetMessageW(&msg, NULL, 0, 0)) != 0)
    {
        if (bRet == -1)
        {
            break;
        }
        if (bRet == 0)
        {
            PostQuitMessage((int)msg.wParam);
            break;
        }
        if (!IsDialogMessageW(g_hSet, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (g_hSet != NULL)
    {
        DestroyWindow(g_hSet);
        g_hSet = NULL;
    }
    EnableWindow(g_hMain, TRUE);
    SetForegroundWindow(g_hMain);

    /* 字体不能在设置窗口存活时销毁，关闭后再切换界面语言 */
    if (g_set.lang != oldLang)
    {
        ApplyLanguage();
    }
    if (g_set.theme != oldTheme)
    {
        ApplyTheme();
    }
}

/*============================= 自绘日历 =============================*/

#define CAL_HEADER_H S(42)
#define CAL_WEEK_H   S(28)
#define CAL_CELL_H   S(46)
#define CAL_WIDTH    S(356)
#define CAL_HEIGHT   (CAL_HEADER_H + CAL_WEEK_H + CAL_CELL_H * 6)

#define CAL_ACCENT      (g_theme->accent)
#define CAL_ACCENT_DARK (g_theme->accentDark)
#define CAL_TODAY       (g_theme->today)
#define CAL_TEXT        (g_theme->text)
#define CAL_MUTED       (g_theme->muted)
#define CAL_HOVER       (g_theme->hover)
#define CAL_SEL_BG      (g_theme->accentSoft)
#define CAL_SUN         (g_theme->sun)
#define CAL_SAT         (g_theme->sat)

/* 当前主题的统一配色 */
#define BG_PAGE         (g_theme->page)
#define CARD_BORDER     (g_theme->border)
#define WEEK_BG_WEEKEND (g_theme->weekendBg)
#define WEEK_BG_NIGHT   (g_theme->nightBg)
#define WEEK_BG_AXIS    (g_theme->axisBg)
#define LIST_BG_ALT     (g_theme->altRow)

struct CalLayout
{
    int W, H;
    int pad;
    int cellW;
    int cellH;
    int gridTop;
    int rows;
    RECT prev;
    RECT next;
};

static int DaysInMonth(int year, int month)
{
    static const int days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0))
    {
        return 29;
    }
    return days[month - 1];
}

static int FirstWeekday(int year, int month)
{
    SYSTEMTIME st;
    FILETIME ft;
    ZeroMemory(&st, sizeof(st));
    st.wYear = (WORD)year;
    st.wMonth = (WORD)month;
    st.wDay = 1;
    if (!SystemTimeToFileTime(&st, &ft))
    {
        return 0;
    }
    if (!FileTimeToSystemTime(&ft, &st))
    {
        return 0;
    }
    return st.wDayOfWeek;
}

/* 收集某天所有活动涉及的分类（去重，最多 max 个），返回分类数量 */
static int DayCategories(int year, int month, int day, int cats[], int max)
{
    int n = 0;
    for (int i = 1; i <= g_count; i++)
    {
        if (g_tasks[i].stStart.wYear == year &&
            g_tasks[i].stStart.wMonth == month &&
            g_tasks[i].stStart.wDay == day)
        {
            int c = g_tasks[i].category;
            if (c < 1 || c > CAT_COUNT) { c = CAT_OTHER; }
            int exist = 0;
            for (int k = 0; k < n; k++)
            {
                if (cats[k] == c) { exist = 1; break; }
            }
            if (!exist && n < max)
            {
                cats[n] = c;
                n++;
            }
        }
    }
    return n;
}

/* 统计某天活动数量 */
static int DayTaskCount(int year, int month, int day)
{
    int n = 0;
    for (int i = 1; i <= g_count; i++)
    {
        if (g_tasks[i].stStart.wYear == year &&
            g_tasks[i].stStart.wMonth == month &&
            g_tasks[i].stStart.wDay == day)
        {
            n++;
        }
    }
    return n;
}

static int HasTasksOnDate(int year, int month, int day)
{
    int cats[4];
    return DayCategories(year, month, day, cats, 4) > 0;
}

static void CalGetLayout(HWND h, struct CalLayout* L)
{
    RECT rc;
    GetClientRect(h, &rc);
    L->W = rc.right;
    L->H = rc.bottom;
    L->pad = S(4);
    L->cellW = (L->W - L->pad * 2) / 7;
    if (L->cellW < 1)
    {
        L->cellW = 1;
    }
    L->gridTop = CAL_HEADER_H + CAL_WEEK_H;
    /* 按当月实际需要的周数（4–6 行）均分高度，避免 5 行月份底部留下整行空白 */
    L->rows = (FirstWeekday(g_calYear, g_calMonth) + DaysInMonth(g_calYear, g_calMonth) + 6) / 7;
    if (L->rows < 4) { L->rows = 4; }
    if (L->rows > 6) { L->rows = 6; }
    int avail = L->H - L->gridTop - S(2);
    if (avail < 1) { avail = 1; }
    L->cellH = avail / L->rows;
    if (L->cellH > S(64)) { L->cellH = S(64); }
    if (L->cellH < 1) { L->cellH = 1; }
    /* 行数较少时把网格垂直居中，而不是全部堆在顶部 */
    L->gridTop += (avail - L->cellH * L->rows) / 2;
    SetRect(&L->prev, L->pad, S(4), L->pad + S(30), CAL_HEADER_H - S(2));
    SetRect(&L->next, L->W - L->pad - S(30), S(4), L->W - L->pad, CAL_HEADER_H - S(2));
}

static void FillRoundedRect(HDC hdc, const RECT* r, COLORREF color)
{
    HBRUSH br = CreateSolidBrush(color);
    HGDIOBJ oldBrush = SelectObject(hdc, br);
    HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(NULL_PEN));
    RoundRect(hdc, r->left, r->top, r->right, r->bottom, S(8), S(8));
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(br);
}

static void DrawChevron(HDC hdc, const RECT* r, int dir, COLORREF color)
{
    int cx = (r->left + r->right) / 2;
    int cy = (r->top + r->bottom) / 2;
    int dx = S(4);
    int dy = S(6);
    int width = S(2);
    if (width < 1)
    {
        width = 1;
    }
    LOGBRUSH lb;
    lb.lbStyle = BS_SOLID;
    lb.lbColor = color;
    lb.lbHatch = 0;
    HPEN pen = ExtCreatePen(PS_GEOMETRIC | PS_ENDCAP_ROUND | PS_JOIN_ROUND,
        (DWORD)width, &lb, 0, NULL);
    if (pen == NULL)
    {
        pen = CreatePen(PS_SOLID, 1, color);
    }
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    if (dir < 0)
    {
        MoveToEx(hdc, cx + dx, cy - dy, NULL);
        LineTo(hdc, cx - dx, cy);
        LineTo(hdc, cx + dx, cy + dy);
    }
    else
    {
        MoveToEx(hdc, cx - dx, cy - dy, NULL);
        LineTo(hdc, cx + dx, cy);
        LineTo(hdc, cx - dx, cy + dy);
    }
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

/* 今天用虚线圆角框标出 */
static void DrawDashedRoundRect(HDC hdc, const RECT* rc, COLORREF color, int inset)
{
    RECT r = *rc;
    InflateRect(&r, -inset, -inset);
    int width = S(2);
    if (width < 1)
    {
        width = 1;
    }
    LOGBRUSH lb;
    lb.lbStyle = BS_SOLID;
    lb.lbColor = color;
    lb.lbHatch = 0;
    HPEN pen = ExtCreatePen(PS_GEOMETRIC | PS_DASH | PS_ENDCAP_FLAT,
        (DWORD)width, &lb, 0, NULL);
    if (pen == NULL)
    {
        pen = CreatePen(PS_DASH, 1, color);
    }
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    RoundRect(hdc, r.left, r.top, r.right, r.bottom, S(8), S(8));
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

/* 实线圆角描边（用于选中状态） */
static void DrawRoundBorder(HDC hdc, const RECT* rc, COLORREF color, int inset)
{
    RECT r = *rc;
    InflateRect(&r, -inset, -inset);
    int width = S(2);
    if (width < 1)
    {
        width = 1;
    }
    LOGBRUSH lb;
    lb.lbStyle = BS_SOLID;
    lb.lbColor = color;
    lb.lbHatch = 0;
    HPEN pen = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_ROUND | PS_JOIN_ROUND,
        (DWORD)width, &lb, 0, NULL);
    if (pen == NULL)
    {
        pen = CreatePen(PS_SOLID, 1, color);
    }
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    RoundRect(hdc, r.left, r.top, r.right, r.bottom, S(8), S(8));
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

/* 画实心圆点 */
static void FillDot(HDC hdc, int cx, int cy, int radius, COLORREF color)
{
    HBRUSH br = CreateSolidBrush(color);
    HGDIOBJ oldBrush = SelectObject(hdc, br);
    HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(NULL_PEN));
    Ellipse(hdc, cx - radius, cy - radius, cx + radius, cy + radius);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(br);
}

static void CalPaint(HWND h)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(h, &ps);

    struct CalLayout L;
    CalGetLayout(h, &L);
    if (L.W <= 0 || L.H <= 0)
    {
        EndPaint(h, &ps);
        return;
    }

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, L.W, L.H);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);

    RECT all;
    SetRect(&all, 0, 0, L.W, L.H);
    HBRUSH white = CreateSolidBrush(g_theme->card);
    FillRect(mem, &all, white);
    DeleteObject(white);

    /* 卡片边框 + 星期表头淡底 */
    {
        HBRUSH cb = CreateSolidBrush(CARD_BORDER);
        FrameRect(mem, &all, cb);
        DeleteObject(cb);
        HBRUSH wh = CreateSolidBrush(WEEK_BG_AXIS);
        RECT wband;
        SetRect(&wband, 0, CAL_HEADER_H, L.W, CAL_HEADER_H + CAL_WEEK_H);
        FillRect(mem, &wband, wh);
        DeleteObject(wh);
    }

    SetBkMode(mem, TRANSPARENT);

    /* 标题：2026年10月 / October 2026 */
    wchar_t title[64];
    FmtMonthTitle(g_calYear, g_calMonth, title, 64);
    SelectObject(mem, g_fontCalTitle);
    SetTextColor(mem, CAL_TEXT);
    RECT rt;
    SetRect(&rt, L.prev.right, 0, L.next.left, CAL_HEADER_H);
    DrawTextW(mem, title, -1, &rt, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    /* 上一月 / 下一月 */
    if (g_calHoverNav == -1)
    {
        FillRoundedRect(mem, &L.prev, CAL_HOVER);
    }
    else if (g_calHoverNav == 1)
    {
        FillRoundedRect(mem, &L.next, CAL_HOVER);
    }
    DrawChevron(mem, &L.prev, -1, g_calHoverNav == -1 ? CAL_ACCENT : CAL_MUTED);
    DrawChevron(mem, &L.next, 1, g_calHoverNav == 1 ? CAL_ACCENT : CAL_MUTED);

    /* 星期表头 */
    SelectObject(mem, g_fontCalWeek);
    for (int col = 0; col < 7; col++)
    {
        RECT wr;
        SetRect(&wr, L.pad + col * L.cellW, CAL_HEADER_H,
            L.pad + (col + 1) * L.cellW, CAL_HEADER_H + CAL_WEEK_H);
        if (col == 0)
        {
            SetTextColor(mem, CAL_SUN);
        }
        else if (col == 6)
        {
            SetTextColor(mem, CAL_SAT);
        }
        else
        {
            SetTextColor(mem, CAL_MUTED);
        }
        DrawTextW(mem, g_weekShort[g_lang][col], -1, &wr,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    /* 日期格子 */
    SYSTEMTIME today;
    GetLocalTime(&today);
    int first = FirstWeekday(g_calYear, g_calMonth);
    int dim = DaysInMonth(g_calYear, g_calMonth);

    for (int row = 0; row < L.rows; row++)
    {
        for (int col = 0; col < 7; col++)
        {
            int day = row * 7 + col - first + 1;
            if (day < 1 || day > dim)
            {
                continue;
            }
            RECT cell;
            SetRect(&cell, L.pad + col * L.cellW, L.gridTop + row * L.cellH,
                L.pad + (col + 1) * L.cellW, L.gridTop + (row + 1) * L.cellH);

            int cats[4];
            int ncat = DayCategories(g_calYear, g_calMonth, day, cats, 4);
            int hasTask = (ncat > 0);
            int selected = (g_calSel.wYear == g_calYear &&
                            g_calSel.wMonth == g_calMonth &&
                            g_calSel.wDay == day);
            int hover = (g_calHoverDay == day);

            RECT inner = cell;
            InflateRect(&inner, -S(3), -S(3));

            COLORREF textColor = CAL_TEXT;
            HFONT font = g_fontCal;
            if (hasTask)
            {
                /* 有活动的日期用当天首个分类的浅色底 */
                FillRoundedRect(mem, &inner, g_catSoft[cats[0]]);
                textColor = g_theme->text;
                font = g_fontCalBold;
            }
            else if (selected)
            {
                FillRoundedRect(mem, &inner, CAL_SEL_BG);
                textColor = CAL_ACCENT_DARK;
                font = g_fontCalBold;
            }
            else if (hover)
            {
                FillRoundedRect(mem, &inner, CAL_HOVER);
            }
            else if (col == 0 || col == 6)
            {
                /* 周末列淡色底 */
                FillRoundedRect(mem, &inner, WEEK_BG_WEEKEND);
            }

            if (selected)
            {
                DrawRoundBorder(mem, &inner, CAL_ACCENT, 0);
            }

            if (today.wYear == g_calYear && today.wMonth == g_calMonth &&
                today.wDay == day)
            {
                DrawDashedRoundRect(mem, &cell, CAL_TODAY, S(2));
            }

            /* 普通周末日期数字用淡红/淡蓝，选中或有活动时保持其配色 */
            if (!hasTask && !selected)
            {
                if (col == 0) { textColor = CAL_SUN; }
                else if (col == 6) { textColor = CAL_SAT; }
            }

            /* 日期数字统一垂直居中；有活动时圆点单独画在格子底部，保证整行数字对齐 */
            wchar_t num[8];
            StringCchPrintfW(num, 8, L"%d", day);
            RECT numRect = cell;
            SelectObject(mem, font);
            SetTextColor(mem, textColor);
            DrawTextW(mem, num, -1, &numRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            /* 分类圆点：当天涉及的分类各画一个饱和色圆点（最多 3 个） */
            if (hasTask)
            {
                int dots = ncat > 3 ? 3 : ncat;
                int dotR = S(3);
                int gap = S(5);
                int totalW = dots * dotR * 2 + (dots - 1) * gap;
                int cx0 = (cell.left + cell.right) / 2 - totalW / 2 + dotR;
                int cy = cell.bottom - S(8);
                for (int d = 0; d < dots; d++)
                {
                    FillDot(mem, cx0 + d * (dotR * 2 + gap), cy, dotR,
                        g_catFill[cats[d]]);
                }
            }
        }
    }

    BitBlt(hdc, 0, 0, L.W, L.H, mem, 0, 0, SRCCOPY);

    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(h, &ps);
}

static int CalPointToDay(const struct CalLayout* L, int x, int y,
    int* outYear, int* outMonth, int* outDay)
{
    if (y < L->gridTop)
    {
        return 0;
    }
    int col = (x - L->pad) / L->cellW;
    int row = (y - L->gridTop) / L->cellH;
    if (col < 0 || col > 6 || row < 0 || row >= L->rows)
    {
        return 0;
    }
    int first = FirstWeekday(g_calYear, g_calMonth);
    int day = row * 7 + col - first + 1;
    int dim = DaysInMonth(g_calYear, g_calMonth);
    if (day < 1 || day > dim)
    {
        return 0;
    }
    *outYear = g_calYear;
    *outMonth = g_calMonth;
    *outDay = day;
    return 1;
}

static void CalShiftMonth(int delta)
{
    int y = g_calYear;
    int m = g_calMonth + delta;
    while (m < 1)
    {
        m += 12;
        y--;
    }
    while (m > 12)
    {
        m -= 12;
        y++;
    }
    if (y < 1601) { y = 1601; }
    if (y > 9999) { y = 9999; }
    g_calYear = y;
    g_calMonth = m;
    InvalidateRect(g_hCal, NULL, FALSE);
}

static void AddDaysToSystemTime(SYSTEMTIME* st, int days)
{
    FILETIME ft;
    ULARGE_INTEGER u;
    if (!SystemTimeToFileTime(st, &ft))
    {
        return;
    }
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    u.QuadPart = (ULONGLONG)((LONGLONG)u.QuadPart + (LONGLONG)days * 864000000000LL);
    ft.dwLowDateTime = u.LowPart;
    ft.dwHighDateTime = u.HighPart;
    FileTimeToSystemTime(&ft, st);
}

static void CalNotify(HWND h)
{
    SendMessageW(GetParent(h), WM_APP_CAL_SELECT, 0, 0);
}

/* 构造某一天的悬停提示文本，并返回当天首个分类色 */
static COLORREF BuildDayTip(int year, int month, int day, wchar_t* buf, int cch)
{
    wchar_t line[160];
    SYSTEMTIME d;
    ZeroMemory(&d, sizeof(d));
    d.wYear = (WORD)year;
    d.wMonth = (WORD)month;
    d.wDay = (WORD)day;
    FILETIME ft;
    if (!SystemTimeToFileTime(&d, &ft))
    {
        return CAL_ACCENT;
    }
    FileTimeToSystemTime(&ft, &d);

    int cnt = DayTaskCount(year, month, day);
    wchar_t date[32];
    FmtDate(&d, date, 32);
    StringCchPrintfW(buf, cch, L"%s %s · ",
        date, g_weekFull[g_lang][d.wDayOfWeek]);
    int headLen = (int)lstrlenW(buf);
    FormatStr(buf + headLen, cch - headLen, STR_DAY_EVENT_COUNT, cnt);

    COLORREF accent = CAL_ACCENT;
    int shown = 0;
    const int MAX_SHOW = 6;
    for (int i = 1; i <= g_count && shown < MAX_SHOW; i++)
    {
        struct Task* t = &g_tasks[i];
        if (t->stStart.wYear == year && t->stStart.wMonth == month &&
            t->stStart.wDay == day)
        {
            if (shown == 0)
            {
                accent = g_catFill[t->category];
            }
            wchar_t tm1[16];
            FmtTime(&t->stStart, tm1, 16);
            StringCchCatW(buf, cch, L"\n");
            StringCchPrintfW(line, 160, L"%s  %s", tm1, t->title);
            StringCchCatW(buf, cch, line);
            shown++;
        }
    }
    if (cnt > shown)
    {
        StringCchCatW(buf, cch, L"\n");
        wchar_t more[64];
        FormatStr(more, 64, STR_TIP_MORE, cnt - shown);
        StringCchCatW(buf, cch, more);
    }
    return accent;
}

/* 月历悬停：显示/隐藏当天活动气泡 */
static void CalUpdateHoverTip(HWND h, int day)
{
    if (day <= 0)
    {
        HideHoverTip();
        return;
    }
    int cnt = DayTaskCount(g_calYear, g_calMonth, day);
    if (cnt <= 0)
    {
        HideHoverTip();
        return;
    }
    struct CalLayout L;
    CalGetLayout(h, &L);
    int first = FirstWeekday(g_calYear, g_calMonth);
    int index = first + day - 1;
    int col = index % 7;
    int row = index / 7;
    RECT cell;
    SetRect(&cell, L.pad + col * L.cellW, L.gridTop + row * L.cellH,
        L.pad + (col + 1) * L.cellW, L.gridTop + (row + 1) * L.cellH);
    POINT tl, br;
    tl.x = cell.left;
    tl.y = cell.top;
    br.x = cell.right;
    br.y = cell.bottom;
    ClientToScreen(h, &tl);
    ClientToScreen(h, &br);
    SetRect(&cell, tl.x, tl.y, br.x, br.y);

    wchar_t tip[1100];
    COLORREF accent = BuildDayTip(g_calYear, g_calMonth, day, tip, 1100);
    ShowHoverTip(tip, &cell, 1, accent);
}

void CalSetDate(const SYSTEMTIME* st)
{
    g_calSel = *st;
    g_calYear = st->wYear;
    g_calMonth = st->wMonth;
    if (g_hCal != NULL)
    {
        InvalidateRect(g_hCal, NULL, FALSE);
    }
}

LRESULT CALLBACK CalWndProc(HWND h, UINT message, WPARAM wParam, LPARAM lParam)
{
    struct CalLayout L;
    switch (message)
    {
    case WM_PAINT:
        CalPaint(h);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_MOUSEMOVE:
    {
        CalGetLayout(h, &L);
        POINT pt;
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);
        int nav = 0;
        if (PtInRect(&L.prev, pt))
        {
            nav = -1;
        }
        else if (PtInRect(&L.next, pt))
        {
            nav = 1;
        }
        int day = 0;
        int yy, mm, dd;
        if (nav == 0 && CalPointToDay(&L, pt.x, pt.y, &yy, &mm, &dd))
        {
            day = dd;
        }
        if (nav != g_calHoverNav || day != g_calHoverDay)
        {
            g_calHoverNav = nav;
            g_calHoverDay = day;
            InvalidateRect(h, NULL, FALSE);
        }
        if (nav == 0)
        {
            CalUpdateHoverTip(h, day);
        }
        else
        {
            HideHoverTip();
        }
        if (!g_calTracking)
        {
            TRACKMOUSEEVENT tme;
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = h;
            TrackMouseEvent(&tme);
            g_calTracking = 1;
        }
        SetCursor(LoadCursorW(NULL, nav != 0 ? IDC_HAND : IDC_ARROW));
        return 0;
    }

    case WM_MOUSELEAVE:
        g_calTracking = 0;
        g_calHoverDay = 0;
        g_calHoverNav = 0;
        HideHoverTip();
        InvalidateRect(h, NULL, FALSE);
        return 0;

    case WM_LBUTTONDOWN:
    {
        SetFocus(h);
        CalGetLayout(h, &L);
        POINT pt;
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);
        if (PtInRect(&L.prev, pt))
        {
            CalShiftMonth(-1);
            return 0;
        }
        if (PtInRect(&L.next, pt))
        {
            CalShiftMonth(1);
            return 0;
        }
        int yy, mm, dd;
        if (CalPointToDay(&L, pt.x, pt.y, &yy, &mm, &dd))
        {
            SYSTEMTIME st;
            ZeroMemory(&st, sizeof(st));
            st.wYear = (WORD)yy;
            st.wMonth = (WORD)mm;
            st.wDay = (WORD)dd;
            NormalizeWeekday(&st);
            g_calSel = st;
            InvalidateRect(h, NULL, FALSE);
            CalNotify(h);
        }
        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        CalShiftMonth(delta > 0 ? -1 : 1);
        return 0;
    }

    case WM_KEYDOWN:
    {
        SYSTEMTIME st = g_calSel;
        if (st.wYear < 1601)
        {
            GetLocalTime(&st);
        }
        switch (wParam)
        {
        case VK_LEFT:  AddDaysToSystemTime(&st, -1); break;
        case VK_RIGHT: AddDaysToSystemTime(&st, 1); break;
        case VK_UP:    AddDaysToSystemTime(&st, -7); break;
        case VK_DOWN:  AddDaysToSystemTime(&st, 7); break;
        case VK_PRIOR:
            CalShiftMonth(-1);
            return 0;
        case VK_NEXT:
            CalShiftMonth(1);
            return 0;
        case VK_HOME:
            GetLocalTime(&st);
            break;
        default:
            return DefWindowProcW(h, message, wParam, lParam);
        }
        g_calSel = st;
        g_calYear = st.wYear;
        g_calMonth = st.wMonth;
        InvalidateRect(h, NULL, FALSE);
        CalNotify(h);
        return 0;
    }

    case WM_GETDLGCODE:
        return DLGC_WANTARROWS;

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT)
        {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(h, &pt);
            CalGetLayout(h, &L);
            if (PtInRect(&L.prev, pt) || PtInRect(&L.next, pt))
            {
                SetCursor(LoadCursorW(NULL, IDC_HAND));
                return TRUE;
            }
        }
        break;
    }
    return DefWindowProcW(h, message, wParam, lParam);
}

/*============================= 悬停提示气泡 =============================*/

#define TIP_MAX_LINES  9
#define TIP_MAX_CHARS  2048
#define TIP_WIDTH      S(280)
#define TIP_PAD        S(11)
#define TIP_LINE_H     S(20)
#define TIP_BAR_W      S(5)

static wchar_t g_tipText[TIP_MAX_CHARS];
static wchar_t* g_tipLines[TIP_MAX_LINES];
static int g_tipLineCount = 0;
static int g_tipFirstBold = 1;
static COLORREF g_tipAccent = RGB(0, 120, 212);

/* 计算气泡所需高度（像素） */
static int TipCalcHeight(HDC hdc)
{
    int y = TIP_PAD * 2;
    HDC useDc = (hdc != NULL) ? hdc : GetDC(NULL);
    for (int i = 0; i < g_tipLineCount; i++)
    {
        HGDIOBJ old = SelectObject(useDc,
            (i == 0 && g_tipFirstBold) ? g_fontTipTitle : g_fontTipBody);
        RECT r;
        SetRect(&r, 0, 0, TIP_WIDTH - TIP_PAD * 2 - TIP_BAR_W - S(6), 0);
        DrawTextW(useDc, g_tipLines[i], -1, &r,
            DT_CALCRECT | DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        int h = r.bottom - r.top;
        if (h < TIP_LINE_H) { h = TIP_LINE_H; }
        y += h + (i == 0 ? S(4) : S(1));
        SelectObject(useDc, old);
    }
    if (hdc == NULL)
    {
        ReleaseDC(NULL, useDc);
    }
    return y + S(4);
}

static void TipPaint(HWND h)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(h, &ps);
    RECT rc;
    GetClientRect(h, &rc);

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);

    HBRUSH bg = CreateSolidBrush(g_theme->tipBg);
    FillRect(mem, &rc, bg);
    DeleteObject(bg);

    /* 左侧分类色条 */
    HBRUSH bar = CreateSolidBrush(g_tipAccent);
    RECT barRc;
    SetRect(&barRc, 0, 0, TIP_BAR_W, rc.bottom);
    FillRect(mem, &barRc, bar);
    DeleteObject(bar);

    SetBkMode(mem, TRANSPARENT);
    int y = TIP_PAD;
    int textLeft = TIP_PAD + TIP_BAR_W + S(6);
    for (int i = 0; i < g_tipLineCount; i++)
    {
        int bold = (i == 0 && g_tipFirstBold);
        HGDIOBJ old = SelectObject(mem, bold ? g_fontTipTitle : g_fontTipBody);
        SetTextColor(mem, bold ? g_theme->tipText : g_theme->tipMuted);
        RECT r;
        SetRect(&r, textLeft, y, rc.right - TIP_PAD, 0);
        RECT r2 = r;
        DrawTextW(mem, g_tipLines[i], -1, &r2,
            DT_CALCRECT | DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        int h = r2.bottom - r2.top;
        if (h < TIP_LINE_H) { h = TIP_LINE_H; }
        r.bottom = y + h;
        DrawTextW(mem, g_tipLines[i], -1, &r,
            DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        y += h + (i == 0 ? S(4) : S(1));
        SelectObject(mem, old);
    }

    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(h, &ps);
}

LRESULT CALLBACK TipWndProc(HWND h, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_PAINT:
        TipPaint(h);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PRINTCLIENT:
    {
        RECT rc;
        GetClientRect(h, &rc);
        HBRUSH bg = CreateSolidBrush(g_theme->tipBg);
        FillRect((HDC)wParam, &rc, bg);
        DeleteObject(bg);
        return 0;
    }
    }
    return DefWindowProcW(h, message, wParam, lParam);
}

/* 显示气泡：text 用 L"\n" 分行；avoid 为屏幕坐标，气泡尽量避开该区域。
   firstBold 表示第一行是否用标题粗体。 */
void ShowHoverTip(const wchar_t* text, const RECT* avoid, int firstBold, COLORREF accent)
{
    if (text == NULL || text[0] == 0)
    {
        HideHoverTip();
        return;
    }

    StringCchCopyW(g_tipText, TIP_MAX_CHARS, text);
    g_tipFirstBold = firstBold;
    g_tipAccent = accent;

    /* 按 \n 拆行 */
    g_tipLineCount = 0;
    g_tipLines[0] = g_tipText;
    g_tipLineCount = 1;
    for (wchar_t* p = g_tipText; *p != 0; p++)
    {
        if (*p == L'\n')
        {
            *p = 0;
            if (g_tipLineCount < TIP_MAX_LINES)
            {
                g_tipLines[g_tipLineCount] = p + 1;
                g_tipLineCount++;
            }
        }
    }

    if (g_hTip2 == NULL)
    {
        g_hTip2 = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            L"TimeManagerTip", L"", WS_POPUP,
            0, 0, TIP_WIDTH, S(10),
            g_hMain, NULL, g_hInst, NULL);
        if (g_hTip2 == NULL)
        {
            return;
        }
    }

    HDC hdc = GetDC(g_hTip2);
    int tipH = TipCalcHeight(hdc);
    int tipW = TIP_WIDTH;
    ReleaseDC(g_hTip2, hdc);

    HRGN rgn = CreateRoundRectRgn(0, 0, tipW + 1, tipH + 1, S(12), S(12));
    SetWindowRgn(g_hTip2, rgn, FALSE);

    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);

    int x = (avoid->left + avoid->right) / 2 - tipW / 2;
    int y = avoid->bottom + S(8);
    if (y + tipH > wa.bottom - S(4))
    {
        y = avoid->top - tipH - S(8);     /* 下方放不下，移到上方 */
    }
    if (y < wa.top + S(4))
    {
        y = wa.top + S(4);
        x = avoid->right + S(10);        /* 仍放不下，放右侧 */
    }
    if (x + tipW > wa.right - S(4))
    {
        x = wa.right - tipW - S(4);
    }
    if (x < wa.left + S(4))
    {
        x = wa.left + S(4);
    }

    SetWindowPos(g_hTip2, HWND_TOPMOST, x, y, tipW, tipH,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_hTip2, NULL, TRUE);
}

void HideHoverTip(void)
{
    if (g_hTip2 != NULL)
    {
        ShowWindow(g_hTip2, SW_HIDE);
    }
    g_weekHoverTask = 0;
}

/*============================= 周视图（时间网格） =============================*/

struct WeekBlock
{
    int taskIndex;     /* g_tasks 下标 */
    int col;           /* 0=周一 … 6=周日 */
    int lane;
    int lanes;
    int cat;
    int startMin;
    int endMin;
    RECT rc;
};

#define WEEK_COLOR_HEAD   (g_theme->card)
#define WEEK_COLOR_LINE   (g_theme->line)
#define WEEK_COLOR_TODAY  (g_theme->todayBg)
#define WEEK_COLOR_AXIS   (g_theme->muted)
#define WEEK_COLOR_NOW    (g_theme->now)

static struct WeekBlock g_wb[MAX_TASKS + 1];
static int g_wbCount = 0;

static void WeekGetMonday(SYSTEMTIME* mon)
{
    MondayOfDate(&g_weekAnchor, mon);
}

static int WeekScrollRange(int gridH)
{
    /* 底部多留 S(26)，使滚到底时 24:00 标签完整可见 */
    int contentH = WEEK_HOURS * WEEK_ROW_H + S(26);
    int r = contentH - gridH;
    return r > 0 ? r : 0;
}

static void WeekClampScroll(HWND h)
{
    RECT rc;
    GetClientRect(h, &rc);
    int max = WeekScrollRange(rc.bottom - WEEK_HEADER_H);
    if (g_weekScroll > max) { g_weekScroll = max; }
    if (g_weekScroll < 0) { g_weekScroll = 0; }
}

/* 仅比较年月日：a 早于 b 返回 -1，相等 0，晚于 1 */
static int CmpDate(const SYSTEMTIME* a, const SYSTEMTIME* b)
{
    if (a->wYear != b->wYear) { return a->wYear < b->wYear ? -1 : 1; }
    if (a->wMonth != b->wMonth) { return a->wMonth < b->wMonth ? -1 : 1; }
    if (a->wDay != b->wDay) { return a->wDay < b->wDay ? -1 : 1; }
    return 0;
}

/* 任务在某列（当天）的起止分钟：跨天部分按 0:00 / 24:00 裁剪 */
static void WeekDaySpan(const struct Task* t, const SYSTEMTIME* d, int* pS, int* pE)
{
    int sMin, eMin;
    if (CmpDate(d, &t->stStart) == 0)
    {
        sMin = t->stStart.wHour * 60 + t->stStart.wMinute;
    }
    else
    {
        sMin = 0;
    }
    if (CmpDate(d, &t->stEnd) == 0)
    {
        eMin = t->stEnd.wHour * 60 + t->stEnd.wMinute;
    }
    else
    {
        eMin = 24 * 60;
    }
    if (eMin <= sMin) { eMin = sMin + 30; }
    *pS = sMin;
    *pE = eMin;
}

/* 生成一周所有任务方块（含分栏与几何），结果写 g_wb / g_wbCount */
static void WeekBuildBlocks(HWND h)
{
    g_wbCount = 0;
    RECT rc;
    GetClientRect(h, &rc);
    int W = rc.right;
    int H = rc.bottom;
    int gridTop = WEEK_HEADER_H;
    int colW = (W - WEEK_AXIS_W) / 7;
    if (colW < 8) { colW = 8; }
    WeekClampScroll(h);

    SYSTEMTIME mon;
    WeekGetMonday(&mon);
    int dayStart = WEEK_START_HOUR * 60;
    int dayEnd = WEEK_END_HOUR * 60;

    /* 每列收集任务（任务下标），g_tasks 已按开始时间排序，按列分组保持有序 */
    int colItems[7][MAX_TASKS + 1];
    int colCnt[7];
    int colLane[7][MAX_TASKS + 1];
    int laneEnd[7][WEEK_MAX_LANES + 1];
    int colLanes[7];
    for (int c = 0; c < 7; c++)
    {
        colCnt[c] = 0;
        colLanes[c] = 1;
        for (int k = 0; k <= WEEK_MAX_LANES; k++)
        {
            laneEnd[c][k] = -1;
        }
    }

    /* 收集每列任务：跨天任务在它覆盖的每一天都出现（含次日凌晨） */
    for (int i = 1; i <= g_count; i++)
    {
        struct Task* t = &g_tasks[i];
        SYSTEMTIME lastDay = t->stEnd;
        /* 结束恰在某天 00:00，则最后覆盖日是前一天 */
        if (t->stEnd.wHour == 0 && t->stEnd.wMinute == 0 &&
            CmpDate(&t->stEnd, &t->stStart) > 0)
        {
            AddDays(&lastDay, -1);
        }
        for (int c = 0; c < 7; c++)
        {
            SYSTEMTIME d = mon;
            AddDays(&d, c);
            if (CmpDate(&d, &t->stStart) >= 0 && CmpDate(&d, &lastDay) <= 0)
            {
                if (colCnt[c] < MAX_TASKS)
                {
                    colCnt[c]++;
                    colItems[c][colCnt[c]] = i;
                }
            }
        }
    }

    /* 每列按当天开始时间排序（跨天延续任务 sMin=0 应排最前） */
    for (int c = 0; c < 7; c++)
    {
        for (int a = 1; a <= colCnt[c]; a++)
        {
            for (int b = a + 1; b <= colCnt[c]; b++)
            {
                struct Task* ta = &g_tasks[colItems[c][a]];
                struct Task* tb = &g_tasks[colItems[c][b]];
                SYSTEMTIME da = mon;
                AddDays(&da, c);
                int sa, sb, ea2, eb2;
                WeekDaySpan(ta, &da, &sa, &ea2);
                WeekDaySpan(tb, &da, &sb, &eb2);
                if (sb < sa)
                {
                    int tmp = colItems[c][a];
                    colItems[c][a] = colItems[c][b];
                    colItems[c][b] = tmp;
                }
            }
        }
    }

    /* 每列做 lane 分配（区间图贪心着色，处理时间重叠） */
    for (int c = 0; c < 7; c++)
    {
        SYSTEMTIME d0 = mon;
        AddDays(&d0, c);
        for (int q = 1; q <= colCnt[c]; q++)
        {
            int idx = colItems[c][q];
            struct Task* t = &g_tasks[idx];
            int sMin, eMin;
            WeekDaySpan(t, &d0, &sMin, &eMin);

            int lane = 0;
            for (int k = 1; k <= colLanes[c]; k++)
            {
                if (laneEnd[c][k] <= sMin)
                {
                    lane = k;
                    break;
                }
            }
            if (lane == 0)
            {
                if (colLanes[c] < WEEK_MAX_LANES)
                {
                    colLanes[c]++;
                    lane = colLanes[c];
                }
                else
                {
                    lane = 1;       /* 超过最大分栏则共用第一栏 */
                }
            }
            laneEnd[c][lane] = eMin;
            colLane[c][q] = lane;
        }
    }

    /* 生成方块几何 */
    for (int c = 0; c < 7; c++)
    {
        int lanes = colLanes[c];
        if (colCnt[c] == 0)
        {
            lanes = 1;
        }
        int laneW = colW / lanes;
        for (int q = 1; q <= colCnt[c]; q++)
        {
            int idx = colItems[c][q];
            struct Task* t = &g_tasks[idx];
            SYSTEMTIME d0 = mon;
            AddDays(&d0, c);
            int sMin, eMin;
            WeekDaySpan(t, &d0, &sMin, &eMin);

            /* 裁剪到显示时间窗 */
            int drawS = sMin < dayStart ? dayStart : sMin;
            int drawE = eMin > dayEnd ? dayEnd : eMin;
            if (drawS >= dayEnd || drawE <= dayStart)
            {
                continue;
            }

            int lane = colLane[c][q];
            int top = gridTop + (drawS - dayStart) * WEEK_ROW_H / 60 - g_weekScroll;
            int bottom = gridTop + (drawE - dayStart) * WEEK_ROW_H / 60 - g_weekScroll;
            if (bottom - top < S(22))
            {
                bottom = top + S(22);
            }
            int left = WEEK_AXIS_W + c * colW + (lane - 1) * laneW;
            int right = WEEK_AXIS_W + c * colW + lane * laneW;
            RECT r;
            SetRect(&r, left + S(2), top + S(2), right - S(2), bottom - S(2));

            g_wbCount++;
            struct WeekBlock* b = &g_wb[g_wbCount];
            b->taskIndex = idx;
            b->col = c;
            b->lane = lane;
            b->lanes = lanes;
            b->cat = (t->category >= 1 && t->category <= CAT_COUNT) ? t->category : CAT_OTHER;
            b->startMin = sMin;
            b->endMin = eMin;
            b->rc = r;
        }
    }
}

/* 取今天对应列（周一=0…周日=6），不在本周返回 -1 */
static int WeekTodayColumn(void)
{
    SYSTEMTIME mon, now;
    WeekGetMonday(&mon);
    GetLocalTime(&now);
    for (int c = 0; c < 7; c++)
    {
        SYSTEMTIME d = mon;
        AddDays(&d, c);
        if (d.wYear == now.wYear && d.wMonth == now.wMonth && d.wDay == now.wDay)
        {
            return c;
        }
    }
    return -1;
}

static void WeekPaint(HWND h)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(h, &ps);
    RECT rc;
    GetClientRect(h, &rc);
    int W = rc.right;
    int H = rc.bottom;
    if (W <= 0 || H <= 0)
    {
        EndPaint(h, &ps);
        return;
    }

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, W, H);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);

    HBRUSH white = CreateSolidBrush(g_theme->card);
    FillRect(mem, &rc, white);
    DeleteObject(white);

    int gridTop = WEEK_HEADER_H;
    int colW = (W - WEEK_AXIS_W) / 7;
    if (colW < 8) { colW = 8; }
    WeekClampScroll(h);

    SYSTEMTIME mon;
    WeekGetMonday(&mon);
    int todayCol = WeekTodayColumn();

    /* 左侧时间轴区底色 */
    {
        HBRUSH ab = CreateSolidBrush(WEEK_BG_AXIS);
        RECT ar;
        SetRect(&ar, 0, gridTop, WEEK_AXIS_W, H);
        FillRect(mem, &ar, ab);
        DeleteObject(ab);
    }

    /* 夜间时段横带（0:00-7:00、22:00-24:00），区分休息时段 */
    {
        HBRUSH nb = CreateSolidBrush(WEEK_BG_NIGHT);
        for (int hh = 0; hh < WEEK_HOURS; hh++)
        {
            if (hh < 7 || hh >= 22)
            {
                int y0 = gridTop + (hh - WEEK_START_HOUR) * WEEK_ROW_H - g_weekScroll;
                int y1 = gridTop + (hh + 1 - WEEK_START_HOUR) * WEEK_ROW_H - g_weekScroll;
                if (y1 > gridTop && y0 < H)
                {
                    if (y0 < gridTop) { y0 = gridTop; }
                    if (y1 > H) { y1 = H; }
                    RECT nr;
                    SetRect(&nr, WEEK_AXIS_W, y0, W, y1);
                    FillRect(mem, &nr, nb);
                }
            }
        }
        DeleteObject(nb);
    }

    /* 周末列（周六、周日）淡色底 */
    {
        HBRUSH wb = CreateSolidBrush(WEEK_BG_WEEKEND);
        for (int c = 5; c <= 6; c++)
        {
            RECT wr;
            SetRect(&wr, WEEK_AXIS_W + c * colW, gridTop,
                WEEK_AXIS_W + (c + 1) * colW, H);
            FillRect(mem, &wr, wb);
        }
        DeleteObject(wb);
    }

    /* 今天列整列浅底 */
    if (todayCol >= 0)
    {
        RECT tr;
        SetRect(&tr, WEEK_AXIS_W + todayCol * colW, gridTop,
            WEEK_AXIS_W + (todayCol + 1) * colW, H);
        HBRUSH tb = CreateSolidBrush(WEEK_COLOR_TODAY);
        FillRect(mem, &tr, tb);
        DeleteObject(tb);
    }

    /* 网格横线与时间轴标签 */
    SetBkMode(mem, TRANSPARENT);
    SelectObject(mem, g_fontWeekAxis);
    SetTextColor(mem, WEEK_COLOR_AXIS);
    for (int hour = WEEK_START_HOUR; hour <= WEEK_END_HOUR; hour++)
    {
        int y = gridTop + (hour - WEEK_START_HOUR) * WEEK_ROW_H - g_weekScroll;
        if (y >= gridTop - S(1) && y <= H)
        {
            HPEN pen = CreatePen(PS_SOLID, 1, WEEK_COLOR_LINE);
            HGDIOBJ oldPen = SelectObject(mem, pen);
            MoveToEx(mem, WEEK_AXIS_W, y, NULL);
            LineTo(mem, W, y);
            SelectObject(mem, oldPen);
            DeleteObject(pen);
        }
        if (hour <= WEEK_END_HOUR && y >= gridTop - S(1) && y <= H)
        {
            wchar_t lab[8];
            StringCchPrintfW(lab, 8, L"%02d:00", hour);
            int arTop = y - S(11);
            int arBot = y + S(12);
            if (arTop < gridTop) { arTop = gridTop; }
            if (arBot > H) { arBot = H; }
            RECT ar;
            SetRect(&ar, 0, arTop, WEEK_AXIS_W - S(4), arBot);
            DrawTextW(mem, lab, -1, &ar, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
        }
    }

    /* 竖列分隔线 */
    HPEN vpen = CreatePen(PS_SOLID, 1, WEEK_COLOR_LINE);
    HGDIOBJ oldvpen = SelectObject(mem, vpen);
    for (int c = 0; c <= 7; c++)
    {
        int x = WEEK_AXIS_W + c * colW;
        MoveToEx(mem, x, gridTop, NULL);
        LineTo(mem, x, H);
    }
    MoveToEx(mem, 0, gridTop, NULL);
    LineTo(mem, W, gridTop);
    SelectObject(mem, oldvpen);
    DeleteObject(vpen);

    /* 列头区淡色底 */
    {
        HBRUSH hb = CreateSolidBrush(WEEK_BG_AXIS);
        RECT hh;
        SetRect(&hh, 0, 0, W, gridTop);
        FillRect(mem, &hh, hb);
        DeleteObject(hb);
    }

    /* 列头：星期 + 日期，今天列蓝色块高亮 */
    for (int c = 0; c < 7; c++)
    {
        SYSTEMTIME d = mon;
        AddDays(&d, c);
        int isToday = (todayCol == c);
        RECT hc;
        SetRect(&hc, WEEK_AXIS_W + c * colW, 0,
            WEEK_AXIS_W + (c + 1) * colW, gridTop);

        if (isToday)
        {
            RECT chip = hc;
            InflateRect(&chip, -S(6), -S(5));
            FillRoundedRect(mem, &chip, CAL_ACCENT);
        }

        wchar_t head[40];
        StringCchPrintfW(head, 40, L"%s  %d/%d",
            g_weekShort[g_lang][d.wDayOfWeek], d.wMonth, d.wDay);
        SelectObject(mem, g_fontWeekHead);
        SetTextColor(mem, isToday ? RGB(255, 255, 255) :
            (d.wDayOfWeek == 0 ? CAL_SUN : (d.wDayOfWeek == 6 ? CAL_SAT : CAL_TEXT)));
        DrawTextW(mem, head, -1, &hc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    /* 任务方块：裁剪到表头下方，防止垂直滚动时方块上移遮挡日期列头 */
    int nSavedDc = SaveDC(mem);
    IntersectClipRect(mem, 0, gridTop, W, H);
    WeekBuildBlocks(h);
    for (int i = 1; i <= g_wbCount; i++)
    {
        struct WeekBlock* b = &g_wb[i];
        if (b->rc.bottom < gridTop || b->rc.top > H)
        {
            continue;
        }
        struct Task* t = &g_tasks[b->taskIndex];
        FillRoundedRect(mem, &b->rc, g_catFill[b->cat]);

        RECT tr = b->rc;
        InflateRect(&tr, -S(5), -S(3));
        SetBkMode(mem, TRANSPARENT);
        SelectObject(mem, g_fontWeekBlock);
        SetTextColor(mem, RGB(255, 255, 255));
        DrawTextW(mem, t->title, -1, &tr,
            DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);

        if (b->rc.bottom - b->rc.top >= S(44))
        {
            wchar_t tm[24];
            StringCchPrintfW(tm, 24, L"%02d:%02d",
                t->stStart.wHour, t->stStart.wMinute);
            RECT tr2 = tr;
            tr2.top += S(19);
            SelectObject(mem, g_fontWeekAxis);
            SetTextColor(mem, RGB(240, 246, 252));
            DrawTextW(mem, tm, -1, &tr2, DT_LEFT | DT_TOP | DT_SINGLELINE);
        }
        if (b->rc.bottom - b->rc.top >= S(66) && t->note[0] != 0)
        {
            RECT tr3 = tr;
            tr3.top += S(35);
            SelectObject(mem, g_fontWeekAxis);
            SetTextColor(mem, RGB(240, 246, 252));
            DrawTextW(mem, t->note, -1, &tr3,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
        }

        /* 选中高亮：蓝色外框 + 白色内框（跨天任务的两段都高亮） */
        if (t->id == g_weekSelId)
        {
            DrawRoundBorder(mem, &b->rc, CAL_ACCENT, -S(1));
            DrawRoundBorder(mem, &b->rc, RGB(255, 255, 255), S(2));
        }
    }
    RestoreDC(mem, nSavedDc);

    /* 当前时间线（仅今天列、且在显示时间窗内） */
    if (todayCol >= 0)
    {
        SYSTEMTIME now2;
        GetLocalTime(&now2);
        int nowMin = now2.wHour * 60 + now2.wMinute;
        if (nowMin >= WEEK_START_HOUR * 60 && nowMin <= WEEK_END_HOUR * 60)
        {
            int y = gridTop + (nowMin - WEEK_START_HOUR * 60) * WEEK_ROW_H / 60
                - g_weekScroll;
            if (y >= gridTop && y <= H)
            {
                HPEN npen = CreatePen(PS_SOLID, S(2), WEEK_COLOR_NOW);
                HGDIOBJ oldPen = SelectObject(mem, npen);
                int x0 = WEEK_AXIS_W + todayCol * colW;
                int x1 = WEEK_AXIS_W + (todayCol + 1) * colW;
                MoveToEx(mem, x0, y, NULL);
                LineTo(mem, x1, y);
                SelectObject(mem, oldPen);
                DeleteObject(npen);
                HBRUSH nb = CreateSolidBrush(WEEK_COLOR_NOW);
                HGDIOBJ oldb = SelectObject(mem, nb);
                HGDIOBJ oldp = SelectObject(mem, GetStockObject(NULL_PEN));
                Ellipse(mem, x0 - S(3), y - S(3), x0 + S(3), y + S(3));
                SelectObject(mem, oldp);
                SelectObject(mem, oldb);
                DeleteObject(nb);
            }
        }
    }

    /* 空周提示 */
    if (g_wbCount == 0)
    {
        SelectObject(mem, g_fontWeekHead);
        SetTextColor(mem, g_theme->empty);
        SetBkMode(mem, TRANSPARENT);
        RECT er;
        SetRect(&er, WEEK_AXIS_W, gridTop, W, H);
        DrawTextW(mem, Str(STR_WEEK_NO_EVENT), -1, &er,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    /* 卡片外边框 */
    {
        HBRUSH bd = CreateSolidBrush(CARD_BORDER);
        FrameRect(mem, &rc, bd);
        DeleteObject(bd);
    }

    BitBlt(hdc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(h, &ps);
}

/* 命中测试：返回任务下标（1..g_count），并给出方块客户矩形；无命中返回 0 */
static int WeekHitTest(HWND h, int x, int y, RECT* outRc)
{
    if (y < WEEK_HEADER_H)
    {
        return 0;       /* 表头区域不响应活动悬停 */
    }
    WeekBuildBlocks(h);
    for (int i = 1; i <= g_wbCount; i++)
    {
        POINT pt;
        pt.x = x;
        pt.y = y;
        if (PtInRect(&g_wb[i].rc, pt))
        {
            *outRc = g_wb[i].rc;
            return g_wb[i].taskIndex;
        }
    }
    return 0;
}

static void WeekUpdateScrollBar(HWND h)
{
    RECT rc;
    GetClientRect(h, &rc);
    int gridH = rc.bottom - WEEK_HEADER_H;
    SCROLLINFO si;
    ZeroMemory(&si, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = WEEK_HOURS * WEEK_ROW_H + S(26);
    si.nPage = (UINT)gridH;
    si.nPos = g_weekScroll;
    SetScrollInfo(h, SB_VERT, &si, TRUE);
}

/* 构造任务方块的悬停提示 */
static void BuildBlockTip(const struct Task* t, wchar_t* buf, int cch)
{
    wchar_t date[32], tm1[16], tm2[16], line[320];
    FmtDate(&t->stStart, date, 32);
    FmtTime(&t->stStart, tm1, 16);
    FmtTime(&t->stEnd, tm2, 16);
    StringCchPrintfW(buf, cch, L"%s", t->title);
    StringCchCatW(buf, cch, L"\n");
    FormatStr(line, 320, STR_TIP_CAT, CatStr(t->category));
    StringCchCatW(buf, cch, line);
    StringCchCatW(buf, cch, L"\n");
    FormatStr(line, 320, STR_TIP_TIME, date, tm1, tm2);
    StringCchCatW(buf, cch, line);
    if (t->rcount > 0)
    {
        StringCchCatW(buf, cch, L"\n");
        FormatStr(line, 320, STR_TIP_REMIND, t->rcount);
        StringCchCatW(buf, cch, line);
    }
    if (t->note[0] != 0)
    {
        StringCchCatW(buf, cch, L"\n");
        FormatStr(line, 320, STR_TIP_NOTE, t->note);
        StringCchCatW(buf, cch, line);
    }
}

void RefreshWeek(void)
{
    if (g_hWeek != NULL)
    {
        WeekClampScroll(g_hWeek);
        WeekUpdateScrollBar(g_hWeek);
        InvalidateRect(g_hWeek, NULL, FALSE);
    }
    if (g_hWeekTitle != NULL)
    {
        SYSTEMTIME mon, sun;
        WeekGetMonday(&mon);
        sun = mon;
        AddDays(&sun, 6);
        wchar_t title[48];
        StringCchPrintfW(title, 48, Str(STR_WEEK_RANGE),
            mon.wMonth, mon.wDay, sun.wMonth, sun.wDay);
        SetWindowTextW(g_hWeekTitle, title);
    }
}

/* 默认滚动位置：本周定位到当前时间稍上方，其它周从早上 7:00 起 */
static int WeekDefaultScroll(void)
{
    if (WeekTodayColumn() >= 0)
    {
        SYSTEMTIME now;
        GetLocalTime(&now);
        int nowMin = now.wHour * 60 + now.wMinute;
        int s = (nowMin - WEEK_START_HOUR * 60) * WEEK_ROW_H / 60 - S(140);
        if (s < 0) { s = 0; }
        return s;
    }
    return (7 - WEEK_START_HOUR) * WEEK_ROW_H;
}

/* 翻周：delta=-1 上周，+1 下周，0 回到本周 */
void WeekShift(int delta)
{
    g_weekSelId = 0;     /* 翻周时清除方块选中 */
    if (delta == 0)
    {
        GetLocalTime(&g_weekAnchor);
        g_weekScroll = WeekDefaultScroll();
    }
    else
    {
        AddDays(&g_weekAnchor, delta * 7);
        /* 翻周保持当前滚动位置（同一时段对照） */
    }
    RefreshWeek();
}

LRESULT CALLBACK WeekWndProc(HWND h, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_PAINT:
        WeekPaint(h);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_SIZE:
        WeekUpdateScrollBar(h);
        return 0;

    case WM_MOUSEWHEEL:
    {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        int step = WEEK_ROW_H;
        g_weekScroll -= (delta > 0 ? step : -step);
        WeekClampScroll(h);
        WeekUpdateScrollBar(h);
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }

    case WM_VSCROLL:
    {
        SCROLLINFO si;
        ZeroMemory(&si, sizeof(si));
        si.cbSize = sizeof(si);
        si.fMask = SIF_ALL;
        GetScrollInfo(h, SB_VERT, &si);
        int oldPos = si.nPos;
        switch (LOWORD(wParam))
        {
        case SB_LINEUP:      si.nPos -= S(28); break;
        case SB_LINEDOWN:    si.nPos += S(28); break;
        case SB_PAGEUP:      si.nPos -= WEEK_ROW_H * 2; break;
        case SB_PAGEDOWN:    si.nPos += WEEK_ROW_H * 2; break;
        case SB_THUMBTRACK:  si.nPos = si.nTrackPos; break;
        default: break;
        }
        si.fMask = SIF_POS;
        SetScrollInfo(h, SB_VERT, &si, TRUE);
        GetScrollInfo(h, SB_VERT, &si);
        if (si.nPos != oldPos)
        {
            g_weekScroll = si.nPos;
            WeekClampScroll(h);
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        POINT pt;
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);
        RECT br;
        int idx = WeekHitTest(h, pt.x, pt.y, &br);
        if (idx > 0)
        {
            if (g_weekHoverTask != g_tasks[idx].id)
            {
                g_weekHoverTask = g_tasks[idx].id;
                POINT tl, brp;
                tl.x = br.left;
                tl.y = br.top;
                brp.x = br.right;
                brp.y = br.bottom;
                ClientToScreen(h, &tl);
                ClientToScreen(h, &brp);
                SetRect(&br, tl.x, tl.y, brp.x, brp.y);
                wchar_t tip[700];
                BuildBlockTip(&g_tasks[idx], tip, 700);
                ShowHoverTip(tip, &br, 1, g_catFill[g_tasks[idx].category]);
            }
            SetCursor(LoadCursorW(NULL, IDC_HAND));
        }
        else
        {
            if (g_weekHoverTask != 0)
            {
                HideHoverTip();
            }
            SetCursor(LoadCursorW(NULL, IDC_ARROW));
        }
        if (!g_weekTracking)
        {
            TRACKMOUSEEVENT tme;
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = h;
            TrackMouseEvent(&tme);
            g_weekTracking = 1;
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        g_weekTracking = 0;
        HideHoverTip();
        return 0;

    case WM_LBUTTONDOWN:
    {
        POINT pt;
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);
        RECT br;
        int idx = WeekHitTest(h, pt.x, pt.y, &br);
        if (idx > 0)
        {
            g_weekSelId = g_tasks[idx].id;
            HideHoverTip();
        }
        else
        {
            g_weekSelId = 0;      /* 点空白处取消选中 */
        }
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }

    case WM_LBUTTONDBLCLK:
    {
        POINT pt;
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);
        RECT br;
        int idx = WeekHitTest(h, pt.x, pt.y, &br);
        if (idx > 0)
        {
            /* 双击方块切到月视图并选中该日 */
            g_calSel = g_tasks[idx].stStart;
            g_calYear = g_calSel.wYear;
            g_calMonth = g_calSel.wMonth;
            g_filterMode = 1;
            g_filterDate = g_calSel;
            SetViewMode(VIEW_MONTH);
            RefreshList();
            RefreshCalendar();
        }
        return 0;
    }
    }
    return DefWindowProcW(h, message, wParam, lParam);
}

/*============================= 语言切换 =============================*/

static void SetChildText(HWND parent, int id, int strId)
{
    HWND child = GetDlgItem(parent, id);
    if (child != NULL)
    {
        SetWindowTextW(child, Str(strId));
    }
}

/* 应用界面风格：只改颜色，不重建字体/控件 */
void ApplyTheme(void)
{
    if (g_set.theme < 0 || g_set.theme >= THEME_COUNT)
    {
        g_set.theme = THEME_DEFAULT;
    }
    g_theme = &g_themes[g_set.theme];

    HBRUSH oldBg = g_hBgBrush;
    g_hBgBrush = CreateSolidBrush(g_theme->page);
    if (g_hMain != NULL)
    {
        SetClassLongPtrW(g_hMain, GCLP_HBRBACKGROUND, (LONG_PTR)g_hBgBrush);
    }
    if (oldBg != NULL)
    {
        DeleteObject(oldBg);
    }

    HBRUSH oldDlg = g_hDlgBrush;
    g_hDlgBrush = CreateSolidBrush(g_theme->dialogBg);
    if (oldDlg != NULL)
    {
        DeleteObject(oldDlg);
    }

    HBRUSH oldCard = g_hCardBrush;
    g_hCardBrush = CreateSolidBrush(g_theme->card);
    if (oldCard != NULL)
    {
        DeleteObject(oldCard);
    }

    if (g_hMain == NULL)
    {
        return;
    }

    if (g_hList != NULL)
    {
        InvalidateRect(g_hList, NULL, TRUE);
    }

    InvalidateRect(g_hMain, NULL, TRUE);
    if (g_hCal != NULL) { InvalidateRect(g_hCal, NULL, TRUE); }
    if (g_hWeek != NULL) { InvalidateRect(g_hWeek, NULL, TRUE); }
    if (g_hStatus != NULL) { InvalidateRect(g_hStatus, NULL, TRUE); }
    if (g_hTip != NULL) { InvalidateRect(g_hTip, NULL, TRUE); }
    if (g_hStatToday != NULL) { InvalidateRect(g_hStatToday, NULL, TRUE); }
    if (g_hChkAuto != NULL) { InvalidateRect(g_hChkAuto, NULL, TRUE); }
    if (g_hTip2 != NULL) { InvalidateRect(g_hTip2, NULL, TRUE); }
    for (int i = 1; i <= MAX_TOASTS; i++)
    {
        if (g_toasts[i].hwnd != NULL)
        {
            InvalidateRect(g_toasts[i].hwnd, NULL, TRUE);
        }
    }
    RedrawWindow(g_hMain, NULL, NULL,
        RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void ApplyLanguage(void)
{
    if (g_set.lang < 0 || g_set.lang >= LANG_COUNT)
    {
        g_set.lang = DetectSystemLang();
    }
    g_lang = g_set.lang;
    CreateAppFonts();

    if (g_hMain == NULL)
    {
        return;
    }

    /* 先换字体再改文字，避免控件短暂持有已销毁字体 */
    EnumChildWindows(g_hMain, SetFontToChild, 0);

    SetWindowTextW(g_hMain, Str(STR_APP_TITLE));
    SetChildText(g_hMain, IDC_BTN_ADD, STR_BTN_ADD);
    SetChildText(g_hMain, IDC_BTN_DELETE, STR_BTN_DELETE);
    UpdateDeleteButton();
    SetChildText(g_hMain, IDC_BTN_SET, STR_BTN_SET);
    SetChildText(g_hMain, IDC_VIEW_MONTH, STR_VIEW_MONTH);
    SetChildText(g_hMain, IDC_VIEW_WEEK, STR_VIEW_WEEK);
    SetChildText(g_hMain, IDC_WEEK_PREV, STR_WEEK_PREV);
    SetChildText(g_hMain, IDC_WEEK_NEXT, STR_WEEK_NEXT);
    SetChildText(g_hMain, IDC_WEEK_TODAY, STR_WEEK_TODAY);
    if (g_hBtnViewMonth != NULL)
    {
        InvalidateRect(g_hBtnViewMonth, NULL, TRUE);
    }
    if (g_hBtnViewWeek != NULL)
    {
        InvalidateRect(g_hBtnViewWeek, NULL, TRUE);
    }
    if (g_hChkAuto != NULL)
    {
        SetWindowTextW(g_hChkAuto, Str(STR_CHK_AUTOSTART));
    }
    if (g_hTip != NULL)
    {
        SetWindowTextW(g_hTip, Str(STR_TIP));
    }

    RefreshList();
    RefreshCalendar();
    RefreshWeek();
    TrayUpdateTip();
}

/*============================= 主窗口 =============================*/

static BOOL CALLBACK SetFontToChild(HWND hChild, LPARAM lParam)
{
    (void)lParam;
    int id = GetDlgCtrlID(hChild);
    HFONT font = g_fontUi;
    if (id == IDC_STATUS || id == IDC_TIP || id == IDC_STAT_TODAY ||
        id == IDC_ADD_HINT || id == IDC_SET_HINT ||
        id == IDC_SET_CREDITS || id == IDC_SET_EMAIL)
    {
        font = g_fontStatus;
    }
    SendMessageW(hChild, WM_SETFONT, (WPARAM)font, TRUE);
    return TRUE;
}

static int Main_OnCreate(HWND hWnd)
{
    CreateAppFonts();

    SYSTEMTIME today;
    GetLocalTime(&today);
    g_calSel = today;
    g_calYear = today.wYear;
    g_calMonth = today.wMonth;
    g_weekAnchor = today;
    g_lastToday = today;

    int tb = S(8);
    int bh = S(34);

    /* 顶部工具栏：左侧月视图 / 周视图切换（分段按钮） */
    g_hBtnViewMonth = CreateWindowExW(0, L"BUTTON", Str(STR_VIEW_MONTH),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        S(12), tb, S(92), bh,
        hWnd, (HMENU)(LONG_PTR)IDC_VIEW_MONTH, g_hInst, NULL);
    g_hBtnViewWeek = CreateWindowExW(0, L"BUTTON", Str(STR_VIEW_WEEK),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        S(106), tb, S(92), bh,
        hWnd, (HMENU)(LONG_PTR)IDC_VIEW_WEEK, g_hInst, NULL);

    /* 周导航（仅周视图可见） */
    CreateWindowExW(0, L"BUTTON", Str(STR_WEEK_PREV),
        WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
        S(212), tb, S(38), bh,
        hWnd, (HMENU)(LONG_PTR)IDC_WEEK_PREV, g_hInst, NULL);
    g_hWeekTitle = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | SS_CENTER | SS_CENTERIMAGE,
        S(252), tb, S(150), bh,
        hWnd, (HMENU)(LONG_PTR)IDC_WEEK_TITLE, g_hInst, NULL);
    CreateWindowExW(0, L"BUTTON", Str(STR_WEEK_NEXT),
        WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
        S(404), tb, S(38), bh,
        hWnd, (HMENU)(LONG_PTR)IDC_WEEK_NEXT, g_hInst, NULL);
    CreateWindowExW(0, L"BUTTON", Str(STR_WEEK_TODAY),
        WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
        S(444), tb, S(78), bh,
        hWnd, (HMENU)(LONG_PTR)IDC_WEEK_TODAY, g_hInst, NULL);

    /* 顶部右侧：添加 / 删除 / 设置 */
    CreateWindowExW(0, L"BUTTON", Str(STR_BTN_SET),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        S(900), tb, S(84), bh,
        hWnd, (HMENU)(LONG_PTR)IDC_BTN_SET, g_hInst, NULL);
    CreateWindowExW(0, L"BUTTON", Str(STR_BTN_DELETE),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        S(790), tb, S(102), bh,
        hWnd, (HMENU)(LONG_PTR)IDC_BTN_DELETE, g_hInst, NULL);
    CreateWindowExW(0, L"BUTTON", Str(STR_BTN_ADD),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        S(680), tb, S(102), bh,
        hWnd, (HMENU)(LONG_PTR)IDC_BTN_ADD, g_hInst, NULL);

    /* 左侧自绘日历 */
    g_hCal = CreateWindowExW(0, L"TimeManagerCalendar", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        S(10), S(54), CAL_WIDTH, CAL_HEIGHT,
        hWnd, (HMENU)(LONG_PTR)IDC_CAL, g_hInst, NULL);

    g_hStatToday = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
        S(12), S(54) + CAL_HEIGHT + S(10), CAL_WIDTH, S(24),
        hWnd, (HMENU)(LONG_PTR)IDC_STAT_TODAY, g_hInst, NULL);

    g_hChkAuto = CreateWindowExW(0, L"BUTTON", Str(STR_CHK_AUTOSTART),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        S(12), S(54) + CAL_HEIGHT + S(40), CAL_WIDTH, S(28),
        hWnd, (HMENU)(LONG_PTR)IDC_CHK_AUTO, g_hInst, NULL);
    Button_SetCheck(g_hChkAuto, IsAutoRun() ? BST_CHECKED : BST_UNCHECKED);

    /* 左右分隔竖线 */
    CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_ETCHEDVERT,
        S(366), S(54), 1, S(600),
        hWnd, (HMENU)(LONG_PTR)IDC_SEPARATOR, g_hInst, NULL);

    /* 右侧：月视图活动流（自绘，无表头） */
    g_hList = CreateWindowExW(0, L"TimeManagerAgenda", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP,
        S(374), S(54), S(600), S(400),
        hWnd, (HMENU)(LONG_PTR)IDC_LIST, g_hInst, NULL);

    /* 右侧：周视图时间网格（初始隐藏） */
    g_hWeek = CreateWindowExW(0, L"TimeManagerWeek", L"",
        WS_CHILD | WS_VSCROLL | WS_CLIPSIBLINGS,
        S(374), S(54), S(600), S(400),
        hWnd, (HMENU)(LONG_PTR)IDC_WEEK, g_hInst, NULL);

    /* 底部状态栏 */
    g_hStatus = CreateWindowExW(WS_EX_STATICEDGE, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
        S(8), S(720), S(900), S(28),
        hWnd, (HMENU)(LONG_PTR)IDC_STATUS, g_hInst, NULL);

    EnumChildWindows(hWnd, SetFontToChild, 0);

    LoadTasks();
    RefreshList();
    RefreshCalendar();
    RefreshWeek();
    SetViewMode(VIEW_MONTH);

    SetTimer(hWnd, TIMER_CHECK, 1000, NULL);
    return 0;
}

/* 切换月视图 / 周视图 */
void SetViewMode(int mode)
{
    int prevMode = g_viewMode;
    g_viewMode = mode;
    g_weekSelId = 0;     /* 切换视图时清除周视图选中 */
    if (g_hBtnViewMonth != NULL)
    {
        InvalidateRect(g_hBtnViewMonth, NULL, TRUE);
    }
    if (g_hBtnViewWeek != NULL)
    {
        InvalidateRect(g_hBtnViewWeek, NULL, TRUE);
    }

    int nav = (mode == VIEW_WEEK) ? SW_SHOW : SW_HIDE;
    HWND navs[4];
    navs[0] = GetDlgItem(g_hMain, IDC_WEEK_PREV);
    navs[1] = GetDlgItem(g_hMain, IDC_WEEK_NEXT);
    navs[2] = GetDlgItem(g_hMain, IDC_WEEK_TODAY);
    navs[3] = g_hWeekTitle;
    for (int i = 0; i < 4; i++)
    {
        if (navs[i] != NULL)
        {
            ShowWindow(navs[i], nav);
        }
    }

    if (g_hWeek != NULL)
    {
        ShowWindow(g_hWeek, mode == VIEW_WEEK ? SW_SHOW : SW_HIDE);
    }
    if (g_hList != NULL)
    {
        ShowWindow(g_hList, mode == VIEW_MONTH ? SW_SHOW : SW_HIDE);
    }

    if (g_hMain != NULL)
    {
        RECT rc;
        GetClientRect(g_hMain, &rc);
        Main_OnSize(g_hMain, rc.right, rc.bottom);
    }

    if (mode == VIEW_WEEK)
    {
        /* 从月视图进入时，周视图跟随日历里选中的日期（默认是今天） */
        if (prevMode != VIEW_WEEK && g_calSel.wYear >= 1601)
        {
            g_weekAnchor = g_calSel;
        }
        if (g_hWeek != NULL)
        {
            g_weekScroll = WeekDefaultScroll();
            WeekClampScroll(g_hWeek);
        }
        RefreshWeek();
    }
    else
    {
        RefreshList();
    }
    UpdateDeleteButton();
}

static void Main_OnSize(HWND hWnd, int W, int H)
{
    if (g_hList == NULL)
    {
        return;
    }

    int top = S(50);
    int bottom = H - S(30);
    int leftW = CAL_WIDTH + S(2);
    int sepX = leftW + S(8);
    int rightX = sepX + S(8);
    int rightW = W - rightX - S(10);

    /* 顶部右侧按钮组：设置 / 删除 / 添加，右对齐 */
    int bh = S(34);
    int tb = S(8);
    int x = W - S(12);
    HWND bSet = GetDlgItem(hWnd, IDC_BTN_SET);
    HWND bDel = GetDlgItem(hWnd, IDC_BTN_DELETE);
    HWND bAdd = GetDlgItem(hWnd, IDC_BTN_ADD);
    x -= S(84); MoveWindow(bSet, x, tb, S(84), bh, TRUE);
    x -= S(102) + S(8); MoveWindow(bDel, x, tb, S(102), bh, TRUE);
    x -= S(102) + S(8); MoveWindow(bAdd, x, tb, S(102), bh, TRUE);

    /* 左栏 */
    MoveWindow(g_hCal, S(10), top, leftW, CAL_HEIGHT, TRUE);
    MoveWindow(g_hStatToday, S(12), top + CAL_HEIGHT + S(10), leftW, S(24), TRUE);
    MoveWindow(g_hChkAuto, S(12), top + CAL_HEIGHT + S(40), leftW, S(28), TRUE);

    HWND sep = GetDlgItem(hWnd, IDC_SEPARATOR);
    MoveWindow(sep, sepX, top, 1, bottom - top, TRUE);

    /* 右栏：列表与周视图同位置切换 */
    int rTop = top;
    int rH = bottom - rTop;
    MoveWindow(g_hList, rightX, rTop, rightW, rH, TRUE);
    MoveWindow(g_hWeek, rightX, rTop, rightW, rH, TRUE);
    AgendaRelayout(g_hList);

    MoveWindow(g_hStatus, S(8), H - S(28), W - S(16), S(28), TRUE);
}

/*========================= 活动流自绘 =========================*/

/* 月视图右侧不再是五列表格，而是按日期分组的一列日程：
   分组标题一行，每个日程一行（时间段 · 分类色点 · 标题 · 备注），
   提醒次数等完整信息收进悬停气泡；点击选中 / Ctrl 加选 / Shift 连选，
   选中集合与“删除选中”按钮、Delete 键共用。 */

#define AG_GROUP 0
#define AG_ITEM  1

/* 分组标题文本：“10/26 一” */
static void FmtAgendaDate(const SYSTEMTIME* st, wchar_t* buf, int cch)
{
    StringCchPrintfW(buf, cch, L"%d/%d  %s", st->wMonth, st->wDay,
        g_weekShort[g_lang][st->wDayOfWeek]);
}

/* 时间段文本：“09:30 – 10:30” */
static void FmtAgendaTime(const SYSTEMTIME* s, const SYSTEMTIME* e, wchar_t* buf, int cch)
{
    wchar_t t1[16], t2[16];
    FmtTime(s, t1, 16);
    FmtTime(e, t2, 16);
    StringCchPrintfW(buf, cch, L"%s – %s", t1, t2);
}

/* 分组标题：今天的日期用强调色，右侧弱化显示当天日程数 */
static void DrawAgendaHeader(HDC dc, struct AgItem* it, int W)
{
    struct AgGroup* g = &g_agGroups[it->group];
    SYSTEMTIME today;
    GetLocalTime(&today);
    int isToday = SameDate(&g->date, &today);

    wchar_t date[64];
    FmtAgendaDate(&g->date, date, 64);

    RECT r = it->rc;
    r.left = AG_PAD_X;
    r.right = W - AG_PAD_X;

    int x = AG_PAD_X;
    HGDIOBJ of = SelectObject(dc, g_fontBold);
    if (isToday)
    {
        const wchar_t* label = Str(STR_AGENDA_TODAY);
        SIZE sz;
        GetTextExtentPoint32W(dc, label, lstrlenW(label), &sz);
        RECT tr;
        tr.left = x;
        tr.top = r.top;
        tr.right = x + sz.cx + S(2);
        tr.bottom = r.bottom;
        SetTextColor(dc, g_theme->accent);
        DrawTextW(dc, label, -1, &tr,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        x += sz.cx + S(8);
    }

    {
        SIZE sz;
        GetTextExtentPoint32W(dc, date, lstrlenW(date), &sz);
        RECT tr;
        tr.left = x;
        tr.top = r.top;
        tr.right = x + sz.cx + S(2);
        tr.bottom = r.bottom;
        SetTextColor(dc, g_theme->text);
        DrawTextW(dc, date, -1, &tr,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    SelectObject(dc, of);

    if (g->count > 0)
    {
        wchar_t cnt[48];
        FormatStr(cnt, 48, STR_DAY_EVENT_COUNT, g->count);
        of = SelectObject(dc, g_fontStatus);
        SetTextColor(dc, CAL_MUTED);
        RECT cr = r;
        cr.right = W - AG_PAD_X;
        DrawTextW(dc, cnt, -1, &cr,
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, of);
    }
}

/* 单行日程：时间（弱化）→ 标题（正常字重）→ 备注（小号淡色），
   彩色圆点代替“分类”列，选中行铺主题浅色底，悬停行铺浅色底 */
static void DrawAgendaRow(HDC dc, struct AgItem* it, int hovered)
{
    struct Task* t = &g_tasks[it->taskIndex];
    RECT r = it->rc;
    int cy = (r.top + r.bottom) / 2;
    const int textLeft = AG_PAD_X + S(16);

    if (AgIsSelected(t->id))
    {
        FillRoundedRect(dc, &r, g_theme->accentSoft);
    }
    else if (hovered)
    {
        FillRoundedRect(dc, &r, g_theme->hover);
    }

    int cat = t->category;
    if (cat < 1 || cat > CAT_COUNT)
    {
        cat = CAT_OTHER;
    }
    FillDot(dc, AG_PAD_X + S(5), cy, S(3), g_catFill[cat]);

    wchar_t tm[40];
    FmtAgendaTime(&t->stStart, &t->stEnd, tm, 40);
    RECT tr;
    tr.left = textLeft;
    tr.top = r.top;
    tr.right = textLeft + S(118);
    tr.bottom = r.bottom;
    HGDIOBJ of = SelectObject(dc, g_fontStatus);
    SetTextColor(dc, CAL_MUTED);
    DrawTextW(dc, tm, -1, &tr,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, of);

    RECT nr;
    nr.left = tr.right + S(10);
    nr.top = r.top;
    nr.right = r.right - S(6);
    nr.bottom = r.bottom;
    if (nr.right <= nr.left)
    {
        return;
    }

    int titleW = 0;
    if (t->title[0] != L'\0')
    {
        int maxTitle = (int)((long)(nr.right - nr.left) * 62 / 100);
        SIZE sz;
        of = SelectObject(dc, g_fontUi);
        SetTextColor(dc, g_theme->text);
        GetTextExtentPoint32W(dc, t->title, lstrlenW(t->title), &sz);
        titleW = sz.cx;
        if (titleW > maxTitle)
        {
            titleW = maxTitle;
        }
        RECT dr = nr;
        dr.right = dr.left + titleW + S(2);
        if (dr.right > nr.right)
        {
            dr.right = nr.right;
        }
        DrawTextW(dc, t->title, -1, &dr,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(dc, of);
    }

    if (t->note[0] != L'\0')
    {
        int nx = nr.left + (titleW > 0 ? titleW + S(10) : 0);
        if (nx < nr.right - S(10))
        {
            RECT an = nr;
            an.left = nx;
            of = SelectObject(dc, g_fontStatus);
            SetTextColor(dc, CAL_MUTED);
            DrawTextW(dc, t->note, -1, &an,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            SelectObject(dc, of);
        }
    }
}

/* 活动流整体绘制：双缓冲，按滚动量把内容坐标换算到窗口坐标 */
static void AgendaPaint(HWND h, HDC hdcWin)
{
    RECT client;
    GetClientRect(h, &client);
    int W = client.right;
    int H = client.bottom;
    if (W <= 0 || H <= 0)
    {
        return;
    }

    HDC dc = CreateCompatibleDC(hdcWin);
    HBITMAP bmp = CreateCompatibleBitmap(hdcWin, W, H);
    HGDIOBJ oldBmp = SelectObject(dc, bmp);

    HBRUSH bg = CreateSolidBrush(g_theme->card);
    FillRect(dc, &client, bg);
    DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT);

    if (g_agCount < 1)
    {
        HGDIOBJ of = SelectObject(dc, g_fontBold);
        SetTextColor(dc, g_theme->text);
        RECT r = client;
        r.top = H / 2 - S(34);
        r.bottom = r.top + S(28);
        DrawTextW(dc, Str(STR_AGENDA_EMPTY), -1, &r,
            DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, of);

        if (g_filterMode != 0)
        {
            of = SelectObject(dc, g_fontStatus);
            SetTextColor(dc, CAL_MUTED);
            r.top += S(28);
            r.bottom = r.top + S(24);
            DrawTextW(dc, Str(STR_AGENDA_EMPTY_DAY), -1, &r,
                DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(dc, of);
        }
    }
    else
    {
        for (int i = 0; i < g_agCount; i++)
        {
            struct AgItem* it = &g_ag[i];
            int top = it->rc.top - g_agScroll;
            int bottom = it->rc.bottom - g_agScroll;
            if (bottom <= 0 || top >= H)
            {
                continue;
            }

            RECT saved = it->rc;
            it->rc.top = top;
            it->rc.bottom = bottom;
            if (it->kind == AG_GROUP)
            {
                DrawAgendaHeader(dc, it, W);
            }
            else
            {
                DrawAgendaRow(dc, it, i == g_agHover);
            }
            it->rc = saved;
        }
    }

    BitBlt(hdcWin, 0, 0, W, H, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
}

/* 键盘移动时把目标行滚进可视区域 */
static void AgEnsureVisible(HWND h, int contentTop, int contentBottom)
{
    RECT rc;
    GetClientRect(h, &rc);
    int page = rc.bottom;
    if (page <= 0)
    {
        return;
    }
    if (contentTop - g_agScroll < 0)
    {
        g_agScroll = contentTop - S(6);
    }
    else if (contentBottom - g_agScroll > page)
    {
        g_agScroll = contentBottom - page + S(6);
    }
    AgClampScroll(h);
    AgUpdateScrollBar(h);
}

/* 选中某个条目下标（同时更新 Shift 锚点与悬停） */
static void AgSelectIndex(HWND h, int idx)
{
    if (idx < 0 || idx >= g_agCount || g_ag[idx].kind != AG_ITEM)
    {
        return;
    }
    AgClearSelection();
    AgAddSelected(g_tasks[g_ag[idx].taskIndex].id);
    g_agAnchor = idx;
    g_agHover = idx;
    AgEnsureVisible(h, g_ag[idx].rc.top, g_ag[idx].rc.bottom);
    UpdateDeleteButton();
    InvalidateRect(h, NULL, FALSE);
}

/* 上下方向键：跨过分组标题，落到相邻日程行 */
static void AgMoveSelection(HWND h, int dir)
{
    if (g_agCount < 1 || dir == 0)
    {
        return;
    }

    int cur = -1;
    for (int i = 0; i < g_agCount; i++)
    {
        if (g_ag[i].kind == AG_ITEM && AgIsSelected(g_tasks[g_ag[i].taskIndex].id))
        {
            cur = i;
            break;
        }
    }

    int idx = cur;
    if (cur < 0)
    {
        idx = (dir > 0) ? -1 : g_agCount;
    }
    for (;;)
    {
        idx += (dir > 0) ? 1 : -1;
        if (idx < 0 || idx >= g_agCount)
        {
            return;
        }
        if (g_ag[idx].kind == AG_ITEM)
        {
            AgSelectIndex(h, idx);
            return;
        }
    }
}

static LRESULT CALLBACK AgendaWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        AgendaPaint(h, dc);
        EndPaint(h, &ps);
        return 0;
    }

    case WM_SIZE:
        AgendaRelayout(h);
        return 0;

    case WM_MOUSEWHEEL:
    {
        RECT rc;
        GetClientRect(h, &rc);
        int page = rc.bottom;
        int old = g_agScroll;
        g_agScroll -= (int)(GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA) * S(56);
        AgClampScroll(h);
        AgUpdateScrollBar(h);
        if (g_agScroll != old)
        {
            InvalidateRect(h, NULL, FALSE);
            HideHoverTip();
        }
        return 0;
    }

    case WM_VSCROLL:
    {
        RECT rc;
        GetClientRect(h, &rc);
        int page = rc.bottom;
        int old = g_agScroll;
        int step = S(56);
        switch (LOWORD(wp))
        {
        case SB_LINEUP:
            g_agScroll -= step;
            break;
        case SB_LINEDOWN:
            g_agScroll += step;
            break;
        case SB_PAGEUP:
            g_agScroll -= (page - step);
            break;
        case SB_PAGEDOWN:
            g_agScroll += (page - step);
            break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION:
            g_agScroll = (int)HIWORD(wp);
            break;
        case SB_TOP:
            g_agScroll = 0;
            break;
        case SB_BOTTOM:
            g_agScroll = g_agContentH;
            break;
        default:
            return 0;
        }
        AgClampScroll(h);
        AgUpdateScrollBar(h);
        if (g_agScroll != old)
        {
            InvalidateRect(h, NULL, FALSE);
            HideHoverTip();
        }
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        int x = GET_X_LPARAM(lp);
        int y = GET_Y_LPARAM(lp);
        int hit = AgendaHitTest(x, y, NULL);
        if (hit != g_agHover)
        {
            g_agHover = hit;
            InvalidateRect(h, NULL, FALSE);
        }
        if (!g_agTracking)
        {
            TRACKMOUSEEVENT tme;
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = h;
            tme.dwHoverTime = 0;
            TrackMouseEvent(&tme);
            g_agTracking = TRUE;
        }

        if (hit >= 0 && g_ag[hit].kind == AG_ITEM)
        {
            struct Task* t = &g_tasks[g_ag[hit].taskIndex];
            wchar_t tip[512];
            RECT avoid;
            RECT wr;
            BuildBlockTip(t, tip, 512);
            GetWindowRect(h, &wr);
            avoid.left = wr.left + g_ag[hit].rc.left;
            avoid.top = wr.top + g_ag[hit].rc.top - g_agScroll;
            avoid.right = wr.left + g_ag[hit].rc.right;
            avoid.bottom = wr.top + g_ag[hit].rc.bottom - g_agScroll;
            ShowHoverTip(tip, &avoid, 1, g_catFill[t->category]);
        }
        else
        {
            HideHoverTip();
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        g_agTracking = FALSE;
        if (g_agHover != -1)
        {
            g_agHover = -1;
            InvalidateRect(h, NULL, FALSE);
        }
        HideHoverTip();
        return 0;

    case WM_LBUTTONDOWN:
    {
        SetFocus(h);
        int x = GET_X_LPARAM(lp);
        int y = GET_Y_LPARAM(lp);
        int hit = AgendaHitTest(x, y, NULL);
        if (hit < 0 || g_ag[hit].kind != AG_ITEM)
        {
            /* 点空白处或分组标题：与列表控件一致，取消选择 */
            if (AgSelectionCount() > 0)
            {
                AgClearSelection();
                InvalidateRect(h, NULL, FALSE);
                UpdateDeleteButton();
            }
            return 0;
        }
        struct Task* t = &g_tasks[g_ag[hit].taskIndex];
        BOOL ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        BOOL shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

        if (ctrl)
        {
            if (AgIsSelected(t->id))
            {
                AgRemoveSelected(t->id);
            }
            else
            {
                AgAddSelected(t->id);
            }
            g_agAnchor = hit;
        }
        else if (shift && g_agAnchor >= 0)
        {
            AgClearSelection();
            int lo = (hit < g_agAnchor) ? hit : g_agAnchor;
            int hi = (hit < g_agAnchor) ? g_agAnchor : hit;
            for (int i = lo; i <= hi; i++)
            {
                if (g_ag[i].kind == AG_ITEM)
                {
                    AgAddSelected(g_tasks[g_ag[i].taskIndex].id);
                }
            }
        }
        else
        {
            AgClearSelection();
            AgAddSelected(t->id);
            g_agAnchor = hit;
        }
        InvalidateRect(h, NULL, FALSE);
        UpdateDeleteButton();
        return 0;
    }

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE)
        {
            AgClearSelection();
            InvalidateRect(h, NULL, FALSE);
            UpdateDeleteButton();
            return 0;
        }
        if (wp == VK_DELETE)
        {
            DeleteSelected();
            return 0;
        }
        if (wp == 'A' && (GetKeyState(VK_CONTROL) & 0x8000))
        {
            AgSelectAll();
            for (int i = 0; i < g_agCount; i++)
            {
                if (g_ag[i].kind == AG_ITEM)
                {
                    g_agAnchor = i;
                    break;
                }
            }
            InvalidateRect(h, NULL, FALSE);
            UpdateDeleteButton();
            return 0;
        }
        if (wp == VK_UP)
        {
            AgMoveSelection(h, -1);
            return 0;
        }
        if (wp == VK_DOWN)
        {
            AgMoveSelection(h, 1);
            return 0;
        }
        if (wp == VK_HOME)
        {
            for (int i = 0; i < g_agCount; i++)
            {
                if (g_ag[i].kind == AG_ITEM)
                {
                    AgSelectIndex(h, i);
                    return 0;
                }
            }
            return 0;
        }
        if (wp == VK_END)
        {
            for (int i = g_agCount - 1; i >= 0; i--)
            {
                if (g_ag[i].kind == AG_ITEM)
                {
                    AgSelectIndex(h, i);
                    return 0;
                }
            }
            return 0;
        }
        return 0;

    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTCHARS;

    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

/* 月视图 / 周视图分段按钮：跟随主题自绘 */
static int DrawViewToggle(LPDRAWITEMSTRUCT dis)
{
    if (dis == NULL)
    {
        return 0;
    }
    if (dis->CtlID != IDC_VIEW_MONTH && dis->CtlID != IDC_VIEW_WEEK)
    {
        return 0;
    }

    int selected = (dis->CtlID == IDC_VIEW_MONTH) ?
        (g_viewMode == VIEW_MONTH) : (g_viewMode == VIEW_WEEK);
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;

    /* 先铺页面底色，圆角外不会留下旧像素 */
    HBRUSH bg = CreateSolidBrush(g_theme->page);
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    RECT r = rc;
    InflateRect(&r, -S(1), -S(1));
    int pressed = (dis->itemState & ODS_SELECTED) ? 1 : 0;
    COLORREF fill = selected ? g_theme->accent : g_theme->card;
    COLORREF border = selected ? g_theme->accent : g_theme->border;
    COLORREF txt = selected ? RGB(255, 255, 255) : g_theme->text;
    if (pressed)
    {
        fill = selected ? g_theme->accentDark : g_theme->hover;
    }
    FillRoundedRect(hdc, &r, fill);
    DrawRoundBorder(hdc, &r, border, 0);

    if (dis->itemState & ODS_FOCUS)
    {
        RECT f = r;
        InflateRect(&f, -S(4), -S(4));
        HPEN pen = CreatePen(PS_DOT, 1, selected ? RGB(255, 255, 255) : g_theme->accent);
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        RoundRect(hdc, f.left, f.top, f.right, f.bottom, S(6), S(6));
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
    }

    wchar_t text[64];
    GetWindowTextW(dis->hwndItem, text, 64);
    SetBkMode(hdc, TRANSPARENT);
    HGDIOBJ oldFont = SelectObject(hdc, g_fontUi);
    SetTextColor(hdc, txt);
    DrawTextW(hdc, text, -1, &r,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(hdc, oldFont);
    return 1;
}

LRESULT CALLBACK MainWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        return Main_OnCreate(hWnd);

    case WM_SIZE:
        Main_OnSize(hWnd, LOWORD(lParam), HIWORD(lParam));
        return 0;

    case WM_GETMINMAXINFO:
    {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = S(1020);
        mmi->ptMinTrackSize.y = S(620);
        return 0;
    }

    case WM_ERASEBKGND:
    {
        RECT rc;
        GetClientRect(hWnd, &rc);
        FillRect((HDC)wParam, &rc, g_hBgBrush);
        return 1;
    }

    case WM_CTLCOLORSTATIC:
    {
        int id = GetDlgCtrlID((HWND)lParam);
        SetBkMode((HDC)wParam, TRANSPARENT);
        if (id == IDC_TIP || id == IDC_STATUS)
        {
            SetTextColor((HDC)wParam, g_theme->muted);
        }
        else
        {
            SetTextColor((HDC)wParam, g_theme->text);
        }
        return (LRESULT)g_hBgBrush;
    }

    case WM_CTLCOLORBTN:
    {
        if (GetDlgCtrlID((HWND)lParam) == IDC_CHK_AUTO)
        {
            SetBkMode((HDC)wParam, TRANSPARENT);
            SetTextColor((HDC)wParam, g_theme->text);
            return (LRESULT)g_hBgBrush;
        }
        break;
    }

    case WM_DRAWITEM:
        if (DrawViewToggle((LPDRAWITEMSTRUCT)lParam))
        {
            return TRUE;
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_BTN_ADD:
            ShowAddDialog();
            break;
        case IDC_BTN_DELETE:
            DeleteSelected();
            break;
        case IDC_VIEW_MONTH:
            SetViewMode(VIEW_MONTH);
            break;
        case IDC_VIEW_WEEK:
            SetViewMode(VIEW_WEEK);
            break;
        case IDC_WEEK_PREV:
            WeekShift(-1);
            break;
        case IDC_WEEK_NEXT:
            WeekShift(1);
            break;
        case IDC_WEEK_TODAY:
            WeekShift(0);
            break;
        case IDC_BTN_SET:
            ShowSettingsDialog();
            break;
        case IDC_CHK_AUTO:
        {
            int checked = Button_GetCheck(g_hChkAuto) == BST_CHECKED;
            g_set.autostart = checked;
            SetAutoRun(checked);
            SaveSettings();
            break;
        }
        case ID_TRAY_SHOW:
            ShowMainWindow();
            break;
        case ID_TRAY_EXIT:
            g_realQuit = 1;
            SaveTasks();
            SaveSettings();
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            DestroyWindow(hWnd);
            break;
        }
        return 0;

    case WM_TIMER:
        if (wParam == TIMER_CHECK)
        {
            CheckReminders();

            /* 跨天时刷新“今天”相关显示 */
            SYSTEMTIME now;
            GetLocalTime(&now);
            if (now.wYear != g_lastToday.wYear ||
                now.wMonth != g_lastToday.wMonth ||
                now.wDay != g_lastToday.wDay)
            {
                g_lastToday = now;
                RefreshList();
                RefreshCalendar();
                RefreshWeek();
            }
        }
        return 0;

    case WM_APP_CAL_SELECT:
        if (g_viewMode == VIEW_WEEK)
        {
            /* 周视图下点击日历：跳转到该日期所在周 */
            g_weekAnchor = g_calSel;
            g_weekScroll = 0;
            RefreshWeek();
        }
        else
        {
            /* 月视图：再次点击已选中的同一天则取消筛选（显示全部） */
            if (g_filterMode == 1 &&
                g_filterDate.wYear == g_calSel.wYear &&
                g_filterDate.wMonth == g_calSel.wMonth &&
                g_filterDate.wDay == g_calSel.wDay)
            {
                g_filterMode = 0;
            }
            else
            {
                g_filterMode = 1;
                g_filterDate = g_calSel;
            }
            RefreshList();
        }
        return 0;

    case WM_APP_TRAY:
        if (lParam == WM_LBUTTONDBLCLK || lParam == WM_LBUTTONUP)
        {
            ShowMainWindow();
        }
        else if (lParam == WM_RBUTTONUP)
        {
            POINT pt;
            GetCursorPos(&pt);
            TrayMenu(pt);
        }
        return 0;

    case WM_CLOSE:
        if (!g_realQuit)
        {
            SaveTasks();
            SaveSettings();
            ShowWindow(hWnd, SW_HIDE);
            if (!g_trayHinted)
            {
                TrayBalloon(Str(STR_TRAY_BALLOON));
                g_trayHinted = 1;
            }
            return 0;
        }
        break;

    case WM_DESTROY:
        KillTimer(hWnd, TIMER_CHECK);
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        DestroyAppFonts();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, message, wParam, lParam);
}

/*============================= 入口 =============================*/

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
    LPWSTR lpCmdLine, int nCmdShow)
{
    (void)hPrevInstance;
    g_hInst = hInstance;

    /* DPI 感知 */
    SetProcessDPIAware();
    HDC hdcScreen = GetDC(NULL);
    if (hdcScreen != NULL)
    {
        g_dpi = GetDeviceCaps(hdcScreen, LOGPIXELSX);
        ReleaseDC(NULL, hdcScreen);
        if (g_dpi < 96)
        {
            g_dpi = 96;
        }
    }

    /* 先读取设置，以便错误提示与主界面使用已保存的语言 */
    InitPaths();
    LoadSettings();
    g_lang = g_set.lang;
    if (g_set.theme < 0 || g_set.theme >= THEME_COUNT)
    {
        g_set.theme = THEME_DEFAULT;
    }
    g_theme = &g_themes[g_set.theme];
    g_hBgBrush = CreateSolidBrush(g_theme->page);
    g_hDlgBrush = CreateSolidBrush(g_theme->dialogBg);
    g_hCardBrush = CreateSolidBrush(g_theme->card);

    /* 单实例 */
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"TimeManeger_SingleInstance_Mutex_v2");
    if (hMutex == NULL || GetLastError() == ERROR_ALREADY_EXISTS)
    {
        MessageBoxW(NULL, Str(STR_ALREADY_RUNNING),
            Str(STR_TOAST_APP), MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
        return 0;
    }

    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_DATE_CLASSES | ICC_STANDARD_CLASSES |
        ICC_LINK_CLASS;
    InitCommonControlsEx(&icc);

    g_set.autostart = IsAutoRun();

    /* 应用图标：标题栏、任务栏、托盘共用 */
    g_hIconLarge = LoadAppIcon(GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
    g_hIconSmall = LoadAppIcon(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    if (g_hBgBrush == NULL) { g_hBgBrush = CreateSolidBrush(g_theme->page); }
    wc.hbrBackground = g_hBgBrush;
    wc.lpszClassName = L"TimeManagerMainWnd";
    wc.hIcon = g_hIconLarge;
    wc.hIconSm = g_hIconSmall;
    RegisterClassExW(&wc);

    WNDCLASSEXW wcAdd;
    ZeroMemory(&wcAdd, sizeof(wcAdd));
    wcAdd.cbSize = sizeof(wcAdd);
    wcAdd.style = CS_HREDRAW | CS_VREDRAW;
    wcAdd.lpfnWndProc = AddDlgProc;
    wcAdd.hInstance = hInstance;
    wcAdd.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wcAdd.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wcAdd.lpszClassName = L"TimeManagerAddWnd";
    wcAdd.hIcon = g_hIconLarge;
    wcAdd.hIconSm = g_hIconSmall;
    RegisterClassExW(&wcAdd);

    WNDCLASSEXW wcSet;
    ZeroMemory(&wcSet, sizeof(wcSet));
    wcSet.cbSize = sizeof(wcSet);
    wcSet.style = CS_HREDRAW | CS_VREDRAW;
    wcSet.lpfnWndProc = SetDlgProc;
    wcSet.hInstance = hInstance;
    wcSet.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wcSet.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wcSet.lpszClassName = L"TimeManagerSetWnd";
    wcSet.hIcon = g_hIconLarge;
    wcSet.hIconSm = g_hIconSmall;
    RegisterClassExW(&wcSet);

    WNDCLASSEXW wcToast;
    ZeroMemory(&wcToast, sizeof(wcToast));
    wcToast.cbSize = sizeof(wcToast);
    wcToast.style = CS_HREDRAW | CS_VREDRAW;
    wcToast.lpfnWndProc = ToastProc;
    wcToast.hInstance = hInstance;
    wcToast.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wcToast.hbrBackground = NULL;
    wcToast.lpszClassName = L"TimeManagerToast";
    RegisterClassExW(&wcToast);

    WNDCLASSEXW wcCal;
    ZeroMemory(&wcCal, sizeof(wcCal));
    wcCal.cbSize = sizeof(wcCal);
    wcCal.style = CS_HREDRAW | CS_VREDRAW;
    wcCal.lpfnWndProc = CalWndProc;
    wcCal.hInstance = hInstance;
    wcCal.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wcCal.hbrBackground = NULL;
    wcCal.lpszClassName = L"TimeManagerCalendar";
    RegisterClassExW(&wcCal);

    WNDCLASSEXW wcWeek;
    ZeroMemory(&wcWeek, sizeof(wcWeek));
    wcWeek.cbSize = sizeof(wcWeek);
    wcWeek.style = CS_HREDRAW | CS_VREDRAW;
    wcWeek.lpfnWndProc = WeekWndProc;
    wcWeek.hInstance = hInstance;
    wcWeek.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wcWeek.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcWeek.lpszClassName = L"TimeManagerWeek";
    RegisterClassExW(&wcWeek);

    WNDCLASSEXW wcAgenda;
    ZeroMemory(&wcAgenda, sizeof(wcAgenda));
    wcAgenda.cbSize = sizeof(wcAgenda);
    wcAgenda.style = CS_HREDRAW | CS_VREDRAW;
    wcAgenda.lpfnWndProc = AgendaWndProc;
    wcAgenda.hInstance = hInstance;
    wcAgenda.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wcAgenda.hbrBackground = NULL;
    wcAgenda.lpszClassName = L"TimeManagerAgenda";
    RegisterClassExW(&wcAgenda);

    WNDCLASSEXW wcTip;
    ZeroMemory(&wcTip, sizeof(wcTip));
    wcTip.cbSize = sizeof(wcTip);
    wcTip.style = CS_HREDRAW | CS_VREDRAW;
    wcTip.lpfnWndProc = TipWndProc;
    wcTip.hInstance = hInstance;
    wcTip.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wcTip.hbrBackground = NULL;
    wcTip.lpszClassName = L"TimeManagerTip";
    RegisterClassExW(&wcTip);

    g_hMain = CreateWindowExW(
        0,
        L"TimeManagerMainWnd",
        Str(STR_APP_TITLE),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, S(1060), S(700),
        NULL, NULL, hInstance, NULL);
    if (g_hMain == NULL)
    {
        return 0;
    }

    /* 居中 */
    RECT rcWnd;
    GetWindowRect(g_hMain, &rcWnd);
    int wndW = rcWnd.right - rcWnd.left;
    int wndH = rcWnd.bottom - rcWnd.top;
    int scrW = GetSystemMetrics(SM_CXSCREEN);
    int scrH = GetSystemMetrics(SM_CYSCREEN);
    int posX = (scrW - wndW) / 2;
    int posY = (scrH - wndH) / 2;
    if (posX < 0) { posX = 0; }
    if (posY < 0) { posY = 0; }
    SetWindowPos(g_hMain, NULL, posX, posY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

    /* 托盘始终存在 */
    TrayAdd();

    /* --min 命令行参数或设置项要求最小化时，后台启动 */
    int hidden = g_set.minStart;
    if (lpCmdLine != NULL && wcsstr(lpCmdLine, L"--min") != NULL)
    {
        hidden = 1;
    }
    if (hidden)
    {
        ShowWindow(g_hMain, SW_HIDE);
    }
    else
    {
        ShowWindow(g_hMain, nCmdShow);
    }
    UpdateWindow(g_hMain);

    MSG msg;
    BOOL bRet;
    while ((bRet = GetMessageW(&msg, NULL, 0, 0)) != 0)
    {
        if (bRet == -1)
        {
            break;
        }
        if (!IsDialogMessageW(g_hMain, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return (int)msg.wParam;
}
