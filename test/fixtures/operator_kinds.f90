program operator_kinds
  implicit none
  integer(kind=1) :: i1
  integer(kind=2) :: i2
  integer :: i4
  integer(kind=8) :: i8, exponent, values(3)
  integer(kind=1) :: narrow_matrix(2,2)
  integer(kind=8) :: wide_matrix(2,2), integer_product(2,2)
  real(kind=4) :: r4
  real(kind=4) :: real_matrix(2,2), real_product(2,2)
  real(kind=8) :: r8
  integer :: complex_calls
  complex(kind=4) :: c4
  complex(kind=8) :: c8
  complex(kind=4) :: narrow_complex(2,2)
  complex(kind=8) :: wide_complex(2,2), complex_product(2,2)
  logical(kind=1) :: l1
  logical(kind=2) :: l2
  logical(kind=8) :: l8
  logical(kind=1) :: narrow_flags(3)
  logical(kind=8) :: wide_flags(3)
  integer :: base_calls = 0, exponent_calls = 0
  real(kind=4), parameter :: decimal_power = 10.0_4 ** (-20_8)
  integer(kind=8), parameter :: exact = 3_8 ** 39_8
  integer(kind=1), parameter :: narrow_value = (-2_1) ** 6_1
  real(kind=8), parameter :: odd_power = (-1.0_8) ** 9007199254740993_8
  real(kind=4), parameter :: odd_single = (-1.0_4) ** 16777217_8
  complex(kind=8), parameter :: odd_complex = (-1.0_8,0.0_8) ** 9007199254740993_8

  if (exact /= 4052555153018976267_8 .or. narrow_value /= 64_1) stop 1
  if (abs(odd_power + 1.0_8) > 0.0_8 .or. abs(odd_single + 1.0_4) > 0.0_4) stop 2
  if (abs(odd_complex - (-1.0_8,0.0_8)) > 0.0_8) stop 3
  if (kind(2_1 ** 7_8) /= 8 .or. kind(2.0_4 ** 3_8) /= 4) stop 4
  if (kind(2_8 ** 0.5_8) /= 8 .or. kind((1.0_4,1.0_4) + 1_8) /= 4) stop 5

  i1 = -2_1
  i2 = 3_2
  i4 = 4
  i8 = 3_8
  exponent = 39_8
  if (i8 ** exponent /= exact) stop 6
  if (i1 ** 6_1 /= narrow_value) stop 7
  if (i1 ** 7_8 /= -128_8 .or. 2_1 ** 7_8 /= 128_8) stop 8
  if (kind(i1 + i2) /= 2 .or. kind(i1 * i4) /= 4) stop 9
  i8 = -2_8
  exponent = 62_8
  if (i8 ** exponent /= 4611686018427387904_8) stop 10
  i8 = -huge(0_8)
  exponent = 1_8
  if (i8 ** exponent /= -huge(0_8)) stop 11
  exponent = -huge(0_8)
  i8 = -1_8
  if (i8 ** exponent /= -1_8) stop 12
  exponent = -huge(0_8) + 1_8
  if (i8 ** exponent /= 1_8) stop 13
  i8 = -2_8
  if (i8 ** exponent /= 0_8) stop 14
  exponent = 0_8
  if (i8 ** exponent /= 1_8) stop 15

  i8 = 2_8
  r8 = 0.5_8
  if (abs(i8 ** r8 - sqrt(2.0_8)) > 1.0e-14_8) stop 16
  r4 = -1.0_4
  r8 = -1.0_8
  exponent = 9007199254740993_8
  if (abs(r4 ** exponent + 1.0_4) > 0.0_4) stop 17
  if (abs(r8 ** exponent + 1.0_8) > 0.0_8) stop 18
  r8 = 2.0_8
  exponent = -1074_8
  r8 = r8 ** exponent
  if (r8 <= 0.0_8 .or. abs(r8 / tiny(0.0_8) - epsilon(0.0_8)) > 0.0_8) stop 19
  c4 = (-1.0_4,0.0_4)
  c8 = (-1.0_8,0.0_8)
  exponent = 9007199254740993_8
  if (abs(c4 ** exponent - (-1.0_4,0.0_4)) > 0.0_4) stop 20
  if (abs(c8 ** exponent - (-1.0_8,0.0_8)) > 0.0_8) stop 21
  c8 = (2.0_8,0.0_8)
  exponent = -1024_8
  c8 = c8 ** exponent
  if (abs(real(c8,8) / tiny(0.0_8) - 0.25_8) > 0.0_8) stop 22
  c8 = i8 ** (0.5_8,0.0_8)
  if (abs(c8 - cmplx(sqrt(2.0_8),0.0_8,8)) > 1.0e-14_8) stop 23
  c8 = (2.0_8,3.0_8)
  c4 = (1.0_4,-1.0_4)
  if (abs(c4 * c8 - (5.0_8,1.0_8)) > 1.0e-14_8) stop 24
  if (abs((c4 + i8) - (3.0_4,-1.0_4)) > 1.0e-6_4) stop 25
  if (abs(-c8 - (-2.0_8,-3.0_8)) > 1.0e-14_8) stop 26

  values = [3_8, -2_8, -1_8]
  values = values ** [39_8, 62_8, 9007199254740993_8]
  if (any(values /= [exact, 4611686018427387904_8, -1_8])) stop 27
  if (base_value() ** exponent_value() /= exact) stop 28
  if (base_calls /= 1 .or. exponent_calls /= 1) stop 29

  l1 = .true._1
  l2 = .false._2
  l8 = .true._8
  if (kind(.not. l1) /= 1 .or. kind(l1 .and. l2) /= 2) stop 30
  if (kind(l1 .eqv. l8) /= 8 .or. .not. (l1 .eqv. l8)) stop 31
  if (.false. .eqv. .true. .or. .true.) stop 32
  if (.not. (.true. .neqv. .false. .or. .true. .and. .false.)) stop 33
  if (.not. 2_8 .lt. 3_1 .or. .false.) stop 34
  if (-2 * 3 ** 2 ** 2 + 1 /= -161) stop 35
  if ('a' // 'b' .ne. 'ab') stop 36
  narrow_flags = [.true._1, .false._1, .true._1]
  wide_flags = [.true._8, .false._8, .true._8]
  if (.not. all(narrow_flags .eqv. narrow_flags)) stop 37
  if (count(wide_flags .eqv. wide_flags) /= 3) stop 38
  if (any(narrow_flags .neqv. wide_flags)) stop 39
  if (any(wide_flags .neqv. wide_flags)) stop 40
  narrow_matrix = reshape([1_1,0_1,0_1,1_1],[2,2])
  wide_matrix = reshape([4294967296_8,1_8,2_8,3_8],[2,2])
  integer_product = matmul(narrow_matrix,wide_matrix)
  if (any(integer_product /= wide_matrix)) stop 41
  real_matrix = reshape([1.0_4,0.0_4,0.0_4,1.0_4],[2,2])
  real_product = matmul(real_matrix,wide_matrix)
  if (any(real_product /= real(wide_matrix,4))) stop 42
  narrow_complex = (0.0_4,0.0_4)
  narrow_complex(1,1) = (1.0_4,0.0_4)
  narrow_complex(2,2) = (1.0_4,0.0_4)
  wide_complex = (1.25_8,2.5_8)
  complex_product = matmul(narrow_complex,wide_complex)
  if (any(abs(complex_product - wide_complex) > 0.0_8)) stop 43
  narrow_complex = (2.0_8,3.0_8)
  if (any(narrow_complex /= cmplx(2.0_4,3.0_4))) stop 44
  wide_complex = (2.0_4,3.0_4)
  if (any(wide_complex /= cmplx(2.0_8,3.0_8,kind=8))) stop 45
  wide_complex = 7_8
  if (any(wide_complex /= cmplx(7.0_8,0.0_8,kind=8))) stop 46
  narrow_complex = -4.0_8
  if (any(narrow_complex /= cmplx(-4.0_4,0.0_4))) stop 47
  complex_calls = 0
  narrow_complex = broadcast_value()
  if (complex_calls /= 1) stop 48
  if (any(narrow_complex /= cmplx(5.0_4,6.0_4))) stop 49
  if (decimal_power /= 1.0e-20_4) stop 50
  r4 = 10.0_4
  exponent = -20_8
  ! Native runtime integer powers can round differently from constant folding.
  ! The independent C ABI client additionally checks f2c's exact reference value.
  if (abs(r4 ** exponent - decimal_power) > 16.0_4*epsilon(r4)*decimal_power) stop 51
  r4 = -1.0_4
  exponent = huge(0_8)
  if (r4 ** exponent /= -1.0_4) stop 52
  r8 = -1.0_8
  if (r8 ** exponent /= -1.0_8) stop 53
  write(*,'(A)') 'operator kinds passed'

contains
  function broadcast_value() result(value)
    complex(kind=8) :: value
    complex_calls = complex_calls + 1
    value = (5.0_8,6.0_8)
  end function
  function base_value() result(value)
    integer(kind=8) :: value
    base_calls = base_calls + 1
    value = 3_8
  end function
  function exponent_value() result(value)
    integer(kind=8) :: value
    exponent_calls = exponent_calls + 1
    value = 39_8
  end function
end program
