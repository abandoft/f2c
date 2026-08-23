program transfer_intrinsic
  implicit none

  type :: pair
    integer :: first
    integer :: second
  end type pair

  real :: reals(4)
  complex, allocatable :: complexes(:)
  integer :: scalar_bits
  integer :: scalar_roundtrip
  real :: converted_scalar
  integer :: mold_calls
  integer :: size_calls
  integer :: source_calls
  integer(kind=1) :: integer_one
  integer(kind=2) :: integer_two
  integer(kind=8) :: integer_eight
  integer(kind=8), allocatable :: wide_integer_result(:)
  integer(kind=8) :: complex_words(2)
  complex(kind=8) :: wide_complex
  complex(kind=8) :: wide_complex_roundtrip
  complex :: compact_complex(1)
  complex(kind=8), allocatable :: converted_complex(:)
  logical :: logical_roundtrip
  logical(kind=1) :: logical_one
  logical(kind=2) :: logical_two
  logical(kind=8) :: logical_eight
  logical(kind=1) :: logical_source(2)
  logical(kind=1) :: logical_mold(1)
  logical(kind=8), allocatable :: logical_target(:)
  character(len=4) :: word
  character(len=:), allocatable :: dynamic_word
  character(len=2) :: character_source(2)
  character(len=3) :: character_target(2)
  character(len=:), allocatable :: dynamic_character_target(:)
  type(pair) :: original
  type(pair) :: copied
  type(pair), allocatable :: derived_source(:)
  type(pair), allocatable :: derived_target(:)
  integer, allocatable :: integer_result(:)
  real, allocatable :: converted_result(:)
  integer, allocatable :: empty_result(:)

  mold_calls = 0
  size_calls = 0
  source_calls = 0
  reals = [1.0, 2.0, 3.0, 4.0]

  scalar_bits = transfer(mold=scalar_bits, source=1.0)
  scalar_roundtrip = 0
  if (abs(transfer(scalar_bits, 0.0) - 1.0) > epsilon(1.0)) stop 1

  scalar_bits = transfer(reals, scalar_bits)
  if (abs(transfer(scalar_bits, 0.0) - 1.0) > epsilon(1.0)) stop 2
  scalar_bits = transfer(reals(4:1:-1), scalar_bits)
  if (abs(transfer(scalar_bits, 0.0) - 4.0) > epsilon(1.0)) stop 41
  converted_scalar = transfer([5], 0)
  if (abs(converted_scalar - 5.0) > epsilon(1.0)) stop 42

  complexes = transfer(reals(4:1:-1), [(0.0, 0.0)])
  if (size(complexes) /= 2) stop 3
  if (abs(real(complexes(1)) - 4.0) > epsilon(1.0)) stop 4
  if (abs(aimag(complexes(1)) - 3.0) > epsilon(1.0)) stop 5
  if (abs(real(complexes(2)) - 2.0) > epsilon(1.0)) stop 6
  if (abs(aimag(complexes(2)) - 1.0) > epsilon(1.0)) stop 7

  complexes = transfer(size=requested_size(), mold=[(0.0, 0.0)], source=source_values())
  if (source_calls /= 1 .or. size_calls /= 1) stop 8
  if (size(complexes) /= 2) stop 9
  if (abs(real(complexes(1)) - 1.0) > epsilon(1.0)) stop 10
  if (abs(aimag(complexes(1)) - 2.0) > epsilon(1.0)) stop 11

  scalar_bits = transfer(1.0, mold_value())
  if (mold_calls /= 0) stop 12

  integer_result = transfer(reals, mold_array())
  if (size(integer_result) /= 4) stop 13
  converted_result = transfer([1, 2], mold_array())
  if (size(converted_result) /= 2) stop 28
  if (abs(converted_result(1) - 1.0) > epsilon(1.0)) stop 29
  if (abs(converted_result(2) - 2.0) > epsilon(1.0)) stop 30

  word = transfer(scalar_bits, word)
  scalar_roundtrip = transfer(word, scalar_roundtrip)
  if (scalar_roundtrip /= scalar_bits) stop 14

  allocate(character(len=5) :: dynamic_word)
  dynamic_word = transfer(scalar_bits, dynamic_word)
  scalar_roundtrip = transfer(dynamic_word, scalar_roundtrip)
  if (len(dynamic_word) /= 5 .or. scalar_roundtrip /= scalar_bits) stop 15

  character_source = ['ab', 'cd']
  character_target = transfer(character_source, ['xx'])
  if (character_target(1) /= 'ab ') stop 16
  if (character_target(2) /= 'cd ') stop 17
  dynamic_character_target = transfer(character_source, ['x'])
  if (len(dynamic_character_target) /= 1 .or. size(dynamic_character_target) /= 4) stop 32
  if (any(dynamic_character_target /= ['a', 'b', 'c', 'd'])) stop 33

  scalar_bits = transfer(.true., scalar_bits)
  logical_roundtrip = transfer(scalar_bits, logical_roundtrip)
  if (.not. logical_roundtrip) stop 18
  logical_one = .true.
  logical_two = .true.
  logical_eight = .true.
  logical_one = transfer(logical_one, logical_one)
  logical_two = transfer(logical_two, logical_two)
  logical_eight = transfer(logical_eight, logical_eight)
  if (.not. logical_one .or. .not. logical_two .or. .not. logical_eight) stop 31
  logical_source = [.true., .false.]
  logical_mold = .false.
  logical_target = transfer(logical_source, logical_mold)
  if (size(logical_target) /= 2) stop 43
  if (.not. logical_target(1) .or. logical_target(2)) stop 44
  logical_target = transfer(logical_source, [logical_one])
  if (size(logical_target) /= 2) stop 45
  if (.not. logical_target(1) .or. logical_target(2)) stop 46

  integer_one = -7
  integer_one = transfer(integer_one, integer_one)
  if (integer_one /= -7) stop 19
  integer_two = -257
  integer_two = transfer(integer_two, integer_two)
  if (integer_two /= -257) stop 20
  integer_eight = 123456789_8
  integer_eight = transfer(integer_eight, integer_eight)
  if (integer_eight /= 123456789_8) stop 21
  wide_integer_result = transfer([3, 4], [0])
  if (size(wide_integer_result) /= 2) stop 34
  if (any(wide_integer_result /= [3_8, 4_8])) stop 35

  wide_complex = cmplx(2.5_8, -3.5_8, kind=8)
  complex_words = transfer(wide_complex, complex_words)
  wide_complex_roundtrip = transfer(complex_words, wide_complex_roundtrip)
  if (abs(real(wide_complex_roundtrip, kind=8) - 2.5_8) > epsilon(2.5_8)) stop 22
  if (abs(aimag(wide_complex_roundtrip) + 3.5_8) > epsilon(3.5_8)) stop 23
  compact_complex(1) = cmplx(1.25, -2.5)
  converted_complex = transfer(compact_complex, [(0.0, 0.0)])
  if (abs(real(converted_complex(1), kind=8) - 1.25_8) > epsilon(1.25_8)) stop 36
  if (abs(aimag(converted_complex(1)) + 2.5_8) > epsilon(2.5_8)) stop 37

  empty_result = transfer(reals, [0], 0)
  if (size(empty_result) /= 0) stop 24

  original%first = 7
  original%second = 11
  copied = transfer(original, copied)
  original%first = 99
  if (copied%first /= 7 .or. copied%second /= 11) stop 25

  allocate(derived_source(1))
  derived_source(1)%first = 13
  derived_source(1)%second = 17
  derived_target = transfer(derived_source, derived_source)
  derived_source(1)%first = 101
  if (size(derived_target) /= 1) stop 26
  if (derived_target(1)%first /= 13 .or. derived_target(1)%second /= 17) stop 27

  write(*, '(A)') 'TRANSFER OK'

contains

  function mold_value() result(value)
    integer :: value
    mold_calls = mold_calls + 1
    value = 0
  end function mold_value

  pure function mold_array() result(values)
    integer :: values(2)
    values = 0
  end function mold_array

  function requested_size() result(value)
    integer :: value
    size_calls = size_calls + 1
    value = 2
  end function requested_size

  function source_values() result(values)
    real :: values(4)
    source_calls = source_calls + 1
    values = [1.0, 2.0, 3.0, 4.0]
  end function source_values

end program transfer_intrinsic
