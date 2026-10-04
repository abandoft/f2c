module elemental_library
  implicit none
contains
  elemental integer function module_offset(value, offset) result(answer)
    integer, intent(in) :: value, offset
    answer = value + offset
  end function module_offset
end module elemental_library

program elemental_procedure
  use elemental_library
  implicit none
  type :: cell
    integer :: value
  end type cell
  integer :: a(4), b(4), values(4), reversed(4), matrix(2,3)
  integer :: scalar, i
  character(len=3) :: words(4), source_words(4)
  type(cell) :: cells(4)
  integer, allocatable :: dynamic_values(:)
  character(len=3), allocatable :: dynamic_words(:)
  type(cell), allocatable :: dynamic_cells(:)
  complex :: empty_complex(0)
  logical :: empty_logical(0)
  character(len=3) :: empty_words(0)
  character(len=0) :: zero_words(2)
  complex, allocatable :: dynamic_complex(:)
  logical, allocatable :: dynamic_logical(:)
  character(len=:), allocatable :: zero_dynamic(:)
  real :: empty_cube(2,0,3)
  real, allocatable :: dynamic_cube(:,:,:)

  a = [1, 2, 3, 4]
  b = [10, 20, 30, 40]

  values = module_offset(a, 5)
  if (any(values /= [6, 7, 8, 9])) error stop 18

  values = combine(y=b, scale=2, x=a)
  if (any(values /= [21, 42, 63, 84])) error stop 1

  call accumulate(values, a)
  if (any(values /= [22, 44, 66, 88])) error stop 2

  call accumulate(increment=1, value=values(4:1:-1))
  if (any(values /= [23, 45, 67, 89])) error stop 3

  values = combine([(i, i = 1, 4)], b, 1)
  call accumulate(values, [[1, 1], [1, 1]])
  if (any(values /= [12, 23, 34, 45])) error stop 17

  where (positive(values)) values = values - 1
  if (any(values /= [11, 22, 33, 44])) error stop 4

  reversed = combine(a(4:1:-1), b, -1)
  if (any(reversed /= [-6, -17, -28, -39])) error stop 5

  matrix = reshape([1, 2, 3, 4, 5, 6], [2, 3])
  matrix = combine(matrix, matrix, 1)
  call accumulate(matrix, 1)
  if (any(matrix /= reshape([3, 5, 7, 9, 11, 13], [2, 3]))) error stop 6

  scalar = combine(1, 2, 3)
  if (scalar /= 7) error stop 7

  source_words = ['one', 'two', 'tri', 'for']
  words = decorate(a)
  if (any(words /= source_words)) error stop 8

  cells = make_cell(a)
  call increment_cell(cells, 5)
  do i = 1, 4
    if (cells(i)%value /= i * 10 + 5) error stop 9
  end do

  dynamic_values = combine(a, b, 1)
  if (.not. allocated(dynamic_values) .or. &
      any(dynamic_values /= [11, 22, 33, 44])) error stop 10
  call accumulate(dynamic_values, 1)
  if (any(dynamic_values /= [12, 23, 34, 45])) error stop 11
  dynamic_values = combine(a(1:2), b(1:2), 1)
  if (any(dynamic_values /= [11, 22])) error stop 12

  dynamic_words = decorate(a(1:2))
  if (size(dynamic_words) /= 2) error stop 19
  dynamic_words = decorate(a)
  if (len(dynamic_words(1)) /= 3 .or. any(dynamic_words /= source_words)) error stop 13

  dynamic_cells = make_cell(a)
  do i = 1, 4
    if (dynamic_cells(i)%value /= i * 10) error stop 14
  end do
  dynamic_cells = make_cell(a(1:2))
  if (dynamic_cells(1)%value /= 10 .or. dynamic_cells(2)%value /= 20) error stop 15

  dynamic_values = combine(a(1:0), b(1:0), 1)
  if (.not. allocated(dynamic_values) .or. size(dynamic_values) /= 0) error stop 16
  dynamic_words = empty_words // ''
  if (.not. allocated(dynamic_words) .or. size(dynamic_words) /= 0) error stop 20
  if (len(dynamic_words) /= 3) error stop 21
  zero_dynamic = zero_words // ''
  if (.not. allocated(zero_dynamic) .or. size(zero_dynamic) /= 2) error stop 22
  if (len(zero_dynamic) /= 0) error stop 23
  dynamic_complex = empty_complex + (1.0, 0.0)
  if (.not. allocated(dynamic_complex) .or. size(dynamic_complex) /= 0) error stop 24
  if (abs(sum(dynamic_complex)) > 0.0) error stop 25
  dynamic_logical = .not. empty_logical
  if (.not. allocated(dynamic_logical) .or. size(dynamic_logical) /= 0) error stop 26
  if (any(dynamic_logical)) error stop 27
  dynamic_cells = make_cell(a(1:0))
  if (.not. allocated(dynamic_cells) .or. size(dynamic_cells) /= 0) error stop 28
  dynamic_cube = empty_cube + 1.0
  if (.not. allocated(dynamic_cube)) error stop 29
  if (any(shape(dynamic_cube) /= [2,0,3])) error stop 30

  deallocate(dynamic_values, dynamic_words, dynamic_cells, &
             dynamic_complex, dynamic_logical, zero_dynamic, dynamic_cube)

  do i = 1, 4
    write (*, '(I0,1X,I0)') values(i), reversed(i)
  end do
  do i = 1, 3
    write (*, '(I0,1X,I0)') matrix(1,i), matrix(2,i)
  end do
  write (*, '(I0)') scalar
  do i = 1, 4
    write (*, '(A)') words(i)
  end do
  do i = 1, 4
    write (*, '(I0)') cells(i)%value
  end do

contains

  elemental integer function combine(x, y, scale) result(value)
    integer, intent(in) :: x, y, scale
    value = x + y * scale
  end function combine

  elemental logical function positive(value) result(result_value)
    integer, intent(in) :: value
    result_value = value > 0
  end function positive

  elemental character(len=3) function decorate(number) result(value)
    integer, intent(in) :: number
    select case (number)
    case (1)
      value = 'one'
    case (2)
      value = 'two'
    case (3)
      value = 'tri'
    case default
      value = 'for'
    end select
  end function decorate

  elemental subroutine accumulate(value, increment)
    integer, intent(inout) :: value
    integer, intent(in) :: increment
    value = value + increment
  end subroutine accumulate

  elemental type(cell) function make_cell(number) result(value)
    integer, intent(in) :: number
    value = cell(number * 10)
  end function make_cell

  elemental subroutine increment_cell(value, increment)
    type(cell), intent(inout) :: value
    integer, intent(in) :: increment
    value%value = value%value + increment
  end subroutine increment_cell

end program elemental_procedure
