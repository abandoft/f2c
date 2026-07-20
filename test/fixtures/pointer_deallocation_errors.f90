module pointer_deallocation_error_support
  implicit none

  integer :: designator_calls = 0

  type :: holder
    integer, pointer :: values(:)
  end type holder

contains

  integer function next_holder()
    designator_calls = designator_calls + 1
    next_holder = 1
  end function next_holder

end module pointer_deallocation_error_support

program pointer_deallocation_errors
  use pointer_deallocation_error_support
  implicit none

  integer, target :: ordinary
  integer, allocatable, target :: dynamic(:)
  integer, pointer :: pointer_value
  integer, pointer :: pointer_array(:)
  integer :: status
  character(len=64) :: message
  type(holder) :: objects(1)

  designator_calls = 0
  allocate(objects(next_holder())%values(2), stat=status, errmsg=message)
  if (status /= 0 .or. designator_calls /= 1) error stop 1
  designator_calls = 0
  deallocate(objects(next_holder())%values, stat=status, errmsg=message)
  if (status /= 0 .or. designator_calls /= 1) error stop 2

  ordinary = 42
  pointer_value => ordinary
  message = ''
  deallocate(pointer_value, stat=status, errmsg=message)
  if (status == 0 .or. len_trim(message) == 0) error stop 3
  if (.not. associated(pointer_value) .or. ordinary /= 42) error stop 4
  nullify(pointer_value)

  allocate(dynamic(2))
  dynamic = [3, 5]
  pointer_array => dynamic
  message = ''
  deallocate(pointer_array, stat=status, errmsg=message)
  if (status == 0 .or. len_trim(message) == 0) error stop 5
  if (.not. allocated(dynamic) .or. sum(dynamic) /= 8) error stop 6
  nullify(pointer_array)
  deallocate(dynamic)

  print '(a)', 'pointer deallocation errors ok'
end program pointer_deallocation_errors
