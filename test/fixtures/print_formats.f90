program print_formats
  implicit none
  integer :: iterator
  character(20) :: runtime_format

  runtime_format = "('RUNTIME',1X,I3)"
  print '(A,1X,I3)', 'LITERAL', 7
  print 100, (iterator, iterator=1,3)
  print runtime_format, 9
  print *

100 format('LABEL',1X,3(I2,1X))
end program print_formats
