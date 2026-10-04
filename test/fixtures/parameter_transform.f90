program parameter_transform
  use iso_fortran_env, only: int64, real32, real64
  implicit none
  type :: pair
    integer :: code
    character(len=2) :: label
  end type
  integer :: i, j
  integer, parameter :: nested(6) = [((i + 10*j, i=1,2), j=1,3)]
  integer, parameter :: flat(6) = [[1,2], [3,4], [5,6]]
  integer, parameter :: matrix(-1:0,4:6) = reshape(flat,[2,3],order=[2,1])
  integer, parameter :: dependent(3) = [matrix(-1,4), matrix(0,5), matrix(-1,6)]
  integer, parameter :: transposed(3,2) = transpose(matrix)
  logical, parameter :: mask(2,3) = reshape([.false.,.true.,.false.,.true.,.false.,.true.],[2,3])
  integer, parameter :: compact(3) = pack(matrix,mask)
  integer, parameter :: restored(2,3) = unpack(compact,mask,-7)
  integer :: expanded(2,2,3)
  integer :: spread_input(2,3)
  integer, parameter :: shifted(2,3) = cshift(matrix,[1,-1,0],dim=1)
  integer, parameter :: off_end(2,3) = eoshift(matrix,[1,-1,3],boundary=[9,8,7],dim=1)
  integer, parameter :: first_location(2) = findloc(matrix,5)
  integer, parameter :: row_location(3) = findloc(matrix,5,dim=1)
  integer, parameter :: scalar_location = findloc([1,2,2],2,dim=1,back=.true.)
  integer, parameter :: padded(2,3) = reshape([1,2],[2,3],pad=[8,9])
  integer, parameter :: empty(0,2) = spread([1,2],1,-3)
  integer, parameter :: empty_locations(2) = findloc(empty,1,dim=1)
  real(real32), parameter :: rounded(1) = [real(1.0000000596046448_real64,real32)]
  real(real64), parameter :: promoted(2) = [real(rounded(1),real64), real(rounded(1),real64)]
  character(len=3), parameter :: words(3) = ['A ', 'BC', 'DE']
  character(len=3), parameter :: selected_words(2) = pack(words,[.true.,.false.,.true.])
  character(len=3), parameter :: moved_words(2) = eoshift(selected_words,1)
  character(len=3), parameter :: repeated_words(2,2) = spread(selected_words,2,2)
  type(pair), parameter :: pairs(3) = [pair(1,'AA'), pair(2,'BB'), pair(3,'CC')]
  type(pair), parameter :: selected_pairs(2) = pack(pairs,[.true.,.false.,.true.])
  type(pair), parameter :: repeated_pairs(2,2) = spread(selected_pairs,2,2)
  type(pair), parameter :: moved_pairs(2) = eoshift(selected_pairs,1,boundary=pair(9,'ZZ'))
  integer :: position

  spread_input = matrix
  expanded = spread(spread_input,2,2)
  if (any(nested /= [11,12,21,22,31,32])) stop 1
  if (any(dependent /= [1,5,3])) stop 2
  if (any(reshape(transposed,[6]) /= flat)) stop 3
  if (any(compact /= [4,5,6])) stop 4
  if (any(reshape(restored,[6]) /= [-7,4,-7,5,-7,6])) stop 5
  if (any(reshape(expanded,[12]) /= [1,4,1,4,2,5,2,5,3,6,3,6])) stop 6
  if (any(reshape(shifted,[6]) /= [4,1,5,2,3,6])) stop 7
  if (any(reshape(off_end,[6]) /= [4,9,8,2,7,7])) stop 8
  if (any(first_location /= [2,2]) .or. any(row_location /= [0,2,0])) stop 9
  if (scalar_location /= 3) stop 10
  if (any(reshape(padded,[6]) /= [1,2,8,9,8,9])) stop 11
  if (size(empty) /= 0 .or. any(empty_locations /= [0,0])) stop 12
  do position = 1,2
    if (transfer(promoted(position),0_int64) /= transfer(real(rounded(1),real64),0_int64)) stop 13
  end do
  if (moved_words(1) /= 'DE ' .or. moved_words(2) /= '   ') stop 14
  if (any(repeated_words(:,1) /= selected_words) .or. &
      any(repeated_words(:,2) /= selected_words)) stop 15
  do position = 1,2
    if (repeated_pairs(position,1)%code /= selected_pairs(position)%code) stop 16
    if (repeated_pairs(position,2)%label /= selected_pairs(position)%label) stop 17
  end do
  if (moved_pairs(1)%code /= 3 .or. moved_pairs(2)%label /= 'ZZ') stop 18
  print '(A)', 'typed constant array constructors, dependencies and transformations passed'
end program
