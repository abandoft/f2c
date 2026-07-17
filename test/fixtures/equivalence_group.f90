program equivalence_group_test
  real :: values(8), alias_one, alias_two, origin, window(4), chained
  equivalence (values(2), alias_one, alias_two)
  equivalence (values(1), origin)
  equivalence (values(3), window(1))
  equivalence (window(2), chained)
  data window / 1.0, 2.0, 3.0, 4.0 /
  alias_one = 4.0
  if (abs(values(2) - 4.0) > 1.0e-6) stop 1
  if (abs(alias_two - 4.0) > 1.0e-6) stop 2
  origin = 8.0
  if (abs(values(1) - 8.0) > 1.0e-6) stop 3
  if (abs(values(3) - 1.0) > 1.0e-6) stop 4
  if (abs(values(6) - 4.0) > 1.0e-6) stop 5
  chained = 9.0
  if (abs(values(4) - 9.0) > 1.0e-6) stop 6
end program equivalence_group_test
