program storage
  integer, volatile :: words(5)
  double precision, volatile :: values(2)
  equivalence (words(2), values(1))
  values = [1.0d0, 2.0d0]
  values(2) = values(1) + 3.0d0
end program
