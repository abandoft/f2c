program array_section_conformance_error
  implicit none
  integer :: target_extent, value_extent
  integer :: target(4), value(4)

  target_extent = 2
  value_extent = 3
  target = 0
  value = [1, 2, 3, 4]
  target(1:target_extent) = value(1:value_extent)
end program array_section_conformance_error
