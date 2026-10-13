subroutine descriptor_read(values, total)
  implicit none
  integer, volatile :: values(:)
  integer, intent(out) :: total
  total = sum(values)
end subroutine
