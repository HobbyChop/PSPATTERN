.text
  
.globl meLibSaveContext
.ent   meLibSaveContext
meLibSaveContext:
  .set push
  .set noreorder
  .set volatile
  .set noat
  
  sw     $k0, -4($sp)
  
  // load CTX_BASE into $k0
  lui    $k0, %hi(CTX_BASE)
  ori    $k0, $k0, %lo(CTX_BASE)
  
  // save k0 and k1 (optional)
  sw     $k1, 128($k0)
  lw     $k1, -4($sp)
  sw     $k1, 124($k0)
  
  // save context
  sw     $at, 0($k0)              // $1
  sw     $v0, 4($k0)              // $2
  sw     $v1, 8($k0)              // $3
  sw     $a0, 12($k0)             // $4
  sw     $a1, 16($k0)             // $5
  sw     $a2, 20($k0)             // $6
  sw     $a3, 24($k0)             // $7
  sw     $t0, 28($k0)             // $8
  sw     $t1, 32($k0)             // $9
  sw     $t2, 36($k0)             // $10
  sw     $t3, 40($k0)             // $11
  sw     $t4, 44($k0)             // $12
  sw     $t5, 48($k0)             // $13
  sw     $t6, 52($k0)             // $14
  sw     $t7, 56($k0)             // $15
  sw     $s0, 60($k0)             // $16
  sw     $s1, 64($k0)             // $17
  sw     $s2, 68($k0)             // $18
  sw     $s3, 72($k0)             // $19
  sw     $s4, 76($k0)             // $20
  sw     $s5, 80($k0)             // $21
  sw     $s6, 84($k0)             // $22
  sw     $s7, 88($k0)             // $23
  sw     $t8, 92($k0)             // $24
  sw     $t9, 96($k0)             // $25
  // skip $k0                     // $26
  // skip $k1                     // $27
  sw     $gp, 100($k0)            // $28
  sw     $sp, 104($k0)            // $29
  sw     $fp, 108($k0)            // $30
  
  mfc0   $k1, $14
  sw     $k1, 112($k0)            // $31
//sw     $ra, 112($k0)            // $31
  
  // save hi and lo
  mflo   $k1
  sw     $k1, 116($k0)
  mfhi   $k1
  sw     $k1, 120($k0)
  
  // get status and check if cp1 is enabled
  mfc0     $k1, $12
  sync
  lui      $t0, 0x2000
  and      $t0, $k1, $t0
  beqz     $t0, 1f
  nop

  // save cp1 registers
  swc1     $f0, 132($k0)
  swc1     $f1, 136($k0)
  swc1     $f2, 140($k0)
  swc1     $f3, 144($k0)
  swc1     $f4, 148($k0)
  swc1     $f5, 152($k0)
  swc1     $f6, 156($k0)
  swc1     $f7, 160($k0)
  swc1     $f8, 164($k0)
  swc1     $f9, 168($k0)
  swc1     $f10, 172($k0)
  swc1     $f11, 176($k0)
  swc1     $f12, 180($k0)
  swc1     $f13, 184($k0)
  swc1     $f14, 188($k0)
  swc1     $f15, 192($k0)
  swc1     $f16, 196($k0)
  swc1     $f17, 200($k0)
  swc1     $f18, 204($k0)
  swc1     $f19, 208($k0)
  swc1     $f20, 212($k0)
  swc1     $f21, 216($k0)
  swc1     $f22, 220($k0)
  swc1     $f23, 224($k0)
  swc1     $f24, 228($k0)
  swc1     $f25, 232($k0)
  swc1     $f26, 236($k0)
  swc1     $f27, 240($k0)
  swc1     $f28, 244($k0)
  swc1     $f29, 248($k0)
  swc1     $f30, 252($k0)
  swc1     $f31, 256($k0)
  1:
  
  // save status
  sw       $k1, 260($k0)

  .set pop
  jr     $ra
  nop
  .end   meLibSaveContext



.globl meLibRestoreContext
.ent   meLibRestoreContext
meLibRestoreContext:
  .set push
  .set noreorder
  .set volatile
  .set noat

  // load CTX_BASE into $k0
  lui    $k0, %hi(CTX_BASE)
  ori    $k0, $k0, %lo(CTX_BASE)

  // restore context
  lw     $at, 0($k0)              // $1
  lw     $v0, 4($k0)              // $2
  lw     $v1, 8($k0)              // $3
  lw     $a0, 12($k0)             // $4
  lw     $a1, 16($k0)             // $5
  lw     $a2, 20($k0)             // $6
  lw     $a3, 24($k0)             // $7
  lw     $t0, 28($k0)             // $8
  lw     $t1, 32($k0)             // $9
  lw     $t2, 36($k0)             // $10
  lw     $t3, 40($k0)             // $11
  lw     $t4, 44($k0)             // $12
  lw     $t5, 48($k0)             // $13
  lw     $t6, 52($k0)             // $14
  lw     $t7, 56($k0)             // $15
  lw     $s0, 60($k0)             // $16
  lw     $s1, 64($k0)             // $17
  lw     $s2, 68($k0)             // $18
  lw     $s3, 72($k0)             // $19
  lw     $s4, 76($k0)             // $20
  lw     $s5, 80($k0)             // $21
  lw     $s6, 84($k0)             // $22
  lw     $s7, 88($k0)             // $23
  lw     $t8, 92($k0)             // $24
  lw     $t9, 96($k0)             // $25
  // skip $k0                     // $26
  // skip $k1                     // $27
  lw     $gp, 100($k0)            // $28
  lw     $sp, 104($k0)            // $29
  lw     $fp, 108($k0)            // $30
  
//lw     $ra, 112($k0)            // $31
  lw     $k1, 112($k0)            // $31
  mtc0   $k1, $14
  sync
  
  // restore hi and lo
  lw     $k1, 116($k0)
  mtlo   $k1
  lw     $k1, 120($k0)
  mthi   $k1
  
  // restore status (for the next check and cp1 restore process)
  lw       $k1, 260($k0)
  mtc0     $k1, $12
  sync
  
  // check if cp1 is enabled
  lui      $t0, 0x2000
  and      $t0, $k1, $t0
  beqz     $t0, 1f
  nop
  
  // restore cp1 registers
  lwc1     $f0, 132($k0)
  lwc1     $f1, 136($k0)
  lwc1     $f2, 140($k0)
  lwc1     $f3, 144($k0)
  lwc1     $f4, 148($k0)
  lwc1     $f5, 152($k0)
  lwc1     $f6, 156($k0)
  lwc1     $f7, 160($k0)
  lwc1     $f8, 164($k0)
  lwc1     $f9, 168($k0)
  lwc1     $f10, 172($k0)
  lwc1     $f11, 176($k0)
  lwc1     $f12, 180($k0)
  lwc1     $f13, 184($k0)
  lwc1     $f14, 188($k0)
  lwc1     $f15, 192($k0)
  lwc1     $f16, 196($k0)
  lwc1     $f17, 200($k0)
  lwc1     $f18, 204($k0)
  lwc1     $f19, 208($k0)
  lwc1     $f20, 212($k0)
  lwc1     $f21, 216($k0)
  lwc1     $f22, 220($k0)
  lwc1     $f23, 224($k0)
  lwc1     $f24, 228($k0)
  lwc1     $f25, 232($k0)
  lwc1     $f26, 236($k0)
  lwc1     $f27, 240($k0)
  lwc1     $f28, 244($k0)
  lwc1     $f29, 248($k0)
  lwc1     $f30, 252($k0)
  lwc1     $f31, 256($k0)
  1:
  
  // restore k0 and k1 (optional)
  lw     $k1, 128($k0)
  lw     $k0, 124($k0)
  
  .set pop
  jr     $ra
  nop
  
.end   meLibRestoreContext

//.equ CTX_BASE, (0xA8300000 + 0x28)
.equ CTX_BASE, (0xbfc00000 + 0x110)


