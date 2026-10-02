program volatile_reductions
  implicit none
  integer(kind=1), volatile :: small(4)
  integer(kind=2), volatile :: medium(4)
  integer(kind=4), volatile :: values(4)
  integer(kind=8), volatile :: wide(4)
  real, volatile :: reals(4)
  real(kind=8), volatile :: doubles(4)
  complex, volatile :: complexes(2)
  complex(kind=8), volatile :: wide_complexes(2)
  logical(kind=1), volatile :: mask1(4)
  logical(kind=2), volatile :: mask2(4)
  logical(kind=4), volatile :: mask4(4)
  logical(kind=8), volatile :: mask8(4)
  integer :: ordinary(4), columns(2), location(2)
  integer, asynchronous :: pending(4)
  integer, volatile :: matrix(2,2), empty(0)
  logical, volatile :: empty_flags(0)
  character(len=3), volatile :: words(4)
  logical(kind=1) :: compact_plain(4)
  logical(kind=8) :: wide_plain(4)
  integer(kind=1) :: small_plain(4)
  real :: real_plain(4)
  complex :: complex_plain(4)
  real, volatile :: f2c_copy_index_0(2)

  small = [1_1, 3_1, 3_1, 2_1]
  medium = [1_2, 3_2, 3_2, 2_2]
  values = [1, 3, 3, 2]
  wide = [1_8, 3_8, 3_8, 2_8]
  reals = [1.0, 3.0, 3.0, 2.0]
  doubles = [1.0_8, 3.0_8, 3.0_8, 2.0_8]
  complexes = [(1.0,2.0), (3.0,4.0)]
  wide_complexes = [(1.0_8,2.0_8), (3.0_8,4.0_8)]
  mask1 = [.false., .true., .true., .false.]
  mask2 = mask1
  mask4 = mask1
  mask8 = mask1
  ordinary = [2, 2, 2, 2]
  pending = values
  matrix = reshape(values, [2,2])
  words = ['ab ', 'bc ', 'aa ', 'zz ']
  compact_plain = [.false., .true., .false., .true.]
  wide_plain = compact_plain
  compact_plain = wide_plain
  small_plain = ordinary
  real_plain = ordinary
  complex_plain = real_plain

  if (sum(small) /= 9_1 .or. product(medium) /= 18_2) stop 1
  if (sum(values) /= 9 .or. sum(wide) /= 9_8) stop 2
  if (sum(reals) /= 9.0 .or. product(doubles) /= 18.0_8) stop 3
  if (maxval(values) /= 3 .or. minval(wide) /= 1_8) stop 4
  if (sum(values(4:1:-1)) /= 9) stop 5
  if (product(values(1:4:2)) /= 3) stop 6
  if (sum(values, mask=mask1) /= 6) stop 7
  if (sum(ordinary, mask=mask8) /= 4) stop 8
  if (product(values, mask=mask2) /= 9) stop 9
  if (maxval(values, mask=mask4) /= 3) stop 10
  if (minval(values, mask=mask8) /= 3) stop 11
  if (maxloc(values, dim=1, mask=mask1) /= 2) stop 12
  if (maxloc(values, dim=1, mask=mask8, back=.true., kind=8) /= 3_8) stop 13
  if (minloc(values, dim=1, mask=mask2, back=.true.) /= 3) stop 14
  if (any(mask1) .neqv. .true.) stop 15
  if (all(mask2)) stop 16
  if (count(mask4) /= 2 .or. count(mask8, kind=1) /= 2_1) stop 17
  if (dot_product(values, ordinary) /= 18) stop 18
  if (dot_product(wide, reals) /= 23.0) stop 19
  if (dot_product(reals, doubles) /= 23.0_8) stop 20
  if (.not. dot_product(mask1, mask8)) stop 21
  if (sum(complexes) /= (4.0,6.0)) stop 22
  if (product(wide_complexes) /= (-5.0_8,10.0_8)) stop 23
  if (dot_product(complexes, complexes) /= (30.0,0.0)) stop 24
  if (dot_product(complexes, wide_complexes) /= (30.0_8,0.0_8)) stop 25
  if (any(values < 0) .or. .not. all(values >= 1)) stop 26
  if (count(values == 3) /= 2) stop 27
  if (count(complexes == (1.0,2.0)) /= 1) stop 28
  if (sum(pending) /= 9) stop 29
  if (sum(empty) /= 0 .or. product(empty) /= 1) stop 30
  if (any(empty_flags) .or. .not. all(empty_flags)) stop 31
  if (count(empty_flags) /= 0) stop 32
  columns = sum(matrix, dim=1)
  if (any(columns /= [4,5])) stop 33
  location = maxloc(matrix, back=.true.)
  if (any(location /= [1,2])) stop 34
  if (count(words < 'bc') /= 2) stop 35
  if (.not. any(words == 'zz')) stop 36
  if (.not. all(words >= 'aa')) stop 37
  if (count(['ab', 'zz'] < words(2)) /= 1) stop 38
  if (count([words(1), words(4)] >= 'bc') /= 1) stop 41
  if (count(wide_plain) /= 2 .or. count(compact_plain) /= 2) stop 42
  if (sum(small_plain) /= 8_1) stop 43
  if (sum(real_plain) /= 8.0 .or. sum(complex_plain) /= (8.0,0.0)) stop 44
  f2c_copy_index_0 = [1.0,2.0]
  if (sum(f2c_copy_index_0) /= 3.0) stop 45
  words(1)(2:3) = words(1)(1:2)
  if (words(1) /= 'aab') stop 46
  words(2) = 'x'
  if (words(2) /= 'x  ') stop 47
  call check_dummy(values, mask4)
  call f2c_copy_index_1()
  print '(A)', 'volatile reduction differential passed'
contains
  subroutine f2c_copy_index_1()
    if (sum(f2c_copy_index_0) /= 3.0) stop 48
  end subroutine f2c_copy_index_1

  subroutine check_dummy(input, selected)
    integer, volatile, intent(inout) :: input(4)
    logical, volatile, intent(inout) :: selected(4)
    if (sum(input, mask=selected) /= 6) stop 39
    if (count(input > 2) /= 2) stop 40
  end subroutine check_dummy
end program volatile_reductions
