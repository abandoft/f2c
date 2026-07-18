program data_statement
  implicit none
  integer, parameter :: repeat = 2
  integer :: matrix(2,2), vector(2), mixed(2), tail, i, j
  character(len=8) :: label
  complex :: values(2)

  data ((matrix(i,j), i=1,2), j=1,2) / 1, 2, 3, 4 /
  data vector / repeat*5 /
  data mixed, tail / 6, 7, 8 /
  data label / 'a/b,c' /
  data values / (1.0,2.0), (3.0,4.0) /

  if (any(vector /= [5, 5])) stop 1
  if (any(matrix(:,1) /= [1, 2])) stop 2
  if (any(matrix(:,2) /= [3, 4])) stop 3
  if (any(mixed /= [6, 7]) .or. tail /= 8) stop 4
  if (label /= 'a/b,c') stop 5
  if (real(values(1)) /= 1.0 .or. aimag(values(1)) /= 2.0) stop 6
  if (real(values(2)) /= 3.0 .or. aimag(values(2)) /= 4.0) stop 7
end program data_statement
