program intrinsic_result_model_main
  implicit none
  integer(kind=8) :: wide
  integer(kind=1) :: narrow(2,2)
  logical(kind=8) :: mask(2,2)
  integer :: models(14), choices(6)
  integer :: matrix(2,3), i
  wide = 17_8
  narrow = reshape([1_1,2_1,3_1,4_1], [2,2])
  mask = .true._8
  call intrinsic_result_probe(wide, narrow, mask, models, choices)
  if (any(models /= [4,4,4,8,1,1,8,8,4,4,8,4,8,2])) stop 1
  if (any(choices /= [103,144,21,108,101,24])) stop 2
  matrix = reshape([1,2,3,4,5,6], [(i,i=2,3)], order=[(i,i=2,1,-1)])
  if (any(matrix(1,:) /= [1,2,3]) .or. any(matrix(2,:) /= [4,5,6])) stop 3
  print '(14(I0,1X))', models
  print '(6(I0,1X))', choices
end program
