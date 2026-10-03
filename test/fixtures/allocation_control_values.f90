module allocation_control_value_cases
  implicit none
  type :: box
    integer, allocatable :: values(:)
  end type
contains
  pure function bound(n) result(value)
    integer, intent(in) :: n
    integer(kind=8), allocatable :: value
    allocate(value)
    value = int(n, kind=8)
  end function
  pure function slot() result(value)
    integer, allocatable :: value
    allocate(value)
    value = 2
  end function
  pure function width() result(value)
    integer, allocatable :: value
    allocate(value)
    value = 4
  end function
  pure function vector(n) result(value)
    integer, intent(in) :: n
    integer, allocatable :: value(:)
    allocate(value(-2:n-3), source=9)
  end function
end module
program allocation_control_values
  use allocation_control_value_cases
  implicit none
  integer, allocatable :: values(:), other(:), scalar
  integer :: status
  integer, volatile :: statuses(3)
  character(len=40), volatile :: messages(3)
  character(len=:), allocatable :: first, second
  character(len=4), allocatable :: fixed
  type(box) :: objects(2)
  status = 17
  allocate(scalar, source=status, stat=status)
  if (status /= 0 .or. scalar /= 17) stop 1
  deallocate(scalar)
  allocate(values(bound(-2):bound(2)), source=6)
  if (lbound(values,1) /= -2 .or. size(values) /= 5) stop 2
  if (any(values /= 6)) stop 3
  deallocate(values)
  allocate(values(bound(-7):bound(-8)))
  if (size(values) /= 0) stop 4
  deallocate(values)
  ! An inquiry can omit evaluation when its result is already known. Use a
  ! pure provider and compare bounds, not an invalid side-effect counter.
  allocate(values(lbound(vector(3),1):ubound(vector(3),1)), source=8)
  if (size(values) /= 3 .or. lbound(values,1) /= 1) stop 5
  if (any(values /= 8)) stop 6
  deallocate(values)
  allocate(character(len=width()) :: first, second)
  if (len(first) /= 4 .or. len(second) /= 4) stop 7
  allocate(character(len=width()) :: fixed)
  if (len(fixed) /= 4) stop 8
  deallocate(first, second, fixed)
  allocate(objects(slot())%values(bound(3)), source=7)
  if (any(objects(2)%values /= 7)) stop 10
  deallocate(objects(slot())%values)
  if (allocated(objects(2)%values)) stop 11
  statuses = -99
  messages = 'untouched'
  allocate(values(2), stat=statuses(slot()), errmsg=messages(slot()))
  if (any(statuses /= [-99,0,-99])) stop 12
  if (any(messages /= 'untouched')) stop 13
  deallocate(values, stat=statuses(slot()), errmsg=messages(slot()))
  if (any(statuses /= [-99,0,-99])) stop 14
  if (any(messages /= 'untouched')) stop 15
  allocate(other(3), stat=statuses(slot()), errmsg=messages(2)(slot():20))
  if (statuses(2) /= 0 .or. any(messages /= 'untouched')) stop 16
  deallocate(other, stat=statuses(slot()), errmsg=messages(2)(slot():20))
  if (statuses(2) /= 0 .or. any(messages /= 'untouched')) stop 17
  print '(A)', 'allocation pure control values passed'
end program
