program nested_transform_derived_ownership
  implicit none

  type :: item
    integer :: value = 0
    integer, allocatable :: payload(:)
  end type item

  type(item) :: items(3), nested_items(3)
  logical :: mask(3)

  items(1)%value = 10
  items(2)%value = 20
  items(3)%value = 30
  allocate(items(1)%payload(1), items(2)%payload(1), items(3)%payload(1))
  items(1)%payload(1) = 101
  items(2)%payload(1) = 202
  items(3)%payload(1) = 303
  mask = (/.true., .false., .true./)

  nested_items = unpack(pack(items, mask), mask, cshift(items, 1))
  items(1)%payload(1) = 999
  items(3)%payload(1) = 777

  if (.not. allocated(nested_items(1)%payload)) stop 1
  if (.not. allocated(nested_items(2)%payload)) stop 2
  if (.not. allocated(nested_items(3)%payload)) stop 3
  if (nested_items(1)%value /= 10 .or. nested_items(1)%payload(1) /= 101) stop 4
  if (nested_items(2)%value /= 30 .or. nested_items(2)%payload(1) /= 303) stop 5
  if (nested_items(3)%value /= 30 .or. nested_items(3)%payload(1) /= 303) stop 6
end program nested_transform_derived_ownership
