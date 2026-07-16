subroutine select_target(pointer_value, first, second, choose_second)
  implicit none
  integer, pointer, intent(inout) :: pointer_value
  integer, target, intent(inout) :: first
  integer, target, intent(inout) :: second
  logical, intent(in) :: choose_second
  if (choose_second) then
    pointer_value => second
  else
    pointer_value => first
  end if
end subroutine select_target

program pointer_dummy
  implicit none
  integer, target :: first
  integer, target :: second
  integer, pointer :: selected
  logical :: choose_second
  external select_target

  first = 10
  second = 20
  choose_second = .true.
  selected => first
  call select_target(selected, first, second, choose_second)
  if (.not. associated(selected, second)) stop 1
  selected = 30
  if (second /= 30) stop 2
end program pointer_dummy
