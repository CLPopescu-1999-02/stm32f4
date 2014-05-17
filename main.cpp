/*
 * (c) 2012 Thomas Wihl
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "StmSystem.h"
#include "CommandInterpreter.h"
#include "Commands.h"
#include "hw/lis302dl.h"
#include "hw/ws2801.h"
#include "hw/ssd1306.h"
#include "hw/tlc5940.h"
#include "Power.h"
#include "sdio.h"
#include "sw/sdcard.h"
#include "sw/eyes.h"
#include "sw/carcontroller.h"

#include <cstdio>
#include <memory>

StmSystem gSys;

class SpiChip : public Spi::Chip
{
public:
    SpiChip(Spi& spi, Gpio::Pin& s0, Gpio::Pin& s1, int value) :
        Spi::Chip(spi),
        mS0(s0),
        mS1(s1),
        mValue(value)
    { }

    void prepare() { mS0.set(mValue & 0x01); mS1.set(mValue & 0x02); }

private:
    Gpio::Pin& mS0;
    Gpio::Pin& mS1;
    int mValue;

};

int main()
{
    ClockControl::Reset::Reason rr = gSys.mRcc.resetReason();
    printf("\n\n\nRESET: ");
    if (rr & ClockControl::Reset::LowPower) printf("LOW POWER   ");
    if (rr & ClockControl::Reset::WindowWatchdog) printf("WINDOW WATCHDOG   ");
    if (rr & ClockControl::Reset::IndependentWatchdog) printf("INDEPENDENT WATCHDOG   ");
    if (rr & ClockControl::Reset::Software) printf("SOFTWARE RESET   ");
    if (rr & ClockControl::Reset::PowerOn) printf("POWER ON   ");
    if (rr & ClockControl::Reset::Pin) printf("PIN RESET   ");
    if (rr & ClockControl::Reset::BrownOut) printf("BROWN OUT   ");
    printf("\n");
    gSys.printInfo();

    gSys.mRcc.enable(ClockControl::Function::Dma1);
    gSys.mRcc.enable(ClockControl::Function::Dma2);

    // SPI1 for acceleration sensor and OLED displays
    gSys.mRcc.enable(ClockControl::Function::Spi1);
    gSys.mRcc.enable(ClockControl::Function::GpioA);
    gSys.mRcc.enable(ClockControl::Function::GpioE);
    // SCK
    gSys.mGpioA.configOutput(Gpio::Index::Pin5, Gpio::OutputType::PushPull, Gpio::Pull::Down);
    gSys.mGpioA.setAlternate(Gpio::Index::Pin5, Gpio::AltFunc::SPI1);
    // MISO
    gSys.mGpioA.configInput(Gpio::Index::Pin6);
    gSys.mGpioA.setAlternate(Gpio::Index::Pin6, Gpio::AltFunc::SPI1);
    // MOSI
    gSys.mGpioA.configOutput(Gpio::Index::Pin7, Gpio::OutputType::PushPull);
    gSys.mGpioA.setAlternate(Gpio::Index::Pin7, Gpio::AltFunc::SPI1);
    // CS
    gSys.mGpioE.configOutput(Gpio::Index::Pin3, Gpio::OutputType::PushPull);
    // INT1
    gSys.mGpioE.configInput(Gpio::Index::Pin0);
    gSys.mRcc.enable(ClockControl::Function::SysCfg);
    gSys.mSysCfg.extIntSource(Gpio::Index::Pin0, SysCfg::Gpio::E);
    InterruptController::Line extInt0(gSys.mNvic, StmSystem::InterruptIndex::EXTI0);
    extInt0.setCallback(&gSys.mExtI);
    extInt0.enable();
    // INT2
    gSys.mGpioE.configInput(Gpio::Index::Pin1);
    gSys.mSysCfg.extIntSource(Gpio::Index::Pin1, SysCfg::Gpio::E);
    InterruptController::Line extInt1(gSys.mNvic, StmSystem::InterruptIndex::EXTI1);
    extInt1.setCallback(&gSys.mExtI);
    extInt1.enable();

    gSys.mSpi1.configDma(new Dma::Stream(gSys.mDma2, Dma::Stream::StreamIndex::Stream3, Dma::Stream::ChannelIndex::Channel3,
                                         new InterruptController::Line(gSys.mNvic, StmSystem::InterruptIndex::DMA2_Stream3)),
                         new Dma::Stream(gSys.mDma2, Dma::Stream::StreamIndex::Stream2, Dma::Stream::ChannelIndex::Channel3,
                                         new InterruptController::Line(gSys.mNvic, StmSystem::InterruptIndex::DMA2_Stream2))
                         );
    gSys.mSpi1.setMasterSlave(Spi::MasterSlave::Master);
    gSys.mSpi1.enable(Device::Part::All);

    CommandInterpreter interpreter(gSys);

    gSys.mRcc.enable(ClockControl::Function::GpioD);

    gSys.mGpioD.configOutput(Gpio::Index::Pin12, Gpio::OutputType::PushPull, Gpio::Pull::None, Gpio::Speed::Fast);
    gSys.mGpioD.configOutput(Gpio::Index::Pin13, Gpio::OutputType::PushPull);
    gSys.mGpioD.configOutput(Gpio::Index::Pin14, Gpio::OutputType::PushPull);
    gSys.mGpioD.configOutput(Gpio::Index::Pin15, Gpio::OutputType::PushPull);

    gSys.mGpioD.set(Gpio::Index::Pin12);
    interpreter.add(new CmdHelp());
    interpreter.add(new CmdInfo(gSys));
    interpreter.add(new CmdFunc(gSys));
    interpreter.add(new CmdRead());
    interpreter.add(new CmdWrite());

    // Motor command
//    gSys.mGpioE.configOutput(Gpio::Index::Pin8, Gpio::OutputType::PushPull);
//    gSys.mGpioE.configOutput(Gpio::Index::Pin10, Gpio::OutputType::PushPull);
//    gSys.mGpioE.configOutput(Gpio::Index::Pin12, Gpio::OutputType::PushPull);
//    gSys.mGpioE.configOutput(Gpio::Index::Pin14, Gpio::OutputType::PushPull);
    gSys.mRcc.enable(ClockControl::Function::Tim1);
    Timer timer1(StmSystem::BaseAddress::TIM1, ClockControl::Clock::APB2);
    timer1.setFrequency(gSys.mRcc, 500);
    timer1.configCompare(Timer::CaptureCompareIndex::Index1, Timer::CompareMode::PwmActiveWhenLower, Timer::CompareOutput::Disabled, Timer::CompareOutput::ActiveHigh);
    timer1.configCompare(Timer::CaptureCompareIndex::Index2, Timer::CompareMode::PwmActiveWhenLower, Timer::CompareOutput::Disabled, Timer::CompareOutput::ActiveHigh);
    timer1.configCompare(Timer::CaptureCompareIndex::Index3, Timer::CompareMode::PwmActiveWhenLower, Timer::CompareOutput::Disabled, Timer::CompareOutput::ActiveHigh);
    timer1.configCompare(Timer::CaptureCompareIndex::Index4, Timer::CompareMode::PwmActiveWhenLower, Timer::CompareOutput::ActiveHigh, Timer::CompareOutput::Disabled);
    timer1.setCompare(Timer::CaptureCompareIndex::Index2, 0);
    timer1.setCompare(Timer::CaptureCompareIndex::Index1, 0);
    timer1.setCompare(Timer::CaptureCompareIndex::Index3, 0);
    timer1.setCompare(Timer::CaptureCompareIndex::Index4, 0);
    timer1.enable();
    gSys.mRcc.enable(ClockControl::Function::Tim3);
//    gSys.mGpioB.configOutput(Gpio::Index::Pin0, Gpio::OutputType::PushPull);
//    gSys.mGpioB.configOutput(Gpio::Index::Pin1, Gpio::OutputType::PushPull);
//    gSys.mGpioB.configOutput(Gpio::Index::Pin4, Gpio::OutputType::PushPull);
//    gSys.mGpioB.configOutput(Gpio::Index::Pin5, Gpio::OutputType::PushPull);
    Timer timer3(StmSystem::BaseAddress::TIM3, ClockControl::Clock::APB1);
    timer3.setFrequency(gSys.mRcc, 500);
    timer3.configCompare(Timer::CaptureCompareIndex::Index1, Timer::CompareMode::PwmActiveWhenLower, Timer::CompareOutput::ActiveHigh, Timer::CompareOutput::Disabled);
    timer3.configCompare(Timer::CaptureCompareIndex::Index2, Timer::CompareMode::PwmActiveWhenLower, Timer::CompareOutput::ActiveHigh, Timer::CompareOutput::Disabled);
    timer3.configCompare(Timer::CaptureCompareIndex::Index3, Timer::CompareMode::PwmActiveWhenLower, Timer::CompareOutput::ActiveHigh, Timer::CompareOutput::Disabled);
    timer3.configCompare(Timer::CaptureCompareIndex::Index4, Timer::CompareMode::PwmActiveWhenLower, Timer::CompareOutput::ActiveHigh, Timer::CompareOutput::Disabled);
    timer3.setCompare(Timer::CaptureCompareIndex::Index1, 0);
    timer3.setCompare(Timer::CaptureCompareIndex::Index2, 0);
    timer3.setCompare(Timer::CaptureCompareIndex::Index3, 0);
    timer3.setCompare(Timer::CaptureCompareIndex::Index4, 0);
    timer3.enable();
    CmdMotor* motor = new CmdMotor;
    motor->add(timer1, Timer::CaptureCompareIndex::Index1, Timer::CaptureCompareIndex::Index2);
    motor->add(timer1, Timer::CaptureCompareIndex::Index3, Timer::CaptureCompareIndex::Index4);
    motor->add(timer3, Timer::CaptureCompareIndex::Index1, Timer::CaptureCompareIndex::Index2);
    motor->add(timer3, Timer::CaptureCompareIndex::Index3, Timer::CaptureCompareIndex::Index4);
    interpreter.add(motor);
    gSys.mRcc.enable(ClockControl::Function::GpioE);
    gSys.mGpioE.setAlternate(Gpio::Index::Pin8, Gpio::AltFunc::TIM1);
    gSys.mGpioE.setAlternate(Gpio::Index::Pin10, Gpio::AltFunc::TIM1);
    gSys.mGpioE.setAlternate(Gpio::Index::Pin12, Gpio::AltFunc::TIM1);
    gSys.mGpioE.setAlternate(Gpio::Index::Pin14, Gpio::AltFunc::TIM1);
    gSys.mRcc.enable(ClockControl::Function::GpioB);
    gSys.mGpioB.setAlternate(Gpio::Index::Pin0, Gpio::AltFunc::TIM3);
    gSys.mGpioB.setAlternate(Gpio::Index::Pin1, Gpio::AltFunc::TIM3);
    gSys.mGpioB.setAlternate(Gpio::Index::Pin4, Gpio::AltFunc::TIM3);
    gSys.mGpioB.setAlternate(Gpio::Index::Pin5, Gpio::AltFunc::TIM3);


    Spi::Chip spi1(gSys.mSpi1);
    // acceleration sensor
    LIS302DL lis(spi1);
    lis.configInterrupt(new ExternalInterrupt::Line(gSys.mExtI, 0), new ExternalInterrupt::Line(gSys.mExtI, 1));
    interpreter.add(new CmdLis(lis));

    // clock command
    InterruptController::Line timer11Irq(gSys.mNvic, StmSystem::InterruptIndex::TIM1_TRG_COM_TIM11);
    Timer timer11(StmSystem::BaseAddress::TIM11, ClockControl::Clock::APB2);
    timer11.setInterrupt(Timer::InterruptType::CaptureCompare, &timer11Irq);
    Power pwr(StmSystem::BaseAddress::PWR);
    pwr.backupDomainWp(false);
    gSys.mRcc.enableRtc(ClockControl::RtcClock::HighSpeedExternal);
    pwr.backupDomainWp(true);
    interpreter.add(new CmdMeasureClock(gSys.mRcc, timer11));

    // pin command
    Gpio* gpio[] = { &gSys.mGpioA, &gSys.mGpioB, &gSys.mGpioC, &gSys.mGpioD, &gSys.mGpioE, &gSys.mGpioF, &gSys.mGpioG, &gSys.mGpioH, &gSys.mGpioI };
    interpreter.add(new CmdPin(gpio, sizeof(gpio) / sizeof(gpio[0])));

    // rgb command
//    gSys.mRcc.enable(ClockControl::Function::Spi2);
//    gSys.mRcc.enable(ClockControl::Function::GpioB);
//    // SCK
//    gSys.mGpioB.configOutput(Gpio::Index::Pin13, Gpio::OutputType::PushPull);
//    gSys.mGpioB.setAlternate(Gpio::Index::Pin13, Gpio::AltFunc::SPI2);
//    // MOSI
//    gSys.mGpioB.configOutput(Gpio::Index::Pin15, Gpio::OutputType::PushPull);
//    gSys.mGpioB.setAlternate(Gpio::Index::Pin15, Gpio::AltFunc::SPI2);

//    gSys.mSpi2.configDma(new Dma::Stream(gSys.mDma1, Dma::Stream::StreamIndex::Stream4, Dma::Stream::ChannelIndex::Channel0,
//                                         new InterruptController::Line(gSys.mNvic, StmSystem::InterruptIndex::DMA1_Stream4)),
//                         new Dma::Stream(gSys.mDma1, Dma::Stream::StreamIndex::Stream3, Dma::Stream::ChannelIndex::Channel0,
//                                         new InterruptController::Line(gSys.mNvic, StmSystem::InterruptIndex::DMA1_Stream3))
//                         );

    // 74HC4052: 1x SPI3 -> 4x SPI
    gSys.mRcc.enable(ClockControl::Function::GpioE);
    Gpio::Pin s0(gSys.mGpioE, Gpio::Index::Pin5);
    Gpio::Pin s1(gSys.mGpioE, Gpio::Index::Pin3);
    gSys.mGpioE.configOutput(Gpio::Index::Pin5, Gpio::OutputType::PushPull);
    gSys.mGpioE.configOutput(Gpio::Index::Pin3, Gpio::OutputType::PushPull);
    s0.reset();
    s1.reset();
    SpiChip spi3_0(gSys.mSpi3, s0, s1, 0);
    SpiChip spi3_1(gSys.mSpi3, s0, s1, 1);
    SpiChip spi3_2(gSys.mSpi3, s0, s1, 2);
    SpiChip spi3_3(gSys.mSpi3, s0, s1, 3);


    // 2 OLED displays
    gSys.mRcc.enable(ClockControl::Function::GpioC);
    Gpio::Pin dataCommand(gSys.mGpioC, Gpio::Index::Pin9);
    Gpio::Pin cs1(gSys.mGpioC, Gpio::Index::Pin7);
    Gpio::Pin cs2(gSys.mGpioC, Gpio::Index::Pin6);
    Gpio::Pin reset(gSys.mGpioC, Gpio::Index::Pin8);
    gSys.mGpioC.configOutput(Gpio::Index::Pin6, Gpio::OutputType::PushPull);
    gSys.mGpioC.configOutput(Gpio::Index::Pin7, Gpio::OutputType::PushPull);
    gSys.mGpioC.configOutput(Gpio::Index::Pin8, Gpio::OutputType::PushPull);
    gSys.mGpioC.configOutput(Gpio::Index::Pin9, Gpio::OutputType::PushPull);
    Ssd1306 oled1(spi1, cs1, dataCommand, reset);
    Ssd1306 oled2(spi1, cs2, dataCommand, reset);
    oled2.reset();
    //oled2.reset(); // Same reset as oled1
    oled1.drawString(0, 0, "Hello World 1!");
    oled1.init();
    oled2.drawString(0, 0, "Hello World 2!");
    oled2.init();


    Eyes eyes(gSys.mSysTick, &oled1, &oled2);
    eyes.start(Eyes::Blink, Eyes::Blink);

    // SPI3
    gSys.mRcc.enable(ClockControl::Function::GpioC);
    // SCK
    gSys.mGpioC.configOutput(Gpio::Index::Pin10, Gpio::OutputType::PushPull, Gpio::Pull::None, Gpio::Speed::Fast);
    gSys.mGpioC.setAlternate(Gpio::Index::Pin10, Gpio::AltFunc::SPI3);
    // MISO
    gSys.mGpioC.configInput(Gpio::Index::Pin11);
    gSys.mGpioC.setAlternate(Gpio::Index::Pin11, Gpio::AltFunc::SPI3);
    // MOSI
    gSys.mGpioC.configOutput(Gpio::Index::Pin12, Gpio::OutputType::PushPull, Gpio::Pull::None, Gpio::Speed::Fast);
    gSys.mGpioC.setAlternate(Gpio::Index::Pin12, Gpio::AltFunc::SPI3);

    gSys.mRcc.enable(ClockControl::Function::Spi3);
    gSys.mSpi3.configDma(new Dma::Stream(gSys.mDma1, Dma::Stream::StreamIndex::Stream5, Dma::Stream::ChannelIndex::Channel0,
                                         new InterruptController::Line(gSys.mNvic, StmSystem::InterruptIndex::DMA1_Stream5)),
                         new Dma::Stream(gSys.mDma1, Dma::Stream::StreamIndex::Stream0, Dma::Stream::ChannelIndex::Channel0,
                                         new InterruptController::Line(gSys.mNvic, StmSystem::InterruptIndex::DMA1_Stream0))
                         );
    gSys.mSpi3.setMasterSlave(Spi::MasterSlave::Master);
    gSys.mSpi3.enable(Device::Part::All);


    // LED command
    // TLC5940: 16x PWM LED
    gSys.mRcc.enable(ClockControl::Function::GpioD);
    gSys.mRcc.enable(ClockControl::Function::GpioB);
    gSys.mRcc.enable(ClockControl::Function::Tim9);
    gSys.mRcc.enable(ClockControl::Function::Tim10);
    Gpio::Pin xlat(gSys.mGpioD, Gpio::Index::Pin1);
    Gpio::Pin blank(gSys.mGpioD, Gpio::Index::Pin3);
    //Gpio::Pin gsclk(gSys.mGpioB, Gpio::Index::Pin8);
    gSys.mGpioD.configOutput(Gpio::Index::Pin1, Gpio::OutputType::PushPull);
    gSys.mGpioD.configOutput(Gpio::Index::Pin3, Gpio::OutputType::PushPull);
    gSys.mGpioB.configOutput(Gpio::Index::Pin8, Gpio::OutputType::PushPull);
    gSys.mGpioB.setAlternate(Gpio::Index::Pin8, Gpio::AltFunc::TIM10);
    Timer gsclkPwm(StmSystem::BaseAddress::TIM10, ClockControl::Clock::APB2);
    gsclkPwm.setFrequency(gSys.mRcc, 4096 * 50);
    Timer gsclkLatch(StmSystem::BaseAddress::TIM9, ClockControl::Clock::APB2);
    InterruptController::Line timer9IrqUpdate(gSys.mNvic, StmSystem::InterruptIndex::TIM1_BRK_TIM9);
    gsclkLatch.setInterrupt(Timer::InterruptType::Update, &timer9IrqUpdate);
    Tlc5940 tlc(spi3_0, xlat, blank, gsclkPwm, gsclkLatch);
    interpreter.add(new CmdLed(tlc));

    Ws2801 ws(spi3_1, 1);
    ws.enable();
    interpreter.add(new CmdRgb(ws));


    gSys.mRcc.enable(ClockControl::Function::GpioC);
    gSys.mGpioC.configInput(Gpio::Index::Pin1, Gpio::Pull::Up);


//    gSys.mRcc.enable(ClockControl::Function::Sdio);
//    gSys.mRcc.enable(ClockControl::Function::GpioC);
//    gSys.mRcc.enable(ClockControl::Function::GpioD);
//    gSys.mGpioC.configOutput(Gpio::Index::Pin8, Gpio::OutputType::PushPull, Gpio::Pull::Up, Gpio::Speed::Fast);    // D0
//    gSys.mGpioC.configOutput(Gpio::Index::Pin9, Gpio::OutputType::PushPull, Gpio::Pull::Up, Gpio::Speed::Fast);    // D1
//    gSys.mGpioC.configOutput(Gpio::Index::Pin10, Gpio::OutputType::PushPull, Gpio::Pull::Up, Gpio::Speed::Fast);    // D2
//    gSys.mGpioC.configOutput(Gpio::Index::Pin11, Gpio::OutputType::PushPull, Gpio::Pull::Up, Gpio::Speed::Fast);    // D3
//    gSys.mGpioC.configOutput(Gpio::Index::Pin12, Gpio::OutputType::PushPull, Gpio::Pull::None, Gpio::Speed::Fast);    // CK
//    gSys.mGpioD.configOutput(Gpio::Index::Pin2, Gpio::OutputType::PushPull, Gpio::Pull::Up, Gpio::Speed::Fast);    // CMD
//    gSys.mGpioC.setAlternate(Gpio::Index::Pin8, Gpio::AltFunc::SDIO);    // D0
//    gSys.mGpioC.setAlternate(Gpio::Index::Pin9, Gpio::AltFunc::SDIO);    // D1
//    gSys.mGpioC.setAlternate(Gpio::Index::Pin10, Gpio::AltFunc::SDIO);    // D2
//    gSys.mGpioC.setAlternate(Gpio::Index::Pin11, Gpio::AltFunc::SDIO);    // D3
//    gSys.mGpioC.setAlternate(Gpio::Index::Pin12, Gpio::AltFunc::SDIO);    // CK
//    gSys.mGpioD.setAlternate(Gpio::Index::Pin2, Gpio::AltFunc::SDIO);    // CMD

//    gSys.mRcc.enable(ClockControl::Function::Dma2);
//    InterruptController::Line sdioIrq(gSys.mNvic, StmSystem::InterruptIndex::SDIO);
//    Dma::Stream sdioDma(gSys.mDma2, Dma::Stream::StreamIndex::Stream3, Dma::Stream::ChannelIndex::Channel4,
//        new InterruptController::Line(gSys.mNvic, StmSystem::InterruptIndex::DMA2_Stream3));

//    Sdio sdio(StmSystem::BaseAddress::SDIO, sdioIrq, sdioDma);
//    SdCard sdCard(sdio, 30);
//    interpreter.add(new CmdSdio(sdCard));

    // 4 x GPIO
    // Hall sensor needs pullup
    gSys.mGpioB.configInput(Gpio::Index::Pin11, Gpio::Pull::Up);
    gSys.mGpioE.configInput(Gpio::Index::Pin11, Gpio::Pull::Up);
    gSys.mGpioE.configInput(Gpio::Index::Pin13, Gpio::Pull::Up);
    gSys.mGpioE.configInput(Gpio::Index::Pin15, Gpio::Pull::Up);

    Gpio::ConfigurablePin gpio0(gSys.mGpioB, Gpio::Index::Pin11);
    Gpio::ConfigurablePin gpio1(gSys.mGpioE, Gpio::Index::Pin15);
    Gpio::ConfigurablePin gpio2(gSys.mGpioE, Gpio::Index::Pin13);
    Gpio::ConfigurablePin gpio3(gSys.mGpioE, Gpio::Index::Pin11);

    gSys.mSysCfg.extIntSource(Gpio::Index::Pin11, SysCfg::Gpio::E);
    gSys.mSysCfg.extIntSource(Gpio::Index::Pin13, SysCfg::Gpio::E);
    gSys.mSysCfg.extIntSource(Gpio::Index::Pin15, SysCfg::Gpio::E);
    InterruptController::Line extInt10_15(gSys.mNvic, StmSystem::InterruptIndex::EXTI15_10);
    extInt10_15.setCallback(&gSys.mExtI);
    extInt10_15.enable();
    HcSr04 hc(gSys.mSysTick, gpio1, new ExternalInterrupt::Line(gSys.mExtI, 15));
    hc.addDevice(gpio2, new ExternalInterrupt::Line(gSys.mExtI, 13));
    hc.addDevice(gpio3, new ExternalInterrupt::Line(gSys.mExtI, 11));
    CmdDistance dist(hc);
    interpreter.add(&dist);

    interpreter.start();
    CarController cc(gSys.mSysTick, hc, 0, 1, 2,
                     *motor, 1, 0, 3,
                     tlc);
    cc.setEyes(&eyes);
    cc.stop();


    //gSys.mIWdg.enable(2000000);
    System::Event* event;
    uint64_t old = 0;
    while (true)
    {
        if (gSys.waitForEvent(event) && event != nullptr)
        {
            if (old == 0 && gSys.mGpioA.get(Gpio::Index::Pin0))
            {
                old = System::instance()->ns();
                if (cc.running()) cc.stop();
                else cc.start();
            }
            else if (old != 0 && !gSys.mGpioA.get(Gpio::Index::Pin0) && System::instance()->ns() - old > 100000000)
            {
                old = 0;
            }
            //gSys.mIWdg.service();
//            gSys.mDebug.write(gSys.mGpioB.get(Gpio::Index::Pin11) ? "1" : "0", 1);

            gSys.mGpioD.set(Gpio::Index::Pin13);
            event->callback();
            gSys.mGpioD.reset(Gpio::Index::Pin13);
        }
    }

    return 0;

}



