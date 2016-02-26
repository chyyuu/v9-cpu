No.:   Addr:    Value:  Intr  Operand  (Dec Format) #Label_id #Meaning
=======================================================================
  1: 00000000: 0000080e: LL   0x000008 (D   8) # label2  # ra = uint(sp[8])
  2: 00000004: 00001026: LBL  0x000010 (D  16)           # rb = uint(sp[16])
  3: 00000008: 0000009a: BOUT 0x000000 (D   0)           # write(ra, &rb, 1)
  4: 0000000c: 00000002: LEV  0x000000 (D   0)           # sp += 0 and return 
  5: 00000010: fffff801: ENT  0xfffff8 (D  -8) # label4  # sp += 16777208

  6: 00000014: 00000123: LI   0x000001 (D   1)           # ra = uint(1)
  7: 00000018: 00008445: SG   0x000084 (D 132)           # uint(gaddr[156]) = ra
  8: 0000001c: 0000200e: LL   0x000020 (D  32)           # ra = uint(sp[32])
  9: 00000020: 00000440: SL   0x000004 (D   4)           # uint(sp[4]) = ra
 10: 00000024: 00002403: JMP  0x000024 (D  36)           # Jmp label1
 11: 00000028: 0000180e: LL   0x000018 (D  24) # label3  # ra = uint(sp[24])
 12: 0000002c: ffffff57: SUBI 0xffffff (D  -1)           # ra = ra - rb
 13: 00000030: 00001840: SL   0x000018 (D  24)           # uint(sp[24]) = ra
 14: 00000034: ffffff1f: LXC  0xffffff (D  -1)           # ra = char(ra[16777215])
 15: 00000038: 0000009d: PSHA 0x000000 (D   0)           # push ra
 16: 0000003c: 0000180e: LL   0x000018 (D  24)           # ra = uint(sp[24])
 17: 00000040: 0000009d: PSHA 0x000000 (D   0)           # push ra
 18: 00000044: ffffb805: JSR  0xffffb8 (D -72)           # Call label2
 19: 00000048: 00001001: ENT  0x000010 (D  16)           # sp += 16

 20: 0000004c: 0000040e: LL   0x000004 (D   4) # label1  # ra = uint(sp[4])
 21: 00000050: 00000157: SUBI 0x000001 (D   1)           # ra = ra - rb
 22: 00000054: 00000440: SL   0x000004 (D   4)           # uint(sp[4]) = ra
 23: 00000058: 00000154: ADDI 0x000001 (D   1)           # ra = ra + rb
 24: 0000005c: ffffc886: BNZ  0xffffc8 (D -56)           # Cond goto label3
 25: 00000060: 0000040e: LL   0x000004 (D   4)           # ra = uint(sp[4])
 26: 00000064: 00000802: LEV  0x000008 (D   8)           # sp += 8 and return 
 27: 00000068: 00000802: LEV  0x000008 (D   8)           # sp += 8 and return 
 28: 0000006c: 00000c9e: PSHI 0x00000c (D  12) # <= ENTRY            # push 12
 29: 00000070: 00001c08: LEAG 0x00001c (D  28)           # ra = pc+28
 30: 00000074: 0000009d: PSHA 0x000000 (D   0)           # push ra
 31: 00000078: 0000019e: PSHI 0x000001 (D   1)           # push 1
 32: 0000007c: ffff9005: JSR  0xffff90 (D-112)           # Call label4
 33: 00000080: 00001801: ENT  0x000018 (D  24)           # sp += 24

 34: 00000084: 00001845: SG   0x000018 (D  24)           # uint(gaddr[156]) = ra
 35: 00000088: 00000000: HALT 0x000000 (D   0)          
 36: 0000008c: 00000002: LEV  0x000000 (D   0)           # sp += 0 and return 
=======================================================================
Data Segment
Address     Hex										         | Char
0x00000090	53 31 5f 49	44 20 53 32	5f 49 44 00	00 00 00 00	 | S1_ID S2_ID.....
