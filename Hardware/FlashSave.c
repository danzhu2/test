#include "stm32f10x.h"
#include "FlashSave.h"

/* ============================================================================
 *  【新增文件】Flash 参数读写实现
 * ==========================================================================*/

/* ⚠️【需修改】参数存放页地址。C8T6（64KB）取最后一页 0x0800FC00。
 *    换芯片容量后必须重算：地址 = 0x08000000 + 总容量 - 页大小 */
#define PARAM_FLASH_ADDR	0x0800FC00
#define PARAM_MAGIC			0x32303236u		/* "2026"，任意自定，用于识别有效数据 */

Params_t g_params;

/* Flash 整页读出到内存：直接用指针读即可（Flash 的读和读 SRAM 一样） */
static void FlashRead(Params_t *out)
{
	uint16_t *src = (uint16_t *)PARAM_FLASH_ADDR;
	uint16_t *dst = (uint16_t *)out;
	uint8_t   i;

	for (i = 0; i < sizeof(Params_t) / 2; i++)
	{
		dst[i] = src[i];
	}
}

void Params_Load(void)
{
	FlashRead(&g_params);

	/* 魔数不对 = 第一次上电（或数据损坏），用默认值并标好魔数。
	 * 注意这里只改内存，不立刻写 Flash —— 写 Flash 会卡 CPU，
	 * 上电阶段卡一下倒也可以接受，但延后到第一次保存更稳妥。 */
	if (g_params.magic != PARAM_MAGIC)
	{
		g_params.magic      = PARAM_MAGIC;
		g_params.baseSpeed  = 450;		/* ⚠️【需修改】默认巡线速度：实测后调 */
		g_params.targetLaps = 1;
		g_params.kp         = 1200;		/* = Kp 12.0 */
		g_params.ki         = 0;		/* = Ki 0（新手先纯 P，稳了再加） */
		g_params.kd         = 3000;		/* = Kd 30.0 */
		g_params.reserve    = 0;
	}
}

uint8_t Params_Save(void)
{
	FLASH_Status status;
	uint16_t    *src = (uint16_t *)&g_params;
	uint8_t      i;

	/* 解锁 Flash 控制器（序列固定：KEY1 → KEY2，库函数已封装） */
	FLASH_Unlock();

	/* 擦除参数所在页。必须先擦：Flash 只能把 1 写成 0，擦除才把整页变回 1 */
	status = FLASH_ErasePage(PARAM_FLASH_ADDR);

	if (status == FLASH_COMPLETE)
	{
		/* 半字（16 位）逐个写入。Flash 编程函数自带等待忙标志 */
		for (i = 0; i < sizeof(Params_t) / 2; i++)
		{
			FLASH_ProgramHalfWord(PARAM_FLASH_ADDR + i * 2, src[i]);
		}
	}

	FLASH_Lock();

	/* 写完回读校验：逐字节比对内存与 Flash，不一致说明没存上。
	 * 返回 0 让上层（菜单）蜂鸣报警提示重新保存——
	 * 如果存错了不吱声，断电后参数悄悄变回默认值，考场上极难排查。 */
	{
		Params_t check;
		uint8_t *a = (uint8_t *)&g_params;
		uint8_t *b = (uint8_t *)&check;
		uint8_t  j;

		FlashRead(&check);
		for (j = 0; j < sizeof(Params_t); j++)
		{
			if (a[j] != b[j])
			{
				return 0;		/* 校验失败 */
			}
		}
	}
	return 1;					/* 保存成功 */
}
