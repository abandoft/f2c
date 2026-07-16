subroutine constructor_values(n, offset, values, words)
  implicit none
  integer, intent(in) :: n, offset
  integer, intent(out) :: values(2*n+6)
  character(len=3), intent(out) :: words(3)
  integer :: i, j, tail(2)
  character(len=3) :: source(3)

  source = ['A  ', 'BB ', 'CCC']
  tail = [900, 901]
  values = [(offset+i, i=1,n), ((100*j+i, i=1,2), j=1,2), &
            (i, i=n,1,-1), tail]
  words = [(source(i), i=3,1,-1)]
end subroutine constructor_values
