program parameter_spread_contract
  implicit none
  type :: pair
    integer :: code
    character(len=2) :: label
  end type
  integer, parameter :: matrix(2,3) = reshape([1,2,3,4,5,6],[2,3],order=[2,1])
  integer, parameter :: expanded(2,2,3) = spread(matrix,2,2)
  character(len=2), parameter :: words(2,2) = reshape(['AA','BB','CC','DD'],[2,2])
  character(len=2), parameter :: repeated_words(2,2,2) = spread(words,2,2)
  type(pair), parameter :: pairs(2,2) = reshape([pair(1,'AA'),pair(2,'BB'), &
                                               pair(3,'CC'),pair(4,'DD')],[2,2])
  type(pair), parameter :: repeated_pairs(2,2,2) = spread(pairs,2,2)
  integer :: i, j, k
  integer :: copies
  integer :: input(2)
  integer(kind=8) :: shift_value
  integer :: shifted(3)
  integer, allocatable :: dynamic_empty(:,:)
  input = [1,2]
  copies = -3
  dynamic_empty = spread(input,1,copies)
  if (.not. allocated(dynamic_empty) .or. size(dynamic_empty) /= 0) stop 5
  shift_value = -9223372036854775807_8 - 1_8
  shifted = cshift([1,2,3],shift_value)
  if (any(shifted /= [2,3,1])) stop 6
  shifted = eoshift([1,2,3],shift_value)
  if (any(shifted /= [0,0,0])) stop 7
  shift_value = 9223372036854775807_8
  shifted = cshift([1,2,3],shift_value)
  if (any(shifted /= [2,3,1])) stop 8
  shifted = eoshift([1,2,3],shift_value)
  if (any(shifted /= [0,0,0])) stop 9
  if (any(reshape(expanded,[12]) /= [1,4,1,4,2,5,2,5,3,6,3,6])) stop 1
  do k = 1,2
    do j = 1,2
      do i = 1,2
        if (repeated_words(i,j,k) /= words(i,k)) stop 2
        if (repeated_pairs(i,j,k)%code /= pairs(i,k)%code) stop 3
        if (repeated_pairs(i,j,k)%label /= pairs(i,k)%label) stop 4
      end do
    end do
  end do
  print '(A)', 'constant middle-dimension SPREAD standard contracts passed'
end program
