#ifndef __LED_H
#define __LED_H

#include "stm32f10x.h"

/* 两个 LED 的引脚定义：按你的实际接线修改。
 * 这里的接法默认"低电平点亮"（IO 输出低 → LED 亮）。 */
#define LED1_GPIO_PORT	GPIOC
#define LED1_GPIO_PIN	GPIO_Pin_13		/* 最小系统板板载 LED（PC13） */

#define LED2_GPIO_PORT	GPIOB
#define LED2_GPIO_PIN	GPIO_Pin_12		/* 外接 LED，串 1k 电阻到 3.3V 或 GND，按实际改 */

void Led_Init(void);
void Led1_On(void);
void Led1_Off(void);
void Led1_Toggle(void);
void Led2_On(void);
void Led2_Off(void);
void Led2_Toggle(void);
void Led_AllOff(void);
