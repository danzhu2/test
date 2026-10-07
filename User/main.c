#include "stm32f10x.h"			
#include "oled.h"				
#include "tick.h"				
#include "key.h"				
#include "led.h"				
#include "menu.h"				


int main(void)
{
	Tick_Init();			
	Key_Init();				
	Led_Init();				
	OLED_Init();			

	Menu_Init();			
	Menu_Task();			

	while (1)
	{
		/* 1. 取一次按键事件：没有按键时立刻返回 KEY_NONE，不会阻塞 */
		uint8_t key = Key_Scan();

		/* 2. 有按键就交给菜单状态机处理 */
		if (key != KEY_NONE)
		{
			Menu_HandleKey(key);
		}

		/* 3. 界面需要更新时才重绘（内部有 s_dirty 标志判断） */
		Menu_Task();
	}
}
