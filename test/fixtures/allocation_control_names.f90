module allocation_control_name_cases
  implicit none
contains
  pure function extent() result(value)
    integer, allocatable :: value
    allocate(value, source=3)
  end function
end module
program allocation_control_names
  use allocation_control_name_cases
  implicit none
  integer, allocatable :: values(:)
  integer :: status
  character(len=40), volatile :: message
  integer :: f2c_retained_value, f2c_retained_index, f2c_retained_capacity
  integer :: f2c_retained_replacement, f2c_message_index
  integer :: f2c_allocation_statement_0_ok, f2c_allocation_statement_0_stat
  integer :: f2c_allocation_statement_0_0_value
  integer :: f2c_allocation_statement_0_0_retained_heap
  f2c_retained_value = 11
  f2c_retained_index = 12
  f2c_retained_capacity = 13
  f2c_retained_replacement = 14
  f2c_message_index = 15
  f2c_allocation_statement_0_ok = 16
  f2c_allocation_statement_0_stat = 17
  f2c_allocation_statement_0_0_value = 18
  f2c_allocation_statement_0_0_retained_heap = 19
  message = 'untouched'
  allocate(values(extent()), source=7, stat=status, errmsg=message)
  if (status /= 0 .or. size(values) /= 3 .or. any(values /= 7)) stop 1
  if (message /= 'untouched') stop 2
  deallocate(values, stat=status, errmsg=message)
  if (status /= 0 .or. message /= 'untouched') stop 3
  deallocate(values, stat=status, errmsg=message)
  if (status <= 0 .or. message == 'untouched') stop 4
  if (f2c_retained_value+f2c_retained_index+f2c_retained_capacity &
      +f2c_retained_replacement+f2c_message_index &
      +f2c_allocation_statement_0_ok+f2c_allocation_statement_0_stat &
      +f2c_allocation_statement_0_0_value &
      +f2c_allocation_statement_0_0_retained_heap /= 135) stop 5
  print '(A)', 'allocation control names passed'
end program
