#ifndef VME_FU_OPCODES_H
#define VME_FU_OPCODES_H

/*
 * VME Functional Unit Opcodes
 *
 * Back and Front buffers are routed to 0x44020000 by default.
 * Opcodes with the _ALT suffix have the 0x00800000 flag set, which reroutes
 * Back -> 0x44020000 and Front -> 0x44020800.
 */


/*
 * PASSTHROUGH
 */
#define VME_FU_OPCODE_PASSTHROUGH                       0x00004000  /* x */
#define VME_FU_OPCODE_PASSTHROUGH_BACK                  0x00008000  /* back[n] */


/*
 * CONST
 */
#define VME_FU_OPCODE_CONST                             0x0000c000  /* b */
#define VME_FU_OPCODE_CONST_1                           0x001b0000  /* b */
#define VME_FU_OPCODE_CONST_2                           0x00088000  /* b */
#define VME_FU_OPCODE_CONST_3                           0x00089000  /* b */
#define VME_FU_OPCODE_CONST_4                           0x0008a000  /* b */
#define VME_FU_OPCODE_CONST_5                           0x0008b000  /* b */


/*
 * ADD
 */

/* Generic ALU */
#define VME_FU_OPCODE_ADD_IMM_RSHIFT                    0x00024000  /* (x + b) >> k */

/* Inter-buffer */
#define VME_FU_OPCODE_ADD_IBUF_RSHIFT                   0x00010000  /* (back[n] + front[n]) >> k */
#define VME_FU_OPCODE_ADD_IBUF_RSHIFT_1                 0x00020000  /* (back[n] + front[n]) >> k */
#define VME_FU_OPCODE_ADD_IBUF_CONST                    0x00030000  /* (back[n] + front[n]) + a */
#define VME_FU_OPCODE_ADD_IBUF_FRONT_RSHIFT             0x00040000  /* back[n] + (front[n] >> b) */
#define VME_FU_OPCODE_ADD_IBUF_ACC                      0x00038000  /* (front[n] + back[n]) */
#define VME_FU_OPCODE_ADD_IBUF_LSHIFT_CONST             0x00070000  /* (back[n] << k) + b */

/* 8-bit channel packing */
#define VME_FU_OPCODE_ADD_CHAN1_LSHIFT_0                0x0005c000  /* ((0xFF & back[n]) + (0xFF & front[n])) << k  with k[0..1] */
#define VME_FU_OPCODE_ADD_CHAN1_LSHIFT_1                0x0005d000  /* ((0xFF & back[n]) + (0xFF & front[n])) << k  with k[0..1] */
#define VME_FU_OPCODE_ADD_CHAN1_LSHIFT_2                0x0005e000  /* ((0xFF & back[n]) + (0xFF & front[n])) << k  with k[0..1] */
#define VME_FU_OPCODE_ADD_CHAN1_LSHIFT_3                0x0005f000  /* ((0xFF & back[n]) + (0xFF & front[n])) << k  with k[0..1] */
#define VME_FU_OPCODE_ADD_CHAN2_SCALE_0                 0x00058000  /* (((0xFF00 & front[n]) + (0xFF00 & back[n])) >> 8) << k */
#define VME_FU_OPCODE_ADD_CHAN2_SCALE_1                 0x00059000  /* (((0xFF00 & front[n]) + (0xFF00 & back[n])) >> 8) << k */
#define VME_FU_OPCODE_ADD_CHAN2_SCALE_2                 0x0005a000  /* (((0xFF00 & front[n]) + (0xFF00 & back[n])) >> 8) << k */
#define VME_FU_OPCODE_ADD_CHAN2_SCALE_3                 0x0005b000  /* (((0xFF00 & front[n]) + (0xFF00 & back[n])) >> 8) << k */
#define VME_FU_OPCODE_ADD_FRONT_BACK_LSHIFT             0x0005c000  /* (back[n] + front[n]) << k  with k[0..1] */

/* Vector ALU */
#define VME_FU_OPCODE_ADD_VEC                           0x00011000  /* back[n] + front[n] */
#define VME_FU_OPCODE_ADD_VEC_RSHIFT                    0x00041000  /* (back[n] + front[n]) >> b */
#define VME_FU_OPCODE_ADD_ACC                           0x00021000  /* back[n] + front[n] */
#define VME_FU_OPCODE_ADD_IMM                           0x00031000  /* (back[n] + front[n]) + a */

/* Stride variants */
#define VME_FU_OPCODE_ADD_IBUF_RSHIFT_2                 0x00310000  /* (back[n] + front[n]) >> k */
#define VME_FU_OPCODE_ADD_IBUF_RSHIFT_3                 0x00410000  /* (back[n] + front[n]) >> k */


/*
 * SUB
 */

/* Generic ALU */
#define VME_FU_OPCODE_SUB_BACK_FRONT_ADD                0x00034000  /* (back[n] - front[n]) + a */
#define VME_FU_OPCODE_SUB_SHIFT                         0x00054000  /* (x >> b) - a */

/* Inter-buffer */
#define VME_FU_OPCODE_SUB_FRONT_RSHIFT                  0x00050000  /* back[n] - (front[n] >> b) */
#define VME_FU_OPCODE_SUB_FRONT_RSHIFT_1                0x00051000  /* back[n] - (front[n] >> b) */
#define VME_FU_OPCODE_SUB_BIAS_VEC                      0x00051000  /* (back[n] - front[n]) - b */
#define VME_FU_OPCODE_SUB_FRONT_RSHIFT_2                0x00052000  /* back[n] - (front[n] >> b) */
#define VME_FU_OPCODE_SUB_FRONT_RSHIFT_3                0x00053000  /* back[n] - (front[n] >> b) */
#define VME_FU_OPCODE_SUB_SHIFT_0                       0x00055000  /* (back[n] >> b) - a */
#define VME_FU_OPCODE_SUB_SHIFT_1                       0x00056000  /* (back[n] >> b) - a */
#define VME_FU_OPCODE_SUB_SHIFT_2                       0x00057000  /* (back[n] >> b) - a */

/* Data conditioning */
#define VME_FU_OPCODE_SUB_BACK_FROM_FRONT               0x0003c000  /* (front[n] - back[n]) */

/* 0x00008000 range */
#define VME_FU_OPCODE_SUB_BACK_FROM_FRONT_RSHIFT        0x00018000  /* (front[n] - back[n]) >> k */
#define VME_FU_OPCODE_SUB_BACK_FROM_FRONT_RSHIFT_1      0x00028000  /* (front[n] - back[n]) >> k */
#define VME_FU_OPCODE_SUB_BACK_FROM_FRONT_CONST         0x00048000  /* (front[n] - back[n]) + b */

/* Stride variants */
#define VME_FU_OPCODE_SUB_IBUF_RSHIFT                   0x00450000  /* (back[n] - front[n]) >> b */
#define VME_FU_OPCODE_SUB_FRONT_RSHIFT_STRIDED          0x00510000  /* (front[n] - back[n]) >> k */


/*
 * NEG
 */
#define VME_FU_OPCODE_NEG_SHIFTED_ADD                   0x0002c000  /* (-(back[n] >> k)) + b */
#define VME_FU_OPCODE_NEG_SHIFT_ADD                     0x0004c000  /* (-(back[n] >> b)) + a */
#define VME_FU_OPCODE_NEG_FRONT                         0x00850000  /* -front[n] */


/*
 * SHIFT_ACC  (shift then accumulate, neither pure shift nor pure add)
 */
#define VME_FU_OPCODE_SHIFT_ACC                         0x00044000  /* (x >> b) + a */


/*
 * SHIFT_LEFT
 */
#define VME_FU_OPCODE_LSHIFT                            0x000a4000  /* (x << b) */
#define VME_FU_OPCODE_LSHIFT_ALT                        0x000b4000  /* (x << b) */
#define VME_FU_OPCODE_LSHIFT_SUB_IMM                    0x00074000  /* (x - b) << k */
#define VME_FU_OPCODE_LSHIFT_IBUF_BY_FRONT              0x000a0000  /* back[n] << front[n] */
#define VME_FU_OPCODE_LSHIFT_IBUF_BY_FRONT_1            0x000b0000  /* back[n] << front[n] */


/*
 * SHIFT_RIGHT
 */
#define VME_FU_OPCODE_RSHIFT                            0x00014000  /* (x >> k) */
#define VME_FU_OPCODE_RSHIFT_ARITH_BACK                 0x0001c000  /* (back[n] >> k) */
#define VME_FU_OPCODE_RSHIFT_BACK_BY_CONST              0x000ac000  /* (back[n]) >> b */
#define VME_FU_OPCODE_RSHIFT_BACK_BY_CONST_1            0x000bc000  /* (back[n]) >> b */
#define VME_FU_OPCODE_RSHIFT_BY_FRONT                   0x000b8000  /* back[n] >> front[n] */


/*
 * BARREL_SHIFT
 */
#define VME_FU_OPCODE_BARREL_RSHIFT_CONST               0x00094000  /* (b >> back[n]) */
#define VME_FU_OPCODE_BARREL_RSHIFT_CONST_0             0x0009c000  /* (b >> back[n]) */
#define VME_FU_OPCODE_BARREL_RSHIFT_IBUF                0x00090000  /* (b >> back[n]) */
#define VME_FU_OPCODE_BARREL_LSHIFT_BACK_IBUF           0x00090000  /* (b << back[n]) */
#define VME_FU_OPCODE_BARREL_LSHIFT_BACK_0              0x00490000  /* (b << back[n]) */
#define VME_FU_OPCODE_BARREL_LSHIFT_BACK_1              0x00098000  /* (b << back[n]) */
#define VME_FU_OPCODE_BARREL_LSHIFT_FRONT_0             0x00890000  /* (b << front[n]) */
#define VME_FU_OPCODE_BARREL_LSHIFT_FRONT_1             0x00c90000  /* (b << front[n]) */


/*
 * MUL
 */
#define VME_FU_OPCODE_MUL_RSHIFT_0                      0x00200000  /* (back[n] * front[n]) >> k */
#define VME_FU_OPCODE_MUL_RSHIFT_1                      0x00201000  /* (back[n] * front[n]) >> k */
#define VME_FU_OPCODE_MUL_RSHIFT_2                      0x00202000  /* (back[n] * front[n]) >> k */
#define VME_FU_OPCODE_MUL_RSHIFT_3                      0x00203000  /* (back[n] * front[n]) >> k */
#define VME_FU_OPCODE_MUL_CONST_RSHIFT_0                0x00204000  /* (back[n] * b) >> k */
#define VME_FU_OPCODE_MUL_CONST_RSHIFT_1                0x00205000  /* (back[n] * b) >> k */
#define VME_FU_OPCODE_MUL_CONST_RSHIFT_2                0x00206000  /* (back[n] * b) >> k */
#define VME_FU_OPCODE_MUL_CONST_RSHIFT_3                0x00207000  /* (back[n] * b) >> k */
#define VME_FU_OPCODE_MUL_VEC_RSHIFT                    0x00220000  /* (back[n] * front[n]) >> k */
#define VME_FU_OPCODE_MUL_SCALAR_RSHIFT_BIAS            0x00260000  /* ((a * n) + b) >> k */
#define VME_FU_OPCODE_MUL_VEC_RSHIFT_BIAS               0x00270000  /* (back[n] * front[n] + b) >> k */
#define VME_FU_OPCODE_MUL_CLAMP_0                       0x00080000  /* (back[n] * front[n]) * 1[-2,2](front[n]) */
#define VME_FU_OPCODE_MUL_CLAMP_1                       0x00081000  /* (back[n] * front[n]) * 1[-2,2](front[n]) */
#define VME_FU_OPCODE_MUL_CLAMP_2                       0x00082000  /* (back[n] * front[n]) * 1[-2,2](front[n]) */
#define VME_FU_OPCODE_MUL_CLAMP_3                       0x00083000  /* (back[n] * front[n]) * 1[-2,2](front[n]) */

/* ALT routing variants */
#define VME_FU_OPCODE_MUL_RSHIFT_ALT                    0x00a00000  /* (back[n] * front[n]) >> k */
#define VME_FU_OPCODE_MUL_VEC_RSHIFT_ALT                0x00a20000  /* (back[n] * front[n]) >> k */
#define VME_FU_OPCODE_MUL_SCALAR_RSHIFT_BIAS_ALT        0x00a60000  /* ((n * a) + b) >> k */
#define VME_FU_OPCODE_MUL_VEC_RSHIFT_BIAS_ALT           0x00a70000  /* (back[n] * front[n] + b) >> k */


/*
 * MUL_NEG
 */
#define VME_FU_OPCODE_MUL_NEG_RSHIFT_0                  0x00208000  /* (-(back[n] * front[n])) >> k */
#define VME_FU_OPCODE_MUL_NEG_RSHIFT_1                  0x00209000  /* (-(back[n] * front[n])) >> k */
#define VME_FU_OPCODE_MUL_NEG_RSHIFT_2                  0x0020a000  /* (-(back[n] * front[n])) >> k */
#define VME_FU_OPCODE_MUL_NEG_RSHIFT_3                  0x0020b000  /* (-(back[n] * front[n])) >> k */
#define VME_FU_OPCODE_MUL_NEG_CONST_RSHIFT_0            0x0020c000  /* (-(back[n] * b)) >> k */
#define VME_FU_OPCODE_MUL_NEG_CONST_RSHIFT_1            0x0020d000  /* (-(back[n] * b)) >> k */
#define VME_FU_OPCODE_MUL_NEG_CONST_RSHIFT_2            0x0020e000  /* (-(back[n] * b)) >> k */
#define VME_FU_OPCODE_MUL_NEG_CONST_RSHIFT_3            0x0020f000  /* (-(back[n] * b)) >> k */
#define VME_FU_OPCODE_MUL_NEG_VEC_RSHIFT                0x00230000  /* -((front[n] * back[n]) >> k) */
#define VME_FU_OPCODE_MUL_NEG_CLAMP_0                   0x00084000  /* -(back[n] * front[n]) * 1[-2,2](front[n]) */
#define VME_FU_OPCODE_MUL_NEG_CLAMP_1                   0x00085000  /* -(back[n] * front[n]) * 1[-2,2](front[n]) */
#define VME_FU_OPCODE_MUL_NEG_CLAMP_2                   0x00086000  /* -(back[n] * front[n]) * 1[-2,2](front[n]) */
#define VME_FU_OPCODE_MUL_NEG_CLAMP_3                   0x00087000  /* -(back[n] * front[n]) * 1[-2,2](front[n]) */

/* ALT routing variant */
#define VME_FU_OPCODE_MUL_NEG_VEC_RSHIFT_ALT            0x00a30000  /* -((back[n] * front[n]) >> k) */


/*
 * MUL_DELAYED  (x[n] * x[n-1], uses previous sample)
 */
#define VME_FU_OPCODE_MUL_DELAYED_SCALAR                0x00210000  /* (x[n] * x[n-1]) >> k  with x[-1] = b */
#define VME_FU_OPCODE_MUL_DELAYED_BACK                  0x00610000  /* back[i] * back[i-1]  with back[-1] = b */
#define VME_FU_OPCODE_MUL_DELAYED_FRONT_PERIODIC        0x00a10000  /* front[n] * front[n-1] + ((!(n % 4) && n) ? front[n-1] / 3 : 0) */


/*
 * MAC  (multiply-accumulate)
 */
#define VME_FU_OPCODE_MAC_INNER_PRODUCT_BIAS            0x00240000  /* (Σn(back[n] * front[n]) + b) >> k */
#define VME_FU_OPCODE_MAC_INNER_PRODUCT_BIAS_ALT        0x00a40000  /* (Σn(back[n] * front[n]) + b) >> k */
#define VME_FU_OPCODE_MAC_ACC_SHIFT_BIAS                0x00250000  /* (i == 0 ? (b >> k) : out[n-1]) + (back[n] >> k) */
#define VME_FU_OPCODE_MAC_RUNNING_SUM_SHIFT_BIAS_ALT    0x00a50000  /* (Σn(front[0..n]) + b) >> k */


/*
 * IIR  (delayed feedback filters)
 */
#define VME_FU_OPCODE_IIR_DELAYED_FEEDBACK              0x00280000  /* 0 if n < 2 else ((out[n-1] + front[n-2] + back[n-2]) >> k) + a */
#define VME_FU_OPCODE_IIR_DELAYED_FEEDBACK_1            0x002a0000  /* 0 if n < 2 else ((out[n-1] + front[n-2] + back[n-2]) >> k) + a */
#define VME_FU_OPCODE_IIR_DELAYED_FEEDBACK_ALT          0x00a80000  /* 0 if n < 2 else ((out[n-1] + front[n-2] + back[n-2]) >> k) + a */
#define VME_FU_OPCODE_IIR_DELAYED_FEEDBACK_ALT_1        0x00aa0000  /* 0 if n < 2 else ((out[n-1] + front[n-2] + back[n-2]) >> k) + a */
#define VME_FU_OPCODE_IIR_RUNNING_SMOOTH                0x00650000  /* (out[n-1] + back[n] + b) >> k */


/*
 * SAD  (sum of absolute differences)
 */
#define VME_FU_OPCODE_SAD                               0x00290000  /* Σ{m=0..n-2} abs(back[m] - front[m]) + b */
#define VME_FU_OPCODE_SAD_1                             0x00690000  /* Σ{m=0..n-2} abs(back[m] - front[m]) + b */
#define VME_FU_OPCODE_SAD_ALT                           0x00a90000  /* Σ{m=0..n-2} abs(back[m] - front[m]) + b */
#define VME_FU_OPCODE_SAD_ALT_1                         0x00e90000  /* Σ{m=0..n-2} abs(back[m] - front[m]) + b */


/*
 * ABS
 */
#define VME_FU_OPCODE_ABS_DIFF_SIGNED24                 0x00170000  /* |back[n] - b| */
#define VME_FU_OPCODE_ABS_SUM_SIGNED24                  0x001c0000  /* |(back[n] + front[n])| */
#define VME_FU_OPCODE_ABS_DIFF_IBUF_OFFSET_A            0x001e0000  /* |(back[n] - front[n])| + a */
#define VME_FU_OPCODE_ABS_DIFF_IBUF_OFFSET_B            0x001f0000  /* |(front[n] - back[n])| + b */
#define VME_FU_OPCODE_ABS_DIFF_INV                      0x000e8000  /* ~(|front[n] - back[n]|) */


/*
 * MIN
 */
#define VME_FU_OPCODE_MIN_IBUF                          0x000c0000  /* min(back[n], front[n]) */
#define VME_FU_OPCODE_MIN_IBUF_1                        0x00078000  /* min(back[n], front[n]) */
#define VME_FU_OPCODE_MIN_RUNNING                       0x00110000  /* min(LASTMIN(out), back[n] - front[n]) */
#define VME_FU_OPCODE_MIN_RUNNING_RATE_LIMITED          0x00120000  /* min(in[n], max(in[0..n-1])); out[0] = in[0] */


/*
 * MAX
 */
#define VME_FU_OPCODE_MAX_IBUF                          0x00140000  /* max(back[n], front[n]) */
#define VME_FU_OPCODE_MAX_RUNNING                       0x00100000  /* max(out[n-1], in[n]) */
#define VME_FU_OPCODE_MAX_RUNNING_EXTREMA               0x00130000  /* max(LASTMIN(out), back[n] - front[n]) */


/*
 * CLAMP
 */
#define VME_FU_OPCODE_CLAMP_BACK                        0x0007c000  /* max(a, min(b, back[n])) */


/*
 * AND
 */
#define VME_FU_OPCODE_AND                               0x000c4000  /* (x & b) */


/*
 * OR
 */
#define VME_FU_OPCODE_OR                                0x000d4000  /* (x | b) */
#define VME_FU_OPCODE_OR_IBUF                           0x000d0000  /* back[n] | front[n] */


/*
 * XOR
 */
#define VME_FU_OPCODE_XOR                               0x000e4000  /* (x ^ b) */
#define VME_FU_OPCODE_XOR_IBUF                          0x000e0000  /* back[n] ^ front[n] */
#define VME_FU_OPCODE_XOR_PARITY                        0x000f8000  /* back[n]0 ^ back[n]1 ^ back[n]2 ^ ... */


/*
 * NOT  (bitwise complement variants)
 */
#define VME_FU_OPCODE_NOT                               0x000fc000  /* ~back[n] */
#define VME_FU_OPCODE_NOT_XOR                           0x000ec000  /* (~back[n]) ^ b */
#define VME_FU_OPCODE_NOT_AND_MASK                      0x000dc000  /* (~back[n]) & ~b */
#define VME_FU_OPCODE_NOT_NAND                          0x000cc000  /* ~(b & back[n]) */


/*
 * COND  (conditional / predicate operations)
 */
#define VME_FU_OPCODE_COND_NEG_ALU                      0x00064000  /* (x & a) != 0 ? x : NEG(x)  (~x + 1) */
#define VME_FU_OPCODE_COND_NEG_IBUF                     0x00060000  /* (front[n] & a) ? -back[n] : back[n] */
#define VME_FU_OPCODE_COND_NEG_VEC                      0x00061000  /* (front[n] & a) ? -back[n] : back[n] */
#define VME_FU_OPCODE_COND_ADDSUB_VEC                   0x00071000  /* (i & a) ? back[i] - b : back[i] + b */
#define VME_FU_OPCODE_COND_CMOV_BIAS                    0x0006c000  /* ((front[n] & a) ? back[n] : 0) + b */
#define VME_FU_OPCODE_COND_BITTEST_CONST_0              0x0008c000  /* (back[n] & a) ? b : 0 */
#define VME_FU_OPCODE_COND_BITTEST_CONST_1              0x0008d000  /* (back[n] & a) ? b : 0 */
#define VME_FU_OPCODE_COND_BITTEST_CONST_2              0x0008e000  /* (back[n] & a) ? b : 0 */
#define VME_FU_OPCODE_COND_BITTEST_CONST_3              0x0008f000  /* (back[n] & a) ? b : 0 */


/*
 * TEST
 */
#define VME_FU_OPCODE_TEST_NONZERO                      0x000f4000  /* (x != 0) */


/*
 * ROR
 */
#define VME_FU_OPCODE_ROR64_BY_FRONT                    0x000a8000  /* back[n] ROR.64 front[n] */


#endif
