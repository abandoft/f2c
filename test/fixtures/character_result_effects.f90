module character_result_effect_storage
  implicit none
  integer :: body_calls = 0
  integer :: host_length = 0
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
  function make_mutable_text(n) result(text)
    integer, intent(inout) :: n
    character(n) :: text
    body_calls = body_calls + 1
    text = 'x'
    n = n + 3
  end function
  function make_host_text() result(text)
    character(host_length) :: text
    body_calls = body_calls + 1
    text = 'x'
    host_length = host_length + 2
  end function
  subroutine consume(text)
    character(*), intent(in) :: text
    if (len(text) /= 0) stop 11
  end subroutine
  subroutine consume_pair(text)
    character(*), intent(in) :: text
    if (len(text) /= 2 .or. text /= 'x ') stop 25
  end subroutine
end module
program character_result_effects
  use character_result_effect_storage
  implicit none
  character(:), allocatable :: text
  character(0) :: empty, values(2)
  character(5) :: fixed
  character(2) :: pairs(2)
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
  n = 0
  text = make_mutable_text(n)
  if (n /= 3 .or. len(text) /= 0 .or. body_calls /= 11) stop 12
  n = 2
  text = make_mutable_text(n)
  if (n /= 5 .or. text /= 'x ' .or. len(text) /= 2 .or. body_calls /= 12) stop 13
  n = 2
  if (make_mutable_text(n) /= 'x ') stop 14
  if (n /= 5 .or. body_calls /= 13) stop 15
  n = 2
  text = make_mutable_text(n) // 'z'
  if (n /= 5 .or. text /= 'x z' .or. len(text) /= 3 .or. body_calls /= 14) stop 16
  n = 2
  fixed = make_mutable_text(n)
  if (n /= 5 .or. fixed /= 'x    ' .or. body_calls /= 15) stop 17
  n = 2
  call consume_pair(make_mutable_text(n))
  if (n /= 5 .or. body_calls /= 16) stop 18
  n = 0
  pairs = make_mutable_text(n)
  if (n /= 3 .or. any(pairs /= '  ') .or. body_calls /= 17) stop 19
  host_length = 0
  text = make_host_text()
  if (host_length /= 2 .or. len(text) /= 0 .or. body_calls /= 18) stop 20
  host_length = 2
  text = make_host_text()
  if (host_length /= 4 .or. text /= 'x ' .or. len(text) /= 2 .or. body_calls /= 19) stop 21
  deallocate(text)
  print '(a)', 'character result effects passed'
end program
