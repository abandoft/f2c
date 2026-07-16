subroutine project_apply(value)
  implicit none
  double precision, intent(inout) :: value
  double precision :: project_combine
  external project_combine, project_scale
  value = project_combine(third=5.0d0, first=value, second=4.0d0)
  call project_scale(scale=2.0d0, value=value)
end subroutine project_apply
