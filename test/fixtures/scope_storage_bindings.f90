! Independent F2018 19.5.1.4(2), items (9) and (10), binding contracts.
! GNU Fortran 16.1 rejects the implicit local DATA shadow in this fixture;
! it is not a valid oracle for that binding path.
module storage_host
  integer :: initialized = 83
  integer :: aliased = 97
contains
  subroutine local_storage()
    implicit integer(a, i, l)
    integer :: local_value
    data initialized /41/
    equivalence(aliased, local_value)
    aliased = 47
    if (initialized /= 41 .or. local_value /= 47) stop 1
    initialized = initialized + 1
  end subroutine local_storage
end module storage_host

program scope_storage_bindings
  use storage_host
  call local_storage()
  if (initialized /= 83 .or. aliased /= 97) stop 2
  print *, 'storage binding passed'
end program scope_storage_bindings
