#include "stm32f10x.h"
#include "Buzzer.h"
#include "tick.h"

/* ============================================================================
 *  有源蜂鸣器驱动（非阻塞）
 *
 *  为什么不用 delay？
 *    发车时要求"蜂鸣一声 + 立刻开始巡线"。如果在这里 delay(200)，
 *    这 200ms 里主循环停摆，巡线控制第一次输出就要延迟 200ms，
 *    起步位置就偏了。所以这里只登记"要响几次、每次多久"，
 *    实际的响/停翻转放在 Buzzer_Task() 里按时间片完成。
 * ==========================================================================*/

/* ⚠️【需修改】蜂鸣器引脚：默认 PB3。
 *    PB3 上电默认是 JTAG 功能（JTDO），依赖 main() 里的"禁 JTAG 保留 SWD"解禁。
 *    原默认 PB14 已让给电机左方向脚（传感器占满了 PA 口低 8 位，只能这么排）。 */
#define BUZZER_GPIO_PORT	GPIOB
#define BUZZER_GPIO_PIN		GPIO_Pin_3

/* 蜂鸣状态机变量 */
uint8_t  s_beepLeft  = 0;		/* 还剩几次没响 */
uint8_t  s_phase     = 0;		/* 0 = 静音段, 1 = 响声段 */
uint16_t s_onMs      = 100;		/* 每次响的时长 */
uint32_t s_phaseTick = 0;		/* 当前段开始时刻 */

void BuzzerOn(void)
{
	GPIO_WriteBit(BUZZER_GPIO_PORT, BUZZER_GPIO_PIN, Bit_SET);		/* 高电平响 */
}

void BuzzerOff(void)
{
	GPIO_WriteBit(BUZZER_GPIO_PORT, BUZZER_GPIO_PIN, Bit_RESET);	/* 低电平停 */
}

void Buzzer_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;		/* 推挽输出，驱动三极管 */
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Pin   = BUZZER_GPIO_PIN;
	GPIO_Init(BUZZER_GPIO_PORT, &GPIO_InitStructure);

	BuzzerOff();
}

void Buzzer_Beep(uint8_t times, uint16_t onMs)
{
	if (times == 0)
	{
		return;
	}

	/* 如果上一轮还没响完，新请求直接覆盖（发车场景下覆盖是合理行为） */
	s_beepLeft  = times;
	s_onMs      = onMs;
	s_phase     = 1;					/* 先进入响声段 */
	s_phaseTick = Tick_Get();
	BuzzerOn();
}

void Buzzer_Task(void)
{
	uint32_t now = Tick_Get();

	if (s_phase == 1)
	{
		/* 响声段结束 → 转入静音段 */
		if ((uint32_t)(now - s_phaseTick) >= s_onMs)
		{
			BuzzerOff();
			s_phase     = 0;
			s_phaseTick = now;

			/* "先减后判"：这一次已经响完了 */
			s_beepLeft--;
		}
	}
	else
	{
		/* 静音段长度 = 响声段长度（响和停一样久，节奏清晰）。
		 * 静音段结束：还有剩余次数就再响，否则整个流程结束 */
		if ((uint32_t)(now - s_phaseTick) >= s_onMs)
		{
			if (s_beepLeft > 0)
			{
				s_phase     = 1;
				s_phaseTick = now;
				BuzzerOn();
			}
		}
	}
}
