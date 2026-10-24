#include "stm32f10x.h"
#include "LineFollow.h"
#include "Sensor.h"
#include "Motor.h"
#include "Pid.h"
#include "Buzzer.h"
#include "FlashSave.h"
#include "tick.h"

/* ============================================================================
 *  巡线核心实现
 *
 *  总体控制结构（每 5ms 执行一次）：
 *
 *      Sensor_Read() 读 8 路灰度
 *            ↓
 *      判断"现在是什么路况"（普通 / 全黑 / 全白 / 极端偏线）
 *            ↓
 *      普通路况：加权误差 err → 转向 PID → 差速输出
 *      特殊路况：按规则改写 err 或直接接管电机（状态机）
 *            ↓
 *      Motor_Set(左轮, 右轮)
 *
 *  ⚠️ 大实话：这套规则在"标准元素"上能跑通，但真实赛道上每个元素
 *     都要现场调阈值/速度。所有需要实测调整的量都集中在本文件顶部的
 *     "调参区"，考场上 90% 的调试就是改这几个数字。
 * ==========================================================================*/

/* ============================================================================
 *  ⚠️【需修改】调参区 —— 所有需要上赛道实测的参数都集中在这里
 * ==========================================================================*/

#define CTRL_PERIOD_MS		5		/* 控制周期：5ms = 200Hz，足够跟上车速 */

/* 发车保护：刚发车 3 秒内不计圈（防止起步时压在起点横杠上立刻 +1 圈） */
#define START_GUARD_MS		3000

/* 起点横杠/十字的识别冷却：两次"全黑"至少隔这么久才算两次事件 */
#define BAR_COOLDOWN_MS		2500

/* 断路找回时间：全丢线超过这个时间还找不到线 → 判定失败停车 */
#define LOST_TIMEOUT_MS		1200

/* 直角判定：最外侧传感器单独压线持续这么久 → 进入原地转向 */
#define ANGLE_HOLD_MS		40

/* 原地转向超时：转这么久中心还没见到线 → 判定失败停车 */
#define PIVOT_TIMEOUT_MS	1500

/* 原地转向力度（PWM 千分比） */
#define PIVOT_SPEED			450

/* 断路时的"记忆寻线"差速比例（基础速度的百分比） */
#define LOST_STEER_RATIO	60

/* -------------------------------------------------------------------------- */

static Pid_t    s_steerPid;				/* 转向环 PID */
static int16_t  s_baseSpeed  = 450;		/* 基础速度（发车时从 g_params 载入） */

static uint8_t  s_running   = 0;
static uint8_t  s_finished  = 0;
static uint8_t  s_laps      = 0;		/* 已完成圈数 */
static int16_t  s_targetLaps= 1;

static uint32_t s_startTick   = 0;		/* 发车时刻 */
static uint32_t s_finishTick  = 0;		/* 完赛时刻 */
static uint32_t s_lastCtrlTick= 0;
static uint32_t s_lastBarTick = 0;		/* 上一次全黑事件的时刻 */
static uint32_t s_lostStart   = 0;		/* 开始丢线的时刻 */
static uint32_t s_angleStart  = 0;		/* 直角特征开始出现的时刻 */

static float    s_lastErr   = 0.0f;		/* 上一次有效误差（断路时用来判断往哪边找线） */

/* 原地转向状态：0 = 没在转；1 = 向左转；2 = 向右转 */
static uint8_t  s_pivot = 0;
static uint32_t s_pivotStart = 0;

/* -------------------------------------------------------------------------- */
/*  内部小工具                                                              */
/* -------------------------------------------------------------------------- */

static void FailStop(void)
{
	/* 失败（找不到线/转向超时）：刹车停住，退出运行态。
	 * 不清 finished——让界面能看出"没完成"。 */
	Motor_Brake();
	s_running = 0;
}

static void FinishRun(void)
{
	Motor_Brake();
	s_finishTick = Tick_Get();
	s_running    = 0;
	s_finished   = 1;
}

/* 原地转向（直角用）：向左 = 右轮前进、左轮后退 */
static void PivotStart(uint8_t dir)
{
	s_pivot      = dir;
	s_pivotStart = Tick_Get();

	if (dir == 1)			/* 左转 */
	{
		Motor_Set(-PIVOT_SPEED, PIVOT_SPEED);
	}
	else					/* 右转 */
	{
		Motor_Set(PIVOT_SPEED, -PIVOT_SPEED);
	}
}

/* -------------------------------------------------------------------------- */
/*  对外接口                                                                */
/* -------------------------------------------------------------------------- */

void Line_Init(void)
{
	Line_ApplyParams();
}

void Line_ApplyParams(void)
{
	/* g_params 里存的是 ×100 的整数，这里除回来。
	 * 用整数存储参数是为了 Flash 保存方便（不存浮点），展示/修改也直观 */
	s_baseSpeed = g_params.baseSpeed;
	s_targetLaps= g_params.targetLaps;

	Pid_Init(&s_steerPid,
	         (float)g_params.kp / 100.0f,
	         (float)g_params.ki / 100.0f,
	         (float)g_params.kd / 100.0f,
	         600.0f);			/* 输出限幅：转向修正最大 ±600，防止一脚打死 */
}

void Line_StartRun(void)
{
	if (s_running)
	{
		return;						/* 已经在跑，重复发车忽略 */
	}

	Pid_Reset(&s_steerPid);			/* 清掉上一轮的积分残留，否则起步就偏 */

	s_laps         = 0;
	s_finished     = 0;
	s_pivot        = 0;
	s_lastErr      = 0.0f;
	s_startTick    = Tick_Get();
	s_lastBarTick  = s_startTick;	/* 从发车那刻开始算冷却 */
	s_lastCtrlTick = 0;				/* 强制本圈主循环立刻执行一次控制 */
	s_running      = 1;

	/* 赛题 4b：发车蜂鸣响一声。200ms 足够摄像机和裁判听清 */
	Buzzer_Beep(1, 200);
}

uint8_t  Line_IsRunning(void)   { return s_running;  }
uint8_t  Line_IsFinished(void)  { return s_finished; }
uint8_t  Line_GetLaps(void)     { return s_laps;     }

uint32_t Line_GetElapsedMs(void)
{
	if (s_finished)
	{
		return s_finishTick - s_startTick;
	}
	if (s_running)
	{
		return Tick_Get() - s_startTick;
	}
	return 0;
}

/* -------------------------------------------------------------------------- */
/*  核心：每 5ms 一次的路况判断与控制                                        */
/* -------------------------------------------------------------------------- */

void Line_Task(void)
{
	uint32_t now;
	uint8_t  word;
	uint8_t  onCount;

	if (!s_running)
	{
		return;						/* 没在跑：零开销返回 */
	}

	now = Tick_Get();
	if ((uint32_t)(now - s_lastCtrlTick) < CTRL_PERIOD_MS)
	{
		return;						/* 还没到下一个控制周期 */
	}
	s_lastCtrlTick = now;

	word     = Sensor_Read();       //读取传感器电平
	onCount  = 0;
	{
		uint8_t i;
		for (i = 0; i < 8; i++)
		{
			if (word & (1u << i)) onCount++;
		}
	}

	/* ============ 分支 0：正在原地转向（直角处理中） ============ */
	if (s_pivot != 0)
	{
		/* 退出条件：中心两路（S4/S5）任意一路见到线 → 已经转到位 */
		if (word & 0x18)
		{
			s_pivot = 0;
			/* 不立刻给 PID 输出，落到下面正常分支继续跟线 */
		}
		else if ((uint32_t)(now - s_pivotStart) >= PIVOT_TIMEOUT_MS)
		{
			FailStop();				/* 转了 1.5 秒还没见到线：方向判断错了/速度太快 */
			return;
		}
		else
		{
			return;					/* 保持当前转向电机输出，下个周期再查 */
		}
	}

	/* ============ 分支 1：全黑（十字 / 起点横杠 / 圆环出入口） ============ */
	if (onCount == 8)
	{
		/* 事件的两个用途必须区分：
		 *  a) 起点横杠 → 计圈（有冷却 + 起步保护，防重复计数）
		 *  b) 十字/圆环出入口 → 直行通过（误差置 0 即可）
		 *
		 * ⚠️【已知简化·务必读懂】当前策略把所有"全黑"都当成潜在的起点横杠：
		 *    只要距上次全黑超过 BAR_COOLDOWN_MS(2.5s) 就 +1 圈。
		 *    风险：复杂地图上"十字 → 直行 3 秒 → 又遇十字"会被误记一圈，
		 *    导致没到终点就提前停车（4e 丢分点）。
		 *    两种升级方案（赛道实测后再决定要不要做）：
		 *      方案 A：起点横杠比十字粗 → 全黑持续时间长，按时长区分；
		 *      方案 B：用编码器里程估算"已跑约一圈"时才允许计圈（更可靠）。
		 *    基础 4b/4c（无特殊元素的简单圈）用当前策略完全够用；
		 *    跑 4e 固定地图前，务必数一遍地图上十字与起点的时间间隔再定阈值。 */
		if ((uint32_t)(now - s_lastBarTick) >= BAR_COOLDOWN_MS &&
		    (uint32_t)(now - s_startTick)   >= START_GUARD_MS)
		{
			s_lastBarTick = now;
			s_laps++;

			Buzzer_Beep(1, 150);	/* 4b：完赛/4c：第一圈蜂鸣提示 */

			if (s_laps >= s_targetLaps)
			{
				FinishRun();		/* 跑满目标圈数 → 停车，计时自动定格 */
				return;
			}
		}

		/* 十字/圆环出入口：径直穿过（中心误差为 0），不做任何转向 */
		s_lastErr = 0.0f;
		Motor_Set(s_baseSpeed, s_baseSpeed);
		return;
	}

	/* ============ 分支 2：全白（断路 / 冲出弯道） ============ */
	if (onCount == 0)
	{
		/* 记忆寻线：用丢线前的误差方向判断线大概在哪边，
		 * 内侧轮减速外侧轮加速，画弧线找回赛道。 */
		if (s_lastErr > 0.0f)
		{
			/* 线在右侧：向右画弧 */
			Motor_Set(s_baseSpeed + s_baseSpeed * LOST_STEER_RATIO / 100,
			          s_baseSpeed - s_baseSpeed * LOST_STEER_RATIO / 100);
		}
		else
		{
			Motor_Set(s_baseSpeed - s_baseSpeed * LOST_STEER_RATIO / 100,
			          s_baseSpeed + s_baseSpeed * LOST_STEER_RATIO / 100);
		}

		/* 超时保护：断路再长也是有限长度，1.2 秒还找不到说明真冲出去了 */
		if (s_lostStart == 0)
		{
			s_lostStart = now;
		}
		else if ((uint32_t)(now - s_lostStart) >= LOST_TIMEOUT_MS)
		{
			FailStop();
		}
		return;
	}

	/* 走到这里说明回到正常路况，清掉丢线计时 */
	s_lostStart = 0;

	/* ============ 分支 3：直角特征（最外侧单边压线且持续） ============ */
	{
		/* S1 单独（或带 S2）压线 = 线在极左侧，大概率是左直角；
		 * S8 同理是右直角。用"持续 ANGLE_HOLD_MS"过滤掉普通急弯的瞬时触发。 */
		uint8_t leftAngle  = (uint8_t)((word & 0x01) && ((word & 0x3C) == 0));
		uint8_t rightAngle = (uint8_t)((word & 0x80) && ((word & 0x3C) == 0));

		if (leftAngle || rightAngle)
		{
			if (s_angleStart == 0)
			{
				s_angleStart = now;					/* 开始计时观察 */
			}
			else if ((uint32_t)(now - s_angleStart) >= ANGLE_HOLD_MS)
			{
				s_angleStart = 0;
				PivotStart(leftAngle ? 1 : 2);		/* 确认直角 → 原地转向 */
				return;
			}
		}
		else
		{
			s_angleStart = 0;						/* 特征消失，重新观察 */
		}
	}

	/* ============ 分支 4：普通路况 → 转向 PID ============ */
	{
		float err   = Sensor_GetError();
		float steer = Pid_Update(&s_steerPid, err);

		/* 差速合成：
		 *   err > 0 = 线偏右 → steer > 0 → 左轮加速右轮减速 → 车向右转。
		 * ⚠️【需修改】如果装好后发现"越偏越远"（正反馈），说明差速方向接反，
		 *    把下面两行的 + 和 - 对调即可（或改 Motor.c 的方向宏）。 */
		int16_t l = (int16_t)(s_baseSpeed + steer);
		int16_t r = (int16_t)(s_baseSpeed - steer);

		if (l < 0)    l = 0;
		if (r < 0)    r = 0;
		if (l > 1000) l = 1000;
		if (r > 1000) r = 1000;

		Motor_Set(l, r);

		s_lastErr = err;		/* 记住本周期误差，断路分支要用 */

		/* 关于折线/锐角弯：不单独写状态机——
		 *   折线 = 瞬时大误差，靠 Kd 的"预判"能力顶过去（把 Kd 调大些）；
		 *   真顶不住时会先触发"直角特征"分支，也能救回来。
		 *   这是刻意的设计：规则越少，随机地图（6.1）的泛化性越好。 */

		/* 关于圆环：环岛的入口/出口会出现"全黑"（已在分支 1 直行处理），
		 *   环上本身是普通弯道，PID 自然跟随。
		 * ⚠️【需修改】如果你们的环岛入口角度很刁（切线进入），
		 *   需要在分支 1 里按"圆环标志位"给固定转向，现场调。 */

		/* 关于 6.4 速度闭环（可选增强）：
		 *   当前是"固定基础速度 + 转向 PID"。想跑更快档位，可以在这里加一层
		 *   速度环：Encoder_GetLeft/Right() 读出两轮增量 → 与目标速度比较 →
		 *   第二个 PID 调 s_baseSpeed。直道提速、弯道降速是提速的主要手段。 */
	}
}
