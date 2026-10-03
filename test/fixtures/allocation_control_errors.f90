module allocation_control_error_cases
  implicit none
contains
  pure function word(n) result(value)
    integer, intent(in) :: n
    character(len=:), allocatable :: value
    value = repeat('q', n)
  end function
  pure integer function length_value()
    length_value = 3
  end function
end module
program allocation_control_errors
  use allocation_control_error_cases
  implicit none
  character(len=4), allocatable :: fixed, array(:)
  integer(kind=1) :: byte_status
  integer(kind=2) :: short_status
  integer(kind=8) :: wide_status
  integer, volatile :: status
  character(len=48), volatile :: message
  integer, allocatable :: first, missing
  message = 'untouched'
  allocate(fixed, source=word(3), stat=byte_status)
  if (byte_status <= 0 .or. allocated(fixed)) stop 1
  allocate(array(2), mold=word(3), stat=wide_status)
  if (wide_status <= 0 .or. allocated(array)) stop 2
  allocate(character(len=length_value()) :: fixed, stat=short_status)
  if (short_status <= 0 .or. allocated(fixed)) stop 3
  allocate(character(len=4) :: fixed, stat=status, errmsg=message)
  if (status /= 0 .or. len(fixed) /= 4 .or. message /= 'untouched') stop 4
  deallocate(fixed, stat=status, errmsg=message)
  if (status /= 0 .or. allocated(fixed) .or. message /= 'untouched') stop 5
  message = repeat('?', 48)
  deallocate(fixed, stat=status, errmsg=message(2:8))
  if (status <= 0 .or. allocated(fixed)) stop 6
  if (message(1:1) /= '?' .or. message(2:8) /= 'object ') stop 7
  if (message(9:48) /= repeat('?',40)) stop 8
  allocate(first, source=7)
  deallocate(first, missing, stat=status, errmsg=message)
  if (status <= 0 .or. allocated(first) .or. allocated(missing)) stop 9
  if (message /= 'object is not deallocatable') stop 10
  print '(A)', 'allocation control errors passed'
end program
