#include "HallSensor.h"
#include "IfxPort.h"

/*********************************************************************************************************************/
/*------------------------------------------------Configuration------------------------------------------------------*/
/*********************************************************************************************************************/

/*
 * DM2246 D0 -> TC375 P02.7
 */
#define HALL_PORT                       (&MODULE_P02)
#define HALL_PIN_INDEX                  (7U)

/*
 * 바퀴에 자석 3개 부착 기준
 * 바퀴 1회전 = pulse 3개
 */
#define HALL_MAGNETS_PER_REV            (3U)

/*
 * 바퀴 둘레 [mm]
 * 실제 바퀴 지름 재서 수정해야 함.
 *
 * 예:
 * 바퀴 지름 70mm라면
 * 둘레 = 70 * 3.14 = 약 220mm
 */
#define HALL_WHEEL_CIRCUMFERENCE_MM     (200U)

/*
 * 속도 계산 주기 [ms]
 * Scheduler.c의 task_50ms()에서 HallSensor_calcSpeed50ms()를 호출하므로 50ms.
 */
#define HALL_SPEED_CALC_PERIOD_MS       (50U)

/*********************************************************************************************************************/
/*------------------------------------------------Static variables---------------------------------------------------*/
/*********************************************************************************************************************/

static volatile uint8_t  s_detected = 0U;
static volatile uint32_t s_pulseCount = 0U;
static volatile uint16_t s_vehicleSpeed = 0U;

static uint8_t  s_prevDetected = 0U;
static uint32_t s_prevPulseCountForSpeed = 0U;

/*********************************************************************************************************************/
/*------------------------------------------------Debug variables----------------------------------------------------*/
/*********************************************************************************************************************/

/*
 * Watch 확인용
 *
 * debugHallRawLevel:
 *   자석 가까움 -> 0
 *   자석 멀어짐 -> 1
 *
 * debugHallDetected:
 *   자석 감지   -> 1
 *   자석 미감지 -> 0
 */
volatile uint8_t debugHallRawLevel = 0U;
volatile uint8_t debugHallDetected = 0U;

/*********************************************************************************************************************/
/*------------------------------------------------Private prototypes-------------------------------------------------*/
/*********************************************************************************************************************/

static uint8_t HallSensor_readRawLevel(void);
static uint8_t HallSensor_convertRawToDetected(uint8_t rawLevel);
static void    HallSensor_updateDebug(uint8_t rawLevel, uint8_t detected);

/*********************************************************************************************************************/
/*------------------------------------------------Public functions---------------------------------------------------*/
/*********************************************************************************************************************/

void HallSensor_init(void)
{
    /*
     * DM2246 D0를 P02.7로 입력.
     *
     * D0가 전압분배/레벨시프터를 거쳐 3.3V 레벨로 들어온다는 기준.
     * 외부 회로가 신호 레벨을 만들고 있으므로 내부 pull-up은 끔.
     */
    IfxPort_setPinModeInput(HALL_PORT,
                            HALL_PIN_INDEX,
                            IfxPort_InputMode_noPullDevice);

    s_detected = 0U;
    s_pulseCount = 0U;
    s_vehicleSpeed = 0U;

    s_prevDetected = 0U;
    s_prevPulseCountForSpeed = 0U;

    debugHallRawLevel = 0U;
    debugHallDetected = 0U;
}

uint8_t HallSensor_isDetected(void)
{
    uint8_t rawLevel;
    uint8_t detected;

    rawLevel = HallSensor_readRawLevel();
    detected = HallSensor_convertRawToDetected(rawLevel);

    HallSensor_updateDebug(rawLevel, detected);

    return detected;
}

void HallSensor_update1ms(void)
{
    uint8_t rawLevel;
    uint8_t nowDetected;

    rawLevel = HallSensor_readRawLevel();
    nowDetected = HallSensor_convertRawToDetected(rawLevel);

    HallSensor_updateDebug(rawLevel, nowDetected);

    s_detected = nowDetected;

    /*
     * 자석이 처음 가까워지는 순간만 pulse 1개 증가.
     *
     * 즉,
     * 자석 멀어짐 -> 자석 가까움
     * 0 -> 1 전이에서만 count 증가.
     */
    if((s_prevDetected == 0U) && (nowDetected == 1U))
    {
        s_pulseCount++;
    }

    s_prevDetected = nowDetected;
}

void HallSensor_calcSpeed50ms(void)
{
    uint32_t nowPulseCount;
    uint32_t diffPulse;
    uint32_t numerator;
    uint32_t denominator;
    uint32_t speedX10;

    nowPulseCount = s_pulseCount;
    diffPulse = nowPulseCount - s_prevPulseCountForSpeed;
    s_prevPulseCountForSpeed = nowPulseCount;

    /*
     * vehicleSpeed 단위: km/h x10
     *
     * 50ms 동안 diffPulse개 들어왔다고 할 때:
     *
     * 바퀴 회전수 = diffPulse / 자석 개수
     * 이동거리[mm] = 바퀴 회전수 * 바퀴둘레[mm]
     *
     * speed[km/h] x10
     * = diffPulse * wheel_circumference_mm * 36 / (period_ms * magnets)
     */
    numerator = diffPulse * HALL_WHEEL_CIRCUMFERENCE_MM * 36U;
    denominator = HALL_SPEED_CALC_PERIOD_MS * HALL_MAGNETS_PER_REV;

    if(denominator == 0U)
    {
        s_vehicleSpeed = 0U;
        return;
    }

    speedX10 = numerator / denominator;

    if(speedX10 > 0xFFFFU)
    {
        speedX10 = 0xFFFFU;
    }

    s_vehicleSpeed = (uint16_t)speedX10;
}

uint32_t HallSensor_getPulseCount(void)
{
    return s_pulseCount;
}

uint16_t HallSensor_getVehicleSpeed(void)
{
    return s_vehicleSpeed;
}

void HallSensor_reset(void)
{
    s_detected = 0U;
    s_pulseCount = 0U;
    s_vehicleSpeed = 0U;

    s_prevDetected = 0U;
    s_prevPulseCountForSpeed = 0U;

    debugHallRawLevel = 0U;
    debugHallDetected = 0U;
}

/*********************************************************************************************************************/
/*------------------------------------------------Private functions--------------------------------------------------*/
/*********************************************************************************************************************/

static uint8_t HallSensor_readRawLevel(void)
{
    IfxPort_State state;

    state = IfxPort_getPinState(HALL_PORT, HALL_PIN_INDEX);

    if(state == IfxPort_State_high)
    {
        return 1U;
    }
    else
    {
        return 0U;
    }
}

static uint8_t HallSensor_convertRawToDetected(uint8_t rawLevel)
{
    /*
     * Active Low:
     * raw 0 -> 자석 감지
     * raw 1 -> 자석 미감지
     */
    return (rawLevel == 0U) ? 1U : 0U;

}

static void HallSensor_updateDebug(uint8_t rawLevel, uint8_t detected)
{
    debugHallRawLevel = rawLevel;
    debugHallDetected = detected;
}
