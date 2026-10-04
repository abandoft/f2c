program loop_result_bounds
  use iso_fortran_env, only: int64
  implicit none
  integer(int64) :: iterator, total, values(3)
  integer :: calls(3)
  character(len=128) :: record
  calls = 0
  total = 0_int64
  do iterator = sum(bound(1)), sum(bound(2)), sum(bound(3))
    if (any(calls /= 1)) stop 1
    total = total + iterator
  end do
  if (total /= 12884901891_int64 .or. iterator /= 4294967299_int64) stop 2
  calls = 0
  values = [(iterator, iterator = sum(bound(1)), sum(bound(2)), sum(bound(3)))]
  if (any(calls /= 1)) stop 3
  if (any(values /= [4294967296_int64, 4294967297_int64, 4294967298_int64])) stop 4
  calls = 0
  write(record, '(3(I12,1X))') &
    (iterator, iterator = sum(bound(1)), sum(bound(2)), sum(bound(3)))
  if (any(calls /= 1) .or. iterator /= 4294967299_int64) stop 5
  read(record, *) values
  if (any(values /= [4294967296_int64, 4294967297_int64, 4294967298_int64])) stop 6
  calls = 0
  write(record, *) (iterator, iterator = sum(bound(1)), sum(bound(2)), sum(bound(3)))
  if (any(calls /= 1) .or. iterator /= 4294967299_int64) stop 7
  print '(A)', 'loop bound result lifetimes passed'
contains
  function bound(which) result(values)
    integer, intent(in) :: which
    integer(int64), allocatable :: values(:)
    calls(which) = calls(which) + 1
    allocate(values(1))
    select case (which)
    case (1)
      values(1) = 4294967296_int64
    case (2)
      values(1) = 4294967298_int64
    case default
      values(1) = 1_int64
    end select
  end function
end program
