module allocation_finalization_cases
  implicit none
  integer :: scalar_finals = 0, vector_finals = 0
  type :: cell
    integer,allocatable :: values(:)
  contains
    final :: finish_scalar, finish_vector
  end type
  type(cell),target :: template, templates(2)
contains
  subroutine finish_scalar(value)
    type(cell),intent(inout) :: value
    if (.not.allocated(value%values)) stop 90
    scalar_finals = scalar_finals + 1
  end subroutine
  subroutine finish_vector(value)
    type(cell),intent(inout) :: value(:)
    integer :: i
    do i=1,size(value)
      if (.not.allocated(value(i)%values)) stop 91
    end do
    vector_finals = vector_finals + 1
  end subroutine
  function owned() result(value)
    type(cell),allocatable :: value
    allocate(value)
    allocate(value%values,source=[7,9])
  end function
  function borrowed() result(value)
    type(cell),pointer :: value
    value => template
  end function
  subroutine local_scope()
    type(cell),allocatable :: local
    allocate(local,source=template)
    if (scalar_finals /= 5) stop 92
  end subroutine
end module
program allocation_result_finalization
  use allocation_finalization_cases
  implicit none
  type(cell),allocatable :: first, second, array(:)
  allocate(template%values,source=[1,2])
  allocate(templates(1)%values,source=[3])
  allocate(templates(2)%values,source=[4,5])
  allocate(first, second, source=owned())
  if (scalar_finals /= 1 .or. vector_finals /= 0) stop 1
  if (any(first%values /= [7,9]) .or. any(second%values /= first%values)) stop 2
  deallocate(first,second)
  if (scalar_finals /= 3 .or. vector_finals /= 0) stop 3
  allocate(first,source=template)
  if (scalar_finals /= 3) stop 4
  deallocate(first)
  if (scalar_finals /= 4) stop 5
  allocate(first,source=borrowed())
  if (scalar_finals /= 4 .or. any(first%values /= [1,2])) stop 6
  deallocate(first)
  if (scalar_finals /= 5) stop 7
  allocate(array,source=templates(2:1:-1))
  if (scalar_finals /= 5 .or. vector_finals /= 0) stop 8
  if (any(array(1)%values /= [4,5]) .or. any(array(2)%values /= [3])) stop 9
  deallocate(array)
  if (scalar_finals /= 5 .or. vector_finals /= 1) stop 10
  call local_scope()
  if (scalar_finals /= 6 .or. vector_finals /= 1) stop 11
  block
    type(cell),allocatable :: local
    allocate(local,source=template)
    if (scalar_finals /= 6) stop 12
  end block
  if (scalar_finals /= 7 .or. vector_finals /= 1) stop 13
  deallocate(template%values,templates(1)%values,templates(2)%values)
  print '(A)', 'allocation finalization contracts passed'
end program
