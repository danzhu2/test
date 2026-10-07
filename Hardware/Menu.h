#ifndef __MENU_H
#define __MENU_H

#include "stm32f10x.h"

/* 页面编号：主菜单 + 4 个子菜单（对应赛题主菜单的 4 个菜单项） */
typedef enum
{
	PAGE_MAIN = 0,
	PAGE_LED,			/* LED 控制    */
	PAGE_INFO,			/* 信息显示    */
	PAGE_LINE,			/* 巡线功能    */
	PAGE_EXT			/* 拓展功能    */
} Page_t;

void Menu_Init(void);					/* 初始化：回到主菜单、箭头指向首行 */

/* 处理一次按键事件，key 取 KEY_1 ~ KEY_4（来自 key.h），无按键不要调用 */
void Menu_HandleKey(uint8_t key);

/* 主循环里反复调用：只有界面内容变化时才重绘 OLED，不占用多余时间 */
void Menu_Task(void);

Page_t Menu_GetPage(void);				/* 查询当前页面（供其他模块判断状态） */