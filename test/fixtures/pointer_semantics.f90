program pointer_semantics
  implicit none
  integer, target :: value
  integer, target :: values(3)
  integer, pointer :: scalar
  integer, pointer :: vector(:)

  value = 4
  values = [1, 2, 3]
  nullify(scalar, vector)
  if (associated(scalar) .or. associated(vector)) stop 1

  scalar => value
  scalar => null(mold=scalar)
  if (associated(scalar)) stop 7

  scalar => value
  if (.not. associated(scalar, value)) stop 2
  scalar = 9
  if (value /= 9) stop 3

  vector => values
  if (.not. associated(vector, values)) stop 4
  vector(2) = 8
  if (values(2) /= 8) stop 5

  nullify(scalar, vector)
  if (associated(scalar) .or. associated(vector)) stop 6
end program pointer_semantics
