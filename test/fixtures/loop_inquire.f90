program loop_inquire
  use iso_fortran_env, only: int64
  implicit none
  integer(int64) :: wide, inner, first, last, stride, value, length, scalar_length
  integer :: calls(3)
  first = 4294967296_int64
  last = first + 2_int64
  stride = 1_int64
  value = 7_int64
  inquire(iolength=scalar_length) value
  calls = 0
  inquire(iolength=length) (wide, wide=bound(1),bound(2),bound(3))
  if (length /= 3_int64*scalar_length .or. any(calls /= 1)) stop 1
  if (wide /= first+3_int64) stop 2
  inquire(iolength=length) ((wide+inner,inner=1_int64,2_int64),wide=first,last)
  if (length /= 6_int64*scalar_length .or. wide /= first+3_int64 .or. inner /= 3_int64) stop 3
  inquire(iolength=length) (wide,wide=last,first,1_int64)
  if (length /= 0_int64 .or. wide /= last) stop 4
  print '(A)', 'loop IOLENGTH semantics passed'
contains
  function bound(which) result(value)
    integer, intent(in) :: which
    integer(int64) :: value
    calls(which) = calls(which) + 1
    select case(which)
    case(1)
      value = first
    case(2)
      value = last
    case default
      value = stride
    end select
  end function
end program
