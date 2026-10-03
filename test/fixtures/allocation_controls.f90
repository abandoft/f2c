module allocation_control_cases
  implicit none
  integer :: bound_calls = 0, index_calls = 0, length_calls = 0
  type :: box
    integer, allocatable :: values(:)
  end type
contains
  function bound(n) result(value)
    integer, intent(in) :: n
    integer(kind=8), allocatable :: value
    allocate(value)
    value = int(n, kind=8)
    bound_calls = bound_calls + 1
  end function
  function slot() result(value)
    integer, allocatable :: value
    allocate(value)
    value = 2
    index_calls = index_calls + 1
  end function
  function width() result(value)
    integer, allocatable :: value
    allocate(value)
    value = 4
    length_calls = length_calls + 1
  end function
  pure function vector(n) result(value)
    integer, intent(in) :: n
    integer, allocatable :: value(:)
    allocate(value(-2:n-3), source=9)
  end function
end module
program allocation_controls
  use allocation_control_cases
  implicit none
  integer, allocatable :: values(:), other(:), scalar
  integer :: status, statuses(3)
  character(len=40) :: messages(3)
  character(len=:), allocatable :: first, second
  character(len=4), allocatable :: fixed
  type(box) :: objects(2)
  status = 17
  allocate(scalar, source=status, stat=status)
  if (status /= 0 .or. scalar /= 17) stop 1
  deallocate(scalar)
  allocate(values(bound(-2):bound(2)), source=6)
  if (bound_calls /= 2 .or. lbound(values,1) /= -2 .or. size(values) /= 5) stop 2
  if (any(values /= 6)) stop 3
  deallocate(values)
  allocate(values(bound(-7):bound(-8)))
  if (bound_calls /= 4 .or. size(values) /= 0) stop 4
  deallocate(values)
  ! An inquiry can omit evaluation when its result is already known. Use a
  ! pure provider and compare bounds, not an invalid side-effect counter.
  allocate(values(lbound(vector(3),1):ubound(vector(3),1)), source=8)
  if (size(values) /= 3 .or. lbound(values,1) /= 1) stop 5
  if (any(values /= 8)) stop 6
  deallocate(values)
  allocate(character(len=width()) :: first, second)
  if (length_calls /= 1 .or. len(first) /= 4 .or. len(second) /= 4) stop 7
  allocate(character(len=width()) :: fixed)
  if (length_calls /= 2 .or. len(fixed) /= 4) stop 8
  deallocate(first, second, fixed)
  allocate(objects(slot())%values(bound(3)), source=7)
  if (index_calls /= 1 .or. bound_calls /= 5) stop 9
  if (any(objects(2)%values /= 7)) stop 10
  deallocate(objects(slot())%values)
  if (index_calls /= 2 .or. allocated(objects(2)%values)) stop 11
  statuses = -99
  messages = 'untouched'
  allocate(values(2), stat=statuses(slot()), errmsg=messages(slot()))
  if (index_calls /= 4 .or. any(statuses /= [-99,0,-99])) stop 12
  if (any(messages /= 'untouched')) stop 13
  deallocate(values, stat=statuses(slot()), errmsg=messages(slot()))
  if (index_calls /= 6 .or. any(statuses /= [-99,0,-99])) stop 14
  if (any(messages /= 'untouched')) stop 15
  allocate(other(3), stat=statuses(slot()), errmsg=messages(2)(slot():20))
  if (index_calls /= 8 .or. statuses(2) /= 0 .or. any(messages /= 'untouched')) stop 16
  deallocate(other, stat=statuses(slot()), errmsg=messages(2)(slot():20))
  if (index_calls /= 10 .or. statuses(2) /= 0 .or. any(messages /= 'untouched')) stop 17
  if (.true.) allocate(other(bound(2)), stat=statuses(slot()))
  if (bound_calls /= 6 .or. index_calls /= 11 .or. size(other) /= 2) stop 18
  if (.true.) deallocate(other, stat=statuses(slot()))
  if (index_calls /= 12 .or. allocated(other) .or. statuses(2) /= 0) stop 19
  print '(A)', 'allocation control values passed'
end program
