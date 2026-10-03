module constructor_finalization_cases
  implicit none
  integer :: scalar_final_count = 0, vector_final_count = 0, final_total = 0
  type :: object
    integer, allocatable :: values(:)
  contains
    final :: finalize_scalar, finalize_vector
  end type
contains
  subroutine finalize_scalar(value)
    type(object), intent(inout) :: value
    scalar_final_count = scalar_final_count + 1
    if (allocated(value%values)) final_total = final_total + sum(value%values)
  end subroutine
  subroutine finalize_vector(value)
    type(object), intent(inout) :: value(:)
    vector_final_count = vector_final_count + size(value)
  end subroutine
  function owned(number) result(value)
    integer, intent(in) :: number
    type(object), allocatable :: value
    allocate(value)
    allocate(value%values(1))
    value%values(1) = number
  end function
  subroutine inspect(values, expected_count, expected_finals)
    type(object), intent(in) :: values(:)
    integer, intent(in) :: expected_count, expected_finals
    if (size(values) /= expected_count) stop 1
    if (scalar_final_count /= expected_finals) stop 2
    if (vector_final_count /= 0) stop 3
    if (any(values(1)%values /= [1])) stop 4
  end subroutine
end module

program constructor_finalization
  use constructor_finalization_cases
  implicit none
  integer :: i
  type(object), allocatable :: objects(:)
  call inspect([owned(1), owned(2)], 2, 0)
  if (scalar_final_count /= 2 .or. vector_final_count /= 0 .or. final_total /= 3) stop 5
  call inspect([(owned(i), i=1,17)], 17, 2)
  if (scalar_final_count /= 19 .or. vector_final_count /= 0 .or. final_total /= 156) stop 6
  allocate(objects(3))
  do i=1,3
    allocate(objects(i)%values(1))
    objects(i)%values(1) = 1
  end do
  call inspect(objects(3:1:-1), 3, 19)
  if (scalar_final_count /= 19 .or. vector_final_count /= 0 .or. final_total /= 156) stop 7
  print *, 'constructor finalization passed'
end program
