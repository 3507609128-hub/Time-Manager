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
    7. 日程与设置自动保存在程序目录（tasks.dat / settings.dat，UTF-8）。

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

/*============================= 数据结构 =============================*/

struct Reminder
{
    int minutes;        /* 提前多少分钟 */
    int fired;          /* 是否已弹过 */
};

struct Task
{
    int id;
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
static const wchar_t* g_fontFamily = L"Microsoft YaHei UI";
static int    g_dpi = 96;

static HWND   g_hMain = NULL;
static HWND   g_hList = NULL;
static HWND   g_hCal = NULL;
static HWND   g_hChkAuto = NULL;
static HWND   g_hStatus = NULL;
static HWND   g_hStatToday = NULL;
static HWND   g_hTip = NULL;

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
static HWND g_dlgRList = NULL, g_dlgRValue = NULL, g_dlgRUnit = NULL;
static int  g_dlgRMinutes[MAX_REMIND + 1];
static int  g_dlgRCount = 0;

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
    X(LIST_TITLE,      L"标题", L"タイトル", L"Title") \
    X(LIST_NOTE,       L"备注", L"メモ", L"Note") \
    X(LIST_REMIND,     L"提醒", L"通知", L"Remind") \
    X(REMIND_COUNT,    L"%d 次", L"%d 回", L"%d×") \
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
    X(NO_SELECTION,    L"请先在列表中单击选中要删除的日程。", L"削除する予定をリストから選択してください。", L"Select an event in the list first.") \
    X(CONFIRM_DELETE,  L"确定删除日程「%s」吗？此操作不可恢复。", L"予定「%s」を削除しますか？この操作は元に戻せません。", L"Delete \"%s\"? This cannot be undone.") \
    X(CONFIRM_DEL_TITLE, L"确认删除", L"削除の確認", L"Confirm deletion") \
    X(ADD_TITLE,       L"添加日程", L"予定を追加", L"Add event") \
    X(LBL_START,       L"开始：", L"開始：", L"Start:") \
    X(LBL_END,         L"结束：", L"終了：", L"End:") \
    X(LBL_TITLE,       L"标题：", L"タイトル：", L"Title:") \
    X(LBL_NOTE,        L"备注：", L"メモ：", L"Note:") \
    X(LBL_RULES,       L"提醒规则：", L"通知ルール：", L"Reminders:") \
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
    X(SET_LANG,        L"语言：", L"言語：", L"Language:") \
    X(LANG_ZH,         L"中文", L"中文", L"中文") \
    X(LANG_JA,         L"日本語", L"日本語", L"日本語") \
    X(LANG_EN,         L"English", L"English", L"English") \
    X(SET_AUTOSTART,   L"开机时自动启动（最小化到托盘）", L"ログイン時に自動起動（トレイに最小化）", L"Start automatically at login (minimized to tray)") \
    X(SET_MINSTART,    L"手动启动时也最小化到托盘", L"手動起動時もトレイに最小化", L"Also minimize to tray on manual start") \
    X(SET_DURATION,    L"通知停留：", L"通知の表示時間：", L"Show notification for:") \
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
    X(LEAD_MIN,        L"%d 分钟", L"%d分", L"%dm")

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

/*============================= 前向声明 =============================*/

LRESULT CALLBACK MainWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK AddDlgProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK SetDlgProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK ToastProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK CalWndProc(HWND, UINT, WPARAM, LPARAM);

void RefreshList(void);
void SaveTasks(void);
void LoadTasks(void);
void SaveSettings(void);
void LoadSettings(void);
void CheckReminders(void);
void DeleteSelected(void);
void QueueToast(const struct Task* t, int leadMinutes);
void ShowAddDialog(void);
void ShowSettingsDialog(void);
void ApplyLanguage(void);
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

static LONGLONG DiffMinutes(const SYSTEMTIME* now, const SYSTEMTIME* target)
{
    ULONGLONG a = SysTimeToU64(now);
    ULONGLONG b = SysTimeToU64(target);
    return (LONGLONG)((b - a) / 10000000ULL / 60ULL);
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
    HFONT* fonts[10] = {
        &g_fontUi, &g_fontBold, &g_fontStatus, &g_fontTTitle, &g_fontTHead,
        &g_fontTBody, &g_fontTI, &g_fontCal, &g_fontCalBold, &g_fontCalTitle
    };
    for (int i = 0; i < 10; i++)
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

    /* 字体尺寸比旧版整体调大：正文 11pt，辅助文字 10pt */
    g_fontUi = MakeFont(11, FALSE);
    g_fontBold = MakeFont(11, TRUE);
    g_fontStatus = MakeFont(10, FALSE);
    g_fontTTitle = MakeFont(10, FALSE);
    g_fontTHead = MakeFont(13, TRUE);
    g_fontTBody = MakeFont(10, FALSE);
    g_fontTI = MakeFont(16, TRUE);
    g_fontCal = MakeFont(11, FALSE);
    g_fontCalBold = MakeFont(11, TRUE);
    g_fontCalWeek = MakeFont(9, TRUE);
    g_fontCalTitle = MakeFont(13, TRUE);
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
        wchar_t* parts[8];
        int n = SplitLine(line, parts, 6);
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
    fwprintf(f, L"%d|%d|%d|%d|%s|%d\n",
        g_set.autostart, g_set.minStart, g_set.sound, g_set.toastSec,
        wav[0] != 0 ? wav : L"", g_set.lang);
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
*/
static void NormalizeRules(struct Task* t, LONGLONG diff, int includeNearest)
{
    if (diff < 0)
    {
        for (int i = 1; i <= t->rcount; i++)
        {
            t->r[i].fired = 1;
        }
        return;
    }
    int nearest = 0;
    for (int i = 1; i <= t->rcount; i++)
    {
        if (diff <= t->r[i].minutes)
        {
            nearest = i;   /* 规则已降序，最后一个满足的是最小分钟数 */
        }
    }
    if (nearest == 0)
    {
        return;
    }
    for (int i = 1; i < nearest; i++)
    {
        t->r[i].fired = 1;
    }
    if (includeNearest && t->r[nearest].minutes > 5)
    {
        t->r[nearest].fired = 1;
    }
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

        fwprintf(f, L"T|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%s|%s\n",
            t->id,
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
            /* 新格式任务行：跳过开头的 "T|" 后按 13 个字段解析 */
            wchar_t* body = line + 2;
            wchar_t* parts[16];
            int n = SplitLine(body, parts, 13);
            if (n < 13)
            {
                havePending = 0;
                continue;
            }
            struct Task t;
            ZeroMemory(&t, sizeof(t));
            t.id = _wtoi(parts[1]);
            t.stStart.wYear = (WORD)_wtoi(parts[2]);
            t.stStart.wMonth = (WORD)_wtoi(parts[3]);
            t.stStart.wDay = (WORD)_wtoi(parts[4]);
            t.stStart.wHour = (WORD)_wtoi(parts[5]);
            t.stStart.wMinute = (WORD)_wtoi(parts[6]);
            t.stEnd.wYear = (WORD)_wtoi(parts[7]);
            t.stEnd.wMonth = (WORD)_wtoi(parts[8]);
            t.stEnd.wDay = (WORD)_wtoi(parts[9]);
            t.stEnd.wHour = (WORD)_wtoi(parts[10]);
            t.stEnd.wMinute = (WORD)_wtoi(parts[11]);
            StringCchCopyW(t.title, MAX_TITLE, parts[12]);
            StringCchCopyW(t.note, MAX_NOTE, parts[13]);
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
    for (int i = 1; i <= g_count; i++)
    {
        NormalizeRules(&g_tasks[i], DiffMinutes(&now, &g_tasks[i].stStart), 0);
    }
    SortTasks();
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
        LONGLONG diff = DiffMinutes(&now, &t->stStart);

        if (diff < 0)
        {
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
            if (t->r[k].fired == 0 && diff <= t->r[k].minutes)
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
    HBRUSH white = CreateSolidBrush(RGB(248, 249, 251));
    FillRect(mem, &all, white);
    DeleteObject(white);

    /* 细边框 */
    HPEN border = CreatePen(PS_SOLID, 1, RGB(225, 226, 230));
    HGDIOBJ oldPen = SelectObject(mem, border);
    HBRUSH cardBrush = CreateSolidBrush(RGB(255, 255, 255));
    HGDIOBJ oldBrush = SelectObject(mem, cardBrush);
    RoundRect(mem, 0, 0, W - 1, H - 1, S(12), S(12));
    SelectObject(mem, oldPen);
    SelectObject(mem, oldBrush);
    DeleteObject(border);
    DeleteObject(cardBrush);

    /* 左侧主色竖条 */
    HBRUSH accent = CreateSolidBrush(RGB(0, 120, 212));
    RECT bar;
    SetRect(&bar, 0, S(10), S(4), H - S(10));
    FillRect(mem, &bar, accent);
    DeleteObject(accent);

    /* 蓝色信息圆 + 白色 i */
    HBRUSH blueBrush = CreateSolidBrush(RGB(0, 120, 212));
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
    SetTextColor(mem, RGB(140, 140, 140));
    RECT rt;
    SetRect(&rt, S(60), S(8), W - S(34), S(18));
    DrawTextW(mem, Str(STR_TOAST_APP), -1, &rt, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

    /* 粗体首行 */
    SelectObject(mem, g_fontTHead);
    SetTextColor(mem, RGB(28, 30, 34));
    RECT rh;
    SetRect(&rh, S(60), S(24), W - S(34), S(44));
    DrawTextW(mem, t->head, -1, &rh, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

    /* 正文两行 */
    SelectObject(mem, g_fontTBody);
    SetTextColor(mem, RGB(96, 100, 108));
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
        HBRUSH hv = CreateSolidBrush(RGB(235, 236, 240));
        HGDIOBJ o = SelectObject(mem, hv);
        SelectObject(mem, GetStockObject(NULL_PEN));
        Ellipse(mem, closeX, closeY, closeX + S(22), closeY + S(22));
        SelectObject(mem, o);
        DeleteObject(hv);
    }
    HPEN xPen = CreatePen(PS_SOLID, 1, t->closeHover ? RGB(40, 40, 40) : RGB(150, 150, 150));
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

/*============================= 主列表 =============================*/

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

static void SetColumnsWidth(int listWidth)
{
    int fixed = S(110) + S(80) + S(66) + S(66) + S(190) + S(88);
    int noteWidth = listWidth - fixed - S(24);
    if (noteWidth < S(70))
    {
        noteWidth = S(70);
    }
    ListView_SetColumnWidth(g_hList, 0, S(110));
    ListView_SetColumnWidth(g_hList, 1, S(80));
    ListView_SetColumnWidth(g_hList, 2, S(66));
    ListView_SetColumnWidth(g_hList, 3, S(66));
    ListView_SetColumnWidth(g_hList, 4, S(190));
    ListView_SetColumnWidth(g_hList, 5, noteWidth);
    ListView_SetColumnWidth(g_hList, 6, S(88));
    ListView_SetColumnWidth(g_hList, 7, 0);
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
    SendMessageW(g_hList, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g_hList);

    int shown = 0;
    int todayCount = 0;
    SYSTEMTIME now;
    GetLocalTime(&now);

    int row = 0;
    for (int i = 1; i <= g_count; i++)
    {
        struct Task* t = &g_tasks[i];
        if (t->stStart.wYear == now.wYear && t->stStart.wMonth == now.wMonth &&
            t->stStart.wDay == now.wDay)
        {
            todayCount++;
        }
        if (!TaskOnFilter(t))
        {
            continue;
        }

        wchar_t date[32], tm1[16], tm2[16], rc[16], idStr[16];
        FmtDate(&t->stStart, date, 32);
        FmtTime(&t->stStart, tm1, 16);
        FmtTime(&t->stEnd, tm2, 16);
        FormatStr(rc, 16, STR_REMIND_COUNT, t->rcount);
        StringCchPrintfW(idStr, 16, L"%d", t->id);

        LVITEMW lvi;
        ZeroMemory(&lvi, sizeof(lvi));
        lvi.mask = LVIF_TEXT | LVIF_PARAM;
        lvi.iItem = row;
        lvi.iSubItem = 0;
        lvi.pszText = date;
        lvi.lParam = (LPARAM)t->id;
        ListView_InsertItem(g_hList, &lvi);
        ListView_SetItemText(g_hList, row, 1, (LPWSTR)g_weekFull[g_lang][t->stStart.wDayOfWeek]);
        ListView_SetItemText(g_hList, row, 2, tm1);
        ListView_SetItemText(g_hList, row, 3, tm2);
        ListView_SetItemText(g_hList, row, 4, t->title);
        ListView_SetItemText(g_hList, row, 5, t->note);
        ListView_SetItemText(g_hList, row, 6, rc);
        ListView_SetItemText(g_hList, row, 7, idStr);
        row++;
        shown++;
    }

    SendMessageW(g_hList, WM_SETREDRAW, TRUE, 0);

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
                date, g_weekFull[g_lang][g_filterDate.wDayOfWeek], shown);
        }
        SetWindowTextW(g_hStatus, status);
    }

    if (g_hStatToday != NULL)
    {
        wchar_t buf[128];
        FormatStr(buf, 128, STR_STAT_TODAY, now.wMonth, now.wDay, todayCount);
        SetWindowTextW(g_hStatToday, buf);
    }
}

void DeleteSelected(void)
{
    int sel = ListView_GetNextItem(g_hList, -1, LVNI_SELECTED);
    if (sel < 0)
    {
        MessageBoxW(g_hMain, Str(STR_NO_SELECTION),
            Str(STR_NOTICE), MB_OK | MB_ICONINFORMATION);
        return;
    }

    LVITEMW lvi;
    ZeroMemory(&lvi, sizeof(lvi));
    lvi.mask = LVIF_PARAM;
    lvi.iItem = sel;
    ListView_GetItem(g_hList, &lvi);
    int id = (int)lvi.lParam;

    int idx = 0;
    for (int i = 1; i <= g_count; i++)
    {
        if (g_tasks[i].id == id)
        {
            idx = i;
            break;
        }
    }
    if (idx == 0)
    {
        RefreshList();
        return;
    }

    wchar_t msg[300];
    FormatStr(msg, 300, STR_CONFIRM_DELETE, g_tasks[idx].title);
    if (MessageBoxW(g_hMain, msg, Str(STR_CONFIRM_DEL_TITLE),
        MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
    {
        return;
    }

    for (int i = idx; i < g_count; i++)
    {
        g_tasks[i] = g_tasks[i + 1];
    }
    g_count--;

    SaveTasks();
    RefreshList();
    RefreshCalendar();
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
        S(104), S(96), S(400), S(30), hWnd, (HMENU)(LONG_PTR)IDC_TITLE, g_hInst, NULL);
    SendMessageW(g_dlgTitle, EM_SETLIMITTEXT, (WPARAM)(MAX_TITLE - 1), 0);

    CreateLabel(hWnd, Str(STR_LBL_NOTE), S(16), S(142), S(80), S(24));
    g_dlgNote = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE |
        ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL,
        S(104), S(136), S(400), S(72), hWnd, (HMENU)(LONG_PTR)IDC_NOTE, g_hInst, NULL);
    SendMessageW(g_dlgNote, EM_SETLIMITTEXT, (WPARAM)(MAX_NOTE - 1), 0);

    CreateLabel(hWnd, Str(STR_LBL_RULES), S(16), S(222), S(400), S(24));
    g_dlgRList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LBS_NOTIFY | WS_VSCROLL |
        LBS_HASSTRINGS,
        S(16), S(246), S(488), S(100),
        hWnd, (HMENU)(LONG_PTR)IDC_RLIST, g_hInst, NULL);

    g_dlgRValue = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
        S(104), S(356), S(64), S(30), hWnd, (HMENU)(LONG_PTR)IDC_RVALUE, g_hInst, NULL);
    SetWindowTextW(g_dlgRValue, L"15");

    g_dlgRUnit = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        S(176), S(356), S(110), S(240),
        hWnd, (HMENU)(LONG_PTR)IDC_RUNIT, g_hInst, NULL);
    SendMessageW(g_dlgRUnit, CB_ADDSTRING, 0, (LPARAM)Str(STR_UNIT_MIN));
    SendMessageW(g_dlgRUnit, CB_ADDSTRING, 0, (LPARAM)Str(STR_UNIT_HOUR));
    SendMessageW(g_dlgRUnit, CB_ADDSTRING, 0, (LPARAM)Str(STR_UNIT_DAY));
    SendMessageW(g_dlgRUnit, CB_SETCURSEL, 0, 0);

    CreateWindowExW(0, L"BUTTON", Str(STR_RULE_ADD),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        S(296), S(354), S(100), S(32), hWnd, (HMENU)(LONG_PTR)IDC_RADD, g_hInst, NULL);
    CreateWindowExW(0, L"BUTTON", Str(STR_RULE_DEL),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        S(404), S(354), S(100), S(32), hWnd, (HMENU)(LONG_PTR)IDC_RDEL, g_hInst, NULL);

    CreateHint(hWnd, Str(STR_ADD_HINT), S(104), S(398), S(400), S(42), IDC_ADD_HINT);

    CreateWindowExW(0, L"BUTTON", Str(STR_OK),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        S(300), S(454), S(100), S(34), hWnd, (HMENU)(LONG_PTR)IDC_OK, g_hInst, NULL);
    CreateWindowExW(0, L"BUTTON", Str(STR_CANCEL),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        S(408), S(454), S(100), S(34), hWnd, (HMENU)(LONG_PTR)IDC_CANCEL, g_hInst, NULL);

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
    if (g_count >= MAX_TASKS)
    {
        ShowNotice(hWnd, STR_ERR_TASK_MAX);
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
    GetWindowTextW(g_dlgTitle, title, MAX_TITLE);
    GetWindowTextW(g_dlgNote, note, MAX_NOTE);
    if (title[0] == 0)
    {
        ShowNotice(hWnd, STR_ERR_TITLE_EMPTY);
        SetFocus(g_dlgTitle);
        return;
    }
    StringCchCopyW(t.title, MAX_TITLE, title);
    StringCchCopyW(t.note, MAX_NOTE, note);
    CleanText(t.title);
    CleanText(t.note);

    t.id = g_nextId;
    g_nextId++;
    t.rcount = g_dlgRCount;
    for (int i = 1; i <= g_dlgRCount; i++)
    {
        t.r[i].minutes = g_dlgRMinutes[i];
        t.r[i].fired = 0;
    }
    SortRules(&t);

    SYSTEMTIME now;
    GetLocalTime(&now);
    NormalizeRules(&t, DiffMinutes(&now, &t.stStart), 1);

    g_count++;
    g_tasks[g_count] = t;
    SortTasks();
    SaveTasks();
    RefreshList();
    RefreshCalendar();

    g_addResult = IDOK;
}

LRESULT CALLBACK AddDlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        AddDlg_OnCreate(hWnd);
        return 0;

    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wParam, TRANSPARENT);
        SetTextColor((HDC)wParam, GetSysColor(COLOR_WINDOWTEXT));
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);

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

    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    DWORD exStyle = WS_EX_DLGMODALFRAME;
    RECT wr;
    SetRect(&wr, 0, 0, S(520), S(506));
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

static BOOL SetDlg_OnCreate(HWND hWnd)
{
    g_dlgSet = g_set;

    CreateLabel(hWnd, Str(STR_SET_LANG), S(16), S(18), S(100), S(28));
    HWND hLang = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        S(120), S(14), S(200), S(260),
        hWnd, (HMENU)(LONG_PTR)IDC_SET_LANG, g_hInst, NULL);
    SendMessageW(hLang, CB_ADDSTRING, 0, (LPARAM)Str(STR_LANG_ZH));
    SendMessageW(hLang, CB_ADDSTRING, 0, (LPARAM)Str(STR_LANG_JA));
    SendMessageW(hLang, CB_ADDSTRING, 0, (LPARAM)Str(STR_LANG_EN));
    if (g_dlgSet.lang < 0 || g_dlgSet.lang >= LANG_COUNT)
    {
        g_dlgSet.lang = LANG_ZH;
    }
    SendMessageW(hLang, CB_SETCURSEL, g_dlgSet.lang, 0);

    CreateWindowExW(0, L"BUTTON", Str(STR_SET_AUTOSTART),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        S(16), S(54), S(480), S(28),
        hWnd, (HMENU)(LONG_PTR)IDC_SET_AUTO, g_hInst, NULL);
    Button_SetCheck(GetDlgItem(hWnd, IDC_SET_AUTO),
        g_dlgSet.autostart ? BST_CHECKED : BST_UNCHECKED);

    CreateWindowExW(0, L"BUTTON", Str(STR_SET_MINSTART),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        S(16), S(86), S(480), S(28),
        hWnd, (HMENU)(LONG_PTR)IDC_SET_MIN, g_hInst, NULL);
    Button_SetCheck(GetDlgItem(hWnd, IDC_SET_MIN),
        g_dlgSet.minStart ? BST_CHECKED : BST_UNCHECKED);

    CreateLabel(hWnd, Str(STR_SET_DURATION), S(16), S(124), S(130), S(28));
    HWND hDur = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        S(150), S(120), S(110), S(260),
        hWnd, (HMENU)(LONG_PTR)IDC_SET_DUR, g_hInst, NULL);
    const int durList[6] = { 8, 10, 12, 15, 20, 30 };
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

    CreateLabel(hWnd, Str(STR_SET_SOUND), S(16), S(160), S(130), S(28));
    HWND hSound = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        S(150), S(156), S(200), S(260),
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
        S(362), S(154), S(90), S(32),
        hWnd, (HMENU)(LONG_PTR)IDC_SET_TEST, g_hInst, NULL);

    CreateLabel(hWnd, Str(STR_SET_WAV), S(16), S(196), S(130), S(28));
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", g_dlgSet.wavPath,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        S(16), S(224), S(400), S(30),
        hWnd, (HMENU)(LONG_PTR)IDC_SET_WAV, g_hInst, NULL);
    CreateWindowExW(0, L"BUTTON", Str(STR_SET_BROWSE),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        S(424), S(222), S(80), S(32),
        hWnd, (HMENU)(LONG_PTR)IDC_SET_BROWSE, g_hInst, NULL);

    CreateHint(hWnd, Str(STR_SET_HINT), S(16), S(264), S(488), S(46), IDC_SET_HINT);

    CreateWindowExW(0, L"BUTTON", Str(STR_OK),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        S(300), S(326), S(100), S(34),
        hWnd, (HMENU)(LONG_PTR)IDC_SET_OK, g_hInst, NULL);
    CreateWindowExW(0, L"BUTTON", Str(STR_CANCEL),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        S(408), S(326), S(100), S(34),
        hWnd, (HMENU)(LONG_PTR)IDC_SET_CANCEL, g_hInst, NULL);

    /* 分隔线 + 制作人员 / 联系邮箱 */
    CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
        S(16), S(376), S(488), 1,
        hWnd, (HMENU)(LONG_PTR)IDC_SEPARATOR, g_hInst, NULL);

    CreateWindowExW(0, L"STATIC", Str(STR_CREDITS_AUTHOR),
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
        S(16), S(384), S(488), S(22),
        hWnd, (HMENU)(LONG_PTR)IDC_SET_CREDITS, g_hInst, NULL);

    wchar_t emailText[256];
    FormatStr(emailText, 256, STR_CREDITS_EMAIL,
        L"3507609128@qq.com", L"3507609128@qq.com");
    CreateWindowExW(0, WC_LINK, emailText,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        S(16), S(408), S(488), S(24),
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
    const int durList[6] = { 8, 10, 12, 15, 20, 30 };
    int autoRun = Button_GetCheck(GetDlgItem(hWnd, IDC_SET_AUTO)) == BST_CHECKED;
    int minStart = Button_GetCheck(GetDlgItem(hWnd, IDC_SET_MIN)) == BST_CHECKED;

    wchar_t wav[MAX_PATH];
    GetWindowTextW(GetDlgItem(hWnd, IDC_SET_WAV), wav, MAX_PATH);

    if (durIdx < 0) { durIdx = 2; }
    if (sound < 0) { sound = SOUND_ASTERISK; }
    if (langIdx < 0 || langIdx >= LANG_COUNT) { langIdx = g_dlgSet.lang; }

    g_set.autostart = autoRun;
    g_set.minStart = minStart;
    g_set.sound = sound;
    g_set.toastSec = durList[durIdx];
    g_set.lang = langIdx;
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

    case WM_CTLCOLORSTATIC:
    {
        int id = GetDlgCtrlID((HWND)lParam);
        SetBkMode((HDC)wParam, TRANSPARENT);
        if (id == IDC_SET_CREDITS)
        {
            SetTextColor((HDC)wParam, RGB(105, 105, 105));
        }
        else
        {
            SetTextColor((HDC)wParam, GetSysColor(COLOR_WINDOWTEXT));
        }
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    }

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

    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    DWORD exStyle = WS_EX_DLGMODALFRAME;
    RECT wr;
    SetRect(&wr, 0, 0, S(520), S(436));
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
}

/*============================= 自绘日历 =============================*/

#define CAL_HEADER_H S(36)
#define CAL_WEEK_H   S(26)
#define CAL_CELL_H   S(30)
#define CAL_WIDTH    S(240)
#define CAL_HEIGHT   (CAL_HEADER_H + CAL_WEEK_H + CAL_CELL_H * 6)

#define CAL_ACCENT      RGB(0, 120, 212)
#define CAL_ACCENT_DARK RGB(0, 76, 140)
#define CAL_TODAY       RGB(224, 78, 26)
#define CAL_TEXT        RGB(32, 32, 32)
#define CAL_MUTED       RGB(128, 128, 128)
#define CAL_HOVER       RGB(242, 246, 251)
#define CAL_SEL_BG      RGB(224, 238, 253)
#define CAL_SUN         RGB(196, 64, 64)
#define CAL_SAT         RGB(64, 104, 176)

struct CalLayout
{
    int W, H;
    int pad;
    int cellW;
    int cellH;
    int gridTop;
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

static int HasTasksOnDate(int year, int month, int day)
{
    for (int i = 1; i <= g_count; i++)
    {
        if (g_tasks[i].stStart.wYear == year &&
            g_tasks[i].stStart.wMonth == month &&
            g_tasks[i].stStart.wDay == day)
        {
            return 1;
        }
    }
    return 0;
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
    L->cellH = (L->H - L->gridTop - S(2)) / 6;
    if (L->cellH < 1)
    {
        L->cellH = 1;
    }
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
    HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(mem, &all, white);
    DeleteObject(white);

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

    for (int row = 0; row < 6; row++)
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

            int hasTask = HasTasksOnDate(g_calYear, g_calMonth, day);
            int selected = (g_calSel.wYear == g_calYear &&
                            g_calSel.wMonth == g_calMonth &&
                            g_calSel.wDay == day);
            int hover = (g_calHoverDay == day);

            RECT inner = cell;
            InflateRect(&inner, -S(3), -S(2));

            COLORREF textColor = CAL_TEXT;
            HFONT font = g_fontCal;
            if (hasTask)
            {
                FillRoundedRect(mem, &inner, CAL_ACCENT);
                textColor = RGB(255, 255, 255);
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

            if (today.wYear == g_calYear && today.wMonth == g_calMonth &&
                today.wDay == day)
            {
                DrawDashedRoundRect(mem, &cell, CAL_TODAY, S(2));
            }

            wchar_t num[8];
            StringCchPrintfW(num, 8, L"%d", day);
            SelectObject(mem, font);
            SetTextColor(mem, textColor);
            DrawTextW(mem, num, -1, &cell, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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
    if (col < 0 || col > 6 || row < 0 || row > 5)
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

/*============================= 语言切换 =============================*/

static void SetChildText(HWND parent, int id, int strId)
{
    HWND child = GetDlgItem(parent, id);
    if (child != NULL)
    {
        SetWindowTextW(child, Str(strId));
    }
}

static void ApplyListHeaders(void)
{
    static const int ids[8] = {
        STR_LIST_DATE, STR_LIST_DAY, STR_LIST_START, STR_LIST_END,
        STR_LIST_TITLE, STR_LIST_NOTE, STR_LIST_REMIND, -1
    };
    if (g_hList == NULL)
    {
        return;
    }
    for (int i = 0; i < 8; i++)
    {
        LVCOLUMNW lvc;
        ZeroMemory(&lvc, sizeof(lvc));
        lvc.mask = LVCF_TEXT;
        lvc.pszText = (LPWSTR)(ids[i] >= 0 ? Str(ids[i]) : L"");
        ListView_SetColumn(g_hList, i, &lvc);
    }
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
    SetChildText(g_hMain, IDC_BTN_TODAY, STR_BTN_TODAY);
    SetChildText(g_hMain, IDC_BTN_ALL, STR_BTN_ALL);
    SetChildText(g_hMain, IDC_BTN_SET, STR_BTN_SET);
    if (g_hChkAuto != NULL)
    {
        SetWindowTextW(g_hChkAuto, Str(STR_CHK_AUTOSTART));
    }
    if (g_hTip != NULL)
    {
        SetWindowTextW(g_hTip, Str(STR_TIP));
    }

    ApplyListHeaders();
    RefreshList();
    RefreshCalendar();
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

    /* 左侧自绘日历 */
    g_hCal = CreateWindowExW(0, L"TimeManagerCalendar", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        S(10), S(10), CAL_WIDTH, CAL_HEIGHT,
        hWnd, (HMENU)(LONG_PTR)IDC_CAL, g_hInst, NULL);

    SYSTEMTIME today;
    GetLocalTime(&today);
    g_calSel = today;
    g_calYear = today.wYear;
    g_calMonth = today.wMonth;
    g_lastToday = today;

    g_hStatToday = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
        S(10), S(10) + CAL_HEIGHT + S(10), S(240), S(24),
        hWnd, (HMENU)(LONG_PTR)IDC_STAT_TODAY, g_hInst, NULL);

    g_hChkAuto = CreateWindowExW(0, L"BUTTON", Str(STR_CHK_AUTOSTART),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        S(10), S(10) + CAL_HEIGHT + S(44), S(240), S(28),
        hWnd, (HMENU)(LONG_PTR)IDC_CHK_AUTO, g_hInst, NULL);
    Button_SetCheck(g_hChkAuto, IsAutoRun() ? BST_CHECKED : BST_UNCHECKED);

    /* 左右分隔竖线 */
    CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_ETCHEDVERT,
        S(260), S(8), 1, S(600),
        hWnd, (HMENU)(LONG_PTR)IDC_SEPARATOR, g_hInst, NULL);

    /* 右侧按钮栏 */
    CreateWindowExW(0, L"BUTTON", Str(STR_BTN_ADD),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        S(272), S(12), S(100), S(32),
        hWnd, (HMENU)(LONG_PTR)IDC_BTN_ADD, g_hInst, NULL);
    CreateWindowExW(0, L"BUTTON", Str(STR_BTN_DELETE),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        S(380), S(12), S(100), S(32),
        hWnd, (HMENU)(LONG_PTR)IDC_BTN_DELETE, g_hInst, NULL);
    CreateWindowExW(0, L"BUTTON", Str(STR_BTN_TODAY),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        S(488), S(12), S(76), S(32),
        hWnd, (HMENU)(LONG_PTR)IDC_BTN_TODAY, g_hInst, NULL);
    CreateWindowExW(0, L"BUTTON", Str(STR_BTN_ALL),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        S(572), S(12), S(76), S(32),
        hWnd, (HMENU)(LONG_PTR)IDC_BTN_ALL, g_hInst, NULL);
    CreateWindowExW(0, L"BUTTON", Str(STR_BTN_SET),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        S(656), S(12), S(84), S(32),
        hWnd, (HMENU)(LONG_PTR)IDC_BTN_SET, g_hInst, NULL);

    g_hTip = CreateWindowExW(0, L"STATIC", Str(STR_TIP),
        WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
        S(750), S(14), S(200), S(28),
        hWnd, (HMENU)(LONG_PTR)IDC_TIP, g_hInst, NULL);

    /* 日程列表 */
    g_hList = CreateWindowExW(0, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL |
        LVS_SHOWSELALWAYS | WS_BORDER | WS_TABSTOP,
        S(272), S(56), S(600), S(400),
        hWnd, (HMENU)(LONG_PTR)IDC_LIST, g_hInst, NULL);
    ListView_SetExtendedListViewStyle(g_hList,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

    static const int headerIds[8] = {
        STR_LIST_DATE, STR_LIST_DAY, STR_LIST_START, STR_LIST_END,
        STR_LIST_TITLE, STR_LIST_NOTE, STR_LIST_REMIND, -1
    };
    int widths[8] = {
        S(110), S(80), S(66), S(66), S(190), S(200), S(88), 0
    };
    for (int i = 0; i < 8; i++)
    {
        LVCOLUMNW lvc;
        ZeroMemory(&lvc, sizeof(lvc));
        lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
        lvc.fmt = LVCFMT_LEFT;
        lvc.iSubItem = i;
        lvc.cx = widths[i];
        lvc.pszText = (LPWSTR)(headerIds[i] >= 0 ? Str(headerIds[i]) : L"");
        ListView_InsertColumn(g_hList, i, &lvc);
    }

    g_hStatus = CreateWindowExW(WS_EX_STATICEDGE, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
        S(8), S(560), S(900), S(30),
        hWnd, (HMENU)(LONG_PTR)IDC_STATUS, g_hInst, NULL);
    EnumChildWindows(hWnd, SetFontToChild, 0);

    LoadTasks();
    RefreshList();
    RefreshCalendar();

    SetTimer(hWnd, TIMER_CHECK, 1000, NULL);
    return 0;
}

static void Main_OnSize(HWND hWnd, int W, int H)
{
    if (g_hList == NULL)
    {
        return;
    }
    MoveWindow(g_hTip, S(750), S(14), W - S(750) - S(12), S(28), TRUE);

    HWND sep = GetDlgItem(hWnd, IDC_SEPARATOR);
    MoveWindow(sep, S(260), S(8), 1, H - S(40), TRUE);

    MoveWindow(g_hList, S(272), S(56), W - S(272) - S(12), H - S(56) - S(40), TRUE);
    MoveWindow(g_hStatus, S(8), H - S(30), W - S(8), S(30), TRUE);
    SetColumnsWidth(W - S(272) - S(12));
}

static void Main_OnNotify(HWND hWnd, LPARAM lParam)
{
    LPNMHDR pnm = (LPNMHDR)lParam;
    if (pnm->idFrom == IDC_LIST)
    {
        if (pnm->code == LVN_KEYDOWN)
        {
            LPNMLVKEYDOWN pkd = (LPNMLVKEYDOWN)lParam;
            if (pkd->wVKey == VK_DELETE)
            {
                DeleteSelected();
            }
        }
    }
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
        mmi->ptMinTrackSize.x = S(980);
        mmi->ptMinTrackSize.y = S(620);
        return 0;
    }

    case WM_CTLCOLORSTATIC:
        return (LRESULT)GetStockObject(WHITE_BRUSH);

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_BTN_ADD:
            ShowAddDialog();
            break;
        case IDC_BTN_DELETE:
            DeleteSelected();
            break;
        case IDC_BTN_TODAY:
        {
            SYSTEMTIME now;
            GetLocalTime(&now);
            CalSetDate(&now);
            g_filterMode = 1;
            g_filterDate = g_calSel;
            RefreshList();
            break;
        }
        case IDC_BTN_ALL:
            g_filterMode = 0;
            RefreshList();
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

    case WM_NOTIFY:
        Main_OnNotify(hWnd, lParam);
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
            }
        }
        return 0;

    case WM_APP_CAL_SELECT:
        g_filterMode = 1;
        g_filterDate = g_calSel;
        RefreshList();
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
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
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

    g_hMain = CreateWindowExW(
        0,
        L"TimeManagerMainWnd",
        Str(STR_APP_TITLE),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, S(1080), S(680),
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
