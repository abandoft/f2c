program do_semantics
  implicit none
  integer :: i, total, upper

  upper = 3
  total = 0
  do i = 1, upper
    total = total + i
  end do
  if (total /= 6 .or. i /= 4) error stop

  upper = 0
  do i = 1, upper
    total = -1
  end do
  if (total /= 6 .or. i /= 1) error stop

  do i = 1, 4
    if (i == 2) exit
  end do
  if (i /= 2) error stop

  total = 0
  do i = 1, 3
    if (i == 2) cycle
    total = total + i
  end do
  if (total /= 4 .or. i /= 4) error stop

  do i = 2, 1
    total = -1
  end do
  if (total /= 4 .or. i /= 2) error stop
end program do_semantics
