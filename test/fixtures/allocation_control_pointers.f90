module allocation_control_pointer_cases
  implicit none
  integer, target :: status = 17
  character(len=40), target :: message = 'untouched'
  integer :: status_calls = 0, message_calls = 0
contains
  function status_variable() result(value)
    integer, pointer :: value
    status_calls = status_calls + 1
    value => status
  end function
  function message_variable() result(value)
    character(len=40), pointer :: value
    message_calls = message_calls + 1
    value => message
  end function
end module
program allocation_control_pointers
  use allocation_control_pointer_cases
  implicit none
  integer, allocatable :: value
  allocate(value, source=status, stat=status_variable(), errmsg=message_variable())
  if (status /= 0 .or. value /= 17 .or. message /= 'untouched') stop 1
  if (status_calls /= 1 .or. message_calls /= 1) stop 2
  allocate(value, stat=status_variable(), errmsg=message_variable())
  if (status <= 0 .or. value /= 17 .or. message == 'untouched') stop 3
  if (status_calls /= 2 .or. message_calls /= 2) stop 4
  message = 'unchanged'
  deallocate(value, stat=status_variable(), errmsg=message_variable())
  if (status /= 0 .or. allocated(value) .or. message /= 'unchanged') stop 5
  if (status_calls /= 3 .or. message_calls /= 3) stop 6
  deallocate(value, stat=status_variable(), errmsg=message_variable())
  if (status <= 0 .or. allocated(value) .or. message == 'unchanged') stop 7
  if (status_calls /= 4 .or. message_calls /= 4) stop 8
  print '(A)', 'allocation pointer control variables passed'
end program
