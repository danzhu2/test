#ifndef __LINEFOLLOW_H
#define __LINEFOLLOW_H

#include "stm32f10x.h"

/* ============================================================================
 *  巡线核心 —— 对应赛题第 4 项 b/c/d/e（35 分）+ 拓展 6.1（15 分）
 *
 *  覆盖的功能：
 *    4b  发车（蜂鸣一声 + 自动计时）+ 正常循迹一圈 + 停车
 *    4c  连续两圈（第一圈到起点蜂鸣不停车）
 *    4d  特殊元素：十字 / 圆环 / 断路 / 直角 / 折线
 *    4e  完整复杂地图（元素处理的组合）
 *    6.1 随机地图：元素处理逻辑是"通用规则"而非"背地图"，抽到哪张都能跑
 *
 *  使用方法：
 *    巡线界面按 K1 → Line_StartRun()（发车）
 *    主循环每圈调用 Line_Task()（内部自带 5ms 控制周期判断）
 * ==========================================================================*/

void Line_Init(void);

/* 发车：蜂鸣一声、清零圈数计时、开始巡线。已在运行中则忽略 */
void Line_StartRun(void);

/* 主循环里反复调用。非运行状态直接返回 */
void Line_Task(void);

/* 发车后如果改过参数，调用一次让新参数生效 */
void Line_ApplyParams(void);

uint8_t  Line_IsRunning(void);		/* 1 = 正在巡线 */
uint8_t  Line_IsFinished(void);		/* 1 = 本轮已完整跑完目标圈数 */
uint8_t  Line_GetLaps(void);		/* 已完成圈数 */
uint32_t Line_GetElapsedMs(void);	/* 运行中 = 已跑时间；完赛 = 总用时 */

#endif
