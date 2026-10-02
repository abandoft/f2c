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
  call apply(callback%action, value)
  if (value /= 11) stop 4
  if (invoke(callback%action, value) /= 12) stop 5
  if (value /= 12) stop 6

contains

  subroutine increment(value)
    integer, intent(inout) :: value
    value = value + 1
  end subroutine increment

  subroutine apply(action, value)
    procedure(integer_action) :: action
    integer, intent(inout) :: value
    call action(value)
  end subroutine apply

  integer function invoke(action, value)
    procedure(integer_action) :: action
    integer, intent(inout) :: value
    call action(value)
    invoke = value
  end function invoke

end program procedure_pointer_component
