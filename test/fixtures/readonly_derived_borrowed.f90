module readonly_derived_borrowed_model
  implicit none
  type :: item
    integer :: value = 0
    integer, allocatable :: payload(:)
  end type
contains
  subroutine inspect(source, matrix)
    type(item), intent(in) :: source(:), matrix(:,:)
    type(item) :: values(4), expanded(4,2), transposed(2,2)
    logical :: mask(4)
    mask = .true.
    values = reshape(source(4:1:-1), [4])
    if (values(1)%value /= 4 .or. values(1)%payload(1) /= 44) stop 1
    values(1)%payload(1) = -1
    if (source(4)%payload(1) /= 44) stop 2
    values = pack(source(4:1:-1), mask, vector=source)
    if (values(2)%value /= 3 .or. values(2)%payload(1) /= 33) stop 3
    values = unpack(source, mask, field=source(4:1:-1))
    if (values(2)%value /= 2 .or. values(2)%payload(1) /= 22) stop 4
    expanded = spread(source(4:1:-1), dim=2, ncopies=2)
    if (expanded(1,2)%payload(1) /= 44) stop 5
    expanded(1,2)%payload(1) = -2
    if (source(4)%payload(1) /= 44) stop 6
    values = cshift(source(4:1:-1), shift=1)
    if (values(1)%payload(1) /= 33 .or. values(4)%payload(1) /= 44) stop 7
    values = eoshift(source(4:1:-1), shift=1, boundary=source(1))
    if (values(1)%payload(1) /= 33 .or. values(4)%payload(1) /= 11) stop 8
    transposed = transpose(matrix(2:1:-1,2:1:-1))
    if (transposed(1,1)%payload(1) /= 44 .or. transposed(2,2)%payload(1) /= 11) stop 9
    transposed(1,1)%payload(1) = -3
    if (matrix(2,2)%payload(1) /= 44) stop 10
  end subroutine
end module

program readonly_derived_borrowed
  use readonly_derived_borrowed_model
  implicit none
  type(item) :: source(4), matrix(2,2)
  integer :: i
  do i=1,4
    source(i)%value = i
    allocate(source(i)%payload(1))
    source(i)%payload(1) = i * 11
  end do
  matrix = reshape(source, [2,2])
  call inspect(source, matrix)
  print '(A)', 'readonly derived borrowed transform contracts passed'
end program
