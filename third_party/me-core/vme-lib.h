/*
 * This code is custom, original work and is not derived from reverse
 * engineering. It is an implementation based on the author's understanding
 * of the hardware at the time this code was written.
 *
 * Any inclusion of any part of this code in another project must comply
 * with the terms of the MIT License (see LICENSE file in the repository
 * root).
 *
 * Copyright (c) 2026 mcidclan (m-c/d, m-cid)
 */
 
#ifndef ME_VME_LIB_H
#define ME_VME_LIB_H

#define MECC_VME_LIB_VERSION      0xBe7a0005

#include "hw-registers.h"
#include "vme-opcodes.h"

#define vmeLibRefresh vmeLibFinish

#define VME_SCRATCHPAD_BASE       0x44000000

#define VME_BASE_BUFFERS          0x44000000
#define VME_TOP_BUFFERS           0x44020000
#define VME_DATAPATH_BASE         0x440f8000

#define VME_TOP_BUFFER_0 (VME_TOP_BUFFERS + 0x2000*0)
#define VME_TOP_BUFFER_1 (VME_TOP_BUFFERS + 0x2000*1)
#define VME_TOP_BUFFER_2 (VME_TOP_BUFFERS + 0x2000*2)
#define VME_TOP_BUFFER_3 (VME_TOP_BUFFERS + 0x2000*3)

#define VME_BASE_BUFFER_0 (VME_BASE_BUFFERS + 0x2000*0)
#define VME_BASE_BUFFER_1 (VME_BASE_BUFFERS + 0x2000*1)
#define VME_BASE_BUFFER_2 (VME_BASE_BUFFERS + 0x2000*2)
#define VME_BASE_BUFFER_3 (VME_BASE_BUFFERS + 0x2000*3)

#define VME_TOP_BUFF0_WOFF (0x8000 + 0x800*0)
#define VME_TOP_BUFF1_WOFF (0x8000 + 0x800*1)
#define VME_TOP_BUFF2_WOFF (0x8000 + 0x800*2)
#define VME_TOP_BUFF3_WOFF (0x8000 + 0x800*3)

#define VME_BASE_BUFF0_WOFF (0x800*0)
#define VME_BASE_BUFF1_WOFF (0x800*1)
#define VME_BASE_BUFF2_WOFF (0x800*2)
#define VME_BASE_BUFF3_WOFF (0x800*3)


#define VME_DEF_MODE              0x84000000
#define agu_mode(mode)           (0x80000000 | ((mode) << 24))

#define VME_DEF_STEP              0x00010000
#define VME_DEF_MAPPER            0x00003210
#define VME_END_TOKEN             0x00200000
#define VME_RING_TOKEN            0x00020000

#define VME_CYCLE_3               0x00030000
#define VME_CYCLE_6               0x00060000
#define VME_CYCLE_9               0x00090000
#define VME_CYCLE_12              0x000c0000

#define vme_cyc(n)  ((n) << 16)
#define agu_step(n) ((n) << 16)
#define fu_code(n)  ((n) << 12)
#define fu_sat(n)   ((n) << 7)
#define fu_zsm(n)   ((n) << 6)
#define fu_on(n)    ((n) << 28)

// co-operand data source selectors
#define VME_MUX_FRONT_NONE        0x00000000

#define VME_MUX_FRONT_TOP_0       0x00000000
#define VME_MUX_FRONT_TOP_1       0x10000000
#define VME_MUX_FRONT_TOP_2       0x20000000
#define VME_MUX_FRONT_TOP_3       0x30000000
#define VME_MUX_FRONT_BASE_0      0x40000000
#define VME_MUX_FRONT_BASE_1      0x50000000
#define VME_MUX_FRONT_BASE_2      0x60000000
#define VME_MUX_FRONT_BASE_3      0x70000000
#define VME_MUX_FRONT_STAGING     0x80000000

#define VME_MUX_FRONT_STAGING_0   0x80000000
#define VME_MUX_FRONT_STAGING_1   0x88000000
#define VME_MUX_FRONT_STAGING_2   0x90000000
#define VME_MUX_FRONT_STAGING_3   0x98000000

#define VME_MUX_FRONT_STAGING_4   0xa0000000
#define VME_MUX_FRONT_STAGING_5   0xa8000000
#define VME_MUX_FRONT_STAGING_6   0xb0000000
#define VME_MUX_FRONT_STAGING_7   0xb8000000

#define VME_MUX_BACK_TOP_0        0x00000000
#define VME_MUX_BACK_TOP_1        0x00800000
#define VME_MUX_BACK_TOP_2        0x01000000
#define VME_MUX_BACK_TOP_3        0x01800000
#define VME_MUX_BACK_BASE_0       0x02000000
#define VME_MUX_BACK_BASE_1       0x02800000
#define VME_MUX_BACK_BASE_2       0x03000000
#define VME_MUX_BACK_BASE_3       0x03800000
#define VME_MUX_BACK_STAGING      0x04000000

#define VME_MUX_BACK_STAGING_0    0x04000000
#define VME_MUX_BACK_STAGING_1    0x04400000
#define VME_MUX_BACK_STAGING_2    0x04800000
#define VME_MUX_BACK_STAGING_3    0x04c00000

#define VME_MUX_BACK_STAGING_4    0x05000000
#define VME_MUX_BACK_STAGING_5    0x05400000
#define VME_MUX_BACK_STAGING_6    0x05800000
#define VME_MUX_BACK_STAGING_7    0x05c00000


#define VME_BASE(index) (hw(VME_BASE_ADDR + (index * 4)))

// PE functional unit 0 descriptors, describing a local MUX, the ALU/Logic/MAC, a Saturator and Shifter.
#define VME_PE_0_FUNCTIONAL_UNIT_PRIMARY       0   // 0x000 // VME_PE_0_TOP_DESCRIPTOR => VME_PE_0_FUNCTIONAL_UNIT_0
#define VME_PE_1_FUNCTIONAL_UNIT_PRIMARY       1   // 0x004
#define VME_PE_2_FUNCTIONAL_UNIT_PRIMARY       2   // 0x008
#define VME_PE_3_FUNCTIONAL_UNIT_PRIMARY       3   // 0x00c

// PE functional unit 1 descriptors, describing a local MUX, the ALU/Logic/MAC, a Saturator and Shifter.
#define VME_PE_0_FUNCTIONAL_UNIT_SECONDARY     4   // 0x010 // VME_PE_0_BASE_DESCRIPTOR => VME_PE_0_FUNCTIONAL_UNIT_1
#define VME_PE_1_FUNCTIONAL_UNIT_SECONDARY     5   // 0x014
#define VME_PE_2_FUNCTIONAL_UNIT_SECONDARY     6   // 0x018
#define VME_PE_3_FUNCTIONAL_UNIT_SECONDARY     7   // 0x01c

// PE FU0 Operation registers/operands
#define VME_PE_0_FU_PRIMARY_REGISTER_A         8   // 0x020 // VME_PE_0_TOP_REGISTER_A => VME_PE_0_FU_0_REGISTER_A, RA
#define VME_PE_0_FU_PRIMARY_REGISTER_B         9   // 0x024 // VME_PE_0_TOP_REGISTER_B => VME_PE_0_FU_0_REGISTER_B, RB
#define VME_PE_1_FU_PRIMARY_REGISTER_A         10  // 0x028
#define VME_PE_1_FU_PRIMARY_REGISTER_B         11  // 0x02c
#define VME_PE_2_FU_PRIMARY_REGISTER_A         12  // 0x030
#define VME_PE_2_FU_PRIMARY_REGISTER_B         13  // 0x034
#define VME_PE_3_FU_PRIMARY_REGISTER_A         14  // 0x038
#define VME_PE_3_FU_PRIMARY_REGISTER_B         15  // 0x03c

// PE FU1 Operation registers/operands
#define VME_PE_0_FU_SECONDARY_REGISTER_A       16  // 0x040
#define VME_PE_0_FU_SECONDARY_REGISTER_B       17  // 0x044
#define VME_PE_1_FU_SECONDARY_REGISTER_A       18  // 0x048
#define VME_PE_1_FU_SECONDARY_REGISTER_B       19  // 0x04c
#define VME_PE_2_FU_SECONDARY_REGISTER_A       20  // 0x050
#define VME_PE_2_FU_SECONDARY_REGISTER_B       21  // 0x054
#define VME_PE_3_FU_SECONDARY_REGISTER_A       22  // 0x058
#define VME_PE_3_FU_SECONDARY_REGISTER_B       23  // 0x05c

// modifiers? (unclear)
#define VME_MODIFIER_A          24  // 0x060
#define VME_MODIFIER_B          25  // 0x064
#define VME_MODIFIER_C          26  // 0x068

// functional units 1 register hosting enable bits (not fully clear)
#define VME_ENABLE_FU_1         27  // 0x06c

// global interconnect
#define VME_INTERCONNECT_SWEN       27 // secondary write enable
#define VME_INTERCONNECT_AGU_TOP    28 
#define VME_INTERCONNECT_AGU_BASE   29
#define VME_INTERCONNECT_AGU_WRITE  30

#define VME_INTERCONNECT_INPUT  28  // 0x070 // input paths, mapping source buffers to PE
#define VME_INTERCONNECT_FLOW   29  // 0x074 // inter-PE routing register. Each nibble selects the source flow PE index for a target PE lane.
#define VME_INTERCONNECT_ARCH   30  // 0x078 // inter-PE routing register. Each nibble selects the source arch PE index for a target PE lane.
#define VME_INTERCONNECT_UNK    31  // 0x07c // unknown
#define VME_INTERCONNECT_XPCR   31  // 0x07c // cross pack control register (read unpack, write pack)
#define VME_INTERCONNECT_SKEW   32  // 0x080 // applies a cycle skew to the data stream forwarded to the target PE.


/*
 * PE0 AGUs
 * */
 
// PE0 AGU Read Top
#define VME_PE_0_READ_TOP_MODE         33  // 0x084
#define VME_PE_0_READ_TOP_COUNT        34  // 0x088
#define VME_PE_0_READ_TOP_INNER_0      35  // 0x08c
#define VME_PE_0_READ_TOP_INNER_1      36  // 0x090
#define VME_PE_0_READ_TOP_FORMAT_0     37  // 0x094
#define VME_PE_0_READ_TOP_FORMAT_1     38  // 0x098

// PE0 AGU Read Base
#define VME_PE_0_READ_BASE_MODE        39  // 0x09c
#define VME_PE_0_READ_BASE_COUNT       40  // 0x0a0
#define VME_PE_0_READ_BASE_INNER_0     41  // 0x0a4
#define VME_PE_0_READ_BASE_INNER_1     42  // 0x0a8
#define VME_PE_0_READ_BASE_FORMAT_0    43  // 0x0ac
#define VME_PE_0_READ_BASE_FORMAT_1    44  // 0x0b0

// PE0 AGU Write
#define VME_PE_0_WRITE_MODE            45  // 0x0b4
#define VME_PE_0_WRITE_COUNT           46  // 0x0b8
#define VME_PE_0_WRITE_INNER_0         47  // 0x0bc
#define VME_PE_0_WRITE_INNER_1         48  // 0x0c0
#define VME_PE_0_WRITE_FORMAT_0        49  // 0x0c4
#define VME_PE_0_WRITE_FORMAT_1        50  // 0x0c8


/*
 * PE1 AGUs
 * */
 
// PE1 AGU Read Top
#define VME_PE_1_READ_TOP_MODE         51  // 0x0cc
#define VME_PE_1_READ_TOP_COUNT        52  // 0x0d0
#define VME_PE_1_READ_TOP_INNER_0      53  // 0x0d4
#define VME_PE_1_READ_TOP_INNER_1      54  // 0x0d8
#define VME_PE_1_READ_TOP_FORMAT_0     55  // 0x0dc
#define VME_PE_1_READ_TOP_FORMAT_1     56  // 0x0e0

// PE1 AGU Read Base
#define VME_PE_1_READ_BASE_MODE        57  // 0x0e4
#define VME_PE_1_READ_BASE_COUNT       58  // 0x0e8
#define VME_PE_1_READ_BASE_INNER_0     59  // 0x0ec
#define VME_PE_1_READ_BASE_INNER_1     60  // 0x0f0
#define VME_PE_1_READ_BASE_FORMAT_0    61  // 0x0f4
#define VME_PE_1_READ_BASE_FORMAT_1    62  // 0x0f8

// PE1 AGU Write
#define VME_PE_1_WRITE_MODE            63  // 0x0fc
#define VME_PE_1_WRITE_COUNT           64  // 0x100
#define VME_PE_1_WRITE_INNER_0         65  // 0x104
#define VME_PE_1_WRITE_INNER_1         66  // 0x108
#define VME_PE_1_WRITE_FORMAT_0        67  // 0x10c
#define VME_PE_1_WRITE_FORMAT_1        68  // 0x110


/*
 * PE2 AGUs
 * */
 
// PE2 AGU Read Top
#define VME_PE_2_READ_TOP_MODE         69  // 0x114
#define VME_PE_2_READ_TOP_COUNT        70  // 0x118
#define VME_PE_2_READ_TOP_INNER_0      71  // 0x11c
#define VME_PE_2_READ_TOP_INNER_1      72  // 0x120
#define VME_PE_2_READ_TOP_FORMAT_0     73  // 0x124
#define VME_PE_2_READ_TOP_FORMAT_1     74  // 0x128

// PE2 AGU Read Base
#define VME_PE_2_READ_BASE_MODE        75  // 0x12c
#define VME_PE_2_READ_BASE_COUNT       76  // 0x130
#define VME_PE_2_READ_BASE_INNER_0     77  // 0x134
#define VME_PE_2_READ_BASE_INNER_1     78  // 0x138
#define VME_PE_2_READ_BASE_FORMAT_0    79  // 0x13c
#define VME_PE_2_READ_BASE_FORMAT_1    80  // 0x140

// PE2 AGU Write
#define VME_PE_2_WRITE_MODE            81  // 0x144
#define VME_PE_2_WRITE_COUNT           82  // 0x148
#define VME_PE_2_WRITE_INNER_0         83  // 0x14c
#define VME_PE_2_WRITE_INNER_1         84  // 0x150
#define VME_PE_2_WRITE_FORMAT_0        85  // 0x154
#define VME_PE_2_WRITE_FORMAT_1        86  // 0x158

/*
 * PE2 AGUs
 * */
 
// PE3 AGU Read Top
#define VME_PE_3_READ_TOP_MODE         87  // 0x15c
#define VME_PE_3_READ_TOP_COUNT        88  // 0x160
#define VME_PE_3_READ_TOP_INNER_0      89  // 0x164
#define VME_PE_3_READ_TOP_INNER_1      90  // 0x168
#define VME_PE_3_READ_TOP_FORMAT_0     91  // 0x16c
#define VME_PE_3_READ_TOP_FORMAT_1     92  // 0x170

// base buffer source
#define VME_PE_3_READ_BASE_MODE        93  // 0x174
#define VME_PE_3_READ_BASE_COUNT       94  // 0x178
#define VME_PE_3_READ_BASE_INNER_0     95  // 0x17c
#define VME_PE_3_READ_BASE_INNER_1     96  // 0x180
#define VME_PE_3_READ_BASE_FORMAT_0    97  // 0x184
#define VME_PE_3_READ_BASE_FORMAT_1    98  // 0x188

// destination
#define VME_PE_3_WRITE_MODE            99  // 0x18c
#define VME_PE_3_WRITE_COUNT           100 // 0x190
#define VME_PE_3_WRITE_INNER_0         101 // 0x194
#define VME_PE_3_WRITE_INNER_1         102 // 0x198
#define VME_PE_3_WRITE_FORMAT_0        103 // 0x19c
#define VME_PE_3_WRITE_FORMAT_1        104 // 0x1a0

// End ?
#define VME_UNKNOWN_105                105 // 0x1a4


// PE processing flow modes
#define VME_UNKNOWN_0   0
#define VME_UNKNOWN_1   1
#define VME_UNKNOWN_2   2
#define VME_DEFAULT     3
#define VME_UNKNOWN_4   4
#define VME_UNKNOWN_5   5
#define VME_UNKNOWN_6   6
#define VME_UNKNOWN_7   7
#define VME_UNKNOWN_8   8

#define vmeLibStart()                             \
{                                                 \
  volatile u32 VME_BASE_ADDR = VME_DATAPATH_BASE; \
  _vmeLibStart();

#define vmeLibProcessAsync() \
{                            \
  vmeLibTrigger();           \
}

#define vmeLibFinish() \
  _vmeLibFinish();     \
}

#define vmeLibFinishAsync()      \
  meCoreDMACPrimWaitVMEFinish(); \
}

#define vmeLibPreloadCustomContext(context) \
{                                           \
  meCoreDMACPrimPreloadVMEContext(context); \
}

#define vmeLibLoadCustomContext(context) \
{                                        \
  vmeLibPreloadCustomContext(context);   \
  meCoreDMACPrimWaitTransferFinish();    \
}

#define vme_set_base(n) VME_BASE_ADDR = ((0x40000000 | n));
#define _vme_set_n(regIdx, val) ((VME_BASE(regIdx)) = (val))

#define vme_set_1(n, NAME, a)                       _vme_set_n(VME_##n##_##NAME, (a))
#define vme_set_2(n, NAME, a, b)                    _vme_set_n(VME_##n##_##NAME, (a)|(b))
#define vme_set_3(n, NAME, a, b, c)                 _vme_set_n(VME_##n##_##NAME, (a)|(b)|(c))
#define vme_set_4(n, NAME, a, b, c, d)              _vme_set_n(VME_##n##_##NAME, (a)|(b)|(c)|(d))
#define vme_set_5(n, NAME, a, b, c, d, e)           _vme_set_n(VME_##n##_##NAME, (a)|(b)|(c)|(d)|(e))
#define vme_set_6(n, NAME, a, b, c, d, e, f)        _vme_set_n(VME_##n##_##NAME, (a)|(b)|(c)|(d)|(e)|(f))
#define vme_set_7(n, NAME, a, b, c, d, e, f, g)     _vme_set_n(VME_##n##_##NAME, (a)|(b)|(c)|(d)|(e)|(f)|(g))
#define vme_set_8(n, NAME, a, b, c, d, e, f, g, h)  _vme_set_n(VME_##n##_##NAME, (a)|(b)|(c)|(d)|(e)|(f)|(g)|(h))

#define _vme_set(_1,_2,_3,_4,_5,_6,_7,_8, n, ...) n
#define vme_set(index, NAME, ...) \
  _vme_set(__VA_ARGS__, vme_set_8, vme_set_7, vme_set_6, vme_set_5, \
  vme_set_4, vme_set_3, vme_set_2, vme_set_1)(index, NAME, __VA_ARGS__)

// interconnect
#define vme_icn(NAME, ...) vme_set(INTERCONNECT, NAME, __VA_ARGS__)

// functionnal unit
#define vme_fu(TYPE) FUNCTIONAL_UNIT_##TYPE

// register
#define fu_reg(TYPE, REG) FU##_##TYPE##_REGISTER_##REG

// modifier
#define vme_mod(NAME, ...) vme_set(MODIFIER, NAME, __VA_ARGS__)

// address generation unit, generic
#define _vme_agu(_1, _2, _3, NAME, ...) NAME
#define vme_agu(...) _vme_agu(__VA_ARGS__, vme_agu_3, vme_agu_2, vme_agu_1)(__VA_ARGS__)
#define vme_agu_3(MODE, TARGET, PARAM)  MODE##_##TARGET##_##PARAM
#define vme_agu_2(MODE, PARAM)          MODE##_##PARAM

// process elements
#define vme_pe0(NAME, ...)  vme_set(PE_0, NAME, __VA_ARGS__)
#define vme_pe1(NAME, ...)  vme_set(PE_1, NAME, __VA_ARGS__)
#define vme_pe2(NAME, ...)  vme_set(PE_2, NAME, __VA_ARGS__)
#define vme_pe3(NAME, ...)  vme_set(PE_3, NAME, __VA_ARGS__)

// address generation unit
/*
#define vme_agu0(MODE, PARAM, ...)  \
  vme_pe0(vme_agu(MODE, PARAM), __VA_ARGS__)
#define vme_agu1(MODE, PARAM, ...)  \
  vme_pe1(vme_agu(MODE, PARAM), __VA_ARGS__)
#define vme_agu2(MODE, PARAM, ...)  \
  vme_pe2(vme_agu(MODE, PARAM), __VA_ARGS__)
#define vme_agu3(MODE, PARAM, ...)  \
  vme_pe3(vme_agu(MODE, PARAM), __VA_ARGS__)
*/

// agu_cnt(4)
 
#define agu_top(PARAM)    vme_agu(READ, TOP, PARAM)
#define agu_base(PARAM)   vme_agu(READ, BASE, PARAM)
#define agu_write(PARAM)  vme_agu(WRITE, PARAM)

// mux
#define _vme_mux(_1, _2, NAME, ...) NAME
#define vme_mux(...) _vme_mux(__VA_ARGS__, vme_mux_2, vme_mux_1)(__VA_ARGS__)
#define vme_mux_2(R0, R1)  (VME_MUX_FRONT_##R0 | VME_MUX_BACK_##R1)
#define vme_mux_1(R0)      (VME_MUX_FRONT_##R0)

// opcodes
#define _fu_op(_1, _2, _3, NAME, ...) NAME
#define fu_op(...) _fu_op(__VA_ARGS__, _fu_op3, _fu_op2, _fu_op1)(__VA_ARGS__)
#define _fu_op3(OPERATION, SOURCE, MODIFIER) VME_FU_OPCODE_##OPERATION##_##SOURCE##_##MODIFIER
#define _fu_op2(OPERATION, SOURCE)           VME_FU_OPCODE_##OPERATION##_##SOURCE
#define _fu_op1(OPERATION)                   VME_FU_OPCODE_##OPERATION

#define VME_LIB_TRIGGER_WITH(...) {                                            \
  _vmeLibStart();                                                              \
  volatile u32 VME_BASE_ADDR = VME_DATAPATH_BASE;                              \
  __VA_ARGS__                                                                  \
  vmeLibTrigger();                                                             \
}

#define VME_CONTEXT_WORD_COUNT 112

#define VME_LIB_CONTEXT_BUILDER(name, param, ...)                               \
void* name(void* param) {                                                       \
                                                                                \
  static u32 context[VME_CONTEXT_WORD_COUNT] __attribute__((aligned(64))) = {0};\
  volatile u32 VME_BASE_ADDR = (u32)context;                                    \
  __VA_ARGS__                                                                   \
  meCoreDcacheWritebackRange((void*)context, VME_CONTEXT_WORD_COUNT*4);         \
  return context;                                                               \
}

#define vmeLibGenContext(generator, target, param)                              \
                                                                                \
  u32 vme_ctx(target)[VME_CONTEXT_WORD_COUNT] __attribute__((aligned(64)));\
  generator((void*)vme_ctx(target), param);
  
#define VME_LIB_CONTEXT_GENERATOR(name, param, ...)              \
void name(void* context, void* param) {                          \
                                                                 \
  meCoreMemset(context, 0, VME_CONTEXT_WORD_COUNT*4);            \
  volatile u32 VME_BASE_ADDR = (u32)context;                     \
  __VA_ARGS__                                                    \
  meCoreDcacheWritebackRange(context, VME_CONTEXT_WORD_COUNT*4); \
}

#define vme_ctx(target) VME##target##CONTEXT

// dma
#define VME_DMAC_TRANSFERT_NO_WAIT     0
#define VME_DMAC_TRANSFERT_WAIT_FINISH 1

#define VME_DMAC_TRANSFERT_REG_CTRL_VALUE     0x008
#define VME_DMAC_TRANSFERT_REG_MEMORY_ADDR    0x010
#define VME_DMAC_TRANSFERT_REG_ITERATION_DIMS 0x014 /*iter count, inner count*/
#define VME_DMAC_TRANSFERT_REG_SPAD_OFFSET    0x018 /*scratchpad base offset*/
#define VME_DMAC_TRANSFERT_REG_SPAD_TARGET    0x01c
#define VME_DMAC_TRANSFERT_REG_ITERATION_STEP 0x020 /*in word bytes*/
#define VME_DMAC_TRANSFERT_REG_GROUP_SIZE     0x024
#define VME_DMAC_TRANSFERT_REG_GROUP_STEPS    0x028 /*outer step, inner step*/
#define VME_DMAC_TRANSFERT_REG_FILL_VALUE     0x02c

#define VME_DMAC_ADDR                 0x440ff000
#define VME_DMAC_BASE(offset)         (hw(VME_DMAC_ADDR | (offset)))
#define _vme_dma_set_n(regIdx, val)   ((VME_DMAC_BASE(regIdx)) = (val))

#define vme_dma_set_1(n, NAME, a)         _vme_dma_set_n(VME_DMAC_TRANSFERT_REG_##n##_##NAME, (a))
#define vme_dma_set_2(n, NAME, a, b)      _vme_dma_set_n(VME_DMAC_TRANSFERT_REG_##n##_##NAME, ((a) << 16)|(b))
#define vme_dma_set_3(n, NAME, a, b, c)   _vme_dma_set_n(VME_DMAC_TRANSFERT_REG_##n##_##NAME, ((a) << 24)|((b) << 16)|(c))

#define _vme_dma_set(_1,_2,_3, n, ...) n
#define vme_dma_set(index, NAME, ...) \
  _vme_dma_set(__VA_ARGS__, vme_dma_set_3, vme_dma_set_2, vme_dma_set_1)(index, NAME, __VA_ARGS__)
  
#define vme_dma(...) vme_dma_set(__VA_ARGS__)

#define useg_mem(v) (((v) & 0x1fffffff) | 0x40000000)

#ifdef __cplusplus
extern "C" {
#endif

void vmeLibEnable();
void vmeLibDisable();
void vmeLibWipe();
void vmeLibICNInvalidate(const int count);

void vmeLibConfigTransfer(const int status);
void vmeLibSendCustomContext(void* context);
void vmeLibClearLocalBuffer(const int dst, const int count);
void vmeLibMemoryToRingBuffer(void* const, const u32, const u32);
void vmeLibRingBufferToMemory(const u32, void* const, const u32);

void vmeLibSetInnerAGU1(const int offset, const int count, const int stride);
void vmeLibSetInnerAGU2(const int offset, const int count, const int stride);

void vmeLibMemTo16(const u32 src, const int offset, const int count, const int wait);
void vmeLibMemFrom16(const u32 dst, const int offset, const int count, const int wait);

void _vmeLibStart();
void _vmeLibFinish();

void vmeLibSetFlow(const int mode);
void vmeLibTrigger();

#ifdef __cplusplus
}
#endif

#endif

