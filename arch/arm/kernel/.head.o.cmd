cmd_arch/arm/kernel/head.o := /home/jkbuha/gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabi/bin/arm-linux-gnueabi-gcc -Wp,-MD,arch/arm/kernel/.head.o.d  -nostdinc -isystem /home/jkbuha/gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabi/bin/../lib/gcc/arm-linux-gnueabi/4.9.4/include -I/home/jkbuha/src/FiiO_Kernel_Android/arch/arm/include -Iarch/arm/include/generated -Iinclude  -include include/generated/autoconf.h -D__KERNEL__ -mlittle-endian -Iarch/arm/mach-rk3188/include -Iarch/arm/plat-rk/include -D__ASSEMBLY__ -mabi=aapcs-linux -mno-thumb-interwork -funwind-tables  -D__LINUX_ARM_ARCH__=7 -march=armv7-a  -include asm/unified.h -msoft-float     -DTEXT_OFFSET=0x00408000   -c -o arch/arm/kernel/head.o arch/arm/kernel/head.S

source_arch/arm/kernel/head.o := arch/arm/kernel/head.S

deps_arch/arm/kernel/head.o := \
    $(wildcard include/config/debug/ll.h) \
    $(wildcard include/config/plat/rk.h) \
    $(wildcard include/config/xip/kernel.h) \
    $(wildcard include/config/xip/phys/addr.h) \
    $(wildcard include/config/smp/on/up.h) \
    $(wildcard include/config/arm/patch/phys/virt.h) \
    $(wildcard include/config/debug/icedcc.h) \
    $(wildcard include/config/arch/netwinder.h) \
    $(wildcard include/config/arch/cats.h) \
    $(wildcard include/config/arch/rpc.h) \
    $(wildcard include/config/smp.h) \
    $(wildcard include/config/alignment/trap.h) \
    $(wildcard include/config/cpu/dcache/disable.h) \
    $(wildcard include/config/cpu/bpredict/disable.h) \
    $(wildcard include/config/cpu/icache/disable.h) \
    $(wildcard include/config/arm/patch/phys/virt/16bit.h) \
    $(wildcard include/config/thumb2/kernel.h) \
  /home/jkbuha/src/FiiO_Kernel_Android/arch/arm/include/asm/unified.h \
    $(wildcard include/config/arm/asm/unified.h) \
  include/linux/linkage.h \
  include/linux/compiler.h \
    $(wildcard include/config/sparse/rcu/pointer.h) \
    $(wildcard include/config/trace/branch/profiling.h) \
    $(wildcard include/config/profile/all/branches.h) \
    $(wildcard include/config/enable/must/check.h) \
    $(wildcard include/config/enable/warn/deprecated.h) \
  /home/jkbuha/src/FiiO_Kernel_Android/arch/arm/include/asm/linkage.h \
  include/linux/init.h \
    $(wildcard include/config/modules.h) \
    $(wildcard include/config/hotplug.h) \
  /home/jkbuha/src/FiiO_Kernel_Android/arch/arm/include/asm/assembler.h \
    $(wildcard include/config/cpu/feroceon.h) \
    $(wildcard include/config/trace/irqflags.h) \
    $(wildcard include/config/rk/check/uaccess.h) \
    $(wildcard include/config/cpu/use/domains.h) \
  /home/jkbuha/src/FiiO_Kernel_Android/arch/arm/include/asm/ptrace.h \
    $(wildcard include/config/cpu/endian/be8.h) \
    $(wildcard include/config/arm/thumb.h) \
  /home/jkbuha/src/FiiO_Kernel_Android/arch/arm/include/asm/hwcap.h \
  /home/jkbuha/src/FiiO_Kernel_Android/arch/arm/include/asm/domain.h \
    $(wildcard include/config/io/36.h) \
  /home/jkbuha/src/FiiO_Kernel_Android/arch/arm/include/asm/asm-offsets.h \
  include/generated/asm-offsets.h \
  /home/jkbuha/src/FiiO_Kernel_Android/arch/arm/include/asm/memory.h \
    $(wildcard include/config/mmu.h) \
    $(wildcard include/config/page/offset.h) \
    $(wildcard include/config/highmem.h) \
    $(wildcard include/config/dram/size.h) \
    $(wildcard include/config/dram/base.h) \
    $(wildcard include/config/have/tcm.h) \
  include/linux/const.h \
  include/linux/types.h \
    $(wildcard include/config/uid16.h) \
    $(wildcard include/config/lbdaf.h) \
    $(wildcard include/config/arch/dma/addr/t/64bit.h) \
    $(wildcard include/config/phys/addr/t/64bit.h) \
    $(wildcard include/config/64bit.h) \
  /home/jkbuha/src/FiiO_Kernel_Android/arch/arm/include/asm/types.h \
  include/asm-generic/int-ll64.h \
  /home/jkbuha/src/FiiO_Kernel_Android/arch/arm/include/asm/bitsperlong.h \
  include/asm-generic/bitsperlong.h \
  arch/arm/mach-rk3188/include/mach/memory.h \
  arch/arm/plat-rk/include/plat/memory.h \
  include/linux/version.h \
  arch/arm/mach-rk3188/include/mach/io.h \
  arch/arm/plat-rk/include/plat/io.h \
  /home/jkbuha/src/FiiO_Kernel_Android/arch/arm/include/asm/sizes.h \
  include/asm-generic/sizes.h \
  include/asm-generic/memory_model.h \
    $(wildcard include/config/flatmem.h) \
    $(wildcard include/config/discontigmem.h) \
    $(wildcard include/config/sparsemem/vmemmap.h) \
    $(wildcard include/config/sparsemem.h) \
  /home/jkbuha/src/FiiO_Kernel_Android/arch/arm/include/asm/thread_info.h \
    $(wildcard include/config/arm/thumbee.h) \
  /home/jkbuha/src/FiiO_Kernel_Android/arch/arm/include/asm/fpstate.h \
    $(wildcard include/config/vfpv3.h) \
    $(wildcard include/config/iwmmxt.h) \
  /home/jkbuha/src/FiiO_Kernel_Android/arch/arm/include/asm/system.h \
    $(wildcard include/config/function/graph/tracer.h) \
    $(wildcard include/config/cpu/32v6k.h) \
    $(wildcard include/config/cpu/xsc3.h) \
    $(wildcard include/config/cpu/fa526.h) \
    $(wildcard include/config/arch/has/barriers.h) \
    $(wildcard include/config/arm/dma/mem/bufferable.h) \
    $(wildcard include/config/cpu/sa1100.h) \
    $(wildcard include/config/cpu/sa110.h) \
    $(wildcard include/config/cpu/v6.h) \
  arch/arm/kernel/head-common.S \
    $(wildcard include/config/cpu/big/endian.h) \
    $(wildcard include/config/of/flattree.h) \

arch/arm/kernel/head.o: $(deps_arch/arm/kernel/head.o)

$(deps_arch/arm/kernel/head.o):
