'''
base class for hwdef processing

AP_FLAKE8_CLEAN
'''

from __future__ import annotations

import json
import os
import re
import shlex
import sys


class IncludeNotFoundException(Exception):
    def __init__(self, hwdef, includer):
        self.hwdef = hwdef
        self.includer = includer


class HWDef:
    def __init__(self, quiet=False, outdir=None, hwdef: list | None = None):
        if hwdef is None:
            hwdef = []

        self.outdir = outdir
        self.hwdef = hwdef
        self.quiet = quiet
        self.config = {}
        self.write_config = {}
        self.loaded_files = set()
        self.spilist = []

    def error(self, message):
        print("Error: " + message)
        sys.exit(1)

    def progress(self, message):
        if self.quiet:
            return
        print(message)

    def read_hwdef(self):
        self.config = {}
        self.loaded_files = set()

        for fname in self.hwdef:
            self.progress(f"Reading {fname}")
            self.loaded_files.add(os.path.abspath(fname))

            try:
                with open(fname, "r", encoding="utf-8") as hwdef_file:
                    data = json.load(hwdef_file)
            except FileNotFoundError:
                self.error(f"Unable to open file {fname}")
            except json.JSONDecodeError as exc:
                self.error(f"Invalid JSON in {fname}: {exc}")

            if not isinstance(data, dict):
                self.error(f"{fname} must contain a JSON object at the top level")

            self.config.update(data)
        return self.config

    def write_header_preamble(self, f):
        f.write(
'''/*
 generated hardware definitions from hwdef.dat - DO NOT EDIT
*/

#pragma once

#include <stdint.h>

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

''')
        f.write('''
//GPIO pin convert macro
#define MAKE_PIN(port,num) ((uint8_t)(port<<3)|(num&0x7))
#define MAKE_PORT(pin) ((GPIO_PRT_Type*) &GPIO_BASE->PRT[(pin>>3)])
#define MAKE_NUM(pin) ((uint8_t)pin&0x07)
''')
        f.write('''
//DMA channel difinition
typedef struct{
    void *dw;
    uint8_t channel;
    uint32_t DMA_irq_num;
    uint32_t trigger_dst;
    uint32_t size;
    uint32_t block_size;
    uint8_t *buf;
    void* callback;
} infineon_dma_def_t;

//uart info difinition
typedef struct {
    uint16_t tx;
    uint16_t rx;
    int txhsiom;
    int rxhsiom;
    void *scb;
    int clk_dst;
    uint32_t baudrate;
    uint8_t divider;
    infineon_dma_def_t *dma[2];
} infineon_uart_def_t;

//i2c info definition
typedef struct {
    uint8_t id;
    uint16_t scl;
    uint16_t sda;
    int scl_hsiom;
    int sda_hsiom;
    void *scb;
    int clk_dst;
    uint8_t divider;
    uint32_t irq_num;
    void* callback;
    void* event_callback;
} infineon_i2c_def_t;

//spi info definition
typedef struct{
    uint8_t id;
    uint16_t mosi;
    uint16_t miso;
    uint16_t sck;
    int mosi_hsiom;
    int miso_hsiom;
    int sck_hsiom;
    uint8_t ss_num;
    uint16_t ss[4];
    int ss_hsiom[4];
    void *scb;
    int clk_dst;
    uint32_t prescaler;
    uint8_t divider;
    uint32_t irq_num;
    void* callback;
    void* event_callback;
    uint8_t mode;
} infineon_spi_def_t;

//analog info definition
typedef struct{
    uint8_t id;
    uint16_t pin;
    uint16_t adc;
    uint8_t port_address,pin_address;
} infineon_analog_def_t;

//spi device definition
typedef struct{
    const char * dev_name;
    uint8_t busid;
    uint8_t cs;
} infineon_spidev_def_t;

//rcout definition
typedef struct{
    uint16_t pin;
    int pinhsiom;
    uint8_t tcpwm_counter;
    uint16_t divider;
} infineon_pwm_def_t;
''')





    def uart_write(self, uart):
        #{'id': 1, 'SCB': 0, 'tx': 'MAKE_PIN(0,1)', 'rx': 'MAKE_PIN(0,0)', 
        # 'baudrate': 115200, 'DIVIDER': 1}
        #{'PERIPHERAL_CLOCK': 'Cy_SysClk_ClkHfGetFrequency(2)', 'BOARD': 'aaa'}
        self.f.write('''
//initize SCB{SCB} as uart{ID}
#define DEFAULT_SERIAL{ID}_BAUD {BAUDRATE}
#define SCB_BLOCK{SCB}_DEFINITION        \\
infineon_uart_def_t uart_info{ID} = {{    \\
    .tx = MAKE_PIN({TXport}, {TXnum}),     \\
    .rx = MAKE_PIN({RXport}, {RXnum}),     \\
    .txhsiom = P{TXport}_{TXnum}_SCB{SCB}_UART_TX,  \\
    .rxhsiom = P{RXport}_{RXnum}_SCB{SCB}_UART_RX,  \\
    .scb = SCB{SCB},                         \\
    .clk_dst = PCLK_SCB{SCB}_CLOCK,           \\
    .baudrate = {BAUDRATE},               \\
    .divider = {DIVIDER},                 \\
    .dma = {{&{RXDMA},&{TXDMA}}}           \\
}};                                       \\
static cy_stc_dma_descriptor_t uart{ID}_tx_dma_desp CY_SECTION_SHAREDMEM CY_ALIGN(__SCB_DCACHE_LINE_SIZE);    \\
static UARTDriver uart{ID}(uart_info{ID},&uart{ID}_tx_dma_desp);  \\
void uart{ID}_rx_dma_isr(){{uart{ID}.uart_dma_rx_isr();}}    \\
void uart{ID}_tx_dma_isr(){{uart{ID}.uart_dma_tx_isr();}}
#define HAL_SERIAL_{ID} &uart{ID}
'''.format(SCB=uart['SCB'], ID=uart['id'], 
    TXport=uart['tx'][0], TXnum=uart['tx'][1], 
    RXport=uart['rx'][0], RXnum=uart['rx'][1],
    BAUDRATE=uart['baudrate'], DIVIDER=uart['DIVIDER'], RXDMA=uart['DMArx'], TXDMA=uart['DMAtx']))




    def uart_dma_write(self, charactor, dma, id):
        self.f.write('''
//initize dma{DW}_{CHAR},{CHANNEL} for uart{ID}
#define SCB_DMA{ID}_{CHAR}_DEFINITION        \\
static uint8_t uart{ID}_{CHAR}_dma_buf[{BUFSIZE}] CY_SECTION_SHAREDMEM CY_ALIGN(__SCB_DCACHE_LINE_SIZE);    \\
void uart{ID}_{CHAR}_dma_isr();      \\
infineon_dma_def_t uart_dma_def{ID}_{CHAR} = {{       \\
    .dw = DW{DW},       \\
    .channel = {CHANNEL},       \\
    .DMA_irq_num = cpuss_interrupts_dw{DW}_{CHANNEL}_IRQn,       \\
    .trigger_dst = {TRIGGER_DST},       \\
    .size = {BUFSIZE},       \\
    .block_size = {BLOCKSIZE},       \\
    .buf = uart{ID}_{CHAR}_dma_buf,       \\
    .callback = (void*)uart{ID}_{CHAR}_dma_isr       \\
}};
'''.format(ID=id, DW=dma['DMA'][0], CHANNEL=dma['DMA'][1],
TRIGGER_DST=dma['trigger'], BUFSIZE=dma['bufsize'], BLOCKSIZE=dma['blocksize'],CHAR=charactor))




    def uart_process(self,uarts):
        self.f.write('#define HAL_UART_NUM_SERIAL_PORTS {num}'
        .format(num=len(uarts)))
        for i in uarts:
            if len(i)>1:
                self.f.write('''
#define SCB{SCB}_DEFINITION \\
SCB_DMA{ID}_rx_DEFINITION  \\
SCB_DMA{ID}_tx_DEFINITION  \\
SCB_BLOCK{SCB}_DEFINITION 
'''.format(SCB=i[0]['SCB'], ID=i[0]['id']))
                i[0]['DMArx'] = 'uart_dma_def{ID}_rx'.format(ID=i[0]['id'])
                i[0]['DMAtx'] = 'uart_dma_def{ID}_tx'.format(ID=i[0]['id'])
                self.uart_dma_write('rx',i[1],i[0]['id'])
                self.uart_dma_write('tx',i[2],i[0]['id'])
            else:
                self.f.write('#define SCB{SCB}_DEFINITION SCB_BLOCK{SCB}_DEFINITION'.format(SCB=i[0]['SCB']))
                i[0]['DMArx'] = 'nullptr'
                i[0]['DMAtx'] = 'nullptr'
            self.uart_write(i[0])



    def i2c_write(self, i2c):
        i2cdata = i2c[0]
        self.f.write('''
//initize SCB{SCB} as i2c{id}
#define SCB{SCB}_DEFINITION //used for i2c bus definition
void i2c{id}_isr();
void i2c{id}_event_isr(uint32_t event);
#define I2C{id}_DIFINITION        \\
infineon_i2c_def_t i2c_info{id} = {{        \\
    .id = {id},                             \\
    .scl = MAKE_PIN({SCLport}, {SCLnum}),     \\
    .sda = MAKE_PIN({SDAport}, {SDAnum}),     \\
    .scl_hsiom = P{SCLport}_{SCLnum}_SCB{SCB}_I2C_SCL,  \\
    .sda_hsiom = P{SDAport}_{SDAnum}_SCB{SCB}_I2C_SDA,  \\
    .scb = SCB{SCB},                         \\
    .clk_dst = PCLK_SCB{SCB}_CLOCK,           \\
    .divider = {DIV},                     \\
    .irq_num = scb_{SCB}_interrupt_IRQn,     \\
    .callback = (void*)i2c{id}_isr,          \\
    .event_callback = (void*)i2c{id}_event_isr          \\
}};             \\
I2CBus i2cbus{id}(i2c_info{id});        \\
void i2c{id}_isr(){{i2cbus{id}.i2c_isr();}}     \\
void i2c{id}_event_isr(uint32_t event){{i2cbus{id}.i2c_event_isr(event);}}
'''.format(SCB=i2cdata['SCB'], id=i2cdata['id'], 
    SCLport=i2cdata['SCL'][0], SCLnum=i2cdata['SCL'][1], 
    SDAport=i2cdata['SDA'][0], SDAnum=i2cdata['SDA'][1]
    , DIV=i2cdata['DIVIDER']))

    def i2c_process(self, i2cs):
        self.f.write('''
//initize i2c blocks
#define I2C_DIFINITION      \\
''')
        for i in i2cs:
            self.f.write('I2C{ID}_DIFINITION  \\\n'.format(ID=i[0]['id']))
        self.f.write('//i2c bus definition\n')
        
        for i in i2cs:
            self.i2c_write(i)

            
        self.f.write('''
#define I2C_BUS_DEFINITION \\
I2CBus *I2CBuses[] = {       \\
''')
        flag = 0
        for i in i2cs:
            if flag == 0:  
                self.f.write('&i2cbus{id}       \\\n'.format(id=i[0]['id']))
                flag = 1
            else:
                self.f.write(',&i2cbus{id}       \\\n'.format(id=i[0]['id']))
        self.f.write('};\n')



    def spi_write(self, spi):
        spidata = spi[0]
        self.f.write('''
//initize SCB{SCB} as spi{id}
#define SCB{SCB}_DEFINITION //used for spi bus definition
void spi{id}_isr();
void spi{id}_event_isr(uint32_t events);
#define SPI{id}_DIFINITION        \\
infineon_spi_def_t spi_info{id} = {{        \\
    .id = {id},                             \\
    .mosi = MAKE_PIN({MOSIport}, {MOSInum}),     \\
    .miso = MAKE_PIN({MISOport}, {MISOnum}),     \\
    .sck = MAKE_PIN({SCKport}, {SCKnum}),     \\
    .mosi_hsiom = P{MOSIport}_{MOSInum}_SCB{SCB}_SPI_MOSI,  \\
    .miso_hsiom = P{MISOport}_{MISOnum}_SCB{SCB}_SPI_MISO,  \\
    .sck_hsiom = P{SCKport}_{SCKnum}_SCB{SCB}_SPI_CLK,  \\
    .ss_num = {SS_NUM},     \\
    .ss = {{     \\
'''.format(SCB=spidata['SCB'], id=spidata['id'],
    MOSIport=spidata['MOSI'][0], MOSInum=spidata['MOSI'][1], 
    MISOport=spidata['MISO'][0], MISOnum=spidata['MISO'][1], 
    SCKport=spidata['CLK'][0], SCKnum=spidata['CLK'][1],
    SS_NUM=len(spidata['select'])))

        flag_ = 0
        for i in range(len(spidata['select'])):
            if flag_ == 0:
                self.f.write('MAKE_PIN({SSport}, {SSnum})\\\n'
                    .format(SSport=spidata['select'][i][0], SSnum=spidata['select'][i][1]))
                flag_ = 1
            else:
                self.f.write(',MAKE_PIN({SSport}, {SSnum})\\\n'
                    .format(SSport=spidata['select'][i][0], SSnum=spidata['select'][i][1]))
        self.f.write('''},     \\
    .ss_hsiom = {     \\
''')

        flag_ = 0
        for i in range(len(spidata['select'])):
            if flag_ == 0:
                self.f.write('P{SSport}_{SSnum}_SCB{SCB}_SPI_SELECT{SSindex}\\\n'
                    .format(SSport=spidata['select'][i][0], SSnum=spidata['select'][i][1], SCB=spidata['SCB'], SSindex=i))
                flag_ = 1
            else:
                self.f.write(',P{SSport}_{SSnum}_SCB{SCB}_SPI_SELECT{SSindex}\\\n'
                    .format(SSport=spidata['select'][i][0], SSnum=spidata['select'][i][1], SCB=spidata['SCB'], SSindex=i))

        self.f.write('''}},     \\
    .scb = SCB{SCB},                         \\
    .clk_dst = PCLK_SCB{SCB}_CLOCK,           \\
    .prescaler = {PRESCALER},                 \\
    .divider = {DIV},                     \\
    .irq_num = scb_{SCB}_interrupt_IRQn,     \\
    .callback = (void*)spi{id}_isr,          \\
    .event_callback = (void*)spi{id}_event_isr,          \\
    .mode = {mode}  \\
}};        \\
SPIBus spibus{id}(spi_info{id});        \\
void spi{id}_isr(){{spibus{id}.spi_isr();}} \\
void spi{id}_event_isr(uint32_t events){{spibus{id}.spi_event_isr(events);}}
'''.format(SCB=spidata['SCB'], id=spidata['id'], DIV=spidata['DIVIDER'], PRESCALER=spidata['prediv'],mode = spidata['mode']))



    def spi_process(self, spis):
        self.f.write('''
//initize spi blocks
#define SPI_DIFINITION      \\
''')
        for i in spis:
            self.f.write('SPI{ID}_DIFINITION  \\\n'.format(ID=i[0]['id']))
        self.f.write('//spi bus definition\n')
        
        for i in spis:
            self.spi_write(i)

            
        self.f.write('''
//initize spi blocks
#define SPI_BUS_DEFINITION      \\
SPIBus *SPIBuses[] = {       \\
''')
        flag=0
        for i in spis:
            if flag == 0:
                self.f.write('&spibus{ID}  \\\n'.format(ID=i[0]['id']))
                flag=1
            else:
                self.f.write(',&spibus{ID}  \\\n'.format(ID=i[0]['id']))
        self.f.write('};\n')



    def analog_process(self,analog):
        self.f.write('''
#define ANALOG_PIN_NUM {len}
#define ANALOG_PINS \\
infineon_analog_def_t analogs[] = {{   \\'''.format(len=len(analog)))
        flag = 0
        for pin in analog:
            if flag == 1:
                self.f.write(', \\')
            self.f.write('''
{{ {id} , MAKE_PIN({port}, {num}) , {adc} ,{portaddr}, {pinaddr} }}  \\
'''.format(id=pin['id'],port=pin['PIN'][0],num=pin['PIN'][1],adc=pin['ADC'],
    portaddr=pin['portaddr'][1],pinaddr=pin['portaddr'][0]))
            flag = 1
        self.f.write('}\n')




    def write_spidev(self,dev):
        self.f.write('''
//add spi device list
#define SPIDEV_{name}_DEFINITION    \\
infineon_spidev_def_t spi_dev_{name}{{  \\
    .dev_name = "{name}",     \\
    .busid = {busid},       \\
    .cs = {cs},     \\
}};
'''.format(name=dev['name'],busid=dev['busid'],cs=dev['address']))
        self.spilist.append([dev['name']])

    
    def spidev_process(self,imus):
        for imu in imus:
            if imu['Bus'] == 'SPI':
                self.write_spidev(imu)



    def spi_dev_process(self):
        self.f.write('#define SPIDEV_DEFINITION   \\\n')
        for i in self.spilist:
            self.f.write('SPIDEV_{name}_DEFINITION   \\\n'.format(name=i[0]))
        self.f.write('infineon_spidev_def_t *spidevs[] = {')
        flag = 0
        for i in self.spilist:
            if flag == 1:
                self.f.write(',&spi_dev_{name}'.format(name=i[0]))
            else:
                self.f.write('&spi_dev_{name}'.format(name=i[0]))
                flag = 1
        self.f.write('};\n')


##define HAL_INS_PROBE2 ADD_BACKEND(AP_InertialSensor_Invensensev3::probe(*this, hal.spi->get_device("icm42688"), ROTATION_ROLL_180_YAW_270))



    def probe_imu(self,imus):
        count = 0
        for imu in imus:
            self.f.write('''
#define IMU_PROBE_{id} ADD_BACKEND({backend}::probe(*this,'''
.format(id=count,backend=imu['backend']))
            flag=0
            for dev in imu['devices_name']:
                if flag == 1:
                    self.f.write(',hal.spi->get_device("{dev}")'.format(dev=dev))
                else:
                    self.f.write('hal.spi->get_device("{dev}")'.format(dev=dev))
                    flag=1
            self.f.write(', {rotation}))'.format(rotation=imu['rotation']))
            count = count + 1

        self.f.write("\n\n#define HAL_INS_PROBE_LIST ")
        for i in range(0,count):
            self.f.write("IMU_PROBE_{id}; ".format(id=i))
        self.f.write("\n")


    def rcout_process(self,pwms):
        self.f.write('\n#define RCOUT_DEFINITION      \\')
        for pwm in pwms:
            self.f.write('''
infineon_pwm_def_t rcout_info{id} = {{    \\
    .pin = MAKE_PIN({port}, {num}),     \\
    .pinhsiom = P{port}_{num}_TCPWM0_LINE{counter},  \\
    .tcpwm_counter = {counter},         \\
    .divider = PCLK_TCPWM0_CLOCKS{counter}  \\
}};                                       \\
\\'''.format(id=pwm['id'], port=pwm['PIN'][0], num=pwm['PIN'][1], counter=pwm['num']))
        self.f.write('\ninfineon_pwm_def_t *rcout_list[] = {')
        flag = 0
        for i in range(len(pwms)):
            if flag == 1:
                self.f.write(',&rcout_info{id}'.format(id=pwms[i]['id']))
            else:
                self.f.write('&rcout_info{id}'.format(id=pwms[i]['id']))
                flag = 1    
        self.f.write('};\n')
        self.f.write('#define RCOUT_NUM {num}\n\n'.format(num=len(pwms)))


    def hwdef_process(self):
        for i in self.config:
            match i:
                case "UART":
                    self.uart_process(self.config[i])
                case "I2C":
                    self.i2c_process(self.config[i])
                case "SPI":
                    self.spi_process(self.config[i])
                case "IMU":
                    self.probe_imu(self.config[i])
                case "Analog":
                    self.analog_process(self.config[i])
                case "SPIDEV":
                    self.spidev_process(self.config[i])
                case "PWM":
                    self.rcout_process(self.config[i])
                case _:
                    self.f.write('#define {name} {defs}\n'
                    .format(name=i, defs=self.config[i]))
        self.spi_dev_process()





    def open_hwdef(self):
        os.makedirs(self.outdir, exist_ok=True)
        outfilename = os.path.join(self.outdir, "hwdef.h")
        self.progress(f"Writing hwdef setup in {outfilename}")

        self.f = open(outfilename, "w", encoding="utf-8")
        self.write_header_preamble(self.f)





    def run(self):
        self.read_hwdef()
        self.open_hwdef()
        self.hwdef_process()
        self.f.close()
