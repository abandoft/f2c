program procedure_pointer_component
  implicit none

  abstract interface
    subroutine integer_action(value)
      integer, intent(inout) :: value
    end subroutine integer_action
  end interface

  type :: callback_holder
    procedure(integer_action), pointer, nopass :: action
  end type callback_holder

  type(callback_holder) :: callback
  integer :: value

  value = 9
  nullify(callback%action)
  if (associated(callback%action)) stop 1
  callback%action => increment
  if (.not. associated(callback%action, increment)) stop 2
  call callback%action(value)
  if (value /= 10) stop 3

contains

  subroutine increment(value)
    integer, intent(inout) :: value
    value = value + 1
  end subroutine increment

end program procedure_pointer_component
