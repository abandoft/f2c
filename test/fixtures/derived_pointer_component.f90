program derived_pointer_component
  implicit none
  type :: node
    integer, pointer :: value
  end type node
  type(node) :: item
  integer, target :: storage

  storage = 7
  item%value => storage
  if (.not. associated(item%value)) stop 1
  if (.not. associated(item%value, storage)) stop 2
  item%value = 19
  if (storage /= 19) stop 3
  nullify(item%value)
  if (associated(item%value)) stop 4
end program derived_pointer_component
