module function_result_cases
  implicit none

  type :: item
    integer, allocatable :: payload(:)
  end type item

  integer :: calls = 0
  integer, target :: storage(5) = [1, 2, 3, 4, 5]

contains

  function explicit_values(n) result(values)
    integer, intent(in) :: n
    integer :: values(-2:n - 3)
    integer :: index

    calls = calls + 1
    do index = -2, n - 3
      values(index) = 10 + index + 2
    end do
  end function explicit_values

  function matrix_values(rows, columns) result(values)
    integer, intent(in) :: rows, columns
    integer :: values(0:rows - 1, -1:columns - 2)
    integer :: row, column

    calls = calls + 1
    do column = -1, columns - 2
      do row = 0, rows - 1
        values(row, column) = 100 * (column + 2) + row
      end do
    end do
  end function matrix_values

  function allocated_values(n) result(values)
    integer, intent(in) :: n
    integer, allocatable :: values(:)
    integer :: index

    calls = calls + 1
    allocate(values(-3:n - 4))
    do index = -3, n - 4
      values(index) = 20 + index + 3
    end do
  end function allocated_values

  function selected_values() result(values)
    integer, pointer :: values(:)
    values => storage(5:1:-2)
  end function selected_values

  function words() result(values)
    character(len=3) :: values(2)
    values = ["one", "two"]
  end function words

  function make_items(n) result(values)
    integer, intent(in) :: n
    type(item) :: values(n)
    integer :: index

    do index = 1, n
      allocate(values(index)%payload(index))
      values(index)%payload = 10 * index
    end do
  end function make_items

  function make_dynamic_items(n) result(values)
    integer, intent(in) :: n
    type(item), allocatable :: values(:)
    integer :: index

    allocate(values(-1:n - 2))
    do index = -1, n - 2
      allocate(values(index)%payload(index + 2))
      values(index)%payload = 100 * (index + 2)
    end do
  end function make_dynamic_items

  subroutine verify_values(values)
    integer, intent(in) :: values(:)
    if (any(values /= [10, 11, 12, 13])) error stop 1
  end subroutine verify_values

  integer function score_words(values)
    character(len=*), intent(in) :: values(:)
    score_words = len(values) + size(values) + len_trim(values(1)) + len_trim(values(2))
  end function score_words

end module function_result_cases

program function_result
  use function_result_cases
  implicit none

  integer :: vector(4), matrix(2, 3), pointer_copy(3), total, word_score
  integer, allocatable :: dynamic(:)
  character(len=4) :: text(2)
  type(item) :: objects(3)
  type(item), allocatable :: dynamic_objects(:)

  vector = explicit_values(4) + 1
  if (any(vector /= [11, 12, 13, 14])) error stop 2
  if (calls /= 1) error stop 3

  total = sum(explicit_values(4) + explicit_values(4))
  if (total /= 92 .or. calls /= 3) error stop 4

  call verify_values(explicit_values(4))
  if (calls /= 4) error stop 5

  matrix = matrix_values(2, 3)
  if (any(matrix /= reshape([100, 101, 200, 201, 300, 301], [2, 3]))) error stop 6

  dynamic = allocated_values(2)
  dynamic = allocated_values(3)
  if (any(dynamic /= [20, 21, 22])) error stop 7
  if (lbound(dynamic, 1) /= 1) error stop 8

  pointer_copy = selected_values()
  if (any(pointer_copy /= [5, 3, 1])) error stop 9

  text = words()
  if (any(text /= ["one ", "two "])) error stop 10
  word_score = score_words(words())
  if (word_score /= 11) error stop 11

  objects = make_items(3)
  if (any(objects(1)%payload /= [10])) error stop 12
  if (any(objects(2)%payload /= [20, 20])) error stop 13
  if (any(objects(3)%payload /= [30, 30, 30])) error stop 14

  dynamic_objects = make_dynamic_items(2)
  dynamic_objects = make_dynamic_items(3)
  if (lbound(dynamic_objects, 1) /= 1) error stop 15
  if (any(dynamic_objects(1)%payload /= [100])) error stop 16
  if (any(dynamic_objects(2)%payload /= [200, 200])) error stop 17
  if (any(dynamic_objects(3)%payload /= [300, 300, 300])) error stop 18

  write (*, '(5(i0,1x))') calls, sum(vector), total, sum(matrix), sum(dynamic)
  write (*, '(3(i0,1x))') pointer_copy
  write (*, '(a,1x,a,1x,i0)') text(1), text(2), word_score
  write (*, '(5(i0,1x))') size(objects), objects(1)%payload(1), &
      objects(2)%payload(2), objects(3)%payload(3), size(dynamic_objects)
end program function_result
