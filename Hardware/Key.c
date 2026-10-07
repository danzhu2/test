#include "Key.h"
#include "Tick.h"

#define KEY_NUM			4
#define KEY_DEBOUNCE_MS	20			/* 消抖时间，抖动大的按键可加大到 30~50 */

/* 按键接线：一端接单片机 IO，一端接 GND，IO 配置为上拉输入 → 按下读到低电平。
 * 按键数量少的话可把 KEY3 / KEY4 的引脚留空，但赛题要求 4 个按键，这里全部保留。 */
typedef struct
{
	GPIO_TypeDef *port;
	uint16_t      pin;
} KeyPin_t;

static const KeyPin_t KeyPin[KEY_NUM] =
{
	{GPIOB, GPIO_Pin_0 },			/* KEY1  -> PB0  */
	{GPIOB, GPIO_Pin_1 },			/* KEY2  -> PB1  */
	{GPIOB, GPIO_Pin_10},			/* KEY3  -> PB10 */
	{GPIOB, GPIO_Pin_11}			/* KEY4  -> PB11 */
};

void Key_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IPU;				/* 上拉输入 */
	GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_10 | GPIO_Pin_11;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB, &GPIO_InitStructure);
}

/* 读原始电平：1 = 松开，0 = 按下 */
static uint8_t Key_ReadRaw(uint8_t index)
{
	return GPIO_ReadInputDataBit(KeyPin[index].port, KeyPin[index].pin) ? 1 : 0;
}

uint8_t Key_Scan(void)
{
	static uint8_t  rawLast[KEY_NUM]    = {1, 1, 1, 1};		/* 上一次读到的原始电平 */
	static uint8_t  stable[KEY_NUM]     = {1, 1, 1, 1};		/* 已确认的稳定状态 */
	static uint32_t changeTick[KEY_NUM] = {0};				/* 电平最后一次变化的时刻 */

	uint32_t now = Tick_Get();
	uint8_t  i;

	for (i = 0; i < KEY_NUM; i++)
	{
		uint8_t raw = Key_ReadRaw(i);

		if (raw != rawLast[i])
		{
			/* 电平刚变化，重新开始计时（抖动期间会反复刷新） */
			rawLast[i]    = raw;
			changeTick[i] = now;
		}
		else if ((now - changeTick[i]) >= KEY_DEBOUNCE_MS && stable[i] != raw)
		{
			/* 连续 20ms 保持同一电平 → 确认状态 */
			stable[i] = raw;

			if (raw == 0)
			{
				return (uint8_t)(i + 1);		/* 只在按下瞬间产生一次按键事件 */
			}
		}
	}

	return KEY_NONE;
}
	