module parameter_tables
  use iso_fortran_env, only: int64, real64
  implicit none
  integer(int64), parameter :: offsets(-1:1) = [4294967296_int64, 4294967297_int64, 4294967298_int64]
  real(real64), parameter :: scales(2) = [1.0000000000000002_real64, -2.0000000000000004_real64]
end module

program parameter_array
  use iso_fortran_env, only: int8, int16, int32, int64, real32, real64
  use parameter_tables, only: positions => offsets, factors => scales
  implicit none
  type :: pair
    integer :: code
    integer :: real64 = 99
    real(kind=real32 + 4) :: value
  end type
  integer(int8), parameter :: tiny(3) = [-127_int8, 0_int8, 127_int8]
  integer(int16), parameter :: small(3) = [-32767_int16, 0_int16, 32767_int16]
  integer(int32), parameter :: ordinary(3) = [-2147483647_int32, 0_int32, 2147483647_int32]
  integer(int64), parameter :: wide(2) = [4294967296_int64, 4294967297_int64]
  integer(int64), parameter :: expected(3) = [-32894_int64, 0_int64, 32894_int64]
  real(real32), parameter :: single(2) = [0.5_real32, -0.25_real32]
  real(real64), parameter :: precise(2) = [1.0000000000000002_real64, -2.0000000000000004_real64]
  complex(real64), parameter :: points(2) = [(1.0000000000000002_real64, -0.25_real64), &
                                           (-2.0000000000000004_real64, 0.5_real64)]
  logical, parameter :: truth(3) = [.true., .false., .true.]
  character(len=4), parameter :: words(3) = ['A ', 'BC', 'DE']
  character, parameter :: letters(2) = ['X', 'Y']
  character(len=0), parameter :: empty_text(2) = ['', '']
  integer, parameter :: empty_values(0) = 0
  type(pair), parameter :: pairs(2) = [pair(code=7, value=0.5_real64), &
                                     pair(code=11, value=-0.25_real64)]
  integer :: index
  real(real64) :: collected
  integer(int64) :: copied(2)

  do index = 1, 3
    if (int(tiny(index), int64) + int(small(index), int64) /= &
        expected(index)) stop 1
  end do
  if (ordinary(3) /= 2147483647_int32 .or. any(wide /= [4294967296_int64, 4294967297_int64])) stop 2
  if (abs(sum(single) - 0.25_real32) > 1e-7) stop 3
  do index = 1, 2
    if (transfer(precise(index), 0_int64) /= transfer(factors(index), 0_int64)) stop 4
    if (transfer(real(points(index), real64), 0_int64) /= transfer(precise(index), 0_int64)) stop 5
    if (pairs(index)%code /= 4 * index + 3) stop 6
    if (pairs(index)%real64 /= 99 .or. kind(pairs(index)%value) /= real64) stop 14
    if (letters(index) /= achar(87 + index)) stop 7
  end do
  if (count(truth) /= 2 .or. size(empty_values) /= 0 .or. len(empty_text) /= 0) stop 8
  if (words(1) /= 'A   ' .or. words(3) /= 'DE  ') stop 9
  copied = wide(2:1:-1)
  if (any(copied /= [4294967297_int64, 4294967296_int64])) stop 10
  call consume(precise, collected)
  if (transfer(collected, 0_int64) /= transfer(sum(factors), 0_int64)) stop 11
  call host_copy()
  print '(A)', 'typed parameter arrays, storage kinds and host/module association passed'
contains
  subroutine consume(values, result_value)
    real(real64), intent(in) :: values(2)
    real(real64), intent(out) :: result_value
    result_value = sum(values)
  end subroutine
  subroutine host_copy()
    integer :: position
    do position = -1, 1
      if (positions(position) /= 4294967297_int64 + position) stop 12
    end do
    if (words(2) /= 'BC  ' .or. pairs(2)%code /= 11) stop 13
  end subroutine
end program
