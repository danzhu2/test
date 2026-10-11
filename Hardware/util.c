#include "util.h"

/* 整数转十进制字符串
 *
 * 为什么不直接用 sprintf？
 *   printf/sprintf 会拖进一大坨库代码（几 KB Flash），而且默认不开浮点支持时
 *   容易踩坑。我们只需要"整数转字符串"这一个功能，手写 10 行更轻、更可控。
 *
 * 思路（以 -123 为例）：
 *   1) 先判断正负，负数先往输出里放一个 '-'
 *   2) 123 反复 % 10 取末位，依次得到 '3' '2' '1'，存进临时数组（顺序是反的）
 *   3) 再把临时数组倒着抄回输出，得到 "123"，最后补 '\0'
 */
void Util_IntToStr(int32_t value, char *out)
{
	char     tmp[12];
	int32_t  i = 0;			/* tmp 里的字符个数 */
	int32_t  n = 0;			/* out 里已写入的字符个数 */
	uint32_t u;

	if (value < 0)
	{
		out[n++] = '-';
		/* 用无符号减法取绝对值，可正确处理 INT32_MIN（直接写 -value 会溢出）*/
		u = (uint32_t)0 - (uint32_t)value;
	}
	else
	{
		u = (uint32_t)value;
	}

	/* 特殊情况：0 一次也进不了循环，要单独放一个 '0' */
	if (u == 0)
	{
		tmp[i++] = '0';
	}

	while (u > 0)
	{
		tmp[i++]  = (char)('0' + (u % 10u));	/* 取末位数字 */
		u        /= 10u;						/* 去掉末位 */
	}

	/* tmp 里是倒序的，倒着抄回去 */
	while (i > 0)
	{
		out[n++] = tmp[--i];
	}

	out[n] = '\0';
}
