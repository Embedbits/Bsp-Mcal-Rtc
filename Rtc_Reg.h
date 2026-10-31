/**
 * \author Mr.Nobody
 * \file Rtc_Reg.h
 * \ingroup Rtc
 * \brief Rtc module Reg component header file.
 *
 * This component contains functionality for register operations. It is private to the
 * module (the register access is not shared with other modules). Register and bit field
 * names match Bsp/Ral/CMSIS_ST/Include/stm32h563xx.h RTC_TypeDef. The binary mode registers
 * (ALRABINR, ALRBBINR) and the TrustZone registers (PRIVCFGR, SECCFGR, SMISR) are not part
 * of this version (see README.md), their register IDs are not listed.
 *
 */

#ifndef RTC_REG_RTC_REG_H
#define RTC_REG_RTC_REG_H

#ifdef __cplusplus
extern "C" {
#endif

/* ============================= INCLUDES =================================== */
#include "Rtc_Types.h"                      /* Module types definition        */
#include "Gpio_Types.h"                     /* gpio_PortId_t / gpio_PinId_t   */
/* ============================= TYPEDEFS =================================== */

/** \brief List of RTC registers accessed by the module. */
typedef enum
{
    RTC_REG_TR       = 0u, /**< RTC time register                   */
    RTC_REG_DR           , /**< RTC date register                   */
    RTC_REG_SSR          , /**< RTC sub second register             */
    RTC_REG_ICSR         , /**< RTC initialization control / status */
    RTC_REG_PRER         , /**< RTC prescaler register              */
    RTC_REG_WUTR         , /**< RTC wake-up timer register          */
    RTC_REG_CR           , /**< RTC control register                */
    RTC_REG_WPR          , /**< RTC write protection register       */
    RTC_REG_CALR         , /**< RTC calibration register            */
    RTC_REG_SHIFTR       , /**< RTC shift control register          */
    RTC_REG_TSTR         , /**< RTC time-stamp time register        */
    RTC_REG_TSDR         , /**< RTC time-stamp date register        */
    RTC_REG_TSSSR        , /**< RTC time-stamp sub second register  */
    RTC_REG_ALRMAR       , /**< RTC alarm A register                */
    RTC_REG_ALRMASSR     , /**< RTC alarm A sub second register     */
    RTC_REG_ALRMBR       , /**< RTC alarm B register                */
    RTC_REG_ALRMBSSR     , /**< RTC alarm B sub second register     */
    RTC_REG_SR           , /**< RTC status register                 */
    RTC_REG_SCR          , /**< RTC status clear register           */
    RTC_REG_OR           , /**< RTC option register                 */
    RTC_REG_CNT            /**< Count of available RTC registers    */
}   rtc_RegId_t;


/** \brief One row of the RTC_OUT2 pin lookup table (GPIO location + alternate function). */
typedef struct
{
    rtc_OutputPin_t    PinItem;  /**< The enum item this row describes */
    gpio_PortId_t      GpioPort; /**< GPIO port of the pin             */
    gpio_PinId_t       GpioPin;  /**< GPIO pin number                  */
    gpio_AltFunction_t AltFunc;  /**< Alternate function of the pin    */
}   rtc_OutputPinInfo_t;


/** \brief One row of the RTC_REFIN pin lookup table (same shape as \ref rtc_OutputPinInfo_t). */
typedef struct
{
    rtc_RefInPin_t     PinItem;
    gpio_PortId_t      GpioPort;
    gpio_PinId_t       GpioPin;
    gpio_AltFunction_t AltFunc;
}   rtc_RefInPinInfo_t;

/* ========================= SYMBOLIC CONSTANTS ============================= */

/* ========================= EXPORTED MACROS ================================ */

/* ========================= EXPORTED VARIABLES ============================= */

/* ======================== EXPORTED FUNCTIONS ============================== */

/* ---------------------------- Generic accessors --------------------------- */

void                        Rtc_Reg_Set_RegBit         ( rtc_RegId_t regId, uint32_t bitMask );
void                        Rtc_Reg_Reset_RegBit       ( rtc_RegId_t regId, uint32_t bitMask );
uint32_t                    Rtc_Reg_Get_RegBit         ( rtc_RegId_t regId, uint32_t bitMask );

void                        Rtc_Reg_Set_RegVal         ( rtc_RegId_t regId, uint32_t regMask, uint32_t regValue );
uint32_t                    Rtc_Reg_Get_RegVal         ( rtc_RegId_t regId, uint32_t regMask );

/* ----------------------------- Write protection ---------------------------- */

void                        Rtc_Reg_Unlock             ( void );
void                        Rtc_Reg_Lock               ( void );

rtc_RequestState_t          Rtc_Reg_EnterInitMode      ( void );
void                        Rtc_Reg_ExitInitMode       ( void );

/* ----------------------------- Pin lookup tables ---------------------------- */

rtc_RequestState_t          Rtc_Reg_Get_OutputPinInfo  ( rtc_OutputPin_t pinItem,
                                                         const rtc_OutputPinInfo_t * * const info );
rtc_RequestState_t          Rtc_Reg_Get_RefInPinInfo   ( rtc_RefInPin_t pinItem,
                                                         const rtc_RefInPinInfo_t * * const info );

#ifdef __cplusplus
}
#endif

#endif /* RTC_REG_RTC_REG_H */
