/**
 * \author Mr.Nobody
 * \file Rtc.h
 * \ingroup Rtc
 * \brief Rtc module private header file.
 *
 * Holds the module state and the prototypes shared between Rtc.c and Rtc_Reg.c. Not
 * included by any other module.
 *
 */

#ifndef RTC_RTC_H
#define RTC_RTC_H

#ifdef __cplusplus
extern "C" {
#endif

/* ============================== INCLUDES ================================== */
#include "Rtc_Types.h"                       /* Module types definition        */
/* ============================== TYPEDEFS ================================== */

/** \brief Module runtime state, kept private to the module. */
typedef struct
{
    rtc_FunctionState_t Initialized;                    /**< RTC_FUNCTION_ACTIVE once \ref Rtc_Init succeeded      */
    rtc_FunctionState_t BackupAccess;                   /**< RTC_FUNCTION_ACTIVE while the Pwr backup access is held */
    rtc_ClkSource_t     ClkSrc;                         /**< Clock source adopted / configured by \ref Rtc_Init    */
    rtc_TimeState_t     TimeState;                      /**< Adopted (valid) or (re)initialized (lost) calendar    */
    uint32_t            PredivS;                        /**< RTC_PRER.PREDIV_S applied / found (SUBFS calculation) */
    rtc_Callback_t     *AlarmCallback[ RTC_ALARM_CNT ]; /**< Per-alarm callback, \ref RTC_NULL_PTR if none         */
    rtc_Callback_t     *WakeUpCallback;                 /**< Wake-up timer callback, \ref RTC_NULL_PTR if none     */
    rtc_Callback_t     *TimestampCallback;              /**< Timestamp callback, \ref RTC_NULL_PTR if none         */
    rtc_IrqPrio_t       IrqPriority;                    /**< RTC interrupt priority applied by \ref Rtc_Init       */
}   rtc_State_t;

/* ========================== SYMBOLIC CONSTANTS ============================ */

/* ========================== EXPORTED MACROS ================================ */

/* ========================== EXPORTED VARIABLES ============================= */

/* ========================= EXPORTED FUNCTIONS =============================== */

#ifdef __cplusplus
}
#endif

#endif /* RTC_RTC_H */
