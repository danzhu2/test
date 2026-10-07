#ifndef __KEY_H
#define __KEY_H

#include "stm32f10x.h"

/* 按键编号（与赛题"按键 1/2/3/4"一一对应） */
#define KEY_NONE	0
#define KEY_1		1
#define KEY_2		2
#define KEY_3		3
#define KEY_4		4

void    Key_Init(void);

/* 非阻塞扫描：有按键按下时返回 1~4（按下瞬间只返回一次），无按键返回 KEY_NONE
 * 必须在主循环里反复调用，内部依赖 Tick_Get() 做 20ms 消抖 */
uint8_t Key_Scan(void);

#endif