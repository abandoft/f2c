module allocation_result_cases
  implicit none
  integer :: calls = 0
  integer, target :: backing(-3:2) = [10,20,30,40,50,60]
  character(len=4), target :: words(2)
  type :: item
    integer, allocatable :: values(:)
    character(len=:), allocatable :: text
    integer, pointer :: alias => null()
  end type
contains
  function number() result(value)
    integer, allocatable :: value
    calls = calls + 1
    allocate(value)
    value = 73
  end function
  function selected() result(value)
    integer, pointer :: value(:)
    calls = calls + 1
    value(-5:) => backing(2:-3:-2)
  end function
  function text(n) result(value)
    integer, intent(in) :: n
    character(len=:), allocatable :: value
    calls = calls + 1
    value = repeat('x', n)
  end function
  function selected_words() result(value)
    character(len=:), pointer :: value(:)
    calls = calls + 1
    value(-2:) => words(2:1:-1)
  end function
  function object() result(value)
    type(item), allocatable :: value
    calls = calls + 1
    allocate(value)
    allocate(value%values, source=[3,5,7])
    value%text = 'a' // achar(0) // 'b'
    value%alias => backing(-3)
  end function
end module
program allocation_result
  use allocation_result_cases
  implicit none
  integer, allocatable :: scalar, vector(:), another(:), molded(:)
  character(len=:), allocatable :: first, second, empty, char_mold(:)
  type(item), allocatable :: left, right
  integer :: before, status

  allocate(scalar, vector(3), source=number(), stat=status)
  if (status /= 0 .or. calls /= 1) stop 1
  if (scalar /= 73 .or. any(vector /= 73)) stop 2
  deallocate(vector)
  allocate(vector, another, source=selected())
  if (calls /= 2 .or. any(vector /= [60,40,20])) stop 3
  if (any(another /= vector)) stop 4
  if (lbound(vector,1) /= 1 .or. ubound(vector,1) /= 3) stop 5
  vector(1) = 99
  if (backing(2) /= 60 .or. another(1) /= 60) stop 6
  allocate(molded, mold=selected())
  if (size(molded) /= 3 .or. lbound(molded,1) /= 1) stop 7
  deallocate(molded)
  allocate(molded(0:1), mold=selected())
  if (size(molded) /= 2 .or. lbound(molded,1) /= 0) stop 8
  before = calls

  allocate(first, second, source=text(3) // achar(0) // text(1))
  if (calls /= before + 2 .or. len(first) /= 5) stop 9
  if (first /= 'xxx' // achar(0) // 'x' .or. second /= first) stop 10
  allocate(empty, source=text(0))
  if (len(empty) /= 0) stop 11
  deallocate(second)
  before = calls
  allocate(second, mold=text(6))
  if (calls /= before + 1 .or. len(second) /= 6) stop 12
  words(1) = 'a' // achar(0) // 'b '
  words(2) = 'word'
  before = calls
  allocate(char_mold, mold=selected_words())
  if (calls /= before + 1 .or. len(char_mold) /= 4) stop 13
  if (size(char_mold) /= 2 .or. lbound(char_mold,1) /= 1) stop 14

  before = calls
  allocate(left, right, source=object())
  if (calls /= before + 1) stop 15
  if (any(left%values /= [3,5,7]) .or. any(right%values /= left%values)) stop 16
  if (left%text /= 'a' // achar(0) // 'b' .or. right%text /= left%text) stop 17
  left%values(1) = 100
  if (right%values(1) /= 3) stop 18
  if (.not.associated(left%alias, backing(-3))) stop 19
  left%alias = 11
  if (right%alias /= 11 .or. backing(-3) /= 11) stop 20
  deallocate(scalar, vector, another, molded, first, second, empty, char_mold, left, right)
  print '(A)', 'allocation result contracts passed'
end program
