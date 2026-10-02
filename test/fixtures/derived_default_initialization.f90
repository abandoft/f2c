module default_initialization_storage
  implicit none
  integer, parameter :: base = 7
  type :: leaf
    integer :: value = 9
  end type
  type :: cell
    integer(kind=8) :: seed = base
    integer :: values(2,2) = 3
    integer :: order(3) = [1,2,3]
    logical :: ready = .true.
    real(kind=8) :: weight = 1.25d0
    complex(kind=8) :: phase = (2.0d0,-3.0d0)
    character(4) :: label = 'ab'
    character(2) :: tags(2) = ['q ','rs']
    integer, pointer :: alias => null()
    integer, allocatable :: payload(:)
    type(leaf) :: nested
    type(leaf) :: rows(2)
  end type
  type, extends(cell) :: child
    integer :: extra = 17
  end type
  type(cell) :: global, globals(2)
  type(leaf) :: initialized = leaf(31), initialized_array(2) = [leaf(5), leaf(6)]
contains
  subroutine verify(value)
    class(cell), intent(in) :: value
    if (value%seed /= 7 .or. any(value%values /= 3)) stop 1
    if (any(value%order /= [1,2,3]) .or. .not. value%ready) stop 2
    if (abs(value%weight-1.25d0) > 1.0d-12) stop 3
    if (abs(value%phase-(2.0d0,-3.0d0)) > 1.0d-12) stop 4
    if (value%label /= 'ab  ' .or. any(value%tags /= ['q ','rs'])) stop 5
    if (associated(value%alias) .or. allocated(value%payload)) stop 6
    if (value%nested%value /= 9 .or. value%rows(1)%value /= 9 .or. value%rows(2)%value /= 9) stop 7
  end subroutine
  subroutine saved_state(expected)
    integer, intent(in) :: expected
    type(cell), save :: value, values(2)
    type(leaf) :: saved_constructor = leaf(31)
    if (value%seed /= expected .or. values(2)%seed /= expected) stop 8
    value%seed = value%seed + 1
    values(2)%seed = values(2)%seed + 1
    if (saved_constructor%value /= expected+24) stop 18
    saved_constructor%value = saved_constructor%value+1
  end subroutine
  subroutine automatic_state()
    type(cell) :: value
    call verify(value)
    value%seed = 100
  end subroutine
  subroutine reset(value)
    type(cell), intent(out) :: value
    call verify(value)
  end subroutine
end module
program derived_default_initialization
  use default_initialization_storage
  implicit none
  type(cell) :: value, copy, values(2)
  type(child) :: extended
  type(cell), allocatable :: heap(:), clone(:)
  type(leaf) :: constructed, nested_copy
  integer :: i
  call verify(value)
  call verify(global)
  call verify(globals(2))
  call verify(values(1))
  call verify(values(2))
  call verify(extended)
  if (extended%extra /= 17) stop 9
  if (initialized%value /= 31 .or. initialized_array(1)%value /= 5 .or. initialized_array(2)%value /= 6) stop 16
  do i = 1, 2
    block
      type(leaf) :: local
      if (local%value /= 9) stop 17
      local%value = 80
    end block
  end do
  call saved_state(7)
  call saved_state(8)
  call automatic_state()
  call automatic_state()
  global%seed = 25
  if (global%seed /= 25) stop 10
  constructed = leaf()
  if (constructed%value /= 9) stop 11
  constructed = leaf(value=29)
  nested_copy = constructed
  if (nested_copy%value /= 29) stop 12
  copy = cell(seed=13)
  if (copy%seed /= 13 .or. copy%nested%value /= 9 .or. copy%rows(2)%value /= 9) stop 19
  if (allocated(copy%payload) .or. associated(copy%alias)) stop 20
  value%seed = 44
  value%nested%value = 33
  value%rows(2)%value = 34
  allocate(value%payload(2))
  value%payload = [4,5]
  copy = value
  value%payload(1) = 90
  if (copy%seed /= 44 .or. copy%nested%value /= 33 .or. copy%rows(2)%value /= 34) stop 13
  if (any(copy%payload /= [4,5])) stop 14
  call reset(copy)
  allocate(heap(2))
  call verify(heap(1))
  heap(2)%seed = 55
  heap(2)%nested%value = 56
  allocate(clone, source=heap)
  if (clone(2)%seed /= 55 .or. clone(2)%nested%value /= 56) stop 15
  deallocate(heap, clone)
  allocate(heap(2))
  heap(1)%seed = 99
  allocate(clone(size(heap)), mold=heap)
  call verify(clone(1))
  deallocate(heap, clone)
  deallocate(value%payload)
  print '(a)', 'derived default initialization passed'
end program
