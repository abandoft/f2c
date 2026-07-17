program descriptor_temporary
  implicit none

  type :: box
    integer :: value
    integer, allocatable :: payload(:)
  end type box

  integer :: values(5), order(3), matrix(2, 2)
  integer :: box_order(2), word_order(3), index
  character(len=3) :: words(4)
  type(box) :: boxes(3)

  values = (/10, 20, 30, 40, 50/)
  order = (/5, 2, 4/)
  matrix = reshape((/1, 2, 3, 4/), (/2, 2/))
  words = (/'aa ', 'b  ', 'ccc', 'd  '/)
  word_order = (/4, 2, 3/)
  box_order = (/3, 1/)

  do index = 1, 3
    boxes(index)%value = index
    allocate(boxes(index)%payload(1))
    boxes(index)%payload(1) = index * 101
  end do

  call check_integer(values(order) + 1)
  call check_constructor((/7, 8, 9/))
  call check_matrix(transpose(matrix + 10))
  call check_character(words(word_order) // 'x')
  call check_boxes(boxes(box_order))
  call check_bumped(bump(boxes))

  write (*, '(A)') 'descriptor-temporary-ok'

contains

  subroutine check_integer(actual)
    integer, intent(in) :: actual(:)
    if (size(actual) /= 3) error stop 1
    if (any(actual /= (/51, 21, 41/))) error stop 2
  end subroutine check_integer

  subroutine check_constructor(actual)
    integer, intent(in) :: actual(:)
    if (size(actual) /= 3) error stop 3
    if (any(actual /= (/7, 8, 9/))) error stop 4
  end subroutine check_constructor

  subroutine check_matrix(actual)
    integer, intent(in) :: actual(:, :)
    if (size(actual, 1) /= 2 .or. size(actual, 2) /= 2) error stop 5
    if (actual(1, 1) /= 11 .or. actual(2, 1) /= 13 .or. &
        actual(1, 2) /= 12 .or. actual(2, 2) /= 14) error stop 6
  end subroutine check_matrix

  subroutine check_character(actual)
    character(len=*), intent(in) :: actual(:)
    if (size(actual) /= 3) error stop 7
    if (actual(1) /= 'd  x' .or. actual(2) /= 'b  x' .or. actual(3) /= 'cccx') error stop 8
  end subroutine check_character

  subroutine check_boxes(actual)
    type(box), intent(in) :: actual(:)
    if (size(actual) /= 2) error stop 9
    if (actual(1)%value /= 3 .or. actual(1)%payload(1) /= 303) error stop 10
    if (actual(2)%value /= 1 .or. actual(2)%payload(1) /= 101) error stop 11
  end subroutine check_boxes

  subroutine check_bumped(actual)
    type(box), intent(in) :: actual(:)
    if (size(actual) /= 3) error stop 12
    if (actual(1)%value /= 11 .or. actual(3)%value /= 13) error stop 13
    if (actual(1)%payload(1) /= 101 .or. actual(3)%payload(1) /= 303) error stop 14
  end subroutine check_bumped

  elemental function bump(source) result(output)
    type(box), intent(in) :: source
    type(box) :: output

    output = source
    output%value = source%value + 10
  end function bump

end program descriptor_temporary
