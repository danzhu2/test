#include "stm32f10x.h"
#include "Encoder.h"

/* ============================================================================
 *  【新增文件】编码器接口模式实现
 *
 *  几个容易踩的点：
 *   1. TIM3/TIM4 挂在 APB1，时钟使能要用 RCC_APB1PeriphClockCmd（用错外设不转）。
 *   2. 输入脚要配成上拉输入（编码器开漏输出/长线时更稳）。
 *   3. TIM_SetAutoreload 把 ARR 拉满到 0xFFFF：16 位计数器循环计数，
 *      读的时候用 (int16_t) 强转，硬件 wrap-around 会自动抵消，不用手动处理。
 *   4. 编码器接口模式是"硬件计数"，GPIO 别再复用成别的功能。
 * ==========================================================================*/

/* ⚠️【需修改】某个轮子读出来方向反了（前进时是负数）就把对应宏改 1 */
#define ENC_L_INVERT	0
#define ENC_R_INVERT	1

static void EncoderTimInit(TIM_TypeDef *tim, uint16_t ch1Pin, uint16_t ch2Pin,
                           GPIO_TypeDef *port, uint8_t apb1)
{
	GPIO_InitTypeDef        GPIO_InitStructure;
	TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
	TIM_ICInitTypeDef       TIM_ICInitStructure;

	RCC_APB1PeriphClockCmd(apb1, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOA, ENABLE);

	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IPU;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Pin   = ch1Pin | ch2Pin;
	GPIO_Init(port, &GPIO_InitStructure);

	TIM_TimeBaseInitStructure.TIM_ClockDivision     = TIM_CKD_DIV1;
	TIM_TimeBaseInitStructure.TIM_CounterMode       = TIM_CounterMode_Up;
	TIM_TimeBaseInitStructure.TIM_Prescaler         = 0;
	TIM_TimeBaseInitStructure.TIM_Period            = 0xFFFF;	/* 拉满，靠 int16 回绕 */
	TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0;
	TIM_TimeBaseInit(tim, &TIM_TimeBaseInitStructure);

	/* 编码器接口模式的关键配置：TI1 和 TI2 都计数（×4 倍频），极性不反相 */
	TIM_EncoderInterfaceConfig(tim, TIM_EncoderMode_12,
	                           TIM_ICPolarity_Rising, TIM_ICPolarity_Rising);

	TIM_ICStructInit(&TIM_ICInitStructure);
	TIM_ICInitStructure.TIM_ICFilter = 0x0A;		/* 数字滤波：滤掉电机火花造成的毛刺 */
	TIM_ICInit(tim, &TIM_ICInitStructure);

	TIM_Cmd(tim, ENABLE);
}

void Encoder_Init(void)
{
	/* ⚠️【关键】TIM3 默认脚 PA6/PA7 已被传感器 S7/S8 占用，必须部分重映射到 PB4/PB5。
	 * 而 PB4/PB5 上电默认是 JTAG 功能，要先"禁用 JTAG（保留 SWD 下载调试）"才能当普通 IO。
	 * 注意：JTAG 禁用必须在 GPIO_PinRemapConfig 之前做，且本工程 Motor(PA15)/Buzzer(PB3)
	 * 也依赖这一解禁 —— 所以这两句统一放在 main() 初始化第一步，这里只保留提醒。 */
	EncoderTimInit(TIM3, GPIO_Pin_4, GPIO_Pin_5, GPIOB, RCC_APB1Periph_TIM3);	/* 左：PB4/PB5（重映射） */
	EncoderTimInit(TIM4, GPIO_Pin_6, GPIO_Pin_7, GPIOB, RCC_APB1Periph_TIM4);	/* 右：PB6/PB7 */
}

static int16_t EncoderRead(TIM_TypeDef *tim, uint8_t invert)
{
	int16_t cnt = (int16_t)TIM_GetCounter(tim);		/* uint16 强转 int16：回绕自动正确 */

	TIM_SetCounter(tim, 0);							/* 读完清零，下次读到的是增量 */

	return invert ? (int16_t)(-cnt) : cnt;
}

int16_t Encoder_GetLeft(void)
{
	return EncoderRead(TIM3, ENC_L_INVERT);
}

int16_t Encoder_GetRight(void)
{
	return EncoderRead(TIM4, ENC_R_INVERT);
}
