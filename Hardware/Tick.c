#include "tick.h"

volatile uint32_t s_tick = 0;

/* 说明：TIM2 挂在 APB1 上，APB1 分频系数不为 1 时定时器时钟为 72MHz。
 * 72MHz / 7200 = 10kHz  →  计数 10 次 = 1ms  */
void Tick_Init(void)   //初始化内部时钟
{
	TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
	NVIC_InitTypeDef  NVIC_InitStructure;

	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

	TIM_InternalClockConfig(TIM2);							/* 使用内部时钟 */

	TIM_TimeBaseInitStructure.TIM_ClockDivision     = TIM_CKD_DIV1;
	TIM_TimeBaseInitStructure.TIM_CounterMode       = TIM_CounterMode_Up;
	TIM_TimeBaseInitStructure.TIM_Period            = 10 - 1;		/* ARR */
	TIM_TimeBaseInitStructure.TIM_Prescaler         = 7200 - 1;		/* PSC */
	TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0;
	TIM_TimeBaseInit(TIM2, &TIM_TimeBaseInitStructure);

	TIM_ClearFlag(TIM2, TIM_FLAG_Update);					/* 先清标志，避免一使能就进中断 */
	TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);

	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
	NVIC_InitStructure.NVIC_IRQChannel                   = TIM2_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
	NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

	TIM_Cmd(TIM2, ENABLE);
}

uint32_t Tick_Get(void)
{
	return s_tick;
}

/* 中断服务函数名固定，必须与启动文件中的向量名一致 */
void TIM2_IRQHandler(void)
{
	if (TIM_GetITStatus(TIM2, TIM_IT_Update) == SET)    //判断是否是更新中断
	{
		s_tick++;
		TIM_ClearITPendingBit(TIM2, TIM_FLAG_Update);
	}
}

