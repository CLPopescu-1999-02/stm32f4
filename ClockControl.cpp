#include "ClockControl.h"

#include <cstdlib>
#include <algorithm>

ClockControl::ClockControl(System::BaseAddress base, uint32_t externalClock) :
    mBase(reinterpret_cast<volatile RCC*>(base)),
    mExternalClock(externalClock)
{
}

ClockControl::~ClockControl()
{
}

bool ClockControl::addChangeHandler(ClockControl::ChangeHandler *changeHandler)
{
    for (unsigned int i = 0; i < MAX_CHANGE_HANDLER; ++i)
    {
        if (mChangeHandler[i] == 0)
        {
            mChangeHandler[i] = changeHandler;
            return true;
        }
    }
    return false;
}

bool ClockControl::removeChangeHandler(ClockControl::ChangeHandler *changeHandler)
{
    for (unsigned int i = 0; i < MAX_CHANGE_HANDLER; ++i)
    {
        if (mChangeHandler[i] == changeHandler)
        {
            mChangeHandler[i] = 0;
            return true;
        }
    }
    return false;
}

void ClockControl::resetClock()
{
    // enable internal clock
    System::setRegister(&mBase->CR, 0x00000081);
    // configure internal clock as clock source and wait till it is active
    System::setRegister(&mBase->CFGR, 0x00000000);
    while (mBase->CFGR.SWS != 0)
    {
    }
    // reset pll config
    System::setRegister(&mBase->PLLCFGR, 0x24003010);
    // reset interrupts
    System::setRegister(&mBase->CIR, 0x00000000);
    // reset I2S pll
    System::setRegister(&mBase->PLLI2SCFGR, 0x20003000);
}

void ClockControl::reset()
{
    resetClock();
    System::setRegister(&mBase->AHB1RSTR, 0x00000000);
    System::setRegister(&mBase->AHB2RSTR, 0x00000000);
    System::setRegister(&mBase->AHB3RSTR, 0x00000000);
    System::setRegister(&mBase->APB1RSTR, 0x00000000);
    System::setRegister(&mBase->APB2RSTR, 0x00000000);
    System::setRegister(&mBase->AHB1ENR, 0x00100000);
    System::setRegister(&mBase->AHB2ENR, 0x00000000);
    System::setRegister(&mBase->AHB3ENR, 0x00000000);
    System::setRegister(&mBase->APB1ENR, 0x00000000);
    System::setRegister(&mBase->APB2ENR, 0x00000000);
    System::setRegister(&mBase->AHB1LPENR, 0x7e6791ff);
    System::setRegister(&mBase->AHB2LPENR, 0x000000f1);
    System::setRegister(&mBase->AHB3LPENR, 0x00000001);
    System::setRegister(&mBase->APB1LPENR, 0x36fec9ff);
    System::setRegister(&mBase->APB2LPENR, 0x00075f33);
    System::setRegister(&mBase->BDCR, 0x00000000);
    System::setRegister(&mBase->CSR, 0x0e000000);
    System::setRegister(&mBase->SSCGR, 0x00000000);
}

bool ClockControl::setSystemClock(uint32_t clock)
{
    for (unsigned int i = 0; i < MAX_CHANGE_HANDLER; ++i)
    {
        if (mChangeHandler[i] != 0) mChangeHandler[i]->clockPrepareChange(clock);
    }
    if (mBase->CR.HSEON) resetClock();
    // enable external oscillator
    mBase->CR.HSEON = 1;

    int timeout = CLOCK_WAIT_TIMEOUT;
    while (!mBase->CR.HSERDY)
    {
        if (--timeout == 0)
        {
            // external oscillator not working, disable and return
            mBase->CR.HSEON = 0;
            return false;
        }
    }
    mBase->APB1ENR.PWREN = 1;
    // AHB = system clock
    mBase->CFGR.HPRE = 0;   // 0-7 = /1, 8 = /2, 9 = /4, 10 = /8, 11 = /16 ... 15 = /512
    // APB2 = AHB / 2
    mBase->CFGR.PPRE2 = 4;  // 0-3 = /1, 4 = /2, 5 = /4, 6 = /8, 7 = /16
    // APB1 = AHB / 4
    mBase->CFGR.PPRE1 = 5;  // 0-3 = /1, 4 = /2, 5 = /4, 6 = /8, 7 = /16
    uint32_t div, mul;
    if (!getPllConfig(clock * 2, div, mul)) return false;
    // e.g. for external clock of 8MHz div should be 8 and mul should be 336
    // VCO in = external oscillator / 8 = 1MHz
    mBase->PLLCFGR.PLLM = div;  // 2..63
    // VCO out = VCO in * 336 = 336MHz
    mBase->PLLCFGR.PLLN = mul;  // 192..432
    // PLL out = VCO out / 2 = 168MHz
    mBase->PLLCFGR.PLLP = 3;    // 0 = /2, 1 = /4, 2 = /6, 3 = /8
    // PLL48CLK = VCO out / 7 = 48MHz
    mBase->PLLCFGR.PLLQ = 7;    // 2..15
    // external oscillator is source for PLL
    mBase->PLLCFGR.PLLSRC = 1;
    // enable PLL and wait till it is ready
    mBase->CR.PLLON = 1;
    while (!mBase->CR.PLLRDY)
    {
    }
    // configure PLL as clock source and wait till it is active
    mBase->CFGR.SW = 2;    // 0 = internal, 1 = external, 2 = PLL
    while (mBase->CFGR.SWS != 2)
    {
    }

    for (unsigned int i = 0; i < MAX_CHANGE_HANDLER; ++i)
    {
        if (mChangeHandler[i] != 0) mChangeHandler[i]->clockChanged(clock);
    }

    return true;
}

uint32_t ClockControl::systemClock()
{
    static const uint32_t PLLP_TABLE[] = { 2, 4, 6, 8 };

    uint32_t clock = 0;
    switch (mBase->CFGR.SWS)
    {
    case 0: // internal clock
        clock = INTERNAL_CLOCK;
        break;
    case 1: // external clock
        clock = mExternalClock;
        break;
    case 2: // pll
        if (mBase->PLLCFGR.PLLSRC)
        {
            // external clock
            clock = mExternalClock;
        }
        else
        {
            // internal clock
            clock = INTERNAL_CLOCK;
        }
        clock = clock / mBase->PLLCFGR.PLLM * mBase->PLLCFGR.PLLN / PLLP_TABLE[mBase->PLLCFGR.PLLP];
        break;
    }
    return clock;
}

uint32_t ClockControl::ahbClock()
{
    return systemClock() >> std::max(0, static_cast<int>(mBase->CFGR.PPRE2) - 7);
}

uint32_t ClockControl::apb1Clock()
{
    return ahbClock() >> std::max(0, static_cast<int>(mBase->CFGR.PPRE2) - 3);
}

uint32_t ClockControl::apb2Clock()
{
    return ahbClock() >> std::max(0, static_cast<int>(mBase->CFGR.PPRE2) - 3);
}

bool ClockControl::getPllConfig(uint32_t clock, uint32_t &div, uint32_t &mul)
{
    // we want to do a 32bit calculation, so we divide pll and external clock by 10
    uint32_t external = mExternalClock / 10;
    uint32_t pll = clock / 10;
    static const uint32_t vcoInMin = 1 * 1000 * 1000 / 10;
    static const uint32_t vcoInMax = 2 * 1000 * 1000 / 10;
    uint32_t tmpdiv, tmpmul, bestdelta = pll;
    for (tmpdiv = 2; tmpdiv <= 63; ++tmpdiv)
    {
        uint32_t vcoIn = external / tmpdiv;
        if (vcoIn >= vcoInMin && vcoIn <= vcoInMax)
        {
            // PLL = external / div * mul;
            tmpmul = pll * tmpdiv / external;
            if (tmpmul >= 192 && tmpmul <= 432)
            {
                uint32_t tmppll = external / tmpdiv * tmpmul;
                uint32_t delta = std::abs(static_cast<int>(pll) - static_cast<int>(tmppll));
                if (delta < bestdelta)
                {
                    div = tmpdiv;
                    mul = tmpmul;
                    bestdelta = delta;
                    if (delta == 0) return true;
                }
            }
        }
    }
    // we didn't find an exact match, if we found an aproximate one return it
    if (bestdelta != pll) return true;
    // we didn't find anything, set div and mul to the lowest cock possible
    div = 63;
    mul = 192;
    return false;
}
