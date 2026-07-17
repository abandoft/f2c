program array_inquiry
  implicit none
  integer :: matrix(-1:1, 4:6)
  integer :: matrix_shape(2), matrix_lower(2), matrix_upper(2)
  integer :: section_shape(2), section_lower(2), section_upper(2)
  integer :: expression_shape(2), expression_lower(2), expression_upper(2)
  integer :: constructor_shape(1)
  integer(kind=8) :: shape8(2), lower8(2), upper8(2), size8
  integer, allocatable :: dynamic(:, :), dynamic_shape(:), empty(:)
  integer :: dim_value

  matrix = reshape([1, 2, 3, 4, 5, 6, 7, 8, 9], [3, 3])
  matrix_shape = shape(matrix)
  matrix_lower = lbound(matrix)
  matrix_upper = ubound(matrix)
  if (any(matrix_shape /= [3, 3])) error stop 1
  if (any(matrix_lower /= [-1, 4])) error stop 2
  if (any(matrix_upper /= [1, 6])) error stop 3

  shape8 = shape(kind=8, source=matrix)
  lower8 = lbound(kind=8, array=matrix)
  upper8 = ubound(array=matrix, kind=8)
  if (any(shape8 /= [3_8, 3_8])) error stop 4
  if (any(lower8 /= [-1_8, 4_8])) error stop 5
  if (any(upper8 /= [1_8, 6_8])) error stop 6

  section_shape = shape(matrix(1:-1:-1, 4:6:2))
  section_lower = lbound(matrix(1:-1:-1, 4:6:2))
  section_upper = ubound(matrix(1:-1:-1, 4:6:2))
  if (any(section_shape /= [3, 2])) error stop 7
  if (any(section_lower /= [1, 1])) error stop 8
  if (any(section_upper /= [3, 2])) error stop 9

  expression_shape = shape(matrix + 1)
  expression_lower = lbound(matrix + 1)
  expression_upper = ubound(matrix + 1)
  constructor_shape = shape([1, 2, 3])
  if (any(expression_shape /= [3, 3])) error stop 17
  if (any(expression_lower /= [1, 1])) error stop 18
  if (any(expression_upper /= [3, 3])) error stop 19
  if (constructor_shape(1) /= 3 .or. size(matrix + 1) /= 9) error stop 20

  dim_value = 2
  size8 = size(kind=8, array=matrix, dim=dim_value)
  if (size(matrix) /= 9 .or. size(matrix, 1) /= 3 .or. size8 /= 3_8) error stop 10
  if (lbound(matrix, dim_value) /= 4 .or. ubound(matrix, dim_value) /= 6) error stop 11

  allocate(dynamic(-2:0, 7:8))
  dynamic_shape = shape(dynamic)
  if (any(dynamic_shape /= [3, 2])) error stop 12
  if (lbound(dynamic, 1) /= -2 .or. ubound(dynamic, 2) /= 8) error stop 13
  if (size(dynamic) /= 6) error stop 14
  call check_assumed_shape(dynamic, 2)
  call check_assumed_shape(matrix, 3)
  call forward_strided_section(matrix(-1:1:2, 6:4:-1))
  if (array_element_count(dynamic) /= 6 .or. &
      array_element_count(matrix) /= 9 .or. &
      strided_sum(matrix(-1:1:2, 6:4:-1)) /= 30) error stop 25

  allocate(empty(3:2))
  if (size(empty) /= 0) error stop 15
  if (lbound(empty, 1) /= 1 .or. ubound(empty, 1) /= 0) error stop 16

  write (*, '(I0,1X,I0,1X,I0,1X,I0,1X,I0,1X,I0)') matrix_shape(1), matrix_shape(2), &
      matrix_lower(1), matrix_lower(2), matrix_upper(1), matrix_upper(2)
  write (*, '(I0,1X,I0,1X,I0,1X,I0,1X,I0,1X,I0)') section_shape(1), section_shape(2), &
      section_lower(1), section_lower(2), section_upper(1), section_upper(2)
  write (*, '(I0,1X,I0,1X,I0,1X,I0,1X,I0)') size(matrix), size8, lbound(dynamic, 1), &
      ubound(dynamic, 2), size(empty)

  deallocate(dynamic, dynamic_shape, empty)

contains

  subroutine check_assumed_shape(array, second_extent)
    integer, intent(in) :: array(0:, :)
    integer, intent(in) :: second_extent
    integer :: dummy_shape(2), dummy_lower(2), dummy_upper(2)

    dummy_shape = shape(array)
    dummy_lower = lbound(array)
    dummy_upper = ubound(array)
    if (any(dummy_shape /= [3, second_extent])) error stop 21
    if (any(dummy_lower /= [0, 1])) error stop 22
    if (any(dummy_upper /= [2, second_extent])) error stop 23
    if (size(array) /= 3 * second_extent .or. &
        size(array, 2) /= second_extent) error stop 24
  end subroutine check_assumed_shape

  subroutine check_strided_section(array)
    integer, intent(in) :: array(-2:, 5:)

    if (size(array, 1) /= 2 .or. size(array, 2) /= 3) error stop 26
    if (lbound(array, 1) /= -2 .or. lbound(array, 2) /= 5) error stop 27
    if (array(-2, 5) /= 7 .or. array(-1, 5) /= 9 .or. &
        array(-2, 6) /= 4 .or. array(-1, 6) /= 6 .or. &
        array(-2, 7) /= 1 .or. array(-1, 7) /= 3) error stop 28
  end subroutine check_strided_section

  subroutine forward_strided_section(array)
    integer, intent(in) :: array(:, :)

    call check_strided_section(array)
  end subroutine forward_strided_section

  integer function array_element_count(array) result(count_value)
    integer, intent(in) :: array(:, :)
    count_value = size(array)
  end function array_element_count

  integer function strided_sum(array) result(total)
    integer, intent(in) :: array(:, :)
    integer :: row, column

    total = 0
    do column = 1, size(array, 2)
      do row = 1, size(array, 1)
        total = total + array(row, column)
      end do
    end do
  end function strided_sum

end program array_inquiry
