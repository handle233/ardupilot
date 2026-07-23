# encoding: utf-8
"""
Waf tool for infineon build
"""

from waflib import Errors, Logs, Task, Utils, Context
from waflib.TaskGen import after_method, before_method, feature

import os
import shutil
import sys
import re
import pickle
import struct
import base64
import subprocess
import traceback

import hal_common

sys.path.append(os.path.join(os.path.dirname(os.path.realpath(__file__)), '../../libraries/AP_HAL_Infineon/hwdef/scripts'))

import infineon_hwdef

class generate_hex(Task.Task):
    def run(self):
            cmd = [self.env.get_flat('OBJCOPY'), '-O', 'ihex', self.inputs[0].relpath(),  self.outputs[0].relpath()]
            print(cmd)
            self.exec_command(cmd)

@feature('ch_ap_program')
@after_method('process_source')
def renesas_hexmake(self):
    link_output = self.link_task.outputs[0]
    bin_target = [self.bld.bldnode.find_or_declare('bin/' + link_output.change_ext('.hex').name)]
    generate_bin_task = self.create_task('generate_hex', src=link_output, tgt=bin_target)
    generate_bin_task.set_run_after(self.link_task)

def configure(cfg):
    cfg.find_program('arm-none-eabi-objcopy', var='OBJCOPY')
    cfg.env.AP_PROGRAM_FEATURES += ['ch_ap_program']
    env = cfg.env
    bldnode = cfg.bldnode.make_node(cfg.variant)

    print('infineon configure')

    env.INCLUDES += [
            cfg.srcnode.find_dir('modules/infineon/TARGET_CYT4BB7').abspath(),
            cfg.srcnode.find_dir('modules/infineon/TARGET_CYT4BB7/config/GeneratedSource').abspath(),

            cfg.srcnode.find_dir('modules/infineon/abstraction-rtos/include').abspath(),
            cfg.srcnode.find_dir('modules/infineon/abstraction-rtos/include/COMPONENT_FREERTOS').abspath(),
            cfg.srcnode.find_dir('modules/infineon/cat1cm0p/COMPONENT_CAT1C').abspath(),
            cfg.srcnode.find_dir('modules/infineon/clib-support/include').abspath(),
            cfg.srcnode.find_dir('modules/infineon/clib-support/source/TOOLCHAIN_GCC_ARM').abspath(),

            cfg.srcnode.find_dir('modules/infineon/cmsis/Core/Include').abspath(),
            cfg.srcnode.find_dir('modules/infineon/cmsis/Core/Include/a-profile').abspath(),
            cfg.srcnode.find_dir('modules/infineon/cmsis/Core/Include/m-profile').abspath(),
            cfg.srcnode.find_dir('modules/infineon/cmsis/Core/Include/r-profile').abspath(),

            cfg.srcnode.find_dir('modules/infineon/core-lib/include').abspath(),

            cfg.srcnode.find_dir('modules/infineon/freertos/Source/include').abspath(),
            cfg.srcnode.find_dir('modules/infineon/freertos/COMPONENT_FREERTOS_TZ/COMPONENT_SECURE_DEVICE/TOOLCHAIN_GCC_ARM').abspath(),
            cfg.srcnode.find_dir('modules/infineon/freertos/Source/portable/COMPONENT_CM7/TOOLCHAIN_GCC_ARM').abspath(),
            
            cfg.srcnode.find_dir('modules/infineon/mtb-hal-cat1/include').abspath(),
            cfg.srcnode.find_dir('modules/infineon/mtb-hal-cat1/include_pvt').abspath(),
            cfg.srcnode.find_dir('modules/infineon/mtb-hal-cat1/COMPONENT_CAT1C/include').abspath(),
            cfg.srcnode.find_dir('modules/infineon/mtb-hal-cat1/COMPONENT_CAT1C/include/pin_packages').abspath(),
            cfg.srcnode.find_dir('modules/infineon/mtb-hal-cat1/COMPONENT_CAT1C/include/triggers').abspath(),
        
            cfg.srcnode.find_dir('modules/infineon/mtb-pdl-cat1/devices/COMPONENT_CAT1C/include').abspath(),
            cfg.srcnode.find_dir('modules/infineon/mtb-pdl-cat1/devices/COMPONENT_CAT1C/include/ip').abspath(),
            cfg.srcnode.find_dir('modules/infineon/mtb-pdl-cat1/drivers/include').abspath(),
            cfg.srcnode.find_dir('modules/infineon/mtb-pdl-cat1/drivers/third_party/ethernet/include').abspath(),
        ]

    def srcpath(path):
        return cfg.srcnode.make_node(path).abspath()
    def bldpath(path):
        return bldnode.make_node(path).abspath()

    env.AP_LIBRARIES += [
            'modules/infineon/TARGET_CYT4BB7/*.c',
            'modules/infineon/TARGET_CYT4BB7/COMPONENT_CM7/*.c',
            'modules/infineon/TARGET_CYT4BB7/config/GeneratedSource/*.c',

            'modules/infineon/abstraction-rtos/source/*.c',
            'modules/infineon/abstraction-rtos/source/COMPONENT_FREERTOS/*.c',
            'modules/infineon/cat1cm0p/COMPONENT_CAT1C/COMPONENT_XMC7xDUAL_CM0P_SLEEP/*.c',
            'modules/infineon/clib-support/source/*.c',
            'modules/infineon/clib-support/source/COMPONENT_FREERTOS/*.c',
            'modules/infineon/clib-support/source/TOOLCHAIN_GCC_ARM/*.c',
            'modules/infineon/freertos/Source/*.c',
            'modules/infineon/freertos/Source/portable/COMPONENT_CM7/TOOLCHAIN_GCC_ARM/*.c',
            'modules/infineon/freertos/Source/portable/MemMang/*.c',
            'modules/infineon/mtb-hal-cat1/source/*.c',
            'modules/infineon/mtb-hal-cat1/COMPONENT_CAT1C/source/pin_packages/*.c',
            'modules/infineon/mtb-hal-cat1/COMPONENT_CAT1C/source/triggers/*.c',
            'modules/infineon/mtb-pdl-cat1/devices/COMPONENT_CAT1C/source/*.c',
            'modules/infineon/mtb-pdl-cat1/drivers/source/*.c',
            'modules/infineon/mtb-pdl-cat1/drivers/source/TOOLCHAIN_GCC_ARM/*.c',
            'modules/infineon/mtb-pdl-cat1/drivers/source/TOOLCHAIN_GCC_ARM/*.S',
            'modules/infineon/mtb-pdl-cat1/utils/TOOLCHAIN_GCC_ARM/*.c',
            'modules/infineon/mtb-pdl-cat1/drivers/third_party/ethernet/src/*.c',
        ]

    env.SRCROOT = srcpath('')
    env.BUILDROOT = bldpath('')

    generate_hwdef(env)

def generate_hwdef(env):
    if len(env.HWDEF) == 0:
        env.HWDEF = os.path.join(env.SRCROOT, 'libraries/AP_HAL_Infineon/hwdef/%s/hwdef.dat' % env.BOARD)
    
    hwdef_dir = [env.HWDEF]
    hwdef_out = env.BUILDROOT

    if not os.path.exists(hwdef_out):
        os.mkdir(hwdef_out)
    if env.HWDEF_EXTRA:
        hwdef.append(env.HWDEF_EXTRA)

    c = infineon_hwdef.HWDef(
        quiet = False,
        outdir = hwdef_out,
        hwdef = hwdef_dir,
    )
    c.run()

def build(bld):
    print('infineon build')
        
    shutil.copy('modules/infineon/TARGET_CYT4BB7/COMPONENT_CM7/TOOLCHAIN_GCC_ARM/linker_d.ld','build/CYT4BB7/')