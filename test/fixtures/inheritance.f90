program inheritance
  implicit none
  type :: base_record
    integer :: identifier
  end type base_record
  type, extends(base_record) :: weighted_record
    real :: weight
  end type weighted_record
  type(weighted_record) :: item

  item%identifier = 17
  item%weight = 2.5
  if (item%identifier /= 17) stop 1
  if (abs(item%weight - 2.5) > 1.0e-6) stop 2
end program inheritance
