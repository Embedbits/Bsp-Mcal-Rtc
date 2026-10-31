/**
 * \author Mr.Nobody
 * \file Rtc_Port.h
 * \ingroup Rtc
 * \brief Rtc module public interface.
 *
 * Public functions of the module, identical for all supported families (see README.md).
 *
 */

#ifndef RTC_RTC_PORT_H
#define RTC_RTC_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

/* ============================== INCLUDES ================================== */
#include "Rtc_Types.h"                       /* Module types definition        */
/* ========================= EXPORTED FUNCTIONS ============================= */

/* --------------------------- Module management ---------------------------- */

rtc_ModuleVersion_t  Rtc_Get_ModuleVersion       ( void );

rtc_RequestState_t   Rtc_Init                    ( const rtc_Config_t * const rtcConfig );
rtc_RequestState_t   Rtc_Deinit                  ( void );
void                 Rtc_Task                    ( void );
rtc_RequestState_t   Rtc_Get_DefaultConfig       ( rtc_Config_t * const rtcConfig );

/* --------------------------------- Time ------------------------------------ */

rtc_RequestState_t   Rtc_Set_DateTime            ( const rtc_DateTime_t * const dateTime );
rtc_RequestState_t   Rtc_Get_DateTime            ( rtc_DateTime_t * const dateTime );
rtc_RequestState_t   Rtc_Set_UnixTime            ( rtc_UnixTime_t unixTime );
rtc_RequestState_t   Rtc_Get_UnixTime            ( rtc_UnixTime_t * const unixTime );
rtc_RequestState_t   Rtc_Get_TimeState           ( rtc_TimeState_t * const timeState );
rtc_RequestState_t   Rtc_Set_DaylightSaving      ( rtc_DaylightSaving_t daylightSaving );

/* -------------------------------- Alarms ------------------------------------ */

rtc_RequestState_t   Rtc_Set_Alarm               ( rtc_AlarmId_t alarmId,
                                                    const rtc_AlarmConfig_t * const alarmConfig );
rtc_RequestState_t   Rtc_Set_AlarmActive         ( rtc_AlarmId_t alarmId );
rtc_RequestState_t   Rtc_Set_AlarmInactive       ( rtc_AlarmId_t alarmId );
rtc_RequestState_t   Rtc_Get_AlarmState          ( rtc_AlarmId_t alarmId,
                                                    rtc_FunctionState_t * const alarmState );

/* ----------------------------- Wake-up timer -------------------------------- */

rtc_RequestState_t   Rtc_Set_WakeUpTimer         ( rtc_Period_ms_t period, rtc_Callback_t * const callback );
rtc_RequestState_t   Rtc_Set_WakeUpTimerActive   ( void );
rtc_RequestState_t   Rtc_Set_WakeUpTimerInactive ( void );
rtc_RequestState_t   Rtc_Get_WakeUpFlag          ( rtc_FlagState_t * const wakeUpFlag );
rtc_RequestState_t   Rtc_Set_WakeUpFlagClear     ( void );

/* ------------------------------- Timestamp ---------------------------------- */

rtc_RequestState_t   Rtc_Set_TimestampConfig     ( const rtc_TimestampConfig_t * const timestampConfig );
rtc_RequestState_t   Rtc_Set_TimestampActive     ( void );
rtc_RequestState_t   Rtc_Set_TimestampInactive   ( void );
rtc_RequestState_t   Rtc_Get_Timestamp           ( rtc_DateTime_t * const dateTime );

/* ------------------------------- Calibration -------------------------------- */

rtc_RequestState_t   Rtc_Set_Calibration         ( rtc_Calibration_ppb_t calibration );
rtc_RequestState_t   Rtc_Get_Calibration         ( rtc_Calibration_ppb_t * const calibration );
rtc_RequestState_t   Rtc_Set_TimeShift           ( rtc_Shift_ms_t shift );
rtc_RequestState_t   Rtc_Set_ReferenceClockActive   ( rtc_RefInPin_t refInPin );
rtc_RequestState_t   Rtc_Set_ReferenceClockInactive ( void );

/* ---------------------------------- Output ---------------------------------- */

rtc_RequestState_t   Rtc_Set_Output              ( const rtc_OutputConfig_t * const outputConfig );

/* ----------------------------- Backup registers ----------------------------- */

rtc_RequestState_t   Rtc_Get_BackupRegisterCount ( rtc_BackupCnt_t * const registerCnt );
rtc_RequestState_t   Rtc_Set_BackupRegister      ( rtc_BackupIndex_t index, rtc_BackupData_t data );
rtc_RequestState_t   Rtc_Get_BackupRegister      ( rtc_BackupIndex_t index, rtc_BackupData_t * const data );

/* ------------------------------- Interrupts ---------------------------------- */

rtc_RequestState_t   Rtc_Set_IrqPriority         ( rtc_IrqPrio_t irqPriority );
rtc_RequestState_t   Rtc_Get_IrqPriority         ( rtc_IrqPrio_t * const irqPriority );

/* ---------------------------------- Pins ------------------------------------- */

rtc_RequestState_t   Rtc_InitTimestampGpio       ( void );
rtc_RequestState_t   Rtc_InitOutputGpio          ( rtc_OutputPin_t outputPin );
rtc_RequestState_t   Rtc_InitRefInGpio           ( rtc_RefInPin_t refInPin );

#ifdef __cplusplus
}
#endif

#endif /* RTC_RTC_PORT_H */
