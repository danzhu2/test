#ifndef __LED_H
#define __LED_H

#include "stm32f10x.h"

/* 两个 LED 的引脚定义：按你的实际接线修改。
  这里的接法默认"低电平点亮"（IO 输出低 → LED 亮）*/
#define LED1_GPIO_PORT	GPIOC
#define LED1_GPIO_PIN	GPIO_Pin_13		//宏定义引脚和端口，改引脚只需要在定义处改

#define LED2_GPIO_PORT	GPIOB
#define LED2_GPIO_PIN	GPIO_Pin_12		
/* 交替闪烁的半周期（毫秒）：500 → 每秒完成一个完整交替周期。
 * 数值越大闪得越慢；想闪快一点改这里即可。 */
#define LED_BLINK_HALF_MS	500

/* 三种工作模式*/
typedef enum
{
	LED_MODE_OFF = 0,		/* 两个都常灭（上电默认 & 返回主菜单时） */
	LED_MODE_ON,			/* 两个都常亮（第 2 项 a） */
	LED_MODE_BLINK			/* 交替闪烁（第 2 项 b） */
} LedMode_t;

void      Led_Init(void);

/* 主循环里反复调用，用来推进交替闪烁的时序（非阻塞，不闪烁时立刻返回） */
void      Led_Task(void);

/* 切换工作模式：一次把两个 LED 设成该模式应有的状态 */
void      Led_SetMode(LedMode_t mode);
LedMode_t Led_GetMode(void);

/* 读回引脚的真实输出状态（1 = 亮，0 = 灭）——第 2 项 d 靠它核对 */
uint8_t   Led1_IsOn(void);
uint8_t   Led2_IsOn(void);

void Led_Init(void);
void Led1_On(void);
void Led1_Off(void);
void Led1_Toggle(void);
void Led2_On(void);
void Led2_Off(void);
void Led2_Toggle(void);
void Led_AllOff(void);
void Led_AllOn(void);
#endif
