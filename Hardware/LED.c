#include "stm32f10x.h"                  
#include "LED.h"
#include "tick.h"			//闪烁计时需要 1ms 时基 



/* 当前 = Bit_SET（高电平点亮）。
 * 若两灯极性不一致，要拆成 LED1_ON_LEVEL / LED2_ON_LEVEL 两个宏。
 * 第 2 项 d 要求"OLED 显示与实际亮灭一致"，极性错了这 1 分必丢，上电第一件事就验证它。 */
#define LED_ON_LEVEL	Bit_SET       //用宏定义替代高低电平
#define LED_OFF_LEVEL	Bit_RESET


/* ---------------------- 模式与闪烁状态 ---------------------- */

LedMode_t s_mode = LED_MODE_OFF;	/* 当前工作模式，上电默认全灭 */    //static_mode  static只在本.c 文件里可见，其他文件访问不了
uint32_t s_blinkTick  = 0;				/* 上一次翻转的时刻（ms） */
uint8_t  s_blinkPhase = 0;				//记录当前谁在亮，0 = LED1 亮；1 = LED2 亮 

/* ---------------------------------------------------------------------- */



void Led_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC | RCC_APB2Periph_GPIOB, ENABLE);
	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;			//推挽输出 
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Pin = LED1_GPIO_PIN;
	GPIO_Init(LED1_GPIO_PORT, &GPIO_InitStructure);
	GPIO_InitStructure.GPIO_Pin = LED2_GPIO_PIN;
	GPIO_Init(LED2_GPIO_PORT, &GPIO_InitStructure);
	s_mode = LED_MODE_OFF;			 //这个改的是显示在OLED上的变量
	Led_AllOff();                   //这个改的是实际上的，直接关灯
}


   //根据当前模式判定LED亮灭
void Led_SetMode(LedMode_t mode)   //  .h文件里的枚举变量LedMode_type   代表这是一个类型名，不是变量
{
	s_mode = mode;       //Menu 里面改这个mode变量

	switch (mode)
	{
	case LED_MODE_ON:
		/* 第 2 项 a：两个灯一起常亮 */
		Led_AllOn();
		break;

	case LED_MODE_BLINK:    //负责让灯进入闪烁第一帧，真正闪烁的在LED_task里
		/* 第 2 项 b：立刻进入"一个亮一个灭"的状态，不等第一个 500ms */
		s_blinkPhase = 0;
		s_blinkTick  = Tick_Get();		/* 从切换到闪烁这一刻开始计时 */
		Led1_On();
		Led2_Off();
		break;

	default:
		/* LED_MODE_OFF：两个灯一起常灭（第 2 项 c 也走这里） */
		Led_AllOff();
		break;
	}
}

LedMode_t Led_GetMode(void)    //返回当前LED模式
{
	return s_mode;
}

/* ============================================================================
 *  交替闪烁的任务函数（非阻塞）
 *
 *  为什么要单独做成"任务函数"而不是在按键里写 while + delay？
 *    第 2 项要求连续闪烁 3 轮 × 30 秒，期间按键必须还能用（K4 要能返回主菜单）。
 *    如果用 delay(500) 死等，那 500ms 里 CPU 什么都干不了，按键会失灵，
 *    而且后面加了巡线控制后会把控制周期彻底拖垮。
 *    所以这里只做一件事："现在到点了吗？到点就翻一下，然后立刻返回"。
 * ==========================================================================*/
void Led_Task(void)
{
	if (s_mode != LED_MODE_BLINK)
	{
		return;						/* 不在闪烁模式，一点时间都不花 */
	}

	if ((uint32_t)(Tick_Get() - s_blinkTick) >= LED_BLINK_HALF_MS)  //Tick_Get() 是上电以来的毫秒数，减去上次翻转时刻 
	{
		s_blinkTick += LED_BLINK_HALF_MS;		//每一个闪烁的时间    /* 按周期累加，长期不跑偏 */
		s_blinkPhase = (uint8_t)(!s_blinkPhase);      //翻转显示亮的LED

		/* 注意顺序：先把要亮的那个点亮，再灭掉另一个。
		 * 这样两次写 IO 之间的空档里是"两个都亮"（微秒级，肉眼看不出），
		 * 比反过来出现"两个都灭"的空档更不容易被摄像机抓到。 */
		if (s_blinkPhase)    //0 = LED1 亮；1 = LED2 亮    上面刚好翻转相位，反转后显示哪个亮，就开启哪个
		{
			Led2_On();
			Led1_Off();
		}
		else
		{
			Led1_On();
			Led2_Off();
		}
	}
}

/* ============================================================================
 *  读回引脚真实状态
 *
 *  GPIO_ReadOutputDataBit 读的是"输出数据寄存器"，也就是我们写进去的值，
 *  而不是引脚上的实际电压，但对 LED 这种负载足够可靠。
 *  第 2 项 d 要求"OLED 显示必须与 LED 实际状态一致"——显示就取这里的值，
 *  而不是取模式推断出来的值，这样两者天然一致。
 * ==========================================================================*/
uint8_t Led1_IsOn(void)
{
	return (GPIO_ReadOutputDataBit(LED1_GPIO_PORT, LED1_GPIO_PIN) == LED_ON_LEVEL) ? 1 : 0;  //判断LED的真实状态与函数状态是否一致
}

uint8_t Led2_IsOn(void)
{
	return (GPIO_ReadOutputDataBit(LED2_GPIO_PORT, LED2_GPIO_PIN) == LED_ON_LEVEL) ? 1 : 0;
}

/* ---------------------------- 单灯控制 ---------------------------- */


void Led1_On(void)
{
	GPIO_WriteBit(LED1_GPIO_PORT, LED1_GPIO_PIN, LED_ON_LEVEL);
}

void Led1_Off(void)
{
	GPIO_WriteBit(LED1_GPIO_PORT, LED1_GPIO_PIN, LED_OFF_LEVEL);
}

void Led1_Toggle(void)     //单灯反转，如果亮关掉，如果关着，亮
{
	/* 读出当前输出状态再取反 */
	if (GPIO_ReadOutputDataBit(LED1_GPIO_PORT, LED1_GPIO_PIN) == LED_ON_LEVEL)
		Led1_Off();
	else
		Led1_On();
}

void Led2_On(void)
{
	GPIO_WriteBit(LED2_GPIO_PORT, LED2_GPIO_PIN, LED_ON_LEVEL);
}

void Led2_Off(void)
{
	GPIO_WriteBit(LED2_GPIO_PORT, LED2_GPIO_PIN, LED_OFF_LEVEL);
}

void Led2_Toggle(void)
{
	if (GPIO_ReadOutputDataBit(LED2_GPIO_PORT, LED2_GPIO_PIN) == LED_ON_LEVEL)
		Led2_Off();
	else
		Led2_On();
}

void Led_AllOn(void)
{
	Led1_On();
	Led2_On();
}

void Led_AllOff(void)
{
	Led1_Off();
	Led2_Off();
}
