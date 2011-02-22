cmd_drivers/input/misc/gpio_axis.o := /root/adam/prebuilt/linux-x86/toolchain/arm-eabi-4.4.0/bin/arm-eabi-gcc -Wp,-MD,drivers/input/misc/.gpio_axis.o.d  -nostdinc -isystem /adam/prebuilt/linux-x86/toolchain/arm-eabi-4.4.0/bin/../lib/gcc/arm-eabi/4.4.0/include -Iinclude  -I/root/streak/3/kern.bak/arch/arm/include -include include/linux/autoconf.h -D__KERNEL__ -mlittle-endian -Iarch/arm/mach-msm/include -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -Werror-implicit-function-declaration -Wno-format-security -fno-delete-null-pointer-checks -Os -marm -fno-omit-frame-pointer -mapcs -mno-sched-prolog -mabi=aapcs-linux -mno-thumb-interwork -D__LINUX_ARM_ARCH__=7 -march=armv7-a -msoft-float -Uarm -Wframe-larger-than=3072 -fno-stack-protector -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wdeclaration-after-statement -Wno-pointer-sign -fno-strict-overflow -fno-dwarf2-cfi-asm -fconserve-stack   -D"KBUILD_STR(s)=\#s" -D"KBUILD_BASENAME=KBUILD_STR(gpio_axis)"  -D"KBUILD_MODNAME=KBUILD_STR(gpio_axis)"  -c -o drivers/input/misc/gpio_axis.o drivers/input/misc/gpio_axis.c

deps_drivers/input/misc/gpio_axis.o := \
  drivers/input/misc/gpio_axis.c \

drivers/input/misc/gpio_axis.o: $(deps_drivers/input/misc/gpio_axis.o)

$(deps_drivers/input/misc/gpio_axis.o):
