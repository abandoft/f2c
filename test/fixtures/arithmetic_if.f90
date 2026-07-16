subroutine arithmetic_branch(value, result)
  implicit none
  real, intent(in) :: value
  integer, intent(out) :: result
  if (value) 10, 20, 30
10 result = -1
  return
20 result = 0
  return
30 result = 1
end subroutine arithmetic_branch
