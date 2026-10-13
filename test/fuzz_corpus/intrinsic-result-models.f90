program intrinsic_result_models
  implicit none
  integer, parameter :: selected = 8
  integer(kind=1) :: values(2,2)
  logical(kind=8) :: mask(2,2)
  integer :: result(8), i
  values = reshape([1_1,2_1,3_1,4_1], [(i,i=2,2),2])
  mask = .true._8
  result = [kind(digits(1_8)), kind(bit_size(1_8)), &
    kind(matmul(matrix_b=values,matrix_a=values)), &
    kind(dot_product(vector_b=values(:,1),vector_a=values(:,2))), &
    kind(all(mask=mask)), kind(logical(l=mask)), &
    kind(count(kind=selected/4,mask=mask)), &
    kind(real(a=cmplx(1.0_8,kind=selected)))]
  print *, result
end program
