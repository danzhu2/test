#ifndef __TICK_H
#define __TICK_H

#include "stm32f10x.h"

/* 1ms 时基：用 TIM2 更新中断产生，供按键消抖、计时等功能使用 */
void     Tick_Init(void);
uint32_t Tick_Get(void);        /* 返回上电以来的毫秒数 */

#endif
