#include "led.h"

/* 低电平点亮：写 0 → 亮，写 1 → 灭。
 * 如果你的 LED 是高电平点亮，把下面两个宏里的 Bit_RESET / Bit_SET 对调即可。 */
#define LED_ON_LEVEL	Bit_SET
#define LED_OFF_LEVEL	Bit_RESET

void Led_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC,RCC_APB2Periph_GPIOB, ENABLE);
	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;			/* 推挽输出 */
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Pin = LED1_GPIO_PIN;
	GPIO_Init(LED1_GPIO_PORT, &GPIO_InitStructure);
	GPIO_InitStructure.GPIO_Pin = LED2_GPIO_PIN;
	GPIO_Init(LED2_GPIO_PORT, &GPIO_InitStructure);
	Led_AllOff();
}

void Led1_On(void)
{
	GPIO_WriteBit(LED1_GPIO_PORT, LED1_GPIO_PIN, LED_ON_LEVEL);
}

void Led1_Off(void)
{
	GPIO_WriteBit(LED1_GPIO_PORT, LED1_GPIO_PIN, LED_OFF_LEVEL);
}

void Led1_Toggle(void)
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

void Led_AllOff(void)
{
	Led1_Off();
	Led2_Off();
}
