module access_provider
  implicit none
  private
  public :: visible_proc
  integer, parameter, public :: exposed = 7
  integer, parameter :: hidden = 11
  type, public :: public_record
    integer :: value
  end type public_record
contains
  integer function visible_proc(value)
    integer, intent(in) :: value
    visible_proc = value + 1
  end function visible_proc
end module access_provider

subroutine public_consumer(result)
  use access_provider, only: exposed, public_record, visible_proc
  implicit none
  integer, intent(out) :: result
  type(public_record) :: item
  item%value = exposed
  result = visible_proc(item%value)
end subroutine public_consumer

program module_access_check
  implicit none
  integer :: result
  call public_consumer(result)
  if (result /= 8) error stop 1
  print '(I0)', result
end program module_access_check
