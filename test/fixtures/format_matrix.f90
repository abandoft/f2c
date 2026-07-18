program format_matrix
  implicit none
  character(24) :: runtime_format

  runtime_format = '(I4,1X,A)'
  write(*, '(I6,1X,B8.8,1X,O6.6,1X,Z4.4)') -42, 10, 10, 10
  write(*, '(F8.2,1X,E12.4,1X,D12.4,1X,G12.4)') 12.5, 12.5, 12.5, 12.5
  write(*, '(SP,I4,SS,1X,L3,1X,A5)') 42, .true., 'xy'
  write(*, '(2(I3,1X),":",I0)') 1, 2, 3
  write(*, '(1P,E12.4,1X,0P,F8.2)') 1.25, 1.25
  write(*, '(D16.6,1X,D16.6)') 1.7976931348623157d308, 2.2250738585072014d-308
  write(*, '(DC,F8.2,DP,1X,F8.2)') 1.25, 1.25
  write(*, '(A,T6,A,TL2,A,TR1,A)') 'A', 'B', 'C', 'D'
  write(*, runtime_format) 7, 'dynamic'
  write(*, 100) 8, 'label'

100 format(I4,1X,A)
end program format_matrix
