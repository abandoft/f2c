program namelist_multidimensional_auto_allocate
  implicit none

  integer, allocatable :: matrix(:, :)
  integer, allocatable :: cube(:, :, :)
  integer, allocatable :: ambiguous(:, :)
  integer :: matrix_snapshot(-1:1, 2:4)
  integer :: status
  integer :: i
  integer :: j
  integer :: expected
  character(1024) :: record
  namelist /sample/ matrix, cube, ambiguous

  record = '&sample matrix(-1:1,2:4:2)=1,2,3,4,5,6, ' // &
           'cube(0:1,-1:0,3:4)=11,12,13,14,15,16,17,18 /'
  read(record, nml=sample, iostat=status)

  if (status /= 0) stop 1
  if (.not. allocated(matrix) .or. .not. allocated(cube)) stop 2
  if (lbound(matrix, 1) /= -1 .or. ubound(matrix, 1) /= 1) stop 3
  if (lbound(matrix, 2) /= 2 .or. ubound(matrix, 2) /= 4) stop 4
  if (any(matrix(:, 2) /= [1, 2, 3])) stop 5
  if (any(matrix(:, 3) /= 0)) stop 6
  if (any(matrix(:, 4) /= [4, 5, 6])) stop 7
  if (lbound(cube, 1) /= 0 .or. ubound(cube, 1) /= 1) stop 8
  if (lbound(cube, 2) /= -1 .or. ubound(cube, 2) /= 0) stop 9
  if (lbound(cube, 3) /= 3 .or. ubound(cube, 3) /= 4) stop 10
  if (cube(0, -1, 3) /= 11 .or. cube(1, -1, 3) /= 12) stop 16
  if (cube(0, 0, 3) /= 13 .or. cube(1, 0, 3) /= 14) stop 17
  if (cube(0, -1, 4) /= 15 .or. cube(1, -1, 4) /= 16) stop 18
  if (cube(0, 0, 4) /= 17 .or. cube(1, 0, 4) /= 18) stop 19

  matrix_snapshot = matrix
  record = '&sample matrix=21,22,23,24,25,26,27,28,29, ambiguous=1,2,3,4 /'
  read(record, nml=sample, iostat=status)
  if (status == 0) stop 11
  if (allocated(ambiguous)) stop 12
  if (any(matrix /= matrix_snapshot)) stop 13

  record = '&sample matrix=21,22,23,24,25,26,27,28,29 /'
  read(record, nml=sample, iostat=status)
  if (status /= 0) stop 14
  expected = 21
  do j = 2, 4
    do i = -1, 1
      if (matrix(i, j) /= expected) stop 15
      expected = expected + 1
    end do
  end do
end program namelist_multidimensional_auto_allocate
