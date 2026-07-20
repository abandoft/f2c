program block_data_common
  implicit none
  integer :: counter, values(3)
  double precision :: factor
  logical :: ready
  complex :: point
  character(len=4) :: label
  character(len=3) :: names(2)
  common /state/ counter, values, factor, ready, point, label, names

  write (*, '(I0,1X,3(I0,1X),F6.2,1X,L1,1X,2(F6.2,1X),A,1X,A,1X,A)') &
      counter, values, factor, ready, point, label, names(1), names(2)
end program block_data_common

block data initialize_state
  implicit none
  integer :: counter, values(3)
  double precision :: factor
  logical :: ready
  complex :: point
  character(len=4) :: label
  character(len=3) :: names(2)
  common /state/ counter, values, factor, ready, point, label, names
  data counter / 7 /, values / 11, 13, 17 /
  data factor / 2.5d0 /, ready / .true. /, point / (1.25, -0.5) /
  data label / 'ok' /
  data names / 'ab', 'xyz' /
end block data initialize_state
