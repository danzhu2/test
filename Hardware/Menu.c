#include "Menu.h"
#include "Key.h"
#include "LED.h"
#include "OLED.h"		
#include "util.h"			/*Util_IntToStr：把变量 a 转成字符串显示 */



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

/* ---------------------------- 主菜单表 ---------------------------------- */
#define OLED_STR(line, col, str)	OLED_ShowString((line), (col), (str))
/* 菜单项显示名：想加菜单项就往这个数组里加，同时给 MainItemPage[] 加对应页面 */
const char * const MainItemName[] =          //用数组储存要显示的内容，后面oled显示调用
{
	"LED Ctrl",
	"Info",
	"Line Follow",
	"Extend"
};

/* 每个菜单项指向的页面，顺序必须与 MainItemName[] 一一对应 */
const Page_t MainItemPage[] =            //这个数组是枚举类型，头文件要定义
{
	PAGE_LED,
	PAGE_INFO,
	PAGE_LINE,
	PAGE_EXT
};

#define MAIN_ITEM_NUM	(sizeof(MainItemName) / sizeof(MainItemName[0]))     //利用元素字节算出多少个元素

/* ---------------------------- 菜单状态 ---------------------------------- */

Page_t  s_page      = PAGE_MAIN;		/* 当前页面 */       //Page_t 是枚举类型，Page——main是里面一个元素
uint8_t s_mainIndex = 0;				/* 主菜单当前选中的行（0 = 首行） */
uint8_t s_dirty     = 1;				/* 1 = 界面需要重绘 */



/* 变量 a。上电初值必须是 0（评分点） */
int32_t s_a = 0;

/* 屏幕上"当前正显示"的两个 LED 状态。
 * 注意它记录的是真实引脚状态的一份拷贝，不是模式，用来和实际状态比对。 */
uint8_t s_shownLed1 = 0;
uint8_t s_shownLed2 = 0;



/* LED 子菜单内部状态（第 2 项的作用域） */
uint8_t s_ledOn     = 0;				/* 两个 LED 是否常亮 */
uint8_t s_blinkOn   = 0;				/* 是否处于交替闪烁（第 2 项 b 待实现） */

/* ---------------------------- 内部函数声明 ------------------------------ */

void Draw_Main(void);
void Draw_Led(void);
void Draw_Info(void);
void Draw_Line(void);
void Draw_Ext(void);

void Menu_EnterPage(Page_t page);
void Menu_BackToMain(void);
void Menu_LedSyncCheck(void);		

/* ---------------------------- 对外接口 ---------------------------------- */

void Menu_Init(void)
{
	s_page      = PAGE_MAIN;
	s_mainIndex = 0;			/* 默认箭头指向首行 */
	s_dirty     = 1;			/* 上电先画一次主菜单 */
	s_a         = 0;			/* 变量 a 上电初值 0 */
}

Page_t Menu_GetPage(void)
{
	return s_page;
}

/* 变量 a 的读写接口（供 serial.c 使用） */

int32_t Menu_GetA(void)
{
	return s_a;
}

void Menu_SetA(int32_t value)
{
	if (s_a != value)
	{
		s_a = value;

		/* 只有正在看信息页时才需要重画；在别的页面改了，
		 * 下次进入信息页 Menu_EnterPage() 会自己置脏，不会漏显示。 */
		if (s_page == PAGE_INFO)
		{
			s_dirty = 1;
		}
	}
}

void Menu_HandleKey(uint8_t key)
{
	switch (s_page)
	{
	/* ---------------- 主菜单：K1/K2 移动箭头，K3 进入 ---------------- */
	case PAGE_MAIN:
		if (key == KEY_1)
		{
			/* 上移一行，到顶后循环到底部（加 N-1 再取模 = 减 1 且不会出现负数） */
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

	/* ---------------- LED 控制子菜单（第 2 项） ---------------- */
	case PAGE_LED:
		if (key == KEY_4)
		{
			/* 第 2 项 c：先回到"常灭"再返回主菜单。
			 * 顺序很重要：必须先关灯，否则返回后灯还亮着，这一分就丢了 */
			Led_SetMode(LED_MODE_OFF);
			s_dirty = 1;
			Menu_BackToMain();
		}
		else if (key == KEY_1)
		{
			/* 第 2 项 a：常灭 -> 常亮；再按 -> 常灭。
			 * 从闪烁模式按 K1 也会切到常亮，符合"按一下就该两个都亮"的预期。 */
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
		else if (key == KEY_2)
		{
			/* 第 2 项 b：进入交替闪烁，具体翻转由主循环里的 Led_Task() 负责 */
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
		/* 变量 a 是串口改的，这个页面本身不需要按键操作 */
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
	/* LED 真实状态一变就置脏，
	 * 所以闪烁时 OLED 会跟着一起变，两者永远对得上（远快于题目要求的 1 秒）。 */
	if (s_page == PAGE_LED)
	{
		Menu_LedSyncCheck();
	}

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

void Menu_EnterPage(Page_t page)
{
	s_page  = page;
	s_dirty = 1;
}

void Menu_BackToMain(void)
{
	s_page  = PAGE_MAIN;
	s_dirty = 1;				/* 回到主菜单时箭头保持上次位置 */
}

/* ---------------------- LED 状态同步检查 ------------------------ */
/*
 * 思路：把"屏幕上显示的两灯状态"和"引脚真实状态"比一下，
 *       不一样就把 s_dirty 置 1，让 Menu_Task() 立刻重画。
 *
 * 好处是不用去猜"什么时候该刷新"：实际状态变了就刷，没变就一次也不刷。
 * 闪烁时每 500ms 变一次，于是 OLED 每 500ms 自动跟着更新一次。
 */
void Menu_LedSyncCheck(void)
{
	uint8_t l1 = Led1_IsOn();      //状态一致返回1，不一样0
	uint8_t l2 = Led2_IsOn();

	if (l1 != s_shownLed1 || l2 != s_shownLed2)
	{
		s_dirty = 1;      //重新绘制
	}
}

/* ---------------------------- 各页面绘制 -------------------------------- */

void Draw_Main(void)
{
	uint8_t i;

	OLED_Clear();

	OLED_STR(1, 1, "MAIN MENU");

	for (i = 0; i < MAIN_ITEM_NUM; i++)
	{
		/* 第 1 列画箭头：选中行显示 ">"，其余行显示空格 */
		OLED_STR((uint8_t)(3 + i), 1, (s_mainIndex == i) ? ">" : " ");
		/* 第 2 列起画菜单项名称 */
		OLED_STR((uint8_t)(3 + i), 2, (char *)MainItemName[i]);
	}

	OLED_STR(7, 1, "K1/K2: Move");
	OLED_STR(8, 1, "K3:Enter  K4:Back");
}

/* ---------------- LED 控制页（第 2 项 a / b / d） ---------------- */
void Draw_Led(void)
{
	/* 先把这一时刻的真实状态记下来，作为下一次比对的基准 */
	s_shownLed1 = Led1_IsOn();
	s_shownLed2 = Led2_IsOn();

	OLED_Clear();
	OLED_STR(1, 1, "LED CTRL");

	/* 第 2 行显示当前模式，方便自己调试时确认按键生效了 */
	switch (Led_GetMode())
	{
	case LED_MODE_ON:		OLED_STR(2, 1, "Mode: ON   "); break;
	case LED_MODE_BLINK:	OLED_STR(2, 1, "Mode: BLINK"); break;
	default:				OLED_STR(2, 1, "Mode: OFF  "); break;
	}

	/* 第 2 项 d：这两行显示的是"引脚此刻的真实状态"，不是模式推断值，
	 * 所以它必然和灯的实际亮灭一致 —— 分数就靠这里拿。 */
	OLED_STR(4, 1, s_shownLed1 ? "LED1: ON " : "LED1: OFF");
	OLED_STR(5, 1, s_shownLed2 ? "LED2: ON " : "LED2: OFF");

	OLED_STR(7, 1, "K1:On/Off K2:Blink");
	OLED_STR(8, 1, "K4:Back");
}

/* ---------------- 信息显示页（第 3 项 a） ---------------- */
static void Draw_Info(void)
{
	char buf[12];

	OLED_Clear();
	OLED_STR(1, 1, "INFO");

	OLED_STR(3, 1, "Name: " );		/* 姓名拼音 */
	OLED_STR(4, 1, "ID  : " );		/* 学号 */

	/* 变量 a：标签固定占前 5 列，数值从第 6 列开始画。
	 * 这里用自己写的 Util_IntToStr 而不是 OLED_ShowSignedNum，
	 * 是因为前者不会补前导零（a=0 就显示 "0"，而不是 "00000"），负数也能直接显示成 "-45"。 */
	Util_IntToStr(s_a, buf);      //转十进制数   buf  字符数组 
	OLED_STR(5, 1, "a   : ");
	OLED_STR(5, 6, buf);

	OLED_STR(8, 1, "K4:Back");
}

static void Draw_Line(void)
{
	OLED_Clear();
	OLED_STR(1, 1, "LINE FOLLOW");
	OLED_STR(3, 1, "Sensor: --");		/* 第 4 项 a：显示传感器状态 */
	OLED_STR(5, 1, "K1:Start");			/* 第 4 项 b：发车 */
	OLED_STR(8, 1, "K4:Back");
}

static void Draw_Ext(void)
{
	OLED_Clear();
	OLED_STR(1, 1, "EXTEND");
	OLED_STR(3, 1, "Reserved");
	OLED_STR(8, 1, "K4:Back");
}