AGID := $(shell git rev-parse HEAD | head -c 7)
SGID := $(shell git -C znet rev-parse HEAD | head -c 7)
SVER := $(shell git -C znet describe --tags | cut -d- -f1 | sed 's/r//')


all: znet dbch.hex

znet:
	$(error Need $@ symbolic links to build)


INCS += -I. -Icarb -Iznet
INCS += -Iznet/protocol/zigbee
INCS += -Iznet/protocol/zigbee/stack
INCS += -Iznet/protocol/zigbee/app/framework/include
INCS += -Iznet/platform/Device/SiliconLabs/EFR32MG12P/Include
INCS += -Iznet/platform/radio/rail_lib/chip/efr32/efr32xg1x
INCS += -Iznet/platform/radio/rail_lib/common
INCS += -Iznet/platform/radio/rail_lib/plugin/coexistence/hal/efr32
INCS += -Iznet/platform/radio/rail_lib/protocol/ieee802154
INCS += -Iznet/util/plugin/plugin-common/mbedtls
INCS += -Iznet/util/third_party/mbedtls
INCS += -Iznet/util/third_party/mbedtls/include
INCS += -Iznet/util/third_party/mbedtls/sl_crypto/include
INCS += -Iznet/platform/base/hal
INCS += -Iznet/platform/base/hal/plugin
INCS += -Iznet/platform/base/hal/micro/cortexm3/efm32
INCS += -Iznet/platform/base/hal/micro/cortexm3/efm32/config
INCS += -Iznet/platform/base/hal/micro/cortexm3/efm32/efr32
INCS += -Iznet/platform/base
INCS += -Iznet/platform/CMSIS/Include
INCS += -Iznet/platform/emdrv/common/inc
INCS += -Iznet/platform/emdrv/dmadrv/inc
INCS += -Iznet/platform/emdrv/gpiointerrupt/inc
INCS += -Iznet/platform/emdrv/sleep/inc
INCS += -Iznet/platform/emdrv/tempdrv/inc
INCS += -Iznet/platform/emdrv/uartdrv/inc
INCS += -Iznet/platform/emdrv/ustimer/inc
INCS += -Iznet/platform/emlib/inc
INCS += -Iznet/platform/middleware/glib
INCS += -Iznet/platform/middleware/glib/glib
INCS += -Iznet/platform/radio/rail_lib/plugin
INCS += -Iznet/platform/halconfig/inc/hal-config
INCS += -Iznet/protocol/zigbee/app/framework
INCS += -Iznet/protocol/zigbee/app/util
INCS += -Iznet/platform/service/sleeptimer/inc
INCS += -Iznet/platform/service/sleeptimer/config
INCS += -Iznet/platform/common/inc
INCS += -Iznet/platform/service/mpu/inc
INCS += -Iznet/util/third_party/segger/systemview/SEGGER
INCS += -Iznet/util/third_party/segger/systemview/Config

DEFS += -DAGID=$(AGID) -DSGID=$(SGID) -DSVER=$(SVER)
DEFS += -DCONFIGURATION_HEADER='"app/framework/util/config.h"'
DEFS += -DCORTEXM3=1
DEFS += -DCORTEXM3_EFM32_MICRO=1
DEFS += -DCORTEXM3_EFR32=1
DEFS += -DCORTEXM3_EFR32_MICRO=1
DEFS += -DNULL_BTL=1
DEFS += -DATTRIBUTE_STORAGE_CONFIGURATION='"dbch_endpoint_config.h"'
DEFS += -DCORTEXM3_EFR32MG12P433F1024GL125=1
DEFS += -DCORTEXM3_EFR32MG12P433F1024GL125_MICRO=1
DEFS += -DGENERATED_TOKEN_HEADER='"dbch_tokens.h"'
DEFS += -DZA_GENERATED_HEADER='"dbch_znet.h"'
DEFS += -DEFR32MG12P=1
DEFS += -DEFR32MG12P433F1024GL125=1
DEFS += -DEFR32_SERIES1_CONFIG2_MICRO=1
DEFS += -DLOCKBITS_IN_MAINFLASH_SIZE=0
DEFS += -DPSSTORE_SIZE=16384
DEFS += -DLONGTOKEN_SIZE=0
DEFS += -DHAL_CONFIG=1
DEFS += -DEMBER_AF_USE_HWCONF=1
DEFS += -DEMBER_AF_API_EMBER_TYPES='"stack/include/ember-types.h"'
DEFS += -DEMBER_AF_API_DEBUG_PRINT='"app/framework/util/print.h"'
DEFS += -DEMBER_AF_API_AF_HEADER='"app/framework/include/af.h"'
DEFS += -DEMBER_AF_API_AF_SECURITY_HEADER='"app/framework/security/af-security.h"'
DEFS += -DEMBER_AF_API_NEIGHBOR_HEADER='"stack/include/stack-info.h"'
DEFS += -DEMBER_STACK_ZIGBEE=1
DEFS += -DMBEDTLS_CONFIG_FILE='"mbedtls-config-generated.h"'
DEFS += -DEMLIB_USER_CONFIG=1
DEFS += -DPHY_RAILGB=1
DEFS += -DAPPLICATION_TOKEN_HEADER='"znet-token.h"'
DEFS += -DAPPLICATION_MFG_TOKEN_HEADER='"znet-mfg-token.h"'
DEFS += -DMBEDTLS_DEVICE_ACCELERATION_CONFIG_FILE='"configs/config-device-acceleration.h"'
DEFS += -DMBEDTLS_DEVICE_ACCELERATION_CONFIG_APP_FILE='"config-device-acceleration-app.h"'
DEFS += -DPHY_RAIL=1

OBJS += af-event.o
OBJS += af-main-common.o af-main-soc.o
OBJS += af-node.o
OBJS += af-security-common.o
OBJS += af-trust-center.o
OBJS += attribute-size.o
OBJS += attribute-storage.o
OBJS += attribute-table.o
OBJS += client-api.o
OBJS += core-cli.o
OBJS += crypto-state.o
OBJS += message.o
OBJS += multi-network.o
OBJS += network-cli.o
OBJS += option-cli.o
OBJS += plugin-cli.o
OBJS += print-formatter.o
OBJS += print.o
OBJS += process-cluster-message.o
OBJS += process-global-message.o
OBJS += security-cli.o
OBJS += service-discovery-common.o
OBJS += service-discovery-soc.o
OBJS += time-util.o
OBJS += util.o
OBJS += zcl-cli.o
OBJS += zdo-cli.o
OBJS += time-server.o
OBJS += strong-random-api.o
OBJS += simple-main.o
OBJS += sim-eeprom.o
#OBJS += uartdrv.o serial.o com.o
OBJS += my-uart.o
OBJS += scan-dispatch.o
OBJS += tunneling-server-cli.o tunneling-server.o
OBJS += tunneling-client-cli.o tunneling-client.o
OBJS += trust-center-backup-cli-posix.o trust-center-backup-cli.o trust-center-backup-posix.o trust-center-backup.o
OBJS += network-find-cli.o network-find.o
OBJS += form-and-join-afv2.o form-and-join-node-adapter.o form-and-join.o
OBJS += crypto_aes.o crypto_management.o
OBJS += key-establishment-cli.o key-establishment-curve-support.o key-establishment-storage-buffers.o key-establishment.o
OBJS += hal-config-gpio.o hal-config.o sl_mpu.o
OBJS += fragmentation-cli.o fragmentation-soc.o fragmentation.o
OBJS += em_adc.o em_cmu.o em_core.o em_cryotimer.o em_emu.o em_eusart.o em_gpio.o em_i2c.o em_ldma.o em_leuart.o
OBJS += em_msc.o em_prs.o em_rmu.o em_rtcc.o em_se.o em_system.o em_timer.o em_usart.o em_wdog.o system_efr32mg12p.o
OBJS += dmadrv.o gpiointerrupt.o sl_sleeptimer.o sl_sleeptimer_hal_rtcc.o tempdrv.o ustimer.o
OBJS += ember-printf.o
OBJS += assert-crash-handlers.o cstartup-common.o diagnostic.o ext-device.o
OBJS += faults-v7m.o isr-stubs.o mfg-token.o micro-common.o micro.o sleep-efm32.o token.o
#OBJS += debug-jtag-efr32.o SEGGER_RTT.o
OBJS += debug-jtag-stub.o
OBJS += counters-cli.o counters-ota.o counters-soc.o
OBJS += concentrator-support-cli.o concentrator-support.o
OBJS += coexistence-802154.o coulomb-counter-802154.o
OBJS += ccm-star.o
OBJS += antenna-stub.o
OBJS += address-table-cli.o address-table.o
OBJS += call-command-handler.o
OBJS += callback-stub.o
OBJS += command-interpreter2.o
OBJS += crc.o
OBJS += ember-base-configuration.o
OBJS += ember-configuration.o
OBJS += endian.o
OBJS += library.o
OBJS += mem-util.o
OBJS += random.o
OBJS += security-address-cache.o
OBJS += stack-handler-stub.o
OBJS += token-def.o
OBJS += zigbee-device-common.o
OBJS += zigbee-device-library.o
OBJS += znet-bookkeeping.o
OBJS += znet-cli.o
OBJS += sub-ghz-server.o sub-ghz-server-cli.o
OBJS += led.o


ifeq ("Hell","freezes over")

AS := iasmarm
CC := iccarm
LD := ilinkarm

DEFS += -DPLATFORM_HEADER='"platform/base/hal/micro/cortexm3/compiler/iar.h"'
override ASFLAGS += --cpu Cortex-M4 --fpu VFPv4_sp -s+ -r -L -N -p80 -t2 $(INCS) $(DEFS) -E100 -S
override CFLAGS += --no_path_in_file_macros --separate_cluster_for_initialized_variables $(INCS) --silent -e --use_c++_inline --cpu Cortex-M4 --fpu VFPv4_sp --debug
override CFLAGS += --dlib_config "C:/Program Files (x86)/IAR Systems/Embedded Workbench 7.5/arm/inc/c/DLib_Config_Normal.h" --endian little --cpu_mode thumb -Ohz --no_unroll --no_clustering $(DEFS)

LIBS += znet/protocol/zigbee/build/debug-basic-library-cortexm3-iar-efr32mg12p-railgb/debug-basic-library.a
LIBS += znet/protocol/zigbee/build/zigbee-pro-stack-cortexm3-iar-efr32mg12p-railgb/zigbee-pro-stack.a
LIBS += znet/protocol/zigbee/build/end-device-bind-library-cortexm3-iar-efr32mg12p-railgb/end-device-bind-library.a
LIBS += znet/protocol/zigbee/build/debug-extended-stub-library-cortexm3-iar-efr32mg12p-railgb/debug-extended-stub-library.a
LIBS += znet/protocol/zigbee/build/binding-table-library-cortexm3-iar-efr32mg12p-railgb/binding-table-library.a
LIBS += znet/protocol/zigbee/build/multi-network-stub-library-cortexm3-iar-efr32mg12p-railgb/multi-network-stub-library.a
LIBS += znet/protocol/zigbee/build/packet-validate-library-cortexm3-iar-efr32mg12p-railgb/packet-validate-library.a
LIBS += znet/protocol/zigbee/build/install-code-library-cortexm3-iar-efr32mg12p-railgb/install-code-library.a
LIBS += znet/protocol/zigbee/SoC-Libraries/ecc-library-283k1-efr32mg12p433f1024gm68.a
LIBS += znet/protocol/zigbee/build/cbke-library-dsa-verify-stub-cortexm3-iar-efr32mg12p-railgb/cbke-library-dsa-verify-stub.a
LIBS += znet/protocol/zigbee/build/zigbee-r22-support-library-cortexm3-iar-efr32mg12p-railgb/zigbee-r22-support-library.a
LIBS += znet/platform/radio/rail_lib/autogen/librail_release/librail_efr32xg12_iar_release.a
LIBS += znet/protocol/zigbee/build/hal-library-cortexm3-iar-efr32mg12p-railgb/hal-library.a
LIBS += znet/protocol/zigbee/build/gp-stub-library-cortexm3-iar-efr32mg12p-railgb/gp-stub-library.a
LIBS += znet/protocol/zigbee/build/cbke-163k1-library-cortexm3-iar-efr32mg12p-railgb/cbke-163k1-library.a
LIBS += znet/protocol/zigbee/build/sim-eeprom1-library-cortexm3-iar-efr32mg12p-railgb/sim-eeprom1-library.a
LIBS += znet/protocol/zigbee/build/cbke-library-dsa-verify-283k1-stub-cortexm3-iar-efr32mg12p-railgb/cbke-library-dsa-verify-283k1-stub.a
LIBS += znet/protocol/zigbee/SoC-Libraries/ecc-library-efr32mg12p433f1024gm68.a
LIBS += znet/protocol/zigbee/build/cbke-library-core-cortexm3-iar-efr32mg12p-railgb/cbke-library-core.a
LIBS += znet/protocol/zigbee/build/zll-stub-library-cortexm3-iar-efr32mg12p-railgb/zll-stub-library.a
LIBS += znet/protocol/zigbee/build/security-library-link-keys-cortexm3-iar-efr32mg12p-railgb/security-library-link-keys.a
LIBS += znet/protocol/zigbee/build/cbke-library-dsa-sign-stub-cortexm3-iar-efr32mg12p-railgb/cbke-library-dsa-sign-stub.a
LIBS += znet/protocol/zigbee/build/cbke-283k1-library-cortexm3-iar-efr32mg12p-railgb/cbke-283k1-library.a
LIBS += znet/protocol/zigbee/build/source-route-library-cortexm3-iar-efr32mg12p-railgb/source-route-library.a

LDFLAGS += --config "app.icf"
LDFLAGS += --config_def NULL_BTL=1
LDFLAGS += --config_def FLASH_SIZE=1048576
LDFLAGS += --config_def RAM_SIZE=262144
LDFLAGS += --config_def SIMEEPROM_SIZE=8192
LDFLAGS += --config_def LOCKBITS_IN_MAINFLASH_SIZE=0
LDFLAGS += --config_def EMBER_MALLOC_HEAP_SIZE=0
LDFLAGS += --config_def HEADER_SIZE=512
LDFLAGS += --config_def PSSTORE_SIZE=16384
LDFLAGS += --config_def LONGTOKEN_SIZE=0
LDFLAGS += --config_def BTL_SIZE=16384
LDFLAGS += --no_wrap_diagnostics --entry __iar_program_start --map last.map
LDFLAGS += --log_file last.log --log initialization,modules,sections,veneers
LDFLAGS += --diag_suppress=Lp012 --redirect _Printf=_PrintfSmallNoMb --redirect _Scanf=_ScanfFullNoMb  

else

AS := arm-none-eabi-gcc
CC := arm-none-eabi-gcc
LD := arm-none-eabi-gcc

DEFS += -DPLATFORM_HEADER='"platform/base/hal/micro/cortexm3/compiler/gcc.h"'
override CFLAGS += -c -mcpu=cortex-m4 -mthumb -std=gnu99 $(DEFS) $(INCS) -Os -Wall -Wno-parentheses -ffunction-sections -fdata-sections -mfpu=fpv4-sp-d16 -mfloat-abi=softfp
override ASFLAGS += $(CFLAGS) -x assembler-with-cpp

LDFLAGS += -Wl,--defsym=NULL_BTL=1
LDFLAGS += -Wl,--defsym=FLASH_SIZE=1048576
LDFLAGS += -Wl,--defsym=RAM_SIZE=262144
LDFLAGS += -Wl,--defsym=SIMEEPROM_SIZE=8192
LDFLAGS += -Wl,--defsym=LOCKBITS_IN_MAINFLASH_SIZE=0
LDFLAGS += -Wl,--defsym=EMBER_MALLOC_HEAP_SIZE=0
LDFLAGS += -Wl,--defsym=HEADER_SIZE=512
LDFLAGS += -Wl,--defsym=PSSTORE_SIZE=16384
LDFLAGS += -Wl,--defsym=LONGTOKEN_SIZE=0
LDFLAGS += -Wl,--defsym=BTL_SIZE=16384
LDFLAGS += -mcpu=cortex-m4 -mthumb -T app.ld -mfpu=fpv4-sp-d16 -mfloat-abi=softfp --specs=nano.specs -Wl,--gc-sections,--no-wchar-size-warning -Wl,-Map=last.map,--start-group

LIBS += -lc
LIBS += znet/protocol/zigbee/build/debug-basic-library-cortexm3-gcc-efr32mg12p-railgb/debug-basic-library.a
LIBS += znet/protocol/zigbee/build/zigbee-pro-stack-cortexm3-gcc-efr32mg12p-railgb/zigbee-pro-stack.a
LIBS += znet/protocol/zigbee/build/end-device-bind-library-cortexm3-gcc-efr32mg12p-railgb/end-device-bind-library.a
LIBS += znet/protocol/zigbee/build/debug-extended-stub-library-cortexm3-gcc-efr32mg12p-railgb/debug-extended-stub-library.a
LIBS += znet/protocol/zigbee/build/binding-table-library-cortexm3-gcc-efr32mg12p-railgb/binding-table-library.a
LIBS += znet/protocol/zigbee/build/multi-network-stub-library-cortexm3-gcc-efr32mg12p-railgb/multi-network-stub-library.a
LIBS += znet/protocol/zigbee/build/packet-validate-library-cortexm3-gcc-efr32mg12p-railgb/packet-validate-library.a
LIBS += znet/protocol/zigbee/build/install-code-library-cortexm3-gcc-efr32mg12p-railgb/install-code-library.a
LIBS += znet/protocol/zigbee/SoC-Libraries/ecc-library-283k1-efr32mg12p433f1024gm68.a
LIBS += znet/protocol/zigbee/build/cbke-library-dsa-verify-stub-cortexm3-gcc-efr32mg12p-railgb/cbke-library-dsa-verify-stub.a
LIBS += znet/protocol/zigbee/build/zigbee-r22-support-library-cortexm3-gcc-efr32mg12p-railgb/zigbee-r22-support-library.a
LIBS += znet/platform/radio/rail_lib/autogen/librail_release/librail_efr32xg12_gcc_release.a
LIBS += znet/protocol/zigbee/build/hal-library-cortexm3-gcc-efr32mg12p-railgb/hal-library.a
LIBS += znet/protocol/zigbee/build/gp-stub-library-cortexm3-gcc-efr32mg12p-railgb/gp-stub-library.a
LIBS += znet/protocol/zigbee/build/cbke-163k1-library-cortexm3-gcc-efr32mg12p-railgb/cbke-163k1-library.a
LIBS += znet/protocol/zigbee/build/sim-eeprom1-library-cortexm3-gcc-efr32mg12p-railgb/sim-eeprom1-library.a
LIBS += znet/protocol/zigbee/build/cbke-library-dsa-verify-283k1-stub-cortexm3-gcc-efr32mg12p-railgb/cbke-library-dsa-verify-283k1-stub.a
LIBS += znet/protocol/zigbee/SoC-Libraries/ecc-library-efr32mg12p433f1024gm68.a
LIBS += znet/protocol/zigbee/build/cbke-library-core-cortexm3-gcc-efr32mg12p-railgb/cbke-library-core.a
LIBS += znet/protocol/zigbee/build/zll-stub-library-cortexm3-gcc-efr32mg12p-railgb/zll-stub-library.a
LIBS += znet/protocol/zigbee/build/security-library-link-keys-cortexm3-gcc-efr32mg12p-railgb/security-library-link-keys.a
LIBS += znet/protocol/zigbee/build/cbke-library-dsa-sign-stub-cortexm3-gcc-efr32mg12p-railgb/cbke-library-dsa-sign-stub.a
LIBS += znet/protocol/zigbee/build/cbke-283k1-library-cortexm3-gcc-efr32mg12p-railgb/cbke-283k1-library.a
LIBS += znet/protocol/zigbee/build/source-route-library-cortexm3-gcc-efr32mg12p-railgb/source-route-library.a
LIBS += znet/protocol/zigbee/build/mfglib-library-cortexm3-gcc-efr32mg12p-railgb/mfglib-library.a
LIBS += -lm -Wl,--end-group -Wl,--start-group -lgcc -lc -lnosys -Wl,--end-group

endif


VPATH = carb
VPATH += znet/platform/base/hal
VPATH += znet/platform/base/hal/micro/cortexm3/efm32
VPATH += znet/platform/base/hal/micro/generic
VPATH += znet/platform/base/hal/plugin/antenna-stub
VPATH += znet/platform/base/hal/plugin/debug-jtag
VPATH += znet/platform/base/hal/plugin/debug-jtag-stub
VPATH += znet/platform/base/hal/plugin/serial
VPATH += znet/platform/base/hal/plugin/serial/cortexm/efm32
VPATH += znet/platform/base/hal/plugin/sim-eeprom1
VPATH += znet/platform/Device/SiliconLabs/EFR32MG12P/Source
VPATH += znet/platform/emdrv/dmadrv/src
VPATH += znet/platform/emdrv/gpiointerrupt/src
VPATH += znet/platform/emdrv/tempdrv/src
VPATH += znet/platform/emdrv/uartdrv/src
VPATH += znet/platform/emdrv/ustimer/src
VPATH += znet/platform/emlib/src
VPATH += znet/platform/radio/rail_lib/plugin/coexistence/protocol/ieee802154
VPATH += znet/platform/service/mpu/src
VPATH += znet/platform/service/sleeptimer/src
VPATH += znet/protocol/zigbee/app/framework/cli
VPATH += znet/protocol/zigbee/app/framework/plugin/address-table
VPATH += znet/protocol/zigbee/app/framework/plugin/concentrator
VPATH += znet/protocol/zigbee/app/framework/plugin/counters
VPATH += znet/protocol/zigbee/app/framework/plugin/fragmentation
VPATH += znet/protocol/zigbee/app/framework/plugin/form-and-join
VPATH += znet/protocol/zigbee/app/framework/plugin/key-establishment
VPATH += znet/protocol/zigbee/app/framework/plugin/network-find
VPATH += znet/protocol/zigbee/app/framework/plugin/scan-dispatch
VPATH += znet/protocol/zigbee/app/framework/plugin/simple-clock
VPATH += znet/protocol/zigbee/app/framework/plugin/simple-main
VPATH += znet/protocol/zigbee/app/framework/plugin/sub-ghz-server
VPATH += znet/protocol/zigbee/app/framework/plugin/time-server
VPATH += znet/protocol/zigbee/app/framework/plugin/trust-center-backup
VPATH += znet/protocol/zigbee/app/framework/plugin/tunneling-client
VPATH += znet/protocol/zigbee/app/framework/plugin/tunneling-server
VPATH += znet/protocol/zigbee/app/framework/plugin/update-tc-link-key
VPATH += znet/protocol/zigbee/app/framework/security
VPATH += znet/protocol/zigbee/app/framework/util
VPATH += znet/protocol/zigbee/app/util/common
VPATH += znet/protocol/zigbee/app/util/serial
VPATH += znet/protocol/zigbee/app/util/security
VPATH += znet/protocol/zigbee/app/util/zigbee-framework
VPATH += znet/protocol/zigbee/stack/config
VPATH += znet/protocol/zigbee/stack/framework
VPATH += znet/util/third_party/mbedtls/sl_crypto/src
VPATH += znet/util/third_party/segger/systemview/SEGGER


# common

%.hex: %.elf
	@echo "OBJ-HEX $@"; arm-none-eabi-objcopy -O ihex $^ $@
%.bin: %.elf
	@echo "OBJ-BIN $@"; arm-none-eabi-objcopy -O binary $^ $@
%.dis: %.elf
	@arm-none-eabi-objdump -D $^ > $@

%.o: %.c
	@echo "CC $@"; $(CC) $(CFLAGS) $< -o $@
%.o: %.s79
	@echo "AS $@"; $(AS) $(ASFLAGS) $< -o $@


# applications

dbch.elf: $(OBJS) malloc.o chf.o time.o meter.o price.o calendar.o fakey.o doap.o mtr-doap.o ota.o spi-fup.o my-store.o
	@echo "LD $@"; $(LD) -o $@ $(LDFLAGS) $^ $(LIBS)

sniffer.elf: $(OBJS) sniffer.o time.o malloc.o doap.o
	@echo "LD $@"; $(LD) -o $@ $(LDFLAGS) $^ $(LIBS)


# other

wipe-simee.hex:
	srec -h -F8192 -ahFA000 > $@

wipe-nv.hex:
	srec -h -F16384 -ahFC000 > $@

prog-%: %.hex
	commander flash -d EFR32MG12P433F1024GM48 $^

#eui-%: tokens-%
#	commander flash -d EFR32MG12P433F1024GM48 --tokengroup znet --tokenfile $^

jlink-%: %.hex
	echo "loadfile $<\nr\ng\nq" > tmp.jlink
	JLinkExe -device EFR32MG12PxxxF1024 -if SWD -speed 4000 -autoconnect 1 -CommanderScript tmp.jlink
	rm tmp.jlink
jlink: jlink-dbch

jdebug:
	JLinkExe -device EFR32MG12PxxxF1024 -if SWD -speed 4000 -autoconnect 1

clean:
	rm -f *.o *.map *.elf *.hex *.bin *.dis *.log *.lst
