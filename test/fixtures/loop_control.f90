program loop_control
  use iso_fortran_env, only: int8, int16, int32, int64
  implicit none
  integer(int8) :: small
  integer(int16) :: medium
  integer(int32) :: ordinary
  integer(int64) :: wide, inner, first, last, stride, total
  integer(int64) :: values(3), nested(4)
  integer(int8) :: small_values(12)
  integer :: calls(3), trips, unit_number, status
  character(len=128) :: record

  total = 0_int64
  do small = -100_int8, 100_int8, 17_int8
    total = total + small
  end do
  if (total /= -78_int64 .or. small /= 104_int8) stop 1
  total = 0_int64
  do medium = -30000_int16, 10000_int16, 10000_int16
    total = total + medium
  end do
  if (total /= -50000_int64 .or. medium /= 20000_int16) stop 2
  total = 0_int64
  do ordinary = -1073741823_int32, 1073741823_int32, 1073741823_int32
    total = total + ordinary
  end do
  if (total /= 0_int64 .or. ordinary /= 2147483646_int32) stop 3

  first = 4294967296_int64
  last = first + 4_int64
  stride = 2_int64
  total = 0_int64
  do wide = first, last, stride
    total = total + wide
    first = 0_int64
    last = 0_int64
    stride = 0_int64
  end do
  if (total /= 12884901894_int64 .or. wide /= 4294967302_int64) stop 4
  total = 0_int64
  do wide = -4294967296_int64, -4294967300_int64, -2_int64
    total = total + wide
  end do
  if (total /= -12884901894_int64 .or. wide /= -4294967302_int64) stop 5
  do wide = 4294967300_int64, 4294967296_int64, 1_int64
    stop 6
  end do
  if (wide /= 4294967300_int64) stop 7
  do wide = -4294967300_int64, -4294967296_int64, -1_int64
    stop 8
  end do
  if (wide /= -4294967300_int64) stop 9

  calls = 0
  total = 0_int64
  do wide = bound(1), bound(2), bound(3)
    total = total + wide
  end do
  if (any(calls /= 1) .or. total /= 12884901894_int64) stop 10
  if (wide /= 4294967302_int64) stop 11
  do small = 2_int64, 6_int64, 2_int64
    if (small < 2_int8 .or. small > 6_int8) stop 12
  end do
  if (small /= 8_int8) stop 13

  trips = 0
  outer: do wide = 4294967296_int64, 4294967300_int64, 2_int64
    do inner = 1_int64, 3_int64
      if (inner == 2_int64) cycle outer
      trips = trips + 1
    end do
  end do outer
  if (trips /= 3 .or. wide /= 4294967302_int64 .or. inner /= 2_int64) stop 14
  outer_exit: do wide = 4294967296_int64, 4294967300_int64, 2_int64
    do inner = 1_int64, 3_int64
      if (inner == 2_int64) exit outer_exit
    end do
  end do outer_exit
  if (wide /= 4294967296_int64 .or. inner /= 2_int64) stop 15
  do wide = 4294967296_int64, 4294967300_int64
    exit
  end do
  if (wide /= 4294967296_int64) stop 16

  calls = 0
  values = [(wide, wide = bound(1), bound(2), bound(3))]
  if (any(calls /= 1)) stop 17
  if (any(values /= [4294967296_int64, 4294967298_int64, 4294967300_int64])) stop 18
  ! Use dynamic bounds here: GNU Fortran 16 rejects the equivalent constant
  ! nested wide constructor. That static case has an independent C-side oracle.
  first = 4294967296_int64
  last = first + 2_int64
  stride = 2_int64
  nested = [((wide + inner, inner = 1_int64, 2_int64), wide = first, last, stride)]
  if (any(nested /= [4294967297_int64, 4294967298_int64, &
                     4294967299_int64, 4294967300_int64])) stop 19
  small_values = [(small, small = -100_int8, 100_int8, 17_int8)]
  if (sum(int(small_values, int32)) /= -78) stop 20

  calls = 0
  write(record, '(3(I12,1X))') (wide, wide = bound(1), bound(2), bound(3))
  if (any(calls /= 1) .or. wide /= 4294967302_int64) stop 21
  read(record, *) values
  if (any(values /= [4294967296_int64, 4294967298_int64, 4294967300_int64])) stop 22
  calls = 0
  write(record, *) (wide, wide = bound(1), bound(2), bound(3))
  if (any(calls /= 1) .or. wide /= 4294967302_int64) stop 23
  read(record, *) values
  if (any(values /= [4294967296_int64, 4294967298_int64, 4294967300_int64])) stop 24
  record = '21 22 23'
  first = 4294967296_int64
  read(record, *) (values(wide-first+1_int64), wide = first, first+2_int64)
  if (any(values /= [21_int64, 22_int64, 23_int64]) .or. wide /= first+3_int64) stop 25
  write(record, '(I12)') (wide, wide = first+2_int64, first, 1_int64)
  if (wide /= first+2_int64 .or. len_trim(record) /= 0) stop 26

  unit_number = 41
  open(unit=unit_number, status='scratch', form='unformatted', iostat=status)
  if (status /= 0) stop 27
  calls = 0
  write(unit_number, iostat=status) (wide, wide = bound(1), bound(2), bound(3))
  if (status /= 0 .or. any(calls /= 1) .or. wide /= 4294967302_int64) stop 28
  rewind(unit_number)
  read(unit_number, iostat=status) values
  if (status /= 0 .or. any(values /= &
       [4294967296_int64, 4294967298_int64, 4294967300_int64])) stop 29
  rewind(unit_number)
  read(unit_number, iostat=status) (values(wide-first+1_int64), wide = first, first+2_int64)
  if (status /= 0 .or. wide /= first+3_int64) stop 30
  close(unit_number)
  print '(A)', 'loop control semantics passed'
contains
  function bound(which) result(value)
    integer, intent(in) :: which
    integer(int64) :: value
    calls(which) = calls(which) + 1
    select case (which)
    case (1)
      value = 4294967296_int64
    case (2)
      value = 4294967300_int64
    case default
      value = 2_int64
    end select
  end function
end program
