! F2018 10.2.1.3(3): scalar assignment preserves bounds, not deferred length.
! GNU Fortran 16.1 leaves the old length; this is an independent contract test.
! Zero-length scalar SPREAD has the SOURCE type parameters (F2018 16.9.181).
! GNU Fortran 16.1 instead reports a missing return-array descriptor.
module character_broadcast_storage
  implicit none
  character(:), allocatable, target :: values(:), grid(:,:)
end module
program character_broadcast_reallocation
  use character_broadcast_storage
  implicit none
  character(:), pointer :: alias
  character(len=-3) :: zero_scalar, zero_values(2), zero_empty(0)
  integer :: status
  zero_values = spread(zero_scalar,1,2)
  zero_empty = spread(zero_scalar,1,0)
  if (len(zero_values) /= 0 .or. size(zero_values) /= 2) stop 17
  if (len(zero_empty) /= 0 .or. size(zero_empty) /= 0) stop 18
  allocate(character(len=0) :: values(2), stat=status)
  if (status /= 0) stop 1
  values = 'overlap'
  if (len(values) /= 7 .or. any(values /= 'overlap')) stop 2
  values = values(1)(2:4)
  if (len(values) /= 3 .or. any(values /= 'ver')) stop 3
  alias => values(1)
  values = 'new'
  if (.not. associated(alias,values(1)) .or. alias /= 'new') stop 4
  nullify(alias)
  values = ''
  if (len(values) /= 0 .or. size(values) /= 2) stop 5
  deallocate(values)
  allocate(character(len=0) :: values(-2:-1), stat=status)
  if (status /= 0) stop 6
  values = 'bounds'
  if (len(values) /= 6 .or. any(values /= 'bounds')) stop 7
  if (lbound(values,1) /= -2 .or. ubound(values,1) /= -1) stop 8
  deallocate(values)
  allocate(character(len=3) :: values(0), stat=status)
  if (status /= 0) stop 9
  values = 'empty'
  if (.not. allocated(values)) stop 10
  if (len(values) /= 5 .or. size(values) /= 0) stop 11
  deallocate(values)
  allocate(character(len=1) :: grid(-1:0,3:4), stat=status)
  if (status /= 0) stop 12
  grid = 'rank2'
  if (len(grid) /= 5 .or. any(grid /= 'rank2')) stop 13
  if (any(shape(grid) /= [2,2])) stop 14
  if (any(lbound(grid) /= [-1,3])) stop 15
  grid = grid(0,4)(2:3)
  if (len(grid) /= 2 .or. any(grid /= 'an')) stop 16
  deallocate(grid)
  print '(a)', 'character broadcast reallocation passed'
end program
