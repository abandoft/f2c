module result_identity_cases
  implicit none
  integer :: calls = 0
  integer, target :: backing = 41
  integer, pointer :: shared(:)
  character(len=4), target :: characters = 'ab' // achar(0) // 'd'
contains
  function selected() result(value)
    integer, pointer :: value
    calls = calls + 1
    value => backing
  end function
  function selected_array() result(value)
    integer, pointer :: value(:)
    calls = calls + 1
    value(2:) => shared(5:1:-2)
  end function
  function selected_character() result(value)
    character(len=:), pointer :: value
    calls = calls + 1
    value => characters
  end function
  function new_scalar() result(value)
    integer, pointer :: value
    calls = calls + 1
    allocate(value)
    value = 88
  end function
  function absent() result(value)
    integer, pointer :: value
    calls = calls + 1
    nullify(value)
  end function
  subroutine change(value)
    integer, intent(inout) :: value
    value = value + 2
  end subroutine
  subroutine change_pointer(value)
    integer, pointer, intent(in) :: value
    value = value + 3
  end subroutine
  subroutine change_array(value)
    integer, intent(inout) :: value(:)
    value = value + 10
  end subroutine
  subroutine change_array_pointer(value)
    integer, pointer, intent(in) :: value(:)
    if (lbound(value,1) /= 2 .or. ubound(value,1) /= 4) stop 11
    value(3) = value(3) + 100
  end subroutine
  integer function read_pointer(value) result(answer)
    integer, pointer, intent(in) :: value
    answer = value
  end function
end module
program result_identity
  use result_identity_cases
  implicit none
  integer, pointer :: scalar, section(:)
  character(len=:), pointer :: text
  integer :: copied(3), answer
  allocate(shared(5))
  shared = [10,20,30,40,50]
  copied = selected_array()
  if (any(copied /= [50,30,10]) .or. calls /= 1) stop 1
  shared(2) = 42
  if (any(shared /= [10,42,30,40,50])) stop 2
  section => selected_array()
  if (lbound(section,1) /= 2 .or. ubound(section,1) /= 4 .or. calls /= 2) stop 3
  section(2) = 51
  call change(selected())
  call change_pointer(selected())
  if (backing /= 46 .or. calls /= 4) stop 4
  answer = read_pointer(selected())
  if (answer /= 46 .or. calls /= 5) stop 5
  call change_array(selected_array())
  call change_array_pointer(selected_array())
  if (any(shared /= [20,42,140,40,61]) .or. calls /= 7) stop 6
  scalar => new_scalar()
  if (scalar /= 88 .or. calls /= 8) stop 7
  deallocate(scalar)
  scalar => absent()
  if (associated(scalar) .or. calls /= 9) stop 8
  text => selected_character()
  if (len(text) /= 4 .or. text /= 'ab' // achar(0) // 'd' .or. calls /= 10) stop 9
  text(4:4) = 'e'
  if (characters /= 'ab' // achar(0) // 'e') stop 10
  deallocate(shared)
  print *, 'pointer result identity preserved'
end program
