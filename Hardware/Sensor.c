#include "stm32f10x.h"
#include "Sensor.h"

/* ============================================================================
 *  8 路灰度传感器：读取 + 加权误差计算
 *
 *  加权误差的原理（巡线算法的核心输入）：
 *    把 8 个传感器按横向位置赋权重：-7 -5 -3 -1  +1 +3 +5 +7
 *    误差 = Σ(权重×压线) / Σ(压线个数)
 *    - 只有 S4 压线：误差 = -1/1 = -1（线略偏左，小左转）
 *    - 只有 S8 压线：误差 = +7（线大幅偏右，急右转）
 *    - S4+S5 压线  ：误差 = 0（正好居中）
 *
 *    为什么要除以 Σ(压线个数)？
 *    弯道上往往两三个相邻传感器同时压线，除一下相当于取"加权平均位置"，
 *    误差大小就不会因为同时压线的个数变化而跳变，PID 输入更平滑。
 * ==========================================================================*/

/* 8 路对应的引脚号：S1..S8 = PA0..PA7。⚠️【需修改】接线顺序不同时改这里 */
static const uint16_t SensorPin[8] =
{
	GPIO_Pin_0, GPIO_Pin_1, GPIO_Pin_2, GPIO_Pin_3,
	GPIO_Pin_4, GPIO_Pin_5, GPIO_Pin_6, GPIO_Pin_7
};

/* 与引脚一一对应的横向权重（半格单位） */
static const int8_t SensorWeight[8] = { -7, -5, -3, -1, 1, 3, 5, 7 };

void Sensor_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IPU;		/* 上拉输入：模块推挽输出时安全，悬空也不乱跳 */
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3 |
	                                GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7;
	GPIO_Init(SENSOR_GPIO_PORT, &GPIO_InitStructure);
}

uint8_t Sensor_Read(void)
{
	/* 一次性读整个端口再按位比对，比循环 8 次 GPIO_ReadInputDataBit 快得多 */
	uint16_t port = GPIO_ReadInputData(SENSOR_GPIO_PORT);         //port 端口
	uint8_t  word = 0;              //计算机里word 代表一组连续的比特
	uint8_t  i;

	for (i = 0; i < 8; i++)
	{
		/* 该路引脚电平 == 压线电平 → 记 1（压线）。
		 * 注意极性由 SENSOR_LINE_LEVEL 宏统一翻转，别在别处再取反 */
		if (((port & SensorPin[i]) ? 1 : 0) == SENSOR_LINE_LEVEL)
		{
			word |= (uint8_t)(1u << i);     //1u   unsigned 1   无符号一，进位不会错误    << 是左移运算符  把1移到i位
			}        //   |=   按位或     word=word | 1u<<i         例如 i=2    0b00000010    只有第i位为1   专门用来标记第i路传感器
	}                                 

	return word;    //word是一个连续的存储器，用或的逻辑就可以存下每一路的状态，不会覆盖前面的    //  0001 | 0010    =0011
}

float Sensor_GetError(void)
{
	uint8_t word = Sensor_Read();
	int32_t sum  = 0;
	int32_t cnt  = 0;
	int8_t  i;

	for (i = 0; i < 8; i++)
	{
		if (word & (1u << i))
		{
			sum += SensorWeight[i];
			cnt++;
		}
	}

	if (cnt == 0)
	{
		return 0.0f;		/* 全丢线：误差无意义，调用方用"全丢线"分支处理 */
	}

	/* cnt==8（全压线）时误差也是 0，正好对应"十字直行"的策略 */
	return (float)sum / (float)cnt;
}

uint8_t Sensor_CountOn(void)     //有多少压线
{
	uint8_t word = Sensor_Read();
	uint8_t cnt  = 0;
	uint8_t i;

	for (i = 0; i < 8; i++)
	{
		if (word & (1u << i))
		{
			cnt++;
		}
	}

	return cnt;
}
