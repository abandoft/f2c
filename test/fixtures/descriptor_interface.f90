subroutine descriptor_add(values)
  implicit none
  integer, volatile, intent(inout) :: values(:)
  values = values + 10
end subroutine

subroutine descriptor_alias(view, target)
  implicit none
  integer, pointer, intent(inout) :: view(:)
  integer, target, volatile, intent(inout) :: target(:)
  view => target
end subroutine

subroutine descriptor_text(values)
  implicit none
  character(*), volatile, intent(inout) :: values(:)
  values = 'ok'
end subroutine
