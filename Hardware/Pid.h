#ifndef __PID_H
#define __PID_H

/* ============================================================================
 *  【新增文件】PID 控制器 —— 对应拓展 6.4（巡线速度与控制性能，推荐 PID 闭环）
 *
 *  这是转向环用的"位置式 PID"。公式：
 *      out = Kp*err + Ki*Σerr + Kd*(err - lastErr)
 *
 *  三个量的直观理解（调参口诀"先 P 后 I 再 D"）：
 *    P：偏差多大就纠正多狠——没有它车根本不认线；太大 → 蛇形摆动
 *    I：积累的偏差——消除"车装歪了导致总往一边偏"的稳态误差；太大 → 来回过冲
 *    D：偏差变化的速度——相当于"预判"，弯前提前收；太大 → 对传感器噪声过敏抖动
 * ==========================================================================*/

typedef struct
{
	float kp;
	float ki;
	float kd;
	float integ;		/* 积分累加值 */
	float lastErr;		/* 上一次误差，用于算微分 */
	float outMax;		/* 输出限幅，防止一脚油门到底 */
} Pid_t;

void  Pid_Init(Pid_t *p, float kp, float ki, float kd, float outMax);

/* 输入当前误差，返回控制输出（已限幅）。控制周期固定时才能这么算积分/微分 */
float Pid_Update(Pid_t *p, float err);

/* 换赛道/重新发车时清历史，否则旧的积分会让起步就跑偏 */
void  Pid_Reset(Pid_t *p);

#endif
