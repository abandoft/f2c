module character_result_effect_storage
  implicit none
  integer :: body_calls = 0
contains
  function make_text(n) result(text)
    integer, intent(in) :: n
    character(len=n) :: text
    body_calls = body_calls + 1
    text = 'x'
  end function
  function make_zero() result(text)
    character(len=0) :: text
    body_calls = body_calls + 1
    text = ''
  end function
  subroutine consume(text)
    character(*), intent(in) :: text
    if (len(text) /= 0) stop 11
  end subroutine
end module
program character_result_effects
  use character_result_effect_storage
  implicit none
  character(:), allocatable :: text
  character(0) :: empty, values(2)
  integer :: n
  n = -3
  text = make_text(n)
  if (len(text) /= 0 .or. body_calls /= 1) stop 1
  n = 0
  text = make_text(n)
  if (len(text) /= 0 .or. body_calls /= 2) stop 2
  n = 3
  text = make_text(n)
  if (text /= 'x  ' .or. body_calls /= 3) stop 3
  text = make_zero()
  if (len(text) /= 0 .or. body_calls /= 4) stop 4
  n = 0
  empty = make_text(n)
  if (len(empty) /= 0 .or. body_calls /= 5) stop 5
  values = make_text(n)
  if (len(values) /= 0 .or. body_calls /= 6) stop 6
  values(:) = make_text(n)
  if (len(values) /= 0 .or. body_calls /= 7) stop 7
  text = make_zero() // make_text(n)
  if (len(text) /= 0 .or. body_calls /= 9) stop 8
  call consume(make_text(n))
  if (body_calls /= 10) stop 9
  deallocate(text)
  print '(a)', 'character result effects passed'
end program
