#ifndef HALLSENSOR_H_
#define HALLSENSOR_H_

#include "Ifx_Types.h"
#include <stdint.h>

void HallSensor_init(void);

void HallSensor_updateMs(uint32_t periodMs);
void HallSensor_update1ms(void);

/*
 * Compatibility wrappers.
 * 기존 Scheduler에서 호출 중이면 그대로 둬도 된다.
 */
void HallSensor_calcSpeed10ms(void);
void HallSensor_calcSpeed50ms(void);

uint8_t  HallSensor_isDetected(void);
uint8_t  HallSensor_isSpeedValid(void);

uint32_t HallSensor_getPulseCount(void);
uint16_t HallSensor_getVehicleSpeed(void);

void HallSensor_reset(void);

#endif /* HALLSENSOR_H_ */
