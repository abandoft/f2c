program qualified_association
  integer, volatile :: values(6)
  call consume(values(6:1:-2))
contains
  subroutine consume(values)
    integer, volatile, contiguous :: values(:)
    values = 1
  end subroutine consume
end program qualified_association
