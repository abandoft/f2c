module scalar_result_cases
  implicit none
  integer :: calls = 0
  integer, target :: backing = 41
  type :: item
    integer, allocatable :: values(:)
    character(len=:), allocatable :: text
  end type
contains
  function number() result(value)
    integer, allocatable :: value
    calls = calls + 1
    allocate(value)
    value = 42
  end function
  function selected() result(value)
    integer, pointer :: value
    calls = calls + 1
    value => backing
  end function
  function word(n) result(value)
    integer, intent(in) :: n
    character(len=:), allocatable :: value
    calls = calls + 1
    value = repeat('x', n)
  end function
  function object() result(value)
    type(item), allocatable :: value
    calls = calls + 1
    allocate(value)
    allocate(value%values(2))
    value%values = [10,20]
    value%text = 'owned'
  end function
  integer function consume(value) result(answer)
    type(item), intent(in) :: value
    answer = sum(value%values)
  end function
end module
program scalar_result
  use scalar_result_cases
  implicit none
  integer :: answer
  integer, pointer :: pointer_value
  character(len=:), allocatable :: text
  type(item) :: value
  answer = number() + selected()
  if (answer /= 83 .or. calls /= 2) stop 1
  pointer_value => selected()
  pointer_value = 43
  if (backing /= 43 .or. calls /= 3) stop 2
  text = word(4)
  if (text /= 'xxxx' .or. len(text) /= 4 .or. calls /= 4) stop 3
  value = object()
  if (any(value%values /= [10,20]) .or. value%text /= 'owned' .or. calls /= 5) stop 4
  answer = consume(object())
  if (answer /= 30 .or. calls /= 6) stop 5
  text = word(0)
  if (len(text) /= 0 .or. calls /= 7) stop 6
  text = word(3) // achar(0) // word(2)
  if (len(text) /= 6 .or. text /= 'xxx' // achar(0) // 'xx' .or. calls /= 9) stop 7
  print *, 'scalar owned result contracts passed'
end program
