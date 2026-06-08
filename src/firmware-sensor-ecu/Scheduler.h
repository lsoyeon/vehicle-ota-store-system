#ifndef SCHEDULER_H_
#define SCHEDULER_H_

#include "Ifx_Types.h"

/*
 * 1ms base tick
 * 10ms task = 10 tick
 * 100ms task = 100 tick
 */
#define SCHEDULER_BASE_MS      1U
#define TASK_10MS_DIV          10U
#define TASK_100MS_DIV         100U

#ifndef ISR_PRIORITY_STM
#define ISR_PRIORITY_STM       10U
#endif

void initScheduler(void);

/*
 * main while에서 계속 호출
 */
void Scheduler_run(void);

uint32 Scheduler_getTick(void);

#endif /* SCHEDULER_H_ */
