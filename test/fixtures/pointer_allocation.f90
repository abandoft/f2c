module pointer_allocation_support
  implicit none

  integer :: finalized = 0

  type :: cell
    integer :: value = 0
  contains
    final :: finalize_cell, finalize_cell_array
  end type cell

  type :: box
    integer, pointer :: values(:) => null()
  end type box

  type :: owned_cell
    integer, allocatable :: payload(:)
  end type owned_cell

contains

  subroutine finalize_cell(value)
    type(cell), intent(inout) :: value
    finalized = finalized + 1
    value%value = -1
  end subroutine finalize_cell

  subroutine finalize_cell_array(values)
    type(cell), intent(inout) :: values(:)
    finalized = finalized + size(values)
  end subroutine finalize_cell_array

  subroutine allocate_values(values, count, status, message)
    integer, pointer, intent(inout) :: values(:)
    integer, intent(in) :: count
    integer, intent(out) :: status
    character(len=*), intent(out) :: message

    message = 'unchanged'
    allocate(values(-1:count), stat=status, errmsg=message)
  end subroutine allocate_values

  subroutine release_values(values, status, message)
    integer, pointer, intent(inout) :: values(:)
    integer, intent(out) :: status
    character(len=*), intent(out) :: message

    message = 'unchanged'
    deallocate(values, stat=status, errmsg=message)
  end subroutine release_values

end module pointer_allocation_support

program pointer_allocation
  use pointer_allocation_support
  implicit none

  integer, pointer :: values(:)
  integer, pointer :: alias(:)
  character(:), pointer :: text
  type(cell), pointer :: cells(:)
  type(owned_cell) :: prototype
  type(owned_cell), pointer :: copies(:)
  type(box) :: containers(1)
  integer :: status
  character(len=64) :: message

  call allocate_values(values, 3, status, message)
  if (status /= 0 .or. .not. associated(values)) error stop 1
  if (lbound(values, 1) /= -1 .or. ubound(values, 1) /= 3) error stop 2
  values = [1, 2, 3, 4, 5]

  alias => values
  allocate(values(2), stat=status, errmsg=message)
  if (status /= 0 .or. lbound(values, 1) /= 1 .or. ubound(values, 1) /= 2) error stop 3
  deallocate(alias, stat=status, errmsg=message)
  if (status /= 0 .or. associated(alias)) error stop 4

  call release_values(values, status, message)
  if (status /= 0 .or. associated(values)) error stop 5

  call release_values(values, status, message)
  if (status == 0 .or. len_trim(message) == 0) error stop 6

  allocate(character(len=5) :: text, stat=status, errmsg=message)
  if (status /= 0 .or. len(text) /= 5) error stop 7
  text = 'abc'
  if (len(text) /= 5 .or. text /= 'abc  ') error stop 8
  deallocate(text, stat=status, errmsg=message)
  if (status /= 0 .or. associated(text)) error stop 9

  allocate(containers(1)%values(0:2), stat=status, errmsg=message)
  if (status /= 0) error stop 10
  containers(1)%values(0) = 7
  containers(1)%values(1) = 8
  containers(1)%values(2) = 9
  if (containers(1)%values(0) + containers(1)%values(1) + containers(1)%values(2) /= 24) &
    error stop 11
  deallocate(containers(1)%values, stat=status, errmsg=message)
  if (status /= 0 .or. associated(containers(1)%values)) error stop 12

  allocate(cells(2), stat=status, errmsg=message)
  if (status /= 0) error stop 13
  cells(1)%value = 10
  cells(2)%value = 20
  deallocate(cells, stat=status, errmsg=message)
  if (status /= 0 .or. finalized /= 2) error stop 14

  allocate(prototype%payload(2))
  prototype%payload = [11, 13]
  allocate(copies(2), source=prototype, stat=status, errmsg=message)
  if (status /= 0) error stop 15
  prototype%payload(1) = -1
  if (copies(1)%payload(1) /= 11 .or. copies(2)%payload(2) /= 13) error stop 16
  deallocate(copies, stat=status, errmsg=message)
  if (status /= 0) error stop 17
  deallocate(prototype%payload)

  print '(a)', 'pointer allocation ok'
end program pointer_allocation
