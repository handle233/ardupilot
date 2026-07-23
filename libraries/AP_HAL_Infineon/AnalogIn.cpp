#include "AnalogIn.h"

using namespace Infineon;
extern const AP_HAL::HAL& hal;

ANALOG_PINS;

AnalogIn::PinMap AnalogIn::pins[ANALOG_PIN_NUM] = {0};
int AnalogIn::bat_channel = 0;
StackType_t AnalogIn::Stack[INFENION_ANALOG_PROCESS_STACK_SIZE];

AnalogSource::AnalogSource(int _channel) :
    channel(_channel)
{}

float AnalogSource::read_average() {
    return AnalogIn::pins[channel].average;
}

float AnalogSource::voltage_average() {
    return base_volt * AnalogIn::pins[channel].average / 4096.0f;
}

float AnalogSource::voltage_latest() {
    return base_volt * AnalogIn::pins[channel].recent / 4096.0f;
}

float AnalogSource::read_latest() {
    return AnalogIn::pins[channel].recent;
}

bool AnalogSource::set_pin(uint8_t p) {
    for(int i=0;i<ANALOG_PIN_NUM;i++){
        if(AnalogIn::pins[i].id == p){
            channel = AnalogIn::pins[i].channel;
        }
    }
    return true;
}

AnalogIn::AnalogIn()
{
    sar_0_config =
    {
        .preconditionTime = 0U,
        .powerupTime = 0U,
        .enableIdlePowerDown = false,
        .msbStretchMode = CY_SAR2_MSB_STRETCH_MODE_1CYCLE,
        .enableHalfLsbConv = false,
        .sarMuxEnable = true,
        .adcEnable = true,
        .sarIpEnable = false,
        .channelConfig = {nullptr}
    };
}

void AnalogIn::init()
{
    infineon_init_clock(PCLK_PASS0_CLOCK_SAR0,CY_SYSCLK_DIV_8_BIT,15,100);

    for(int i=0;i<ANALOG_PIN_NUM;i++){
        pins[i].id = analogs[i].id;
        pins[i].channel = i;
        pins[i].average = pins[i].average_count = 0;

        if(pins[i].id == ANALOG_BAT_ID){
            bat_channel = i;
        }

        sar_0_config.channelConfig[i] = &channels[i];

        channels[i].channelHwEnable = true;
        channels[i].triggerSelection = CY_SAR2_TRIGGER_OFF;
        channels[i].channelPriority = 3U;
        channels[i].preenptionType = CY_SAR2_PREEMPTION_FINISH_RESUME;
        channels[i].doneLevel = CY_SAR2_DONE_LEVEL_PULSE;

        channels[i].isGroupEnd = true;

        channels[i].pinAddress = (cy_en_sar2_pin_address_t)analogs[i].pin_address;
        channels[i].portAddress = (cy_en_sar2_port_address_t)analogs[i].port_address;
        channels[i].extMuxEnable = false;
        channels[i].extMuxSelect = 0U;
        channels[i].preconditionMode = CY_SAR2_PRECONDITION_MODE_OFF;
        channels[i].overlapDiagMode = CY_SAR2_OVERLAP_DIAG_MODE_OFF;
        channels[i].sampleTime = 100U;
        channels[i].calibrationValueSelect = CY_SAR2_CALIBRATION_VALUE_REGULAR;
        channels[i].postProcessingMode = CY_SAR2_POST_PROCESSING_MODE_AVG;
        channels[i].resultAlignment = CY_SAR2_RESULT_ALIGNMENT_RIGHT;
        channels[i].signExtention = CY_SAR2_SIGN_EXTENTION_UNSIGNED;
        channels[i].averageCount = 16U;
        channels[i].rightShift = 4U;
        channels[i].positiveReload = 0U;
        channels[i].negativeReload = 0U;
        channels[i].rangeDetectionMode = CY_SAR2_RANGE_DETECTION_MODE_BELOW_LO;
        channels[i].rangeDetectionLoThreshold = 0U;
        channels[i].rangeDetectionHiThreshold = 65535U;

        Cy_GPIO_Pin_FastInit(MAKE_PORT(analogs[i].pin),
        MAKE_NUM(analogs[i].pin),CY_GPIO_DM_ANALOG,1,P6_1_GPIO);
    }

    channels[ANALOG_PIN_NUM-1].isGroupEnd = true;

    Cy_SAR2_Init(PASS0_SAR0,&sar_0_config);

    Cy_SAR2_Enable(PASS0_SAR0);

    task_handle = xTaskCreateStatic(periodic_process,"ANALOG_SAMPLE",
        INFENION_ANALOG_PROCESS_STACK_SIZE,this,
        RTOS_PRIORITY_ANALOG_PROCESS,Stack,&_thread);
}

AP_HAL::AnalogSource* AnalogIn::channel(int16_t n) {
    for(int i=0;i<ANALOG_PIN_NUM;i++){
        if(pins[i].id == n){
            return NEW_NOTHROW AnalogSource(pins[i].channel);
        }
    }
    DEV_PRINTF("HAL_AnalogSource : find analog pin %d failed\n",n);
    return nullptr;
}

float AnalogIn::board_voltage(void)
{

    return 5.0f;
}

void Infineon::AnalogIn::periodic_process(void* pAnalog)
{
    for(;;)
    for(int i=0;i<ANALOG_PIN_NUM;i++){
        vTaskDelay(1);
        Cy_SAR2_Channel_SoftwareTrigger(PASS0_SAR0,pins[i].channel);
        while(0 == (CY_SAR2_INT_GRP_DONE &
             Cy_SAR2_Channel_GetInterruptStatus(PASS0_SAR0,pins[i].channel))){
            vTaskDelay(1);
        }
        pins[i].recent = Cy_SAR2_Channel_GetResult(PASS0_SAR0,pins[i].channel,NULL);
        Cy_SAR2_Channel_ClearInterrupt(PASS0_SAR0,pins[i].channel,CY_SAR2_INT_GRP_DONE);

        pins[i].average = (pins[i].average * pins[i].average_count + pins[i].recent)
            / (++pins[i].average_count);
        
        if(pins[i].average_count>1024){
            pins[i].average_count = 0;
        }
    }
    vTaskDelete(NULL);
}
