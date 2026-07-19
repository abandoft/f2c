program bit_intrinsics
  implicit none
  integer(1) :: i8
  integer(2) :: i16
  integer(4) :: i32
  integer(8) :: i64
  integer(4) :: values(4)
  logical :: flags(4)

  i8 = ibset(0_1, 7)
  if (.not. btest(i8, 7)) error stop 1
  if (ibclr(i8, 7) /= 0_1) error stop 2
  if (ibits(-1_1, 4, 4) /= 15_1) error stop 3
  if (not(0_1) /= -1_1) error stop 4

  i16 = iand(-1_2, 255_2)
  if (i16 /= 255_2) error stop 5
  i16 = ior(256_2, 3_2)
  if (i16 /= 259_2) error stop 6
  i16 = ieor(255_2, 15_2)
  if (i16 /= 240_2) error stop 7

  i32 = ishft(-1_4, -32)
  if (i32 /= 0_4) error stop 8
  i32 = ishft(1_4, 31)
  if (.not. btest(i32, 31)) error stop 9

  i64 = ishftc(1_8, -1)
  if (.not. btest(i64, 63)) error stop 10
  if (ishftc(-16_1, 1, 4) /= -16_1) error stop 11
  if (bit_size(i8) /= 8_1 .or. bit_size(i16) /= 16_2 .or. &
      bit_size(i32) /= 32_4 .or. bit_size(i64) /= 64_8) error stop 12

  values = [1_4, 2_4, 4_4, 8_4]
  values = ior(values, [16_4, 32_4, 64_4, 128_4])
  flags = btest(values, [4, 5, 6, 7])
  if (.not. all(flags)) error stop 13
  values = ieor(values, [17_4, 34_4, 68_4, 136_4])
  if (any(values /= 0_4)) error stop 14

  print '(A,1X,4(I0,1X))', 'BITS', bit_size(i8), bit_size(i16), &
    bit_size(i32), bit_size(i64)
end program bit_intrinsics
