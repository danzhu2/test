#include "Menu.h"
#include "Key.h"
#include "LED.h"
#include "OLED.h"
#include "util.h"			/* Util_IntToStr */
#include "tick.h"
#include "Sensor.h"			/* 巡线页显示 8 路传感器 */
#include "LineFollow.h"		/* 发车 / 圈数 / 计时 */
#include "FlashSave.h"		/* 拓展页参数编辑与保存 */
#include "Buzzer.h"			/* 参数保存成功提示音 */

/* ============================================================================
 *  2026 DEBUG 软件组赛题 · 菜单状态机
 *
 *  在原版（基础 1/2/3 项）基础上：
 *   - 巡线页（PAGE_LINE）：4a 传感器显示、4b/4c 发车圈数计时
 *   - 拓展页（PAGE_EXT）：6.2 参数查看 / 修改 / 掉电保存
 *   - LED 页、信息页逻辑与原版一致
 * ==========================================================================*/

#define OLED_STR(line, col, str)	OLED_ShowString((line), (col), (str))

/* ---------------------------- 主菜单表 ---------------------------------- */
const char * const MainItemName[] =
{
	"LED Ctrl",
	"Info",
	"Line Follow",
	"Extend"
};

const Page_t MainItemPage[] =
{
	PAGE_LED,
	PAGE_INFO,
	PAGE_LINE,
	PAGE_EXT
};

#define MAIN_ITEM_NUM	(sizeof(MainItemName) / sizeof(MainItemName[0]))     

/* ---------------------------- 菜单状态 ---------------------------------- */

static Page_t  s_page      = PAGE_MAIN;
static uint8_t s_mainIndex = 0;
static uint8_t s_dirty     = 1;            //判断是否需要重绘

/* 变量 a。上电初值必须是 0（评分点） */
static int32_t s_a = 0;

/* LED 页：屏幕上"当前正显示"的两灯状态（与引脚真实状态比对用） */
static uint8_t s_shownLed1 = 0;
static uint8_t s_shownLed2 = 0;

/* 巡线页：屏幕上"当前正显示"的传感器字 + 计时刷新节拍 */
static uint8_t  s_shownSensor     = 0xFF;	/* 初值给一个不可能的值，保证第一帧必刷 */
static uint32_t s_lineRefreshTick = 0;

/* 拓展页：当前选中的参数行 */
static uint8_t s_paramSel = 0;

/* ---------------------------- 内部函数声明 ------------------------------ */

static void Draw_Main(void);
static void Draw_Led(void);
static void Draw_Info(void);
static void Draw_Line(void);
static void Draw_Ext(void);

static void Menu_EnterPage(Page_t page);
static void Menu_BackToMain(void);
static void Menu_LedSyncCheck(void);
static void Menu_LineSyncCheck(void);

/* ---------------------------- 对外接口 ---------------------------------- */

void Menu_Init(void)
{
	s_page      = PAGE_MAIN;
	s_mainIndex = 0;
	s_dirty     = 1;
	s_a         = 0;
}

Page_t Menu_GetPage(void)
{
	return s_page;
}

int32_t Menu_GetA(void)
{
	return s_a;
}

void Menu_SetA(int32_t value)
{
	if (s_a != value)
	{
		s_a = value;

		if (s_page == PAGE_INFO)
		{
			s_dirty = 1;
		}
	}
}

void Menu_HandleKey(uint8_t key)         //处理一次按键事件
{
	switch (s_page)
	{
	/* ---------------- 主菜单：K1/K2 移动箭头，K3 进入 ---------------- */
	case PAGE_MAIN:
		if (key == KEY_1)
		{
			s_mainIndex = (uint8_t)((s_mainIndex + MAIN_ITEM_NUM - 1) % MAIN_ITEM_NUM);      //行数索引       加页数取模防止页数超出
			s_dirty = 1;
		}
		else if (key == KEY_2)
		{
			s_mainIndex = (uint8_t)((s_mainIndex + 1) % MAIN_ITEM_NUM);
			s_dirty = 1;
		}
		else if (key == KEY_3)
		{
			Menu_EnterPage(MainItemPage[s_mainIndex]);
		}
		break;

	/* ---------------- LED 控制子菜单（第 2 项） ---------------- */
	case PAGE_LED:
		if (key == KEY_4)
		{
			/* 第 2 项 c：必须先关灯再返回，顺序不能反 */
			Led_SetMode(LED_MODE_OFF);
			s_dirty = 1;
			Menu_BackToMain();
		}
		else if (key == KEY_1)      //控制LED开关
		{
			if (Led_GetMode() == LED_MODE_ON)
			{
				Led_SetMode(LED_MODE_OFF);
			}
			else
			{
				Led_SetMode(LED_MODE_ON);
			}
			s_dirty = 1;
		}
		else if (key == KEY_2)      //LED闪烁
		{
			Led_SetMode(LED_MODE_BLINK);
			s_dirty = 1;
		}
		break;

	/* ---------------- 信息显示子菜单（第 3 项 a） ---------------- */
	case PAGE_INFO:
		if (key == KEY_4)
		{
			Menu_BackToMain();
		}
		break;

	/* ---------------- 巡线功能子菜单（第 4 项） ---------------- */
	case PAGE_LINE:
		if (key == KEY_1)
		{
			/* 4b：发车 = 蜂鸣一声 + 清计时计圈 + 开始巡线。
			 * 已在运行中重复按忽略（Line_StartRun 内部有保护） */
			Line_StartRun();
			s_dirty = 1;
		}
		else if (key == KEY_4)      //按键4退出
		{
			/* 车还在跑时禁止退出巡线页 —— 防止跑飞了没人管。
			 * 想退出：等完赛自动停，或者直接断电重启 */
			if (!Line_IsRunning())
			{
				Menu_BackToMain();
			}
		}
		break;

	/* ---------------- 拓展功能子菜单（6.2 参数设置） ---------------- */
	case PAGE_EXT:
	{
		/* 参数表：名字 / 存储位置 / 步长 / 上下限
		 * 加新参数（6.2 允许自定义）：往三个数组各加一行、EXT_PARAM_NUM 改一下即可 */
		static const char * const name[5] =
			{ "Speed", "Laps", "KP x100", "KI x100", "KD x100" };
		static int16_t * const addr[5] =
			{ &g_params.baseSpeed, &g_params.targetLaps, &g_params.kp, &g_params.ki, &g_params.kd };
		static const int16_t step[5]  = { 50, 1, 100, 50, 100 };
		static const int16_t limLo[5] = { 100, 1, 0, 0, 0 };
		static const int16_t limHi[5] = { 1000, 10, 5000, 2000, 10000 };
#define EXT_PARAM_NUM	5

		if (key == KEY_1)
		{
			s_paramSel = (uint8_t)((s_paramSel + 1) % EXT_PARAM_NUM);
			s_dirty = 1;
		}
		else if (key == KEY_2)
		{
			/* 加步长并限幅 */
			*addr[s_paramSel] = (int16_t)((*addr[s_paramSel] + step[s_paramSel] > limHi[s_paramSel])
			                    ? limHi[s_paramSel] : *addr[s_paramSel] + step[s_paramSel]);
			s_dirty = 1;
		}
		else if (key == KEY_3)
		{
			*addr[s_paramSel] = (int16_t)((*addr[s_paramSel] - step[s_paramSel] < limLo[s_paramSel])
			                    ? limLo[s_paramSel] : *addr[s_paramSel] - step[s_paramSel]);
			s_dirty = 1;
		}
		else if (key == KEY_4)
		{
			/* 【关键设计】返回主菜单的同时自动保存。
			 * 为什么在这里保存？
			 *   1. 用户"改完就走"的肌肉记忆最可靠，不用单独记"保存键"；
			 *   2. Flash 擦页会卡 CPU 几十毫秒，此时车一定是停的（在菜单里），
			 *      满足"绝不在巡线运行中写 Flash"的安全约束。 */
			Params_Save();
			Line_ApplyParams();		/* 新参数立刻生效，不用断电重启 */
			Buzzer_Beep(1, 150);	/* 响一声 = 保存完成，给用户一个确认反馈 */
			s_paramSel = 0;
			Menu_BackToMain();
		}
		break;
	}

	default:
		Menu_BackToMain();
		break;
	}
}

void Menu_Task(void)
{
	/* LED 页：真实状态一变就置脏（2d 评分点） */
	if (s_page == PAGE_LED)
	{
		Menu_LedSyncCheck();
	}

	/* 巡线页：传感器一变就置脏（4a 评分点"1 秒内更新"，实测几乎零延迟）；
	 * 计时数字每 100ms 刷一次 */
	if (s_page == PAGE_LINE)
	{
		Menu_LineSyncCheck();
	}

	if (!s_dirty)
	{
		return;
	}

	switch (s_page)
	{
	case PAGE_MAIN:	Draw_Main();	break;
	case PAGE_LED:	Draw_Led();		break;
	case PAGE_INFO:	Draw_Info();	break;
	case PAGE_LINE:	Draw_Line();	break;
	case PAGE_EXT:	Draw_Ext();		break;
	default:		Draw_Main();	break;
	}

	s_dirty = 0;
}

/* ---------------------------- 页面切换 ---------------------------------- */

static void Menu_EnterPage(Page_t page)
{
	s_page  = page;
	s_dirty = 1;

	/* 进巡线页时把计时刷新节拍归位：
	 * 不归位的话，隔了很久再进这一页，(现在 - 旧时刻) 远超 100ms，
	 * 会立刻触发一轮刷新（虽无害，但节拍基准干净一点更好维护） */
	if (page == PAGE_LINE)
	{
		s_lineRefreshTick = Tick_Get();
		s_shownSensor     = 0xFF;	/* 强制第一帧刷新传感器显示 */
	}
}

static void Menu_BackToMain(void)
{
	s_page  = PAGE_MAIN;
	s_dirty = 1;				/* 箭头保持上次位置 */
}

/* ---------------------- LED 状态同步检查 ------------------------ */
static void Menu_LedSyncCheck(void)
{
	uint8_t l1 = Led1_IsOn();
	uint8_t l2 = Led2_IsOn();

	if (l1 != s_shownLed1 || l2 != s_shownLed2)
	{
		s_dirty = 1;
	}
}

/* ---------------------- 巡线页同步检查 ------------------------ */
static void Menu_LineSyncCheck(void)
{
	uint8_t w = Sensor_Read();

	/* 传感器状态变了 → 立刻重画（4a：每一路 1 秒内更新，这里实际 <100ms） */
	if (w != s_shownSensor)
	{
		s_shownSensor = w;
		s_dirty = 1;
	}

	/* 计时显示每 100ms 刷一次：0.1 秒的显示精度正好匹配显示格式 */
	if ((uint32_t)(Tick_Get() - s_lineRefreshTick) >= 100)
	{
		s_lineRefreshTick += 100;
		s_dirty = 1;
	}
}

/* ---------------------------- 各页面绘制 -------------------------------- */

static void Draw_Main(void)
{
	uint8_t i;

	OLED_Clear();

	OLED_STR(1, 1, "MAIN MENU");

	for (i = 0; i < MAIN_ITEM_NUM; i++)
	{
		OLED_STR((uint8_t)(3 + i), 1, (s_mainIndex == i) ? ">" : " ");
		OLED_STR((uint8_t)(3 + i), 2, (char *)MainItemName[i]);
	}

	OLED_STR(7, 1, "K1/K2: Move");
	OLED_STR(8, 1, "K3:Enter  K4:Back");
}

static void Draw_Led(void)
{
	s_shownLed1 = Led1_IsOn();
	s_shownLed2 = Led2_IsOn();

	OLED_Clear();
	OLED_STR(1, 1, "LED CTRL");

	switch (Led_GetMode())
	{
	case LED_MODE_ON:		OLED_STR(2, 1, "Mode: ON   "); break;
	case LED_MODE_BLINK:	OLED_STR(2, 1, "Mode: BLINK"); break;
	default:				OLED_STR(2, 1, "Mode: OFF  "); break;
	}

	/* 显示的是引脚真实状态，和灯的实际亮灭天然一致（2d） */
	OLED_STR(4, 1, s_shownLed1 ? "LED1: ON " : "LED1: OFF");
	OLED_STR(5, 1, s_shownLed2 ? "LED2: ON " : "LED2: OFF");

	OLED_STR(7, 1, "K1:On/Off K2:Blink");
	OLED_STR(8, 1, "K4:Back");
}

static void Draw_Info(void)
{
	char buf[12];

	OLED_Clear();
	OLED_STR(1, 1, "INFO");

	/* ⚠️【需修改】姓名学号！改成你自己的（拼音 + 学号）。
	 *    考核时这里显示错 = 第 3 项 a 直接丢 1 分，一定提前改好烧录验证 */
	OLED_STR(3, 1, "Name: ZhangSan");
	OLED_STR(4, 1, "ID  : 32300000");

	Util_IntToStr(s_a, buf);
	OLED_STR(5, 1, "a   : ");
	OLED_STR(5, 7, buf);

	OLED_STR(8, 1, "K4:Back");
}

/* 把毫秒格式化成 "T: 12.3S" */
static void LineTimeStr(uint32_t ms, char *out)
{
	uint32_t t10 = ms / 100;			/* 0.1 秒为单位的整数，避免浮点 */
	uint32_t sec = t10 / 10;
	uint8_t  i   = 0;

	out[i++] = 'T';
	out[i++] = ':';
	out[i++] = ' ';

	if (sec >= 100) { out[i++] = (char)('0' + sec / 100); }
	if (sec >= 10)  { out[i++] = (char)('0' + sec / 10 % 10); }
	out[i++] = (char)('0' + sec % 10);
	out[i++] = '.';
	out[i++] = (char)('0' + t10 % 10);
	out[i++] = 'S';
	out[i]   = '\0';
}

static void Draw_Line(void)
{
	char buf[16];
	uint8_t w = Sensor_Read();
	uint8_t i;

	s_shownSensor = w;			/* 记下这一帧画的内容，作为下次比对的基准 */

	OLED_Clear();
	OLED_STR(1, 1, "LINE FOLLOW");

	/* 第 2 行：运行状态 + 圈数
	 * ⚠️ 列布局计算（8x16 字体，一屏 16 列）：
	 *   "ST:DONE" 占 1~7 列；"L:" 在 9~10；圈数从 11 起（最多 2 位，到 12）；
	 *   "/" 在 13；目标圈数从 14 起（limHi=10，最多 2 位，到 15）。
	 *   全部 ≤16 列，不越界。别把目标圈数再往右放，16 列写两位会写出屏幕。 */
	if (Line_IsRunning())       OLED_STR(2, 1, "ST:RUN ");
	else if (Line_IsFinished()) OLED_STR(2, 1, "ST:DONE");
	else                        OLED_STR(2, 1, "ST:IDLE");

	OLED_STR(2, 9, "L:");
	Util_IntToStr(Line_GetLaps(), buf);
	OLED_STR(2, 11, buf);
	OLED_STR(2, 13, "/");
	Util_IntToStr(g_params.targetLaps, buf);
	OLED_STR(2, 14, buf);

	/* 第 3 行：8 路传感器状态。S1(最左) 在最前，压线显示 1
	 * 【4a 评分点】必须能看清"全部传感器 + 当前状态" */
	buf[0] = 'S';
	buf[1] = ':';
	for (i = 0; i < 8; i++)
	{
		buf[2 + i] = (char)(((w >> i) & 1u) ? '1' : '0');
	}
	buf[10] = '\0';
	OLED_STR(3, 1, buf);

	/* 第 4 行：计时（4b 评分点：完赛后自动停表显示本圈用时） */
	LineTimeStr(Line_GetElapsedMs(), buf);
	OLED_STR(4, 1, buf);

	OLED_STR(7, 1, Line_IsRunning() ? "RUNNING..." : "K1:Start");
	OLED_STR(8, 1, Line_IsRunning() ? "Wait finish" : "K4:Back");
}

static void Draw_Ext(void)
{
	/* 与 Menu_HandleKey 里 PAGE_EXT 的三张表保持一致 */
	static const char * const name[5] =
		{ "Speed", "Laps", "KP x100", "KI x100", "KD x100" };
	static int16_t * const addr[5] =
		{ &g_params.baseSpeed, &g_params.targetLaps, &g_params.kp, &g_params.ki, &g_params.kd };
	uint8_t i;
	char buf[12];

	OLED_Clear();
	OLED_STR(1, 1, "PARAM SETTING");

	for (i = 0; i < 5; i++)
	{
		OLED_STR((uint8_t)(2 + i), 1, (s_paramSel == i) ? ">" : " ");
		OLED_STR((uint8_t)(2 + i), 2, (char *)name[i]);

		Util_IntToStr(*addr[i], buf);
		OLED_STR((uint8_t)(2 + i), 12, buf);
	}

	/* KP/KI/KD 显示的是 ×100 的整数，实际作用 = 显示值/100 */
	OLED_STR(8, 1, "K1:Sel K2:+ K3:-");
	OLED_STR(8, 13, "K4:SAVE");
}
