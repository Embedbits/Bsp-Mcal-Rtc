/**
 * \author Mr.Nobody
 * \file Rtc_Reg.c
 * \ingroup Rtc
 * \brief Rtc module Reg component functionality.
 *
 * Register access of the module: generic accessors (bounds checked register ID), the write
 * protection sequence (RM0481 56.3.7, write 0xCA then 0x53 to RTC_WPR to unlock, any other
 * byte locks it again), the calendar initialization mode sequence (ICSR.INIT / INITF) and the
 * RTC_OUT2 / RTC_REFIN pin lookup tables (STM32CubeMX database, see Rtc_Types.h file header).
 *
 */
/* ============================== INCLUDES ================================== */
#include "Rtc_Reg.h"                        /* Self include                   */
#include "Rtc_Types.h"                      /* Module types definitions       */
#include "Stm32.h"                          /* RTC registers definition       */
/* ============================== TYPEDEFS ================================== */

/** \brief Physical address of an RTC register, referenced through its enumeration id. */
typedef struct
{
    rtc_RegId_t          RegId;   /**< Peripheral register ID      */
    volatile uint32_t   *RegAddr; /**< Peripheral register address */
}   rtc_RegList_t;

/* ========================== SYMBOLIC CONSTANTS ============================ */

/** \brief Register value without any bit set */
#define RTC_REG_BITS_CLEAR              ( 0u )

/** \brief First write protection unlock key (RM0481 56.3.7) */
#define RTC_REG_WPR_KEY1                 ( 0xCAu )

/** \brief Second write protection unlock key */
#define RTC_REG_WPR_KEY2                 ( 0x53u )

/** \brief Any value that is not a valid unlock key locks the write protection again */
#define RTC_REG_WPR_LOCK                 ( 0xFFu )

/** \brief Bounded wait loop limit used by \ref Rtc_Reg_EnterInitMode (register polling only,
 *  no hardware timer dependency: generous for the slowest supported RTC clock). */
#define RTC_REG_INIT_WAIT_LIMIT           ( 1000000u )

/* ========================== LOCAL VARIABLES ============================== */

/** \brief RTC register addresses, referenced through their enumeration id. */
static const rtc_RegList_t rtc_RegList[] =
{
    { .RegId = RTC_REG_TR      , .RegAddr = &(RTC->TR      ) },
    { .RegId = RTC_REG_DR      , .RegAddr = &(RTC->DR      ) },
    { .RegId = RTC_REG_SSR     , .RegAddr = &(RTC->SSR     ) },
    { .RegId = RTC_REG_ICSR    , .RegAddr = &(RTC->ICSR    ) },
    { .RegId = RTC_REG_PRER    , .RegAddr = &(RTC->PRER    ) },
    { .RegId = RTC_REG_WUTR    , .RegAddr = &(RTC->WUTR    ) },
    { .RegId = RTC_REG_CR      , .RegAddr = &(RTC->CR      ) },
    { .RegId = RTC_REG_WPR     , .RegAddr = &(RTC->WPR     ) },
    { .RegId = RTC_REG_CALR    , .RegAddr = &(RTC->CALR    ) },
    { .RegId = RTC_REG_SHIFTR  , .RegAddr = &(RTC->SHIFTR  ) },
    { .RegId = RTC_REG_TSTR    , .RegAddr = &(RTC->TSTR    ) },
    { .RegId = RTC_REG_TSDR    , .RegAddr = &(RTC->TSDR    ) },
    { .RegId = RTC_REG_TSSSR   , .RegAddr = &(RTC->TSSSR   ) },
    { .RegId = RTC_REG_ALRMAR  , .RegAddr = &(RTC->ALRMAR  ) },
    { .RegId = RTC_REG_ALRMASSR, .RegAddr = &(RTC->ALRMASSR) },
    { .RegId = RTC_REG_ALRMBR  , .RegAddr = &(RTC->ALRMBR  ) },
    { .RegId = RTC_REG_ALRMBSSR, .RegAddr = &(RTC->ALRMBSSR) },
    { .RegId = RTC_REG_SR      , .RegAddr = &(RTC->SR      ) },
    { .RegId = RTC_REG_SCR     , .RegAddr = &(RTC->SCR     ) },
    { .RegId = RTC_REG_OR      , .RegAddr = &(RTC->OR      ) },
};

_Static_assert( (sizeof(rtc_RegList) / sizeof(rtc_RegList_t)) == RTC_REG_CNT,
                "Rtc_Reg: rtc_RegList has incorrect size." );

/** \brief RTC_OUT2 pin lookup table (STM32CubeMX database, see Rtc_Types.h file header). */
static const rtc_OutputPinInfo_t rtc_OutputPinList[] =
{
#if defined(STM32H503xx)
    { RTC_OUT2_PIN_PA0, GPIO_PORT_A, GPIO_PIN_ID_0, GPIO_ALT_FUNC_0 },
#endif
    { RTC_OUT2_PIN_PB2, GPIO_PORT_B, GPIO_PIN_ID_2, GPIO_ALT_FUNC_0 },
};

/** \brief RTC_REFIN pin lookup table (STM32CubeMX database, see Rtc_Types.h file header). */
static const rtc_RefInPinInfo_t rtc_RefInPinList[] =
{
    { RTC_REFIN_PIN_PB15, GPIO_PORT_B, GPIO_PIN_ID_15, GPIO_ALT_FUNC_0 },
};

/* ========================= EXPORTED FUNCTIONS ============================= */

/**
 * \brief Sets the register bit with given mask.
 *
 * \param regId   [in]: RTC register ID
 * \param bitMask [in]: Mask to apply to the register
 */
void Rtc_Reg_Set_RegBit( rtc_RegId_t regId, uint32_t bitMask )
{
    if( RTC_REG_CNT > regId )
    {
        *rtc_RegList[regId].RegAddr |= bitMask;
    }
    else
    {
        /* Invalid register ID, no action required */
    }
}


/**
 * \brief Resets the register bit with given mask.
 *
 * \param regId   [in]: RTC register ID
 * \param bitMask [in]: Mask to apply to the register
 */
void Rtc_Reg_Reset_RegBit( rtc_RegId_t regId, uint32_t bitMask )
{
    if( RTC_REG_CNT > regId )
    {
        *rtc_RegList[regId].RegAddr &= ~bitMask;
    }
    else
    {
        /* Invalid register ID, no action required */
    }
}


/**
 * \brief Gets the register bit value with given mask.
 *
 * \param regId   [in]: RTC register ID
 * \param bitMask [in]: Mask to apply to the register
 * \return Register bit value with applied mask, no bit set for invalid register ID
 */
uint32_t Rtc_Reg_Get_RegBit( rtc_RegId_t regId, uint32_t bitMask )
{
    uint32_t retValue = RTC_REG_BITS_CLEAR;

    if( RTC_REG_CNT > regId )
    {
        retValue = ( *rtc_RegList[regId].RegAddr & bitMask );
    }
    else
    {
        /* Invalid register ID, no bit is set */
        retValue = RTC_REG_BITS_CLEAR;
    }

    return ( retValue );
}


/**
 * \brief Updates the register value with given mask and value.
 *
 * \param regId    [in]: RTC register ID
 * \param regMask  [in]: Mask to apply to the register
 * \param regValue [in]: Value to set in the register
 */
void Rtc_Reg_Set_RegVal( rtc_RegId_t regId, uint32_t regMask, uint32_t regValue )
{
    if( RTC_REG_CNT > regId )
    {
        *rtc_RegList[regId].RegAddr = ( ( *rtc_RegList[regId].RegAddr & ~regMask ) |
                                        ( regValue & regMask ) );
    }
    else
    {
        /* Invalid register ID, no action required */
    }
}


/**
 * \brief Gets the register value with given mask.
 *
 * \param regId   [in]: RTC register ID
 * \param regMask [in]: Mask to apply to the register
 * \return Register value with applied mask, no bit set for invalid register ID
 */
uint32_t Rtc_Reg_Get_RegVal( rtc_RegId_t regId, uint32_t regMask )
{
    uint32_t retValue = RTC_REG_BITS_CLEAR;

    if( RTC_REG_CNT > regId )
    {
        retValue = ( *rtc_RegList[regId].RegAddr & regMask );
    }
    else
    {
        /* Invalid register ID, no bit is set */
        retValue = RTC_REG_BITS_CLEAR;
    }

    return ( retValue );
}


/**
 * \brief Unlocks the RTC write protection (RTC_WPR key sequence).
 */
void Rtc_Reg_Unlock( void )
{
    *rtc_RegList[RTC_REG_WPR].RegAddr = RTC_REG_WPR_KEY1;
    *rtc_RegList[RTC_REG_WPR].RegAddr = RTC_REG_WPR_KEY2;
}


/**
 * \brief Locks the RTC write protection again.
 */
void Rtc_Reg_Lock( void )
{
    *rtc_RegList[RTC_REG_WPR].RegAddr = RTC_REG_WPR_LOCK;
}


/**
 * \brief Enters the calendar initialization mode (sets ICSR.INIT and waits for ICSR.INITF).
 *
 * The caller must have unlocked the write protection (\ref Rtc_Reg_Unlock) first.
 *
 * \return \ref RTC_REQUEST_OK once INITF is set, \ref RTC_REQUEST_ERROR on timeout
 */
rtc_RequestState_t Rtc_Reg_EnterInitMode( void )
{
    rtc_RequestState_t retState = RTC_REQUEST_ERROR;
    uint32_t            initF    = RTC_REG_BITS_CLEAR;

    *rtc_RegList[RTC_REG_ICSR].RegAddr |= RTC_ICSR_INIT;

    for( uint32_t waitIdx = 0u; RTC_REG_INIT_WAIT_LIMIT > waitIdx; waitIdx++ )
    {
        initF = ( *rtc_RegList[RTC_REG_ICSR].RegAddr & RTC_ICSR_INITF );

        if( RTC_REG_BITS_CLEAR != initF )
        {
            break;
        }
        else
        {
            /* INITF not set yet, keep waiting */
        }
    }

    if( RTC_REG_BITS_CLEAR != initF )
    {
        retState = RTC_REQUEST_OK;
    }
    else
    {
        /* Timeout waiting for INITF: clear ICSR.INIT again so the calendar resumes running
           instead of staying frozen in init mode, then report the error */
        *rtc_RegList[RTC_REG_ICSR].RegAddr &= ~RTC_ICSR_INIT;
        retState = RTC_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Leaves the calendar initialization mode (clears ICSR.INIT).
 */
void Rtc_Reg_ExitInitMode( void )
{
    *rtc_RegList[RTC_REG_ICSR].RegAddr &= ~RTC_ICSR_INIT;
}


/**
 * \brief Looks up the GPIO location of an RTC_OUT2 pin item.
 *
 * \param pinItem [in]:  Output pin enumerator (never \ref RTC_OUT2_PIN_UNUSED)
 * \param info    [out]: Set to point at the matching table row
 * \return \ref RTC_REQUEST_OK if found, \ref RTC_REQUEST_ERROR otherwise (info untouched)
 */
rtc_RequestState_t Rtc_Reg_Get_OutputPinInfo( rtc_OutputPin_t pinItem, const rtc_OutputPinInfo_t * * const info )
{
    rtc_RequestState_t retState = RTC_REQUEST_ERROR;
    const uint32_t      rowCnt  = (uint32_t)( sizeof(rtc_OutputPinList) / sizeof(rtc_OutputPinInfo_t) );

    if( RTC_NULL_PTR != info )
    {
        for( uint32_t rowIdx = 0u; rowCnt > rowIdx; rowIdx++ )
        {
            if( pinItem == rtc_OutputPinList[rowIdx].PinItem )
            {
                *info    = &rtc_OutputPinList[rowIdx];
                retState = RTC_REQUEST_OK;
                break;
            }
            else
            {
                /* Not this row, keep searching */
            }
        }
    }
    else
    {
        /* Invalid output pointer, no action required */
    }

    return ( retState );
}


/**
 * \brief Looks up the GPIO location of an RTC_REFIN pin item.
 *
 * \param pinItem [in]:  Reference input pin enumerator (never \ref RTC_REFIN_PIN_UNUSED)
 * \param info    [out]: Set to point at the matching table row
 * \return \ref RTC_REQUEST_OK if found, \ref RTC_REQUEST_ERROR otherwise (info untouched)
 */
rtc_RequestState_t Rtc_Reg_Get_RefInPinInfo( rtc_RefInPin_t pinItem, const rtc_RefInPinInfo_t * * const info )
{
    rtc_RequestState_t retState = RTC_REQUEST_ERROR;
    const uint32_t      rowCnt  = (uint32_t)( sizeof(rtc_RefInPinList) / sizeof(rtc_RefInPinInfo_t) );

    if( RTC_NULL_PTR != info )
    {
        for( uint32_t rowIdx = 0u; rowCnt > rowIdx; rowIdx++ )
        {
            if( pinItem == rtc_RefInPinList[rowIdx].PinItem )
            {
                *info    = &rtc_RefInPinList[rowIdx];
                retState = RTC_REQUEST_OK;
                break;
            }
            else
            {
                /* Not this row, keep searching */
            }
        }
    }
    else
    {
        /* Invalid output pointer, no action required */
    }

    return ( retState );
}

/* =========================== LOCAL FUNCTIONS ============================== */

/* =========================== INTERRUPT HANDLERS =========================== */

/* ================================ TASKS =================================== */
