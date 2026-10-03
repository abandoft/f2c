module constructor_result_cases
  implicit none
  integer :: calls = 0
  integer, target :: values(5) = [1, 2, 3, 4, 5]
  character(len=4), target :: text_target = 'live'
  type :: item
    integer, allocatable :: values(:)
    character(len=:), allocatable :: label
  end type
contains
  function owned(number) result(output)
    integer, intent(in) :: number
    integer, allocatable :: output
    calls = calls + 1
    allocate(output)
    output = number
  end function
  function owned_array(number) result(output)
    integer, intent(in) :: number
    integer, allocatable :: output(:)
    calls = calls + 1
    allocate(output(-2:-1))
    output = [number, -number]
  end function
  function borrowed() result(output)
    integer, pointer :: output(:)
    calls = calls + 1
    output => values(5:1:-2)
  end function
  function owned_matrix(number) result(output)
    integer, intent(in) :: number
    integer, allocatable :: output(:,:)
    calls = calls + 1
    allocate(output(-1:0,3:4))
    output = reshape([number, number+1, number+2, number+3], [2,2])
  end function
  function text(number) result(output)
    integer, intent(in) :: number
    character(len=:), allocatable :: output
    calls = calls + 1
    if (number == 1) then
      output = 'a' // achar(0) // 'bc'
    else
      output = 'next'
    end if
  end function
  function borrowed_text() result(output)
    character(len=:), pointer :: output
    calls = calls + 1
    output => text_target
  end function
  function object(number) result(output)
    integer, intent(in) :: number
    type(item), allocatable :: output
    calls = calls + 1
    allocate(output)
    allocate(output%values(2))
    output%values = [number, -number]
    output%label = 'deep'
  end function
end module

program constructor_result
  use constructor_result_cases
  implicit none
  integer :: fixed(2), i, j
  integer, allocatable :: array(:)
  character(len=4) :: fixed_text(2)
  character(len=:), allocatable :: text_array(:)
  type(item) :: objects(2)

  fixed = [owned(41) + 1, owned(42)]
  if (calls /= 2 .or. any(fixed /= [42, 42])) stop 1
  calls = 0
  array = [(owned(i), i=1,17)]
  if (calls /= 17 .or. size(array) /= 17) stop 2
  if (any(array /= [(i, i=1,17)])) stop 3
  calls = 0
  array = [((owned(10*i+j), j=1,3), i=1,2)]
  if (calls /= 6 .or. any(array /= [11,12,13,21,22,23])) stop 4
  calls = 0
  array = [(owned(i), i=owned(1),owned(3))]
  if (calls /= 5 .or. any(array /= [1,2,3])) stop 5
  calls = 0
  array = [(owned(i), i=2,owned(1))]
  if (calls /= 1 .or. size(array) /= 0) stop 6
  calls = 0
  fixed(1) = sum([owned(1), owned(2)])
  if (fixed(1) /= 3 .or. calls /= 2) stop 7
  calls = 0
  fixed(1) = sum([(owned(i), i=1,17)])
  if (fixed(1) /= 153 .or. calls /= 17) stop 23
  calls = 0
  array = reshape([(owned(i), i=1,6)], [6])
  if (calls /= 6 .or. any(array /= [1,2,3,4,5,6])) stop 24
  calls = 0
  fixed(1) = size([owned_array(1), owned_array(2)])
  if (fixed(1) /= 4 .or. calls /= 2) stop 25
  calls = 0
  array = [sum([owned(1), owned(2)]), owned(4)]
  if (calls /= 3 .or. any(array /= [3,4])) stop 21
  calls = 0
  array = [owned_matrix(1), owned_matrix(10)]
  if (calls /= 2 .or. any(array /= [1,2,3,4,10,11,12,13])) stop 22
  calls = 0
  array = [owned_array(7), owned_array(8)]
  if (calls /= 2 .or. any(array /= [7,-7,8,-8])) stop 8
  calls = 0
  array = [(borrowed(), i=1,3)]
  if (calls /= 3 .or. any(array /= [5,3,1,5,3,1,5,3,1])) stop 9
  if (any(values /= [1,2,3,4,5])) stop 10
  calls = 0
  array = [values(5:1:-2), values]
  if (calls /= 0 .or. any(array /= [5,3,1,1,2,3,4,5])) stop 11
  calls = 0
  fixed_text = [text(1), text(2)]
  if (calls /= 2 .or. len(fixed_text) /= 4) stop 12
  if (fixed_text(1) /= 'a'//achar(0)//'bc' .or. fixed_text(2) /= 'next') stop 13
  calls = 0
  text_array = [(text(2), i=1,17)]
  if (calls /= 17 .or. size(text_array) /= 17 .or. len(text_array) /= 4) stop 14
  if (any(text_array /= 'next')) stop 15
  calls = 0
  text_array = [borrowed_text(), text(2)]
  if (calls /= 2 .or. any(text_array /= ['live','next'])) stop 16
  if (text_target /= 'live') stop 17
  calls = 0
  fixed(1) = count([(text(2), i=1,17)] == 'next')
  if (fixed(1) /= 17 .or. calls /= 17) stop 26
  calls = 0
  objects = [object(10), object(20)]
  if (calls /= 2 .or. any(objects(1)%values /= [10,-10])) stop 18
  if (any(objects(2)%values /= [20,-20]) .or. objects(2)%label /= 'deep') stop 19
  objects(1)%values(1) = 99
  if (objects(2)%values(1) /= 20) stop 20
  print *, 'constructor result ownership passed'
end program
