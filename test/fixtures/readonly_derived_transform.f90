module readonly_derived_transform_model
  implicit none
  type :: item
    integer :: value = 0
    integer, allocatable :: payload(:)
    integer, pointer :: link => null()
  end type
  type :: container
    type(item) :: values(4)
  end type
contains
  subroutine check_value(actual, expected)
    type(item), intent(in) :: actual
    integer, intent(in) :: expected
    if (actual%value /= expected) stop 1
    if (.not. allocated(actual%payload)) stop 2
    if (actual%payload(1) /= expected * 11) then
      print '(A,3(I0,1X))', 'payload mismatch: ', actual%value, actual%payload(1), expected
      stop 3
    end if
    if (.not. associated(actual%link)) stop 4
    if (actual%link /= 97) stop 5
  end subroutine

  impure elemental function cloned(source) result(output)
    type(item), intent(in) :: source
    type(item) :: output
    output = source
  end function

  subroutine inspect(source, matrix, boundary, owner)
    type(item), intent(in) :: source(:), matrix(:,:), boundary
    type(container), intent(in) :: owner
    type(item), allocatable :: values(:), expanded(:,:), transposed(:,:)
    logical :: mask(4), empty_mask(0)
    integer :: ordering(4)
    allocate(values(0), expanded(0,0), transposed(0,0))
    mask = [.true., .false., .true., .false.]
    ordering = [4, 2, 3, 1]

    values = reshape(source(4:1:-1), [4])
    call check_value(values(1), 4)
    values(1)%payload(1) = -1
    call check_value(source(4), 4)

    values = reshape((source), [4])
    call check_value(values(1), 1)
    values(1)%payload(1) = -2
    call check_value(source(1), 1)

    values = reshape(source(1:2), [4], pad=source(4:3:-1))
    call check_value(values(3), 4)
    call check_value(values(4), 3)

    values = pack((source(4:1:-1)), mask, vector=source)
    call check_value(values(1), 4)
    call check_value(values(2), 2)
    call check_value(values(3), 3)
    values(2)%payload(1) = -3
    call check_value(source(2), 2)

    values = unpack(source(1:2), mask, field=source(4:1:-1))
    call check_value(values(1), 1)
    call check_value(values(2), 3)
    call check_value(values(3), 2)
    call check_value(values(4), 1)

    expanded = spread(source(4:1:-1), dim=2, ncopies=2)
    call check_value(expanded(1,2), 4)
    expanded(1,2)%payload(1) = -4
    call check_value(source(4), 4)

    values = spread(boundary, dim=1, ncopies=3)
    call check_value(values(2), 4)
    values(2)%payload(1) = -5
    call check_value(boundary, 4)

    values = cshift((source(4:1:-1)), shift=1)
    call check_value(values(1), 3)
    call check_value(values(4), 4)

    values = eoshift(source(4:1:-1), shift=1, boundary=boundary)
    call check_value(values(1), 3)
    call check_value(values(4), 4)

    transposed = transpose(matrix(2:1:-1,2:1:-1))
    call check_value(transposed(1,1), 4)
    call check_value(transposed(2,2), 1)
    transposed(1,1)%payload(1) = -6
    call check_value(matrix(2,2), 4)

    values = reshape(owner%values(4:1:-1), [4])
    call check_value(values(1), 4)
    values(1)%payload(1) = -7
    call check_value(owner%values(4), 4)

    values = reshape(merge(source, source(4:1:-1), mask), [4])
    call check_value(values(1), 1)
    call check_value(values(2), 3)
    call check_value(values(3), 3)
    call check_value(values(4), 1)
    values(1)%payload(1) = -8
    call check_value(source(1), 1)

    values = reshape(cloned(source(4:1:-1)), [4])
    call check_value(values(1), 4)
    values(1)%payload(1) = -9
    call check_value(source(4), 4)

    values = reshape(source(ordering), [4])
    call check_value(values(2), 2)
    values(2)%payload(1) = -10
    call check_value(source(2), 2)

    values = unpack(pack(source, mask), mask, cshift(source, 1))
    call check_value(values(1), 1)
    call check_value(values(2), 3)
    call check_value(values(3), 3)
    call check_value(values(4), 1)

    values = reshape(source(1:0), [0])
    if (size(values) /= 0) stop 6
    values = cshift(source(1:0), 1)
    if (size(values) /= 0) stop 10
    values = eoshift(source(1:0), 1, boundary)
    if (size(values) /= 0) stop 11
    values = unpack(source(1:0), empty_mask, boundary)
    if (size(values) /= 0) stop 12
    transposed = transpose(matrix(1:0,:))
    if (size(transposed) /= 0) stop 13
    expanded = spread(source(1:0), dim=2, ncopies=2)
    if (size(expanded) /= 0) stop 14
    values = pack(source, .false.)
    if (size(values) /= 0) stop 7
    expanded = spread(source, dim=2, ncopies=-3)
    if (size(expanded) /= 0) stop 8
  end subroutine
end module

program readonly_derived_transform
  use readonly_derived_transform_model
  implicit none
  type(item) :: source(4), matrix(2,2)
  type(container) :: owner
  integer, target :: linked = 97
  integer :: i
  do i=1,4
    source(i)%value = i
    allocate(source(i)%payload(1))
    source(i)%payload(1) = i * 11
    source(i)%link => linked
  end do
  matrix = reshape(source, [2,2])
  owner%values = source
  call inspect(source, matrix, source(4), owner)
  do i=1,4
    call check_value(source(i), i)
    call check_value(owner%values(i), i)
  end do
  if (linked /= 97) stop 9
  print '(A)', 'readonly derived transformational value contracts passed'
end program
