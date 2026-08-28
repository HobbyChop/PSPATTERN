// WIP: The following are revisions of the previously tested opcodes
// (see the 'psp-media-engine-cracking-the-unknown' project)
// All duplicates need to be retested to make sure they are not simply
// activating the 'k' register

// TODO: Include 'k' in expressions

#ifndef VME_FU_OPS
#define VME_FU_OPS

#define op_sel(value) ((value) << 12)

#define OP_ZER             0x000 // zero
#define UN_001             0x001
#define UN_002             0x002
#define UN_003             0x003
#define OP_BAK             0x004 // back[n]
#define UN_005             0x005
#define UN_006             0x006
#define UN_007             0x007
#define UN_008             0x008 // back[n] (duplicate?)
#define UN_009             0x009
#define UN_00a             0x00a
#define UN_00b             0x00b
#define OP_CST             0x00c // b
#define UN_00d             0x00d
#define UN_00e             0x00e
#define UN_00f             0x00f

#define OP_ADD             0x010 // (back[n] + front[n])
#define UN_011             0x011
#define UN_012             0x012
#define UN_013             0x013
#define UN_014             0x014 // back[n] (duplicate?)
#define UN_015             0x015
#define UN_016             0x016
#define UN_017             0x017
#define OP_SUB             0x018 // (front[n] - back[n])
#define UN_019             0x019
#define UN_01a             0x01a
#define UN_01b             0x01b
#define UN_01c             0x01c // back[n] (duplicate?)
#define UN_01d             0x01d
#define UN_01e             0x01e
#define UN_01f             0x01f

#define UN_020             0x020 // (back[n] + front[n]) (duplicate?)
#define UN_021             0x021
#define UN_022             0x022
#define UN_023             0x023
#define OP_ADD_CST         0x024 // (back[n] + b)
#define UN_025             0x025
#define UN_026             0x026
#define UN_027             0x027
#define UN_028             0x028 // (front[n] - back[n]) (duplicate?)
#define UN_029             0x029
#define UN_02a             0x02a
#define UN_02b             0x02b
#define OP_NEG_OFB         0x02c // -(back[n]) + b
#define UN_02d             0x02d
#define UN_02e             0x02e
#define UN_02f             0x02f

#define OP_ADD_OFA         0x030 // (back[n] + front[n]) + a (OFA = add constant a)
#define UN_031             0x031
#define UN_032             0x032
#define UN_033             0x033
#define OP_SUB_INV_OFA     0x034 // (back[n] - front[n]) + a (INV = inverted, OFA = add constant a)
#define UN_035             0x035
#define UN_036             0x036
#define UN_037             0x037
#define UN_038             0x038 // (front[n] + back[n]) (duplicate?)
#define UN_039             0x039
#define UN_03a             0x03a
#define UN_03b             0x03b
#define UN_03c             0x03c // (front[n] - back[n]) (duplicate?)
#define UN_03d             0x03d
#define UN_03e             0x03e
#define UN_03f             0x03f

#define OP_ADD_SHF         0x040 // back[n] + (front[n] >> b) (SHF = shifted)
#define UN_041             0x041
#define UN_042             0x042
#define UN_043             0x043
#define OP_SHR_CST_OFA     0x044 // (back[n] >> b) + a (OFA = add constant a)
#define UN_045             0x045
#define UN_046             0x046
#define UN_047             0x047
#define OP_SUB_OFB         0x048 // (front[n] - back[n]) + b (OFB = add constant b)
#define UN_049             0x049
#define UN_04a             0x04a
#define UN_04b             0x04b
#define OP_SHR_CST_NEG_OFA 0x04c // (-(back[n] >> b)) + a (NEG = negated, OFA = add constant a)
#define UN_04d             0x04d
#define UN_04e             0x04e
#define UN_04f             0x04f

#define OP_SUB_INV_SHF     0x050 // back[n] - (front[n] >> b) (INV = inverted, SHF = shifted)
#define UN_051             0x051
#define UN_052             0x052
#define UN_053             0x053
#define OP_SHR_CST_OFS     0x054 // (back[n] >> b) - a 
#define UN_055             0x055
#define UN_056             0x056
#define UN_057             0x057
#define OP_ADD_HIB         0x058 // ((0xFF00 & front[n]) + (0xFF00 & back[n])) >> 8 (HIB = high byte)
#define UN_059             0x059
#define UN_05a             0x05a
#define UN_05b             0x05b
#define OP_ADD_LOB         0x05c // (0xFF & back[n]) + (0xFF & front[n]) (LOB = low byte)
#define UN_05d             0x05d
#define UN_05e             0x05e
#define UN_05f             0x05f

#define OP_SEL_NEG         0x060 // (front[n] & a) ? -back[n] : back[n] (SEL = select, NEG = negate)
#define UN_061             0x061
#define UN_062             0x062
#define UN_063             0x063
#define OP_SEL_NEG_SLF     0x064 // (back[n] & a) != 0 ? x : NEG(back[n]) (~back[n] + 1) (NEG = negate, SLF = test on back[n] itself)
#define UN_065             0x065
#define UN_066             0x066
#define UN_067             0x067
#define OP_SEL_CST         0x068 // (front[n] & a) ? b : back[n] (SEL = select)
#define UN_069             0x069
#define UN_06a             0x06a
#define UN_06b             0x06b
#define OP_SEL_CST_OFB     0x06c // ((front[n] & a) ? back[n] : 0) + b (OFB = add constant b)
#define UN_06d             0x06d
#define UN_06e             0x06e
#define UN_06f             0x06f

#define OP_SEL_ADS         0x070 // ((back[i] & a) ? (back[i] - b) : (back[i] + b)) (ADS = add-or-sub)
#define UN_071             0x071
#define UN_072             0x072
#define UN_073             0x073
#define OP_SEL_ADS_INV     0x074 // ((back[i] & a) ? (back[i] + b) : (back[i] - b)) (INV = inverted)
#define UN_075             0x075
#define UN_076             0x076
#define UN_077             0x077
#define OP_MIN             0x078 // min(back[n], front[n])
#define UN_079             0x079
#define UN_07a             0x07a
#define UN_07b             0x07b
#define OP_CLP             0x07c // max(b, min(a, back[n]))
#define UN_07d             0x07d
#define UN_07e             0x07e
#define UN_07f             0x07f

#define OP_MUL_CLP         0x080 // clamp(back[n], NEG2, POS2) * front[n]
#define UN_081             0x081
#define UN_082             0x082
#define UN_083             0x083
#define OP_MUL_CLP_NEG     0x084 // -(back[n] * front[n]) * 1[-2,2]​(front[n])
#define UN_085             0x085
#define UN_086             0x086
#define UN_087             0x087
#define OP_SEL_CST_ZER     0x088 // (back[n] & a) == 0 ? b : 0 (ZER = zero)
#define UN_089             0x089
#define UN_08a             0x08a
#define UN_08b             0x08b
#define OP_SEL_CST_ZER_INV 0x08c // (back[n] & a) != 0 ? b : 0 (INV = inverted)
#define UN_08d             0x08d
#define UN_08e             0x08e
#define UN_08f             0x08f

#define OP_SHL_CST_DYN     0x090 // back[n] != 0 ? (b << back[n]) : b (DYN = dynamic)
#define UN_091             0x091
#define UN_092             0x092
#define UN_093             0x093
#define OP_SHL_CST_DYN_ZER 0x094 // (back[n] <= 0) ? (b << (-back[n])) : 0 (ZER = zero)
#define UN_095             0x095
#define UN_096             0x096
#define UN_097             0x097
#define OP_SHF_CST_M24     0x098 // (back[n] >= 0) ? (b << back[n]) & 0xFFFFFF : (b >> (-back[n])) (SHF = shift, M24 = 24-bit mask)
#define UN_099             0x099
#define UN_09a             0x09a
#define UN_09b             0x09b
#define UN_09c             0x09c // (back[n] <= 0) ? (b << (-back[n])) : 0 // same as #define UN_000 0x094 ? (duplicate?)
#define UN_09d             0x09d
#define UN_09e             0x09e
#define UN_09f             0x09f

#define OP_SHF             0x0a0 // (front[n] >= 0) ? (back[n] << front[n]) : (back[n] >> -front[n])
#define UN_0a1             0x0a1 
#define UN_0a2             0x0a2
#define UN_0a3             0x0a3
#define OP_SHL_CST         0x0a4 // (back[n] << b)
#define UN_0a5             0x0a5
#define UN_0a6             0x0a6
#define UN_0a7             0x0a7
#define OP_SHF_INV         0x0a8 // (front[n] >= 0) ? (back[n] >> front[n]) : (back[n] << -front[n]) (INV = inverted)
#define UN_0a9             0x0a9
#define UN_0aa             0x0aa
#define UN_0ab             0x0ab
#define OP_SHR_CST         0x0ac // (back[n]) >> b
#define UN_0ad             0x0ad
#define UN_0ae             0x0ae
#define UN_0af             0x0af

#define OP_SHF_M24         0x0b0 // (front[n] >= 0) ? (back[n] << front[n]) : ((back[n] & 0xFFFFFF) >> -front[n]) (M24 = 24-bit mask)
#define UN_0b1             0x0b1
#define UN_0b2             0x0b2
#define UN_0b3             0x0b3
#define OP_SHL_CST_M24     0x0b4 // (back[n] & 0xFFFFFF) << b (M24 = 24-bit mask)
#define UN_0b5             0x0b5
#define UN_0b6             0x0b6
#define UN_0b7             0x0b7
#define OP_SHF_INV_M24     0x0b8 // (front[n] >= 0) ? ((back[n] & 0xFFFFFF) >> front[n]) : (back[n] << -front[n]) (INV = inverted, M24 = 24-bit mask)
#define UN_0b9             0x0b9
#define UN_0ba             0x0ba
#define UN_0bb             0x0bb
#define OP_SHR_CST_M24     0x0bc // (back[n] & 0xFFFFFF) >> b (M24 = 24-bit mask)
#define UN_0bd             0x0bd
#define UN_0be             0x0be
#define UN_0bf             0x0bf

#define OP_AND             0x0c0 // (back[n] & front[n])
#define UN_0c1             0x0c1
#define UN_0c2             0x0c2
#define UN_0c3             0x0c3
#define OP_AND_CST         0x0c4 // (back[n] & b)
#define UN_0c5             0x0c5
#define UN_0c6             0x0c6
#define UN_0c7             0x0c7
#define OP_NAD             0x0c8 // ~(front[n] & back[n]) (NAD = NAND)
#define UN_0c9             0x0c9
#define UN_0ca             0x0ca
#define UN_0cb             0x0cb
#define OP_NAD_CST         0x0cc // -(b & back[n]) - 1 // ~(b & back[n]) (NAD = NAND)
#define UN_0cd             0x0cd
#define UN_0ce             0x0ce
#define UN_0cf             0x0cf

#define OP_OR              0x0d0 // (back[n] | front[n])
#define UN_0d1             0x0d1
#define UN_0d2             0x0d2
#define UN_0d3             0x0d3
#define OP_OR_CST          0x0d4 // (back[n] | b)
#define UN_0d5             0x0d5
#define UN_0d6             0x0d6
#define UN_0d7             0x0d7
#define OP_NOR             0x0d8 // ~(front[n] | back[n])
#define UN_0d9             0x0d9
#define UN_0da             0x0da
#define UN_0db             0x0db
#define OP_NOR_CST         0x0dc // (-back[n] - 1) & ~b // (~back[n]) & ~b
#define UN_0dd             0x0dd
#define UN_0de             0x0de
#define UN_0df             0x0df

#define OP_XOR             0x0e0 // back[n] ^ front[n]
#define UN_0e1             0x0e1
#define UN_0e2             0x0e2
#define UN_0e3             0x0e3
#define OP_XOR_CST         0x0e4 // (back[n] ^ b)
#define UN_0e5             0x0e5
#define UN_0e6             0x0e6
#define UN_0e7             0x0e7
#define OP_ADF_NOT         0x0e8 // ~(|front[n] - back[n]|) (ADF = abs diff, NOT = complement)
#define UN_0e9             0x0e9
#define UN_0ea             0x0ea
#define UN_0eb             0x0eb
#define OP_XOR_CST_NOT     0x0ec // (~back[n]) ^ b (NOT = complemented)
#define UN_0ed             0x0ed
#define UN_0ee             0x0ee
#define UN_0ef             0x0ef

#define OP_TST_EM1         0x0f0 // (back[n] == -1) ? 1 : 0 (EM1 = equals minus one)
#define UN_0f1             0x0f1
#define UN_0f2             0x0f2
#define UN_0f3             0x0f3
#define OP_TST_NNZ         0x0f4 // (back[n] != 0) ? 1 : 0 (NNZ = non-zero)
#define UN_0f5             0x0f5
#define UN_0f6             0x0f6
#define UN_0f7             0x0f7
#define OP_TST_PAR         0x0f8 // XOR(back[n].bit[0], ..., back[n].bit[23]) ? 1 : 0 (PAR = parity)
#define UN_0f9             0x0f9
#define UN_0fa             0x0fa
#define UN_0fb             0x0fb
#define OP_NOT             0x0fc //  ~back[n] 
#define UN_0fd             0x0fd
#define UN_0fe             0x0fe
#define UN_0ff             0x0ff

#endif
