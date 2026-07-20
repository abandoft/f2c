program numeric_operation_intrinsics
  implicit none

  type :: pair
    integer :: first
    integer :: second
    integer, allocatable :: payload(:)
  end type pair

  integer(kind=1) :: i1, p1, rounded1
  integer(kind=2) :: i2, p2, rounded2
  integer(kind=4) :: i4, p4, rounded4
  integer(kind=8) :: i8, p8, rounded8
  integer :: integer_values(4), integer_divisors(4), integer_results(4)
  real :: r4, p4_real, zero4, negative_zero4, real_values(4), real_results(4)
  double precision :: r8, p8_real
  logical :: choose, masks(4), logical_values(4), logical_results(4)
  character(len=3) :: true_words(4), false_words(4), selected_words(4)
  type(pair) :: first_pair, second_pair, selected_pair
  type(pair) :: first_pairs(4), second_pairs(4), selected_pairs(4)
  character(len=2), parameter :: selected_constant = &
    merge(mask=.false., fsource='NO', tsource='OK')
  integer(kind=8), parameter :: rounded_constant = nint(a=-1.5d0, kind=8)

  if (selected_constant /= 'NO' .or. rounded_constant /= -2_8) error stop 1

  i1 = -17_1
  p1 = 3_1
  if (mod(i1, p1) /= -2_1 .or. modulo(i1, p1) /= 1_1) error stop 2
  i1 = ibset(0_1, bit_size(i1) - 1)
  p1 = -1_1
  if (mod(i1, p1) /= 0_1 .or. sign(i1, p1) /= i1) error stop 3

  i2 = 30000_2
  p2 = -123_2
  if (dim(i2, p2) /= 30123_2 .or. modulo(i2, p2) /= -12_2) error stop 4
  i4 = -2000000000_4
  p4 = 17_4
  if (mod(i4, p4) /= -14_4 .or. sign(i4, p4) /= 2000000000_4) error stop 5
  i8 = 5000000000_8
  p8 = -3000000001_8
  if (dim(i8, p8) /= 8000000001_8 .or. modulo(i8, p8) /= -1000000002_8) error stop 6
  if (abs(-120_1) /= 120_1 .or. abs(-30000_2) /= 30000_2 .or. &
      abs(-2000000000_4) /= 2000000000_4 .or. &
      abs(-5000000000_8) /= 5000000000_8) error stop 35

  r4 = -3.75
  p4_real = 2.5
  if (abs(aint(r4) + 3.0) > epsilon(r4)) error stop 7
  if (abs(anint(r4) + 4.0) > epsilon(r4)) error stop 8
  if (abs(dim(r4, p4_real)) > epsilon(r4)) error stop 9
  if (abs(mod(r4, p4_real) + 1.25) > epsilon(r4)) error stop 10
  if (abs(modulo(r4, p4_real) - 1.25) > epsilon(r4)) error stop 11
  if (abs(sign(r4, p4_real) - 3.75) > epsilon(r4)) error stop 12

  r8 = 6.5d0
  p8_real = -4.0d0
  if (abs(dim(r8, p8_real) - 10.5d0) > epsilon(r8)) error stop 13
  if (abs(mod(r8, p8_real) - 2.5d0) > epsilon(r8)) error stop 14
  if (abs(modulo(r8, p8_real) + 1.5d0) > epsilon(r8)) error stop 15
  if (abs(sign(r8, p8_real) + 6.5d0) > epsilon(r8)) error stop 16

  rounded1 = ceiling(1.25, kind=1)
  rounded2 = floor(a=-1.25d0, kind=2)
  rounded4 = nint(-1.5)
  rounded8 = nint(kind=8, a=2147483648.25d0)
  if (rounded1 /= 2_1 .or. rounded2 /= -2_2 .or. rounded4 /= -2_4 .or. &
      rounded8 /= 2147483648_8) error stop 17
  if (abs(aint(a=1.99999999d0, kind=4) - 1.0) > epsilon(r4)) error stop 18
  if (abs(anint(kind=8, a=-2.5) + 3.0d0) > epsilon(r8)) error stop 19

  zero4 = 0.0
  negative_zero4 = -zero4
  if (1.0 / sign(zero4, negative_zero4) > 0.0) error stop 20
  r4 = 6.0
  p4_real = -3.0
  zero4 = modulo(r4, p4_real)
  if (1.0 / zero4 > 0.0) error stop 21

  integer_values = [-17, 17, -20, 20]
  integer_divisors = [3, -3, -6, 6]
  integer_results = modulo(a=integer_values, p=integer_divisors)
  if (any(integer_results /= [1, -1, -2, 2])) error stop 22
  masks = [.true., .false., .true., .false.]
  integer_results = merge(mask=masks, fsource=integer_divisors, tsource=integer_values)
  if (any(integer_results /= [-17, -3, -20, 6])) error stop 23

  real_values = [-3.75, 3.75, -6.5, 6.5]
  real_results = anint(real_values)
  if (real_results(1) /= -4.0 .or. real_results(2) /= 4.0 .or. &
      real_results(3) /= -7.0 .or. real_results(4) /= 7.0) error stop 24
  real_results = modulo(real_values, 2.5)
  if (real_results(1) /= 1.25 .or. real_results(2) /= 1.25 .or. &
      real_results(3) /= 1.0 .or. real_results(4) /= 1.5) error stop 25

  logical_values = [.true., .true., .false., .false.]
  logical_results = merge(logical_values, .false., masks)
  if (any(logical_results .neqv. [.true., .false., .false., .false.])) error stop 26
  true_words = ['ONE', 'TWO', 'RED', 'HOT']
  false_words = ['NO ', 'YES', 'BLU', 'ICE']
  selected_words = merge(mask=masks, fsource=false_words, tsource=true_words)
  if (any(selected_words /= ['ONE', 'YES', 'RED', 'ICE'])) error stop 27

  first_pair%first = 1
  first_pair%second = 2
  second_pair%first = 3
  second_pair%second = 4
  allocate(first_pair%payload(2), second_pair%payload(2))
  first_pair%payload = [10, 20]
  second_pair%payload = [30, 40]
  choose = .false.
  selected_pair = merge(first_pair, second_pair, choose)
  first_pairs = first_pair
  second_pairs = second_pair
  selected_pairs = [first_pair, second_pair, first_pair, second_pair]
  if (selected_pairs(1)%payload(2) /= 20 .or. &
      selected_pairs(2)%payload(2) /= 40) error stop 29
  selected_pairs = merge(first_pairs, second_pairs, masks)
  if (selected_pair%first /= 3 .or. selected_pair%second /= 4 .or. &
      selected_pair%payload(1) /= 30) error stop 28
  if (selected_pairs(1)%first /= 1 .or. selected_pairs(2)%first /= 3 .or. &
      selected_pairs(3)%second /= 2 .or. selected_pairs(4)%second /= 4 .or. &
      selected_pairs(1)%payload(1) /= 10 .or. selected_pairs(2)%payload(1) /= 30) error stop 30
  first_pair%payload(1) = -10
  second_pair%payload(1) = -30
  first_pairs(1)%payload(1) = -100
  second_pairs(2)%payload(1) = -300

  choose = .true.
  selected_pair = merge(make_pair(5, 6, 50), make_pair(7, 8, 70), choose)
  if (selected_pair%first /= 5 .or. selected_pair%second /= 6 .or. &
      selected_pair%payload(1) /= 50 .or. selected_pair%payload(2) /= 51) error stop 31
  choose = .false.
  selected_pair = merge(first_pair, &
    merge(make_pair(9, 10, 90), make_pair(11, 12, 110), .false.), choose)
  if (selected_pair%first /= 11 .or. selected_pair%second /= 12 .or. &
      selected_pair%payload(1) /= 110 .or. selected_pair%payload(2) /= 111) error stop 32
  selected_pairs = merge(make_pair(13, 14, 130), second_pairs, masks)
  if (selected_pairs(1)%first /= 13 .or. selected_pairs(2)%first /= 3 .or. &
      selected_pairs(3)%second /= 14 .or. selected_pairs(4)%second /= 4 .or. &
      selected_pairs(1)%payload(1) /= 130 .or. selected_pairs(1)%payload(2) /= 131 .or. &
      selected_pairs(3)%payload(1) /= 130 .or. &
      selected_pairs(3)%payload(2) /= 131) error stop 33
  r8 = dint(r8) + dnint(r8) + ddim(r8, 1.0d0) + dmod(r8, 2.0d0) + dsign(r8, -1.0d0)
  rounded4 = idnint(r8) + idim(rounded4, 1) + isign(rounded4, -1)
  r4 = amod(r4, p4_real)
  if (r8 > huge(r8) .or. rounded4 > huge(rounded4) .or. r4 > huge(r4)) error stop 34

  print '(A)', 'NUMERIC-OPERATION-PASS'

contains

  function make_pair(first, second, payload_base) result(value)
    integer, intent(in) :: first, second, payload_base
    type(pair) :: value

    value%first = first
    value%second = second
    value%payload = [payload_base, payload_base + 1]
  end function make_pair
end program numeric_operation_intrinsics
