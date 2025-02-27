/*
 * Copyright (c) 2018-2020, Andreas Kling <kling@serenityos.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <AK/Assertions.h>
#include <AK/StringView.h>
#include <AK/Types.h>
#include <Kernel/Arch/i386/CPU.h>
#include <Kernel/IO.h>
#include <Kernel/Interrupts/APIC.h>
#include <Kernel/Interrupts/SpuriousInterruptHandler.h>
#include <Kernel/VM/MemoryManager.h>
#include <Kernel/VM/TypedMapping.h>

#define IRQ_APIC_SPURIOUS 0x7f

#define APIC_BASE_MSR 0x1b

#define APIC_REG_EOI 0xb0
#define APIC_REG_LD 0xd0
#define APIC_REG_DF 0xe0
#define APIC_REG_SIV 0xf0
#define APIC_REG_TPR 0x80
#define APIC_REG_ICR_LOW 0x300
#define APIC_REG_ICR_HIGH 0x310
#define APIC_REG_LVT_TIMER 0x320
#define APIC_REG_LVT_THERMAL 0x330
#define APIC_REG_LVT_PERFORMANCE_COUNTER 0x340
#define APIC_REG_LVT_LINT0 0x350
#define APIC_REG_LVT_LINT1 0x360
#define APIC_REG_LVT_ERR 0x370

namespace Kernel {

namespace APIC {

class ICRReg {
    u32 m_reg { 0 };

public:
    enum DeliveryMode {
        Fixed = 0x0,
        LowPriority = 0x1,
        SMI = 0x2,
        NMI = 0x4,
        INIT = 0x5,
        StartUp = 0x6,
    };
    enum DestinationMode {
        Physical = 0x0,
        Logical = 0x0,
    };
    enum Level {
        DeAssert = 0x0,
        Assert = 0x1
    };
    enum class TriggerMode {
        Edge = 0x0,
        Level = 0x1,
    };
    enum DestinationShorthand {
        NoShorthand = 0x0,
        Self = 0x1,
        AllIncludingSelf = 0x2,
        AllExcludingSelf = 0x3,
    };

    ICRReg(u8 vector, DeliveryMode delivery_mode, DestinationMode destination_mode, Level level, TriggerMode trigger_mode, DestinationShorthand destination)
        : m_reg(vector | (delivery_mode << 8) | (destination_mode << 11) | (level << 14) | (static_cast<u32>(trigger_mode) << 15) | (destination << 18))
    {
    }

    u32 low() const { return m_reg; }
    u32 high() const { return 0; }
};

static PhysicalAddress g_apic_base;

static PhysicalAddress get_base()
{
    u32 lo, hi;
    MSR msr(APIC_BASE_MSR);
    msr.get(lo, hi);
    return PhysicalAddress(lo & 0xfffff000);
}

static void set_base(const PhysicalAddress& base)
{
    u32 hi = 0;
    u32 lo = base.get() | 0x800;
    MSR msr(APIC_BASE_MSR);
    msr.set(lo, hi);
}

static void write_register(u32 offset, u32 value)
{
    *map_typed_writable<u32>(g_apic_base.offset(offset)) = value;
}

static u32 read_register(u32 offset)
{
    return *map_typed<u32>(g_apic_base.offset(offset));
}

static void write_icr(const ICRReg& icr)
{
    write_register(APIC_REG_ICR_HIGH, icr.high());
    write_register(APIC_REG_ICR_LOW, icr.low());
}

#define APIC_LVT_MASKED (1 << 16)
#define APIC_LVT_TRIGGER_LEVEL (1 << 14)
#define APIC_LVT(iv, dm) ((iv & 0xff) | ((dm & 0x7) << 8))

asm(
    ".globl apic_ap_start \n"
    ".type apic_ap_start, @function \n"
    "apic_ap_start: \n"
    ".set begin_apic_ap_start, . \n"
    "    jmp apic_ap_start\n" // TODO: implement
    ".set end_apic_ap_start, . \n"
    "\n"
    ".globl apic_ap_start_size \n"
    "apic_ap_start_size: \n"
    ".word end_apic_ap_start - begin_apic_ap_start \n");

extern "C" void apic_ap_start(void);
extern "C" u16 apic_ap_start_size;

void eoi()
{
    write_register(APIC_REG_EOI, 0x0);
}

u8 spurious_interrupt_vector()
{
    return IRQ_APIC_SPURIOUS;
}

bool init()
{
    // FIXME: Use the ACPI MADT table
    if (!MSR::have())
        return false;

    // check if we support local apic
    CPUID id(1);
    if ((id.edx() & (1 << 9)) == 0)
        return false;

    PhysicalAddress apic_base = get_base();
    klog() << "Initializing APIC, base: " << apic_base;
    set_base(apic_base);

    g_apic_base = apic_base;

    return true;
}

void enable_bsp()
{
    // FIXME: Ensure this method can only be executed by the BSP.
    enable(0);
}

void enable(u32 cpu)
{
    klog() << "Enabling local APIC for cpu #" << cpu;

    // dummy read, apparently to avoid a bug in old CPUs.
    read_register(APIC_REG_SIV);
    // set spurious interrupt vector
    write_register(APIC_REG_SIV, (IRQ_APIC_SPURIOUS + IRQ_VECTOR_BASE) | 0x100);

    // local destination mode (flat mode)
    write_register(APIC_REG_DF, 0xf0000000);

    // set destination id (note that this limits it to 8 cpus)
    write_register(APIC_REG_LD, 0);

    SpuriousInterruptHandler::initialize(IRQ_APIC_SPURIOUS);

    write_register(APIC_REG_LVT_TIMER, APIC_LVT(0, 0) | APIC_LVT_MASKED);
    write_register(APIC_REG_LVT_THERMAL, APIC_LVT(0, 0) | APIC_LVT_MASKED);
    write_register(APIC_REG_LVT_PERFORMANCE_COUNTER, APIC_LVT(0, 0) | APIC_LVT_MASKED);
    write_register(APIC_REG_LVT_LINT0, APIC_LVT(0, 7) | APIC_LVT_MASKED);
    write_register(APIC_REG_LVT_LINT1, APIC_LVT(0, 0) | APIC_LVT_TRIGGER_LEVEL);
    write_register(APIC_REG_LVT_ERR, APIC_LVT(0, 0) | APIC_LVT_MASKED);

    write_register(APIC_REG_TPR, 0);

    if (cpu != 0) {
        // INIT
        write_icr(ICRReg(0, ICRReg::INIT, ICRReg::Physical, ICRReg::Assert, ICRReg::TriggerMode::Edge, ICRReg::AllExcludingSelf));

        IO::delay(10 * 1000);

        for (int i = 0; i < 2; i++) {
            // SIPI
            write_icr(ICRReg(0x08, ICRReg::StartUp, ICRReg::Physical, ICRReg::Assert, ICRReg::TriggerMode::Edge, ICRReg::AllExcludingSelf)); // start execution at P8000

            IO::delay(200);
        }
    }
}

}

}
