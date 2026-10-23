#include "stm32f10x.h"
#include "Pid.h"

void Pid_Init(Pid_t *p, float kp, float ki, float kd, float outMax)
{
	p->kp      = kp;
	p->ki      = ki;
	p->kd      = kd;
	p->outMax  = outMax;
	p->integ   = 0.0f;
	p->lastErr = 0.0f;
}

float Pid_Update(Pid_t *p, float err)
{
	float dErr;
	float out;

	/* ---- 积分项 ----
	 * 为什么要限幅（抗积分饱和）？
	 *   起步大偏差时积分会越积越大，等车转过头了积分还得慢慢"泄"，
	 *   表现为严重的来回过冲。限制积分上限 = 给纠正力度封顶。 */
	p->integ += err * p->ki;

	if (p->integ >  p->outMax)  p->integ =  p->outMax;
	if (p->integ < -p->outMax)  p->integ = -p->outMax;

	/* ---- 微分项 ----
	 * 用 (本次误差 - 上次误差) 近似误差变化率。控制周期固定（5ms），
	 * 所以不用真的除以 dt，比例已经吸收进 Kd 里了。 */
	dErr = err - p->lastErr;
	p->lastErr = err;

	out = p->kp * err + p->integ + p->kd * dErr;

	if (out >  p->outMax)  out =  p->outMax;
	if (out < -p->outMax)  out = -p->outMax;

	return out;
}

void Pid_Reset(Pid_t *p)
{
	p->integ   = 0.0f;
	p->lastErr = 0.0f;
}
