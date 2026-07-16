program vector_subscript
  implicit none
  integer :: values(5)
  integer :: order(3)

  values = [10, 20, 30, 40, 50]
  order = [5, 2, 4]

  values(order) = values([1, 3, 2])
  if (values(1) /= 10) stop 1
  if (values(2) /= 30) stop 2
  if (values(3) /= 30) stop 3
  if (values(4) /= 20) stop 4
  if (values(5) /= 10) stop 5

  values([1, 3]) = [7, 8]
  if (values(1) /= 7) stop 6
  if (values(3) /= 8) stop 7
end program vector_subscript
