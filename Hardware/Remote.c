#include "stm32f10x.h"
#include "Remote.h"
#include "Motor.h"
#include "LineFollow.h"
#include "FlashSave.h"

/* ============================================================================
 * 遥控实现
 *
 *  为什么遥控和巡线要互斥？
 *    两边都在直接写电机。如果巡线 PID 每 5ms 覆盖一次遥控的输出，
 *    遥控就会"完全没反应"；反过来遥控占着电机巡线就跑不了。
 *    所以规定：巡线运行中遥控指令忽略；要遥控必须先在巡线页让车停稳
 *    （或按 STOP）。这就是"资源只有一个，使用权必须唯一"。
 * ==========================================================================*/

/* 遥控速度（PWM 千分比）：固定值比用 baseSpeed 更安全，
 * 遥控时通常要慢一点好控。⚠️【需修改】嫌慢可调大，上限 1000 */
#define REMOTE_SPEED		400

/* 保持当前输出（按下 FWD 后没有新指令也持续前进），直到收到下一条指令 */
const char *s_state = "IDLE";

void SetState(const char *st)
{
	s_state = st;

	/* 回传状态给上位机 —— 拓展 6.3 评分点"上位机显示当前运动状态" */
	/* 注意：这里不能直接调 Serial_SendString（会循环包含），
	 * 由 serial.c 在路由时统一回传。 */
}

void Remote_Handle(const char *cmd)
{
	if (Line_IsRunning())
	{
		return;						/* 巡线运行中，遥控一律忽略 */
	}

	/* cmd 已被 serial.c 转成大写 */
	if (0)       //如果想要预留一段命令，就写在这里，想要启用直接改0即可
	{
	}
	else if (cmd[0] == 'F' && cmd[1] == 'W' && cmd[2] == 'D')
	{
		Motor_Set(REMOTE_SPEED, REMOTE_SPEED);
		SetState("FWD");
	}
	else if (cmd[0] == 'B' && cmd[1] == 'A' && cmd[2] == 'C' && cmd[3] == 'K')
	{
		Motor_Set(-REMOTE_SPEED, -REMOTE_SPEED);
		SetState("BACK");
	}
	else if (cmd[0] == 'L' && cmd[1] == 'E' && cmd[2] == 'F' && cmd[3] == 'T')
	{
		/* 左转 = 左轮慢右轮快（差速） */
		Motor_Set(-REMOTE_SPEED / 2, REMOTE_SPEED);
		SetState("LEFT");
	}
	else if (cmd[0] == 'R' && cmd[1] == 'I' && cmd[2] == 'G' && cmd[3] == 'H' && cmd[4] == 'T')
	{
		Motor_Set(REMOTE_SPEED, -REMOTE_SPEED / 2);
		SetState("RIGHT");
	}
	else if (cmd[0] == 'S' && cmd[1] == 'T' && cmd[2] == 'O' && cmd[3] == 'P')
	{
		Motor_Brake();
		SetState("STOP");
	}
	/* 其他指令不动作也不改状态 */
}

void Remote_Reset(void)
{
	s_state = "IDLE";
}

const char *Remote_GetState(void)
{
	return s_state;
}
