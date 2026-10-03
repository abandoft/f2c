! F2018 9.5.4 and C1539 permit these simply contiguous column sections.
! GNU Fortran 16.1 rejects them because it rejects every VOLATILE section
! associated with an explicit-shape VOLATILE dummy. Keep an independent
! executable contract rather than changing the source to satisfy that restriction.
program qualified_contiguous
  implicit none
  integer, volatile :: matrix(4,3)
  character(len=3), volatile :: words(2,2)
  matrix = reshape([1,2,3,4,5,6,7,8,9,10,11,12],[4,3])
  words = reshape(['one','two','six','ten'],[2,2])
  call numeric_column(matrix(:,2))
  if (any(matrix /= reshape([1,2,3,4,105,106,107,108,9,10,11,12],[4,3]))) stop 1
  call character_column(words(:,2))
  if (any(words /= reshape(['one','two','six','new'],[2,2]))) stop 2
  print '(A)', 'qualified contiguous contracts passed'
contains
  subroutine numeric_column(values)
    integer, volatile :: values(4)
    values = values + 100
  end subroutine numeric_column

  subroutine character_column(values)
    character(len=*), volatile :: values(2)
    if (values(1) /= 'six') stop 3
    values(2) = 'new'
  end subroutine character_column
end program qualified_contiguous
