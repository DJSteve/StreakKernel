cmd_arch/arm/boot/compressed/vmlinux := /root/adam/prebuilt/linux-x86/toolchain/arm-eabi-4.4.0/bin/arm-eabi-ld -EL    --defsym zreladdr=0x20008000 --defsym initrd_phys=0x24000000 --defsym params_phys=0x20000100 -p --no-undefined -X /adam/prebuilt/linux-x86/toolchain/arm-eabi-4.4.0/bin/../lib/gcc/arm-eabi/4.4.0/libgcc.a -T arch/arm/boot/compressed/vmlinux.lds arch/arm/boot/compressed/head.o arch/arm/boot/compressed/piggy.o arch/arm/boot/compressed/misc.o -o arch/arm/boot/compressed/vmlinux 
