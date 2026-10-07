#include "Menu.h"
#include "Key.h"
#include "LED.h"
#include "OLED.h"		

/* ============================================================================
 *  2026 DEBUG 软件组赛题 · 基础第 1 项：OLED 菜单
 *
 *  实现要点（对照评分标准）：
 *    a. 主菜单（3 分）
 *       1) 上电后显示主菜单                        -> Menu_Init() 里 s_page = PAGE_MAIN
 *       2) 默认箭头指向首行                        -> s_mainIndex 初值 0
 *       3) 按键 1 / 2 改变箭头指向行                -> Menu_HandleKey() 中循环移动
 *       4) 主菜单至少 4 个菜单项                    -> MainItemName[] 共 4 项，可继续加
 *    b. 子菜单（3 分）
 *       5) 按键 3 进入当前选中的子菜单              -> Menu_EnterPage()
 *       6) 子菜单界面按键 4 返回主菜单              -> Menu_BackToMain()
 *
 *  显示字体说明：
 *   菜单项较多，这里统一用 8 号字体（6x8，一屏 8 行 × 21 列），
 *   所以行号用 1~8、列号用 1~21。如果你的驱动没有 FontSize 参数，见下方 OLED_STR 宏。
 * ==========================================================================*/

#define FONT_SMALL	8			/* 8 号字体：6x8，一屏 8 行 */

#define OLED_STR(line, col, str, font)  OLED_ShowString((line), (col), (str))
/* ---------------------------- 主菜单表 ---------------------------------- */

/* 菜单项显示名：想加菜单项就往这个数组里加，同时给 MainItemPage[] 加对应页面 */
static const char * const MainItemName[] =
{
	"LED Ctrl",
	"Info",
	"Line Follow",
	"Extend"
};

/* 每个菜单项指向的页面，顺序必须与 MainItemName[] 一一对应 */
static const Page_t MainItemPage[] =
{
	PAGE_LED,
	PAGE_INFO,
	PAGE_LINE,
	PAGE_EXT
};

#define MAIN_ITEM_NUM	(sizeof(MainItemName) / sizeof(MainItemName[0]))

/* ---------------------------- 菜单状态 ---------------------------------- */

static Page_t  s_page      = PAGE_MAIN;		/* 当前页面 */
static uint8_t s_mainIndex = 0;				/* 主菜单当前选中的行（0 = 首行） */
static uint8_t s_dirty     = 1;				/* 1 = 界面需要重绘 */

/* LED 子菜单内部状态（第 2 项的作用域） */
static uint8_t s_ledOn     = 0;				/* 两个 LED 是否常亮 */
static uint8_t s_blinkOn   = 0;				/* 是否处于交替闪烁（第 2 项 b 待实现） */

/* ---------------------------- 内部函数声明 ------------------------------ */

static void Draw_Main(void);
static void Draw_Led(void);
static void Draw_Info(void);
static void Draw_Line(void);
static void Draw_Ext(void);

static void Menu_EnterPage(Page_t page);
static void Menu_BackToMain(void);

/* ---------------------------- 对外接口 ---------------------------------- */

void Menu_Init(void)
{
	s_page      = PAGE_MAIN;
	s_mainIndex = 0;			/* 默认箭头指向首行 */
	s_dirty     = 1;			/* 上电先画一次主菜单 */
}

Page_t Menu_GetPage(void)
{
	return s_page;
}

void Menu_HandleKey(uint8_t key)
{
	switch (s_page)
	{
	/* ---------------- 主菜单：K1/K2 移动箭头，K3 进入 ---------------- */
	case PAGE_MAIN:
		if (key == KEY_1)
		{
			/* 上移一行，到顶后循环到底部 */
			s_mainIndex = (uint8_t)((s_mainIndex + MAIN_ITEM_NUM - 1) % MAIN_ITEM_NUM);
			s_dirty = 1;
		}
		else if (key == KEY_2)
		{
			/* 下移一行，到底后循环到顶部 */
			s_mainIndex = (uint8_t)((s_mainIndex + 1) % MAIN_ITEM_NUM);
			s_dirty = 1;
		}
		else if (key == KEY_3)
		{
			/* 进入当前选中的子菜单 */
			Menu_EnterPage(MainItemPage[s_mainIndex]);
		}
		/* K4 在主菜单不响应 */
		break;

	/* ---------------- LED 控制子菜单 ---------------- */
	case PAGE_LED:
		if (key == KEY_4)
		{
			/* 第 2 项 c：返回主菜单后 LED 变为常灭 */
			s_ledOn   = 0;
			s_blinkOn = 0;
			Led_AllOff();
			Menu_BackToMain();
		}
		else if (key == KEY_1)
		{
			/* 第 2 项 a：按 K1 两个 LED 由灭变亮，再按由亮变灭（这里只是最小演示） */
			s_ledOn = (uint8_t)(!s_ledOn);
			if (s_ledOn) { Led1_On(); Led2_On(); }
			else         { Led1_Off(); Led2_Off(); }
			s_dirty = 1;
		}
		else if (key == KEY_2)
		{
			/* TODO（第 2 项 b）：交替闪烁需要定时器配合，建议在非阻塞主循环里用
			 * Tick_Get() 判断时间片来翻转两个 LED，不要用 delay 死等。 */
			s_blinkOn = (uint8_t)(!s_blinkOn);
			s_dirty = 1;
		}
		break;

	/* ---------------- 信息显示子菜单（第 3 项） ---------------- */
	case PAGE_INFO:
		if (key == KEY_4)
		{
			Menu_BackToMain();
		}
		/* TODO（第 3 项）：这里后续加姓名 / 学号 / 变量 a 的显示与串口修改变量 */
		break;

	/* ---------------- 巡线功能子菜单（第 4 项） ---------------- */
	case PAGE_LINE:
		if (key == KEY_4)
		{
			Menu_BackToMain();
		}
		else if (key == KEY_1)
		{
			/* TODO（第 4 项 b）：按键发车、蜂鸣器响一声、开始巡线并计时 */
		}
		break;

	/* ---------------- 拓展功能子菜单（第 5 项 / 拓展部分） ---------------- */
	case PAGE_EXT:
		if (key == KEY_4)
		{
			Menu_BackToMain();
		}
		break;

	default:
		/* 异常状态：直接回主菜单，保证不会卡死 */
		Menu_BackToMain();
		break;
	}
}

void Menu_Task(void)
{
	if (!s_dirty)
	{
		return;					/* 内容没变化就不刷屏，省时间给其他任务 */
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
}

static void Menu_BackToMain(void)
{
	s_page  = PAGE_MAIN;
	s_dirty = 1;				/* 回到主菜单时箭头保持上次位置 */
}

/* ---------------------------- 各页面绘制 -------------------------------- */

static void Draw_Main(void)
{
	uint8_t i;

	OLED_Clear();

	OLED_STR(1, 1, "MAIN MENU", FONT_SMALL);

	for (i = 0; i < MAIN_ITEM_NUM; i++)
	{
		/* 第 1 列画箭头：选中行显示 ">"，其余行显示空格 */
		OLED_STR((uint8_t)(3 + i), 1, (s_mainIndex == i) ? ">" : " ", FONT_SMALL);
		/* 第 2 列起画菜单项名称 */
		OLED_STR((uint8_t)(3 + i), 2, (char *)MainItemName[i], FONT_SMALL);
	}

	OLED_STR(7, 1, "K1/K2: Move", FONT_SMALL);
	OLED_STR(8, 1, "K3:Enter  K4:Back", FONT_SMALL);
}

static void Draw_Led(void)
{
	OLED_Clear();
	OLED_STR(1, 1, "LED CTRL", FONT_SMALL);
	OLED_STR(3, 1, s_ledOn   ? "Both LED: ON " : "Both LED: OFF", FONT_SMALL);
	OLED_STR(4, 1, s_blinkOn ? "Blink   : ON " : "Blink   : OFF", FONT_SMALL);
	OLED_STR(7, 1, "K1:On/Off  K2:Blink", FONT_SMALL);
	OLED_STR(8, 1, "K4:Back", FONT_SMALL);
}

static void Draw_Info(void)
{
	OLED_Clear();
	OLED_STR(1, 1, "INFO", FONT_SMALL);
	OLED_STR(3, 1, "Name: ZHANG SAN", FONT_SMALL);	/* 换成你自己的姓名拼音 */
	OLED_STR(4, 1, "ID  : 30000000", FONT_SMALL);	/* 换成你自己的学号 */
	OLED_STR(5, 1, "a   : 0", FONT_SMALL);			/* 变量 a，第 3 项再实现串口修改 */
	OLED_STR(8, 1, "K4:Back", FONT_SMALL);
}

static void Draw_Line(void)
{
	OLED_Clear();
	OLED_STR(1, 1, "LINE FOLLOW", FONT_SMALL);
	OLED_STR(3, 1, "Sensor: --", FONT_SMALL);		/* 第 4 项 a：显示传感器状态 */
	OLED_STR(5, 1, "K1:Start", FONT_SMALL);			/* 第 4 项 b：发车 */
	OLED_STR(8, 1, "K4:Back", FONT_SMALL);
}

static void Draw_Ext(void)
{
	OLED_Clear();
	OLED_STR(1, 1, "EXTEND", FONT_SMALL);
	OLED_STR(3, 1, "Reserved", FONT_SMALL);
	OLED_STR(8, 1, "K4:Back", FONT_SMALL);
}
