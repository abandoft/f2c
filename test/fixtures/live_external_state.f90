subroutine observe_live_pointer(values, results)
  implicit none
  integer, pointer, volatile :: values(:)
  integer, intent(out) :: results(8)
  external :: change_state
  results(1) = values(lbound(values,1))
  results(2) = size(values)
  results(3) = lbound(values,1)
  call change_state()
  results(4) = values(lbound(values,1))
  results(5) = size(values)
  results(6) = lbound(values,1)
  results(7) = ubound(values,1)
  results(8) = sum(values)
end subroutine observe_live_pointer

subroutine observe_live_allocatable(values, results)
  implicit none
  integer, allocatable, volatile :: values(:)
  integer, intent(out) :: results(2)
  external :: change_state
  results(1) = 0
  if (allocated(values)) results(1) = 1
  call change_state()
  results(2) = 0
  if (allocated(values)) results(2) = 1
end subroutine observe_live_allocatable

subroutine observe_live_character(text, results)
  implicit none
  character(:), pointer, volatile :: text
  integer, intent(out) :: results(2)
  external :: change_state
  results(1) = len(text)
  call change_state()
  results(2) = len(text)
end subroutine observe_live_character
