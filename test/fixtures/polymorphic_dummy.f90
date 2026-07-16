program polymorphic_dummy
  implicit none
  type :: base_record
    integer :: identifier
  end type base_record
  type, extends(base_record) :: weighted_record
    real :: weight
  end type weighted_record
  type(weighted_record) :: item
  integer :: observed

  item%identifier = 23
  item%weight = 4.0
  call read_identifier(item, observed)
  if (observed /= 23) stop 1

contains

  subroutine read_identifier(object, value)
    class(base_record), intent(in) :: object
    integer, intent(out) :: value
    value = object%identifier
  end subroutine read_identifier

end program polymorphic_dummy
