subroutine project_scale(value, scale)
  implicit none
  double precision, intent(inout) :: value
  double precision, intent(in) :: scale
  value = value * scale
end subroutine project_scale

double precision function project_combine(first, second, third)
  implicit none
  double precision, intent(in) :: first, second, third
  project_combine = first + 10.0d0 * second + 100.0d0 * third
end function project_combine
