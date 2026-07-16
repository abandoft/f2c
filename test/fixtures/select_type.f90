program select_type_dispatch
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
  call inspect(item)

contains

  subroutine inspect(object)
    class(base_record), intent(in) :: object
    select type (object)
    type is (weighted_record)
      if (object%identifier /= 17) stop 1
      if (object%weight /= 2.5) stop 2
    class is (base_record)
      stop 3
    class default
      stop 4
    end select
  end subroutine inspect

end program select_type_dispatch
