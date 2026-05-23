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
 * Wheel has 2 magnets.
 * 1 wheel revolution = 2 pulses.
 */
#define HALL_MAGNETS_PER_REV            (2U)

/*
 * Wheel circumference [mm]
 * 73cm = 730mm
 */
#define HALL_WHEEL_CIRCUMFERENCE_MM     (730U)

/*
 * Ignore too-short pulse intervals as noise/glitches.
 */
#define HALL_MIN_PULSE_INTERVAL_MS      (10U)

/*
 * If no new pulse arrives for this time, treat the vehicle as stopped.
 *
 * 기존 3000ms는 감속/정지 반영이 너무 느릴 수 있음.
 * 800ms면 약 1.64km/h 이하 영역에서 빠르게 0으로 떨어짐.
 */
#define HALL_NO_PULSE_TIMEOUT_MS        (800U)

/*
 * 새 펄스가 없어도 pulseAgeMs 기준으로 속도를 낮추는 기능.
 */
#define HALL_DECAY_ENABLE               (1U)

/*
 * 정지 후 첫 번째 펄스가 들어왔을 때 바로 0이 아닌 값을 표시하기 위한 시작 추정값.
 *
 * 단위: km/h x100
 * 150 = 1.50km/h
 *
 * 실제 속도는 두 번째 펄스부터 interval 기반으로 다시 계산되어 덮어써짐.
 */
#define HALL_STARTUP_ESTIMATE_ENABLE    (1U)
#define HALL_STARTUP_SPEED_X100         (100U)

/*
 * 감속 보정 시작 시점.
 *
 * 80이면 마지막 펄스 간격의 80% 시간이 지난 시점부터 감속 보정을 시작.
 * 값을 낮추면 더 빨리 떨어짐.
 */
#define HALL_DECAY_START_PERCENT        (100U)

/*
 * 감속 보정 강도.
 *
 * 160이면 pulseAge 증가분을 1.6배로 반영.
 * 값을 높이면 더 빨리 떨어짐.
 */
#define HALL_DECAY_GAIN_PERCENT         (120U)

/*********************************************************************************************************************/
/*------------------------------------------------Static variables---------------------------------------------------*/
/*********************************************************************************************************************/

static volatile uint8_t  s_detected = 0U;
static volatile uint32_t s_pulseCount = 0U;
static volatile uint16_t s_vehicleSpeed = 0U;

static uint8_t  s_prevDetected = 0U;
static uint8_t  s_hasPulseBase = 0U;
static uint8_t  s_hasValidInterval = 0U;

/*
 * Software time advanced by HallSensor_updateMs().
 */
static uint32_t s_timeMs = 0U;

/*
 * Pulse interval based speed calculation state.
 */
static uint32_t s_lastPulseTimeMs = 0U;
static uint32_t s_lastPulseIntervalMs = 0U;

/*********************************************************************************************************************/
/*------------------------------------------------Debug variables----------------------------------------------------*/
/*********************************************************************************************************************/

volatile uint8_t  debugHallRawLevel = 0U;
volatile uint8_t  debugHallDetected = 0U;
volatile uint32_t debugHallTimeMs = 0U;
volatile uint32_t debugHallPulseCount = 0U;
volatile uint32_t debugHallLastPulseTimeMs = 0U;
volatile uint32_t debugHallLastPulseIntervalMs = 0U;
volatile uint32_t debugHallPulseAgeMs = 0U;
volatile uint16_t debugHallVehicleSpeedX100 = 0U;
volatile uint32_t debugHallIgnoredPulseCount = 0U;
volatile uint8_t  debugHallHasSpeed = 0U;

/*
 * Added debug variables.
 */
volatile uint16_t debugHallAgedVehicleSpeedX100 = 0U;
volatile uint32_t debugHallDecayCount = 0U;
volatile uint8_t  debugHallTimeoutZero = 0U;
volatile uint8_t  debugHallStartupEstimated = 0U;

/*
 * Legacy watch variables. Kept for existing debugger watch setups.
 */
volatile uint16_t debugHallRawVehicleSpeed = 0U;
volatile uint16_t debugHallFilteredVehicleSpeed = 0U;
volatile uint8_t  debugHallHasValidInterval = 0U;

/*********************************************************************************************************************/
/*------------------------------------------------Private prototypes-------------------------------------------------*/
/*********************************************************************************************************************/

static uint8_t  HallSensor_readRawLevel(void);
static uint8_t  HallSensor_convertRawToDetected(uint8_t rawLevel);
static void     HallSensor_updateDebug(uint8_t rawLevel, uint8_t detected);
static uint16_t HallSensor_calcSpeedX100(uint32_t intervalMs);
static void     HallSensor_updatePulseAgeAndTimeout(void);
static void     HallSensor_storeSpeed(uint16_t speedX100);

/*********************************************************************************************************************/
/*------------------------------------------------Public functions---------------------------------------------------*/
/*********************************************************************************************************************/

void HallSensor_init(void)
{
    /*
     * DM2246 D0 input.
     * Existing hardware uses active-low output for magnet detection.
     */
    IfxPort_setPinModeInput(HALL_PORT,
                            HALL_PIN_INDEX,
                            IfxPort_InputMode_pullUp);

    HallSensor_reset();
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

void HallSensor_updateMs(uint32_t periodMs)
{
    uint8_t rawLevel;
    uint8_t nowDetected;

    if(periodMs == 0U)
    {
        periodMs = 1U;
    }

    s_timeMs += periodMs;
    debugHallTimeMs = s_timeMs;

    rawLevel = HallSensor_readRawLevel();
    nowDetected = HallSensor_convertRawToDetected(rawLevel);

    HallSensor_updateDebug(rawLevel, nowDetected);

    s_detected = nowDetected;

    /*
     * Count only the transition from not-detected to detected.
     *
     * Active-low sensor:
     * raw 1 -> raw 0 순간이 magnet detected
     * detected 0 -> detected 1 순간만 pulse로 인정
     */
    if((s_prevDetected == 0U) && (nowDetected == 1U))
    {
        uint32_t nowMs;
        uint32_t intervalMs;

        nowMs = s_timeMs;

        /*
         * 첫 번째 펄스는 정확한 속도 계산이 불가능하다.
         * 하지만 반응성을 위해 시작 추정 속도를 바로 넣는다.
         */
        if(s_hasPulseBase == 0U)
        {
            s_pulseCount++;
            s_hasPulseBase = 1U;
            s_hasValidInterval = 0U;

            s_lastPulseTimeMs = nowMs;
            s_lastPulseIntervalMs = 0U;

#if (HALL_STARTUP_ESTIMATE_ENABLE != 0U)
            HallSensor_storeSpeed(HALL_STARTUP_SPEED_X100);
            debugHallHasSpeed = 1U;
            debugHallStartupEstimated = 1U;
#endif

            debugHallPulseCount = s_pulseCount;
            debugHallLastPulseTimeMs = s_lastPulseTimeMs;
            debugHallLastPulseIntervalMs = s_lastPulseIntervalMs;
            debugHallPulseAgeMs = 0U;
            debugHallTimeoutZero = 0U;
        }
        else
        {
            intervalMs = nowMs - s_lastPulseTimeMs;

            if(intervalMs >= HALL_MIN_PULSE_INTERVAL_MS)
            {
                uint16_t speedX100;

                s_pulseCount++;
                s_lastPulseIntervalMs = intervalMs;
                s_lastPulseTimeMs = nowMs;
                s_hasValidInterval = 1U;

                /*
                 * 두 번째 펄스부터는 실제 펄스 간격 기반 속도로 계산한다.
                 */
                speedX100 = HallSensor_calcSpeedX100(intervalMs);
                HallSensor_storeSpeed(speedX100);

                debugHallPulseCount = s_pulseCount;
                debugHallLastPulseIntervalMs = s_lastPulseIntervalMs;
                debugHallLastPulseTimeMs = s_lastPulseTimeMs;
                debugHallPulseAgeMs = 0U;
                debugHallHasSpeed = 1U;
                debugHallHasValidInterval = s_hasValidInterval;
                debugHallTimeoutZero = 0U;
                debugHallStartupEstimated = 0U;
            }
            else
            {
                /*
                 * 너무 짧은 간격은 노이즈로 판단.
                 */
                debugHallIgnoredPulseCount++;
            }
        }
    }

    s_prevDetected = nowDetected;

    /*
     * 중요:
     * 새 펄스가 없어도 매 주기마다 pulseAgeMs를 보고
     * 속도를 빠르게 낮춰준다.
     */
    HallSensor_updatePulseAgeAndTimeout();
}

void HallSensor_update1ms(void)
{
    HallSensor_updateMs(1U);
}

void HallSensor_calcSpeed10ms(void)
{
    /*
     * Deprecated compatibility wrapper.
     * 기존 Scheduler에서 이 함수를 부르고 있다면 그대로 둬도 된다.
     */
    HallSensor_updatePulseAgeAndTimeout();
}

void HallSensor_calcSpeed50ms(void)
{
    /*
     * Deprecated compatibility wrapper.
     * 예전 50ms 계산 구조와 호환용.
     */
    HallSensor_updatePulseAgeAndTimeout();
}

uint8_t HallSensor_isSpeedValid(void)
{
    return s_hasValidInterval;
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
    s_hasPulseBase = 0U;
    s_hasValidInterval = 0U;

    s_timeMs = 0U;
    s_lastPulseTimeMs = 0U;
    s_lastPulseIntervalMs = 0U;

    debugHallRawLevel = 0U;
    debugHallDetected = 0U;
    debugHallTimeMs = 0U;
    debugHallPulseCount = 0U;
    debugHallLastPulseTimeMs = 0U;
    debugHallLastPulseIntervalMs = 0U;
    debugHallPulseAgeMs = 0U;
    debugHallVehicleSpeedX100 = 0U;
    debugHallIgnoredPulseCount = 0U;
    debugHallHasSpeed = 0U;

    debugHallAgedVehicleSpeedX100 = 0U;
    debugHallDecayCount = 0U;
    debugHallTimeoutZero = 0U;
    debugHallStartupEstimated = 0U;

    debugHallRawVehicleSpeed = 0U;
    debugHallFilteredVehicleSpeed = 0U;
    debugHallHasValidInterval = 0U;
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
     * Active low:
     * raw 0 -> magnet detected
     * raw 1 -> magnet not detected
     */
    return (rawLevel == 0U) ? 1U : 0U;
}

static void HallSensor_updateDebug(uint8_t rawLevel, uint8_t detected)
{
    debugHallRawLevel = rawLevel;
    debugHallDetected = detected;
}

static uint16_t HallSensor_calcSpeedX100(uint32_t intervalMs)
{
    uint32_t denominator;
    uint32_t speedX100;

    denominator = HALL_MAGNETS_PER_REV * intervalMs;

    if(denominator == 0U)
    {
        return 0U;
    }

    /*
     * speed[km/h] = circumference[mm] / interval[ms] / magnets * 3.6
     *
     * speedX100 = speed[km/h] * 100
     *           = circumference[mm] * 360 / (magnets * intervalMs)
     */
    speedX100 = (HALL_WHEEL_CIRCUMFERENCE_MM * 360U) / denominator;

    if(speedX100 > 0xFFFFU)
    {
        speedX100 = 0xFFFFU;
    }

    return (uint16_t)speedX100;
}

static void HallSensor_updatePulseAgeAndTimeout(void)
{
    uint32_t pulseAgeMs;

    if(s_hasPulseBase == 0U)
    {
        debugHallPulseAgeMs = 0U;
        return;
    }

    pulseAgeMs = s_timeMs - s_lastPulseTimeMs;
    debugHallPulseAgeMs = pulseAgeMs;

#if (HALL_DECAY_ENABLE != 0U)
    /*
     * 빠른 감속 반영 로직.
     *
     * 마지막 펄스 간격의 80% 시간이 지난 뒤부터 감속 보정을 시작한다.
     * 그리고 pulseAge 증가분을 HALL_DECAY_GAIN_PERCENT만큼 더 크게 반영한다.
     *
     * 즉, 다음 펄스가 예상보다 늦어지면
     * 실제 속도가 떨어지고 있다고 보고 속도값을 미리 낮춘다.
     */
    if(s_hasValidInterval != 0U)
    {
        if(s_lastPulseIntervalMs > 0U)
        {
            uint32_t decayStartMs;

            decayStartMs = (s_lastPulseIntervalMs * HALL_DECAY_START_PERCENT) / 100U;

            if(decayStartMs < HALL_MIN_PULSE_INTERVAL_MS)
            {
                decayStartMs = HALL_MIN_PULSE_INTERVAL_MS;
            }

            if(pulseAgeMs > decayStartMs)
            {
                uint32_t extraAgeMs;
                uint32_t effectiveAgeMs;
                uint16_t agedSpeedX100;

                extraAgeMs = pulseAgeMs - decayStartMs;

                effectiveAgeMs = s_lastPulseIntervalMs +
                                 ((extraAgeMs * HALL_DECAY_GAIN_PERCENT) / 100U);

                if(effectiveAgeMs < s_lastPulseIntervalMs)
                {
                    effectiveAgeMs = s_lastPulseIntervalMs;
                }

                agedSpeedX100 = HallSensor_calcSpeedX100(effectiveAgeMs);
                debugHallAgedVehicleSpeedX100 = agedSpeedX100;

                /*
                 * 감속 방향으로만 보정.
                 * 새 펄스 없이 속도를 올리지는 않는다.
                 */
                if(agedSpeedX100 < s_vehicleSpeed)
                {
                    HallSensor_storeSpeed(agedSpeedX100);
                    debugHallDecayCount++;
                }
            }
        }
    }
#endif

    /*
     * 최종 정지 판정.
     *
     * 주의:
     * s_hasValidInterval 조건을 걸지 않는다.
     * 첫 번째 펄스에서 시작 추정 속도를 넣은 뒤 두 번째 펄스가 안 들어오는 경우도
     * timeout이 지나면 반드시 0으로 떨어져야 한다.
     */
    if(pulseAgeMs > HALL_NO_PULSE_TIMEOUT_MS)
    {
        HallSensor_storeSpeed(0U);

        /*
         * After timeout, the next pulse becomes a new baseline.
         * This prevents a restart from using the long stopped interval.
         */
        s_hasPulseBase = 0U;
        s_hasValidInterval = 0U;
        s_lastPulseIntervalMs = 0U;

        debugHallHasSpeed = 0U;
        debugHallHasValidInterval = 0U;
        debugHallTimeoutZero = 1U;
        debugHallStartupEstimated = 0U;
    }
}

static void HallSensor_storeSpeed(uint16_t speedX100)
{
    s_vehicleSpeed = speedX100;

    debugHallVehicleSpeedX100 = speedX100;

    /*
     * Legacy aliases.
     */
    debugHallRawVehicleSpeed = speedX100;
    debugHallFilteredVehicleSpeed = speedX100;
}
