
#include "GPIO.h"

using namespace Infineon;

GPIO::GPIO()
{

}

void GPIO::init()
{

}

void GPIO::pinMode(uint8_t pin, uint8_t output)
{
    cy_stc_gpio_pin_config_t led_config = {
        .outVal = 0,                  // initial value (0=off, 1=on, depending on the board)
        .driveMode = CY_GPIO_DM_HIGHZ, // high resistance output
        .hsiom = HSIOM_SEL_GPIO,     // select GPIO function
        .intEdge = CY_GPIO_INTR_DISABLE,
        .intMask = 0,
        .vtrip = CY_GPIO_VTRIP_CMOS,
        .slewRate = CY_GPIO_SLEW_FAST,
        .driveSel = CY_GPIO_DRIVE_FULL,
    };

    switch(output){
        case HAL_GPIO_INPUT:
            led_config.driveMode = CY_GPIO_DM_PULLUP;// input with pull-up
        break;
        case HAL_GPIO_OUTPUT:
            led_config.driveMode = CY_GPIO_DM_STRONG_IN_OFF;// strong output
        break;
        default:
        break;
    }

    Cy_GPIO_Pin_Init(MAKE_PORT(pin), MAKE_NUM(pin), &led_config);

}

uint8_t GPIO::read(uint8_t pin) {
    return Cy_GPIO_Read(MAKE_PORT(pin), MAKE_NUM(pin));
}

void GPIO::write(uint8_t pin, uint8_t value)
{
    Cy_GPIO_Write(MAKE_PORT(pin), MAKE_NUM(pin),value);
}

void GPIO::toggle(uint8_t pin)
{
    Cy_GPIO_Inv(MAKE_PORT(pin), MAKE_NUM(pin));  // toggle LED
}

/* Alternative interface: */
AP_HAL::DigitalSource* GPIO::channel(uint16_t n) {
    return NEW_NOTHROW DigitalSource(0);
}

bool GPIO::usb_connected(void)
{
    return false;
}

DigitalSource::DigitalSource(uint8_t v) :
    _v(v)
{}

void DigitalSource::mode(uint8_t output)
{}

uint8_t DigitalSource::read() {
    return _v;
}

void DigitalSource::write(uint8_t value) {
    _v = value;
}

void DigitalSource::toggle() {
    _v = !_v;
}
