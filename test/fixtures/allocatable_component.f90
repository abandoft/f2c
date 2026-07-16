program allocatable_component
  implicit none
  type :: container
    integer, allocatable :: values(:)
  end type container
  type(container) :: box
  type(container) :: copy

  if (allocated(box%values)) stop 1
  allocate(box%values(-1:1))
  if (.not. allocated(box%values)) stop 2
  box%values(-1) = 7
  box%values(0) = 11
  box%values(1) = 13
  if (box%values(-1) + box%values(0) + box%values(1) /= 31) stop 3
  copy = box
  box%values(0) = 99
  if (copy%values(0) /= 11) stop 4
  deallocate(box%values)
  if (allocated(box%values)) stop 5
  if (.not. allocated(copy%values)) stop 6
end program allocatable_component
