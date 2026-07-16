program procedure_pointer
  implicit none(type, external)

  abstract interface
    integer function integer_operation(value) result(answer)
      integer, intent(in) :: value
    end function integer_operation
  end interface

  procedure(integer_operation), pointer :: operation
  integer :: answer

  nullify(operation)
  if (associated(operation)) stop 1

  operation => add_one
  if (.not. associated(operation)) stop 2
  answer = operation(41)
  if (answer /= 42) stop 3

  operation => twice
  if (.not. associated(operation, twice)) stop 4
  answer = operation(21)
  if (answer /= 42) stop 5

contains

  integer function add_one(value) result(answer)
    integer, intent(in) :: value
    answer = value + 1
  end function add_one

  integer function twice(value) result(answer)
    integer, intent(in) :: value
    answer = value * 2
  end function twice

end program procedure_pointer
