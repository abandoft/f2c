program loop_storage
  use iso_fortran_env, only: int32, int64
  implicit none
  integer(int64), volatile :: observed
  integer(int64), target :: target
  integer(int64), pointer :: pointer
  integer(int64), allocatable :: allocated
  integer(int32), volatile :: words(4)
  integer(int64), volatile :: unaligned
  integer(int64) :: total, first, last, stride, values(3)
  character(len=128) :: record
  equivalence (words(2), unaligned)

  first = 4294967296_int64
  last = first + 2_int64
  stride = 1_int64
  total = 0_int64
  do observed = first, last, stride
    total = total + observed
  end do
  if (total /= 12884901891_int64 .or. observed /= first+3_int64) stop 1
  values = [(observed, observed = first, last, stride)]
  if (observed /= first+3_int64) stop 9
  if (any(values /= [4294967296_int64, 4294967297_int64, 4294967298_int64])) stop 10

  total = 0_int64
  do unaligned = first, last, stride
    total = total + unaligned
  end do
  if (total /= 12884901891_int64 .or. unaligned /= first+3_int64) stop 2
  write(record, '(3(I12,1X))') (unaligned, unaligned = first, last, stride)
  if (unaligned /= first+3_int64) stop 3
  read(record, *) values
  if (any(values /= [4294967296_int64, 4294967297_int64, 4294967298_int64])) stop 4

  pointer => target
  total = 0_int64
  do pointer = first, last, stride
    total = total + pointer
  end do
  if (total /= 12884901891_int64 .or. target /= first+3_int64) stop 5
  allocate(allocated)
  total = 0_int64
  do allocated = first, last, stride
    total = total + allocated
  end do
  if (total /= 12884901891_int64 .or. allocated /= first+3_int64) stop 6
  call dummy_loop(pointer)
  if (target /= first+3_int64) stop 7
  deallocate(allocated)
  print '(A)', 'loop storage semantics passed'
contains
  subroutine dummy_loop(iterator)
    integer(int64), intent(inout) :: iterator
    integer(int64) :: sum_value
    sum_value = 0_int64
    do iterator = first, last, stride
      sum_value = sum_value + iterator
    end do
    if (sum_value /= 12884901891_int64) stop 8
  end subroutine
end program
